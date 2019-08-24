#pragma once

// (C) Copyright Takayama Fumihiko 2018.
// Distributed under the Boost Software License, Version 1.0.
// (See http://www.boost.org/LICENSE_1_0.txt)

// `pqrs::local_datagram::impl::client` can be used safely in a multi-threaded environment.

#ifdef ASIO_STANDALONE
#include <asio.hpp>
#else
#define ASIO_STANDALONE
#include <asio.hpp>
#undef ASIO_STANDALONE
#endif

#include "client_send_entry.hpp"
#include <deque>
#include <nod/nod.hpp>
#include <optional>
#include <pqrs/dispatcher.hpp>

namespace pqrs {
namespace local_datagram {
namespace impl {
class client final : public dispatcher::extra::dispatcher_client {
public:
  // Signals (invoked from the dispatcher thread)

  nod::signal<void(void)> connected;
  nod::signal<void(const asio::error_code&)> connect_failed;
  nod::signal<void(void)> closed;
  nod::signal<void(const asio::error_code&)> error_occurred;

  // Methods

  client(const client&) = delete;

  client(std::weak_ptr<dispatcher::dispatcher> weak_dispatcher) : dispatcher_client(weak_dispatcher),
                                                                  io_service_(),
                                                                  work_(std::make_unique<asio::io_service::work>(io_service_)),
                                                                  socket_(nullptr),
                                                                  connected_(false),
                                                                  server_check_timer_(*this) {
    io_service_thread_ = std::thread([this] {
      (this->io_service_).run();
    });
  }

  virtual ~client(void) {
    async_close();

    if (io_service_thread_.joinable()) {
      work_ = nullptr;
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

      // A margin (1 byte) is required to append client_send_entry::type.
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
                                 send();
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

      // Signal

      if (connected_) {
        connected_ = false;

        enqueue_to_dispatcher([this] {
          closed();
        });
      }
    });
  }

  void async_send(std::shared_ptr<client_send_entry> entry) {
    io_service_.post([this, entry] {
      send_entries_.push_back(entry);
    });

    send();
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
    auto b = std::make_shared<client_send_entry>(client_send_entry::type::server_check);
    async_send(b);
  }

  void send(void) {
    io_service_.post([this] {
      if (!socket_) {
        return;
      }

      if (!connected_) {
        return;
      }

      while (!send_entries_.empty()) {
        if (auto entry = send_entries_.front()) {
          size_t sent = 0;
          size_t no_buffer_space_error_count = 0;
          do {
            asio::error_code error_code;
            sent += socket_->send(asio::buffer(entry->get_buffer()),
                                  asio::socket_base::message_flags(0),
                                  error_code);
            if (error_code) {
              if (error_code == asio::error::no_buffer_space) {
                //
                // Retrying the sending data or abort the buffer is required.
                //
                // - Keep the connection.
                // - Keep or drop the entry.
                //

                // Retry if no_buffer_space error is continued too much times.
                ++no_buffer_space_error_count;
                if (no_buffer_space_error_count > 10) {
                  // `send` always returns no_buffer_space error on macOS
                  // when entry->get_buffer().size() > server_buffer_size.
                  //
                  // Thus, we have to cancel sending data in the such case.
                  // (We consider we have to cancel when `sent` == 0.)

                  if (sent == 0) {
                    // Abort

                    enqueue_to_dispatcher([this, error_code] {
                      error_occurred(error_code);
                    });
                    break;

                  } else {
                    // Retry
                    send();
                    return;
                  }
                }

                // Wait until buffer is available.
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

              } else if (error_code == asio::error::message_size) {
                //
                // Problem of the sending data.
                //
                // - Keep the connection.
                // - Drop the entry.
                //

                enqueue_to_dispatcher([this, error_code] {
                  error_occurred(error_code);
                });
                break;

              } else {
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
            } else {
              no_buffer_space_error_count = 0;
            }
          } while (sent < entry->get_buffer().size());

          if (auto&& processed = entry->get_processed()) {
            enqueue_to_dispatcher([processed] {
              processed();
            });
          }
        }

        send_entries_.pop_front();
      }
    });
  }

  asio::io_service io_service_;
  std::unique_ptr<asio::io_service::work> work_;
  std::unique_ptr<asio::local::datagram_protocol::socket> socket_;
  std::deque<std::shared_ptr<client_send_entry>> send_entries_;
  std::thread io_service_thread_;
  bool connected_;

  dispatcher::extra::timer server_check_timer_;
};
} // namespace impl
} // namespace local_datagram
} // namespace pqrs
