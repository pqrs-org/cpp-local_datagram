#pragma once

// (C) Copyright Takayama Fumihiko 2018.
// Distributed under the Boost Software License, Version 1.0.
// (See http://www.boost.org/LICENSE_1_0.txt)

// `pqrs::local_datagram::impl::client_impl` can be used safely in a multi-threaded environment.

#include "asio_helper.hpp"
#include "base_impl.hpp"
#include "send_entry.hpp"
#include <deque>
#include <nod/nod.hpp>
#include <optional>
#include <pqrs/dispatcher.hpp>

namespace pqrs {
namespace local_datagram {
namespace impl {
class client_impl final : public base_impl {
public:
  // Signals (invoked from the dispatcher thread)

  nod::signal<void(void)> connected;
  nod::signal<void(const asio::error_code&)> connect_failed;
  nod::signal<void(void)> closed;
  nod::signal<void(const asio::error_code&)> error_occurred;

  // Methods

  client_impl(const client_impl&) = delete;

  client_impl(std::weak_ptr<dispatcher::dispatcher> weak_dispatcher,
              std::shared_ptr<std::deque<std::shared_ptr<send_entry>>> send_entries) : base_impl(weak_dispatcher, send_entries),
                                                                                       send_invoker_(io_service_, asio_helper::time_point::pos_infin()),
                                                                                       send_deadline_(io_service_, asio_helper::time_point::pos_infin()),
                                                                                       connected_(false),
                                                                                       server_check_timer_(*this) {
    io_service_thread_ = std::thread([this] {
      this->io_service_.run();
    });
  }

  virtual ~client_impl(void) {
    async_close();

    io_service_.post([this] {
      work_ = nullptr;
    });

    if (io_service_thread_.joinable()) {
      io_service_thread_.join();
    }

    detach_from_dispatcher();
  }

  void async_connect(const std::string& path,
                     size_t buffer_size,
                     std::optional<std::chrono::milliseconds> server_check_interval) {
    io_service_.post([this, path, buffer_size, server_check_interval] {
      if (socket_) {
        return;
      }

      socket_ = std::make_unique<asio::local::datagram_protocol::socket>(io_service_);

      connected_ = false;

      // Open

      {
        asio::error_code error_code;
        socket_->open(asio::local::datagram_protocol::socket::protocol_type(),
                      error_code);
        if (error_code) {
          enqueue_to_dispatcher([this, error_code] {
            connect_failed(error_code);
          });
          return;
        }
      }

      // Set options

      // A margin (1 byte) is required to append send_entry::type.
      socket_->set_option(asio::socket_base::send_buffer_size(buffer_size + 1));

      // Connect

      socket_->async_connect(asio::local::datagram_protocol::endpoint(path),
                             [this, server_check_interval](auto&& error_code) {
                               if (error_code) {
                                 enqueue_to_dispatcher([this, error_code] {
                                   connect_failed(error_code);
                                 });
                               } else {
                                 connected_ = true;

                                 stop_server_check();
                                 start_server_check(server_check_interval);

                                 enqueue_to_dispatcher([this] {
                                   connected();
                                 });

                                 // Flush send_entries_.
                                 await_entry(std::nullopt);

                                 // Enable send_deadline_.
                                 send_deadline_.expires_at(asio_helper::time_point::pos_infin());
                                 check_deadline();
                               }
                             });
    });
  }

  void async_close(void) {
    io_service_.post([this] {
      if (!socket_) {
        return;
      }

      stop_server_check();

      // Close socket

      asio::error_code error_code;

      socket_->cancel(error_code);
      socket_->close(error_code);

      socket_ = nullptr;

      send_invoker_.cancel();
      send_deadline_.cancel();

      // Signal

      if (connected_) {
        connected_ = false;

        enqueue_to_dispatcher([this] {
          closed();
        });
      }
    });
  }

  void async_send(std::shared_ptr<send_entry> entry) {
    if (!entry) {
      return;
    }

    io_service_.post([this, entry] {
      send_entries_->push_back(entry);
      send_invoker_.expires_after(std::chrono::milliseconds(0));
    });
  }

private:
  // This method is executed in `io_service_thread_`.
  void start_server_check(std::optional<std::chrono::milliseconds> server_check_interval) {
    if (server_check_interval) {
      server_check_timer_.start(
          [this] {
            io_service_.post([this] {
              check_server();
            });
          },
          *server_check_interval);
    }
  }

  // This method is executed in `io_service_thread_`.
  void stop_server_check(void) {
    server_check_timer_.stop();
  }

  // This method is executed in `io_service_thread_`.
  void check_server(void) {
    auto b = std::make_shared<send_entry>(send_entry::type::server_check);
    async_send(b);
  }

  // This method is executed in `io_service_thread_`.
  void await_entry(std::optional<std::chrono::milliseconds> delay) {
    if (!socket_ ||
        !connected_) {
      return;
    }

    if (delay || send_entries_->empty()) {
      // Sleep until new entry is added.
      if (delay) {
        send_invoker_.expires_after(*delay);
      } else {
        send_invoker_.expires_at(asio_helper::time_point::pos_infin());
      }

      send_invoker_.async_wait(
          [this](const auto& error_code) {
            await_entry(std::nullopt);
          });

    } else {
      auto entry = send_entries_->front();

      send_deadline_.expires_after(std::chrono::milliseconds(5000));

      socket_->async_send(
          entry->make_buffer(),
          [this, entry](const auto& error_code, auto bytes_transferred) {
            handle_send(error_code, bytes_transferred, entry);
          });
    }
  }

  // This method is executed in `io_service_thread_`.
  void handle_send(const asio::error_code& error_code,
                   size_t bytes_transferred,
                   std::shared_ptr<send_entry> entry) {
    std::optional<std::chrono::milliseconds> next_delay;

    entry->add_bytes_transferred(bytes_transferred);

    send_deadline_.expires_at(asio_helper::time_point::pos_infin());

    //
    // Handle error.
    //

    if (error_code == asio::error::no_buffer_space) {
      //
      // Retrying the sending data or abort the buffer is required.
      //
      // - Keep the connection.
      // - Keep or drop the entry.
      //

      // Retry if no_buffer_space error is continued too much times.
      entry->set_no_buffer_space_error_count(
          entry->get_no_buffer_space_error_count() + 1);

      if (entry->get_no_buffer_space_error_count() > 10) {
        // `send` always returns no_buffer_space error on macOS
        // when entry->buffer_.size() > server_buffer_size.
        //
        // Thus, we have to cancel sending data in the such case.
        // (We consider we have to cancel when `send_entry::bytes_transferred` == 0.)

        if (entry->get_bytes_transferred() == 0 ||
            // Abort if too many errors
            entry->get_no_buffer_space_error_count() > 100) {
          // Drop entry

          entry->add_bytes_transferred(entry->rest_bytes());

          enqueue_to_dispatcher([this, error_code] {
            error_occurred(error_code);
          });
        }
      }

      // Wait until buffer is available.
      next_delay = std::chrono::milliseconds(100);

    } else if (error_code == asio::error::message_size) {
      //
      // Problem of the sending data.
      //
      // - Keep the connection.
      // - Drop the entry.
      //

      entry->add_bytes_transferred(entry->rest_bytes());

      enqueue_to_dispatcher([this, error_code] {
        error_occurred(error_code);
      });

    } else if (error_code) {
      //
      // Other errors (e.g., connection error)
      //
      // - Close the connection.
      // - Keep the entry.
      //

      enqueue_to_dispatcher([this, error_code] {
        error_occurred(error_code);
      });

      async_close();
      return;
    }

    //
    // Remove send_entry if transfer is completed.
    //

    if (entry->transfer_complete()) {
      pop_front_send_entry();
    }

    await_entry(next_delay);
  }

  // This method is executed in `io_service_thread_`.
  void pop_front_send_entry(void) {
    if (send_entries_->empty()) {
      return;
    }

    auto entry = send_entries_->front();
    if (auto&& processed = entry->get_processed()) {
      processed();
    }

    send_entries_->pop_front();
  }

  // This method is executed in `io_service_thread_`.
  void check_deadline(void) {
    if (!socket_ ||
        !connected_) {
      return;
    }

    if (send_deadline_.expiry() < asio_helper::time_point::now()) {
      // The deadline has passed.
      async_close();
    } else {
      send_deadline_.async_wait(
          [this](const auto& error_code) {
            check_deadline();
          });
    }
  }

  asio::steady_timer send_invoker_;
  asio::steady_timer send_deadline_;
  std::thread io_service_thread_;
  bool connected_;

  dispatcher::extra::timer server_check_timer_;
};
} // namespace impl
} // namespace local_datagram
} // namespace pqrs
