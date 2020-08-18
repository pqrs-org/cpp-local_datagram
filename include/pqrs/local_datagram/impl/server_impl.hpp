#pragma once

// (C) Copyright Takayama Fumihiko 2018.
// Distributed under the Boost Software License, Version 1.0.
// (See http://www.boost.org/LICENSE_1_0.txt)

// `pqrs::local_datagram::impl::server_impl` can be used safely in a multi-threaded environment.

#include "base_impl.hpp"
#include "client_impl.hpp"
#include <nod/nod.hpp>
#include <pqrs/dispatcher.hpp>
#include <unistd.h>

namespace pqrs {
namespace local_datagram {
namespace impl {
class server_impl final : public base_impl {
public:
  // Signals (invoked from the dispatcher thread)

  nod::signal<void(void)> bound;
  nod::signal<void(const asio::error_code&)> bind_failed;
  nod::signal<void(std::shared_ptr<std::vector<uint8_t>>)> received;

  // Methods

  server_impl(const server_impl&) = delete;

  server_impl(std::weak_ptr<dispatcher::dispatcher> weak_dispatcher,
              std::shared_ptr<std::deque<std::shared_ptr<send_entry>>> send_entries) : base_impl(weak_dispatcher, send_entries),
                                                                                       server_check_timer_(*this),
                                                                                       server_check_client_send_entries_(std::make_shared<std::deque<std::shared_ptr<impl::send_entry>>>()) {
  }

  ~server_impl(void) {
    async_close();

    terminate_base_impl();
  }

  void async_bind(const std::string& path,
                  size_t buffer_size,
                  std::optional<std::chrono::milliseconds> server_check_interval) {
    async_close();

    io_service_.post([this, path, buffer_size, server_check_interval] {
      socket_ready_ = false;

      // Open

      socket_ = std::make_unique<asio::local::datagram_protocol::socket>(io_service_);

      {
        asio::error_code error_code;
        socket_->open(asio::local::datagram_protocol::socket::protocol_type(),
                      error_code);
        if (error_code) {
          enqueue_to_dispatcher([this, error_code] {
            bind_failed(error_code);
          });
          return;
        }
      }

      // Set options

      // A margin (32 byte) is required to receive data which size == buffer_size.
      size_t buffer_margin = 32;
      receive_buffer_.resize(buffer_size + buffer_margin);
      socket_->set_option(asio::socket_base::receive_buffer_size(receive_buffer_.size()));

      // Bind

      {
        asio::error_code error_code;
        socket_->bind(asio::local::datagram_protocol::endpoint(path),
                      error_code);

        if (error_code) {
          enqueue_to_dispatcher([this, error_code] {
            bind_failed(error_code);
          });
          return;
        }
      }

      // Signal

      socket_ready_ = true;
      bound_path_ = path;

      start_server_check(path,
                         server_check_interval);

      enqueue_to_dispatcher([this] {
        bound();
      });

      // Receive

      async_receive();
    });
  }

private:
  // This method is executed in `io_service_thread_`.
  void async_receive(void) {
    if (!socket_ ||
        !socket_ready_) {
      return;
    }

    socket_->async_receive(asio::buffer(receive_buffer_),
                           [this](auto&& error_code, auto&& bytes_transferred) {
                             if (!error_code) {
                               if (bytes_transferred > 0) {
                                 auto t = send_entry::type(receive_buffer_[0]);
                                 if (t == send_entry::type::user_data) {
                                   auto v = std::make_shared<std::vector<uint8_t>>(bytes_transferred - 1);
                                   std::copy(std::begin(receive_buffer_) + 1,
                                             std::begin(receive_buffer_) + bytes_transferred,
                                             std::begin(*v));

                                   enqueue_to_dispatcher([this, v] {
                                     received(v);
                                   });
                                 }
                               }
                             }

                             // receive once if not closed

                             if (socket_ready_) {
                               async_receive();
                             }
                           });
  }

  // This method is executed in `io_service_thread_`.
  void start_server_check(const std::string& path,
                          std::optional<std::chrono::milliseconds> server_check_interval) {
    if (server_check_interval) {
      server_check_timer_.start(
          [this, path] {
            io_service_.post([this, path] {
              check_server(path);
            });
          },
          *server_check_interval);
    }
  }

  // This method is executed in `io_service_thread_`.
  void stop_server_check(void) {
    server_check_timer_.stop();
    server_check_client_impl_ = nullptr;
  }

  // This method is executed in `io_service_thread_`.
  void check_server(const std::string& path) {
    if (!socket_ ||
        !socket_ready_) {
      stop_server_check();
    }

    if (!server_check_client_impl_) {
      server_check_client_impl_ = std::make_unique<client_impl>(
          weak_dispatcher_,
          server_check_client_send_entries_);

      server_check_client_impl_->connected.connect([this] {
        io_service_.post([this] {
          server_check_client_impl_ = nullptr;
        });
      });

      server_check_client_impl_->connect_failed.connect([this](auto&& error_code) {
        async_close();
      });

      size_t buffer_size = 32;
      server_check_client_impl_->async_connect(path, buffer_size, std::nullopt);
    }
  }

  std::vector<uint8_t> receive_buffer_;

  dispatcher::extra::timer server_check_timer_;
  std::unique_ptr<client_impl> server_check_client_impl_;
  std::shared_ptr<std::deque<std::shared_ptr<send_entry>>> server_check_client_send_entries_;
};
} // namespace impl
} // namespace local_datagram
} // namespace pqrs
