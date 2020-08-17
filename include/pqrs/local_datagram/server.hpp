#pragma once

// (C) Copyright Takayama Fumihiko 2018.
// Distributed under the Boost Software License, Version 1.0.
// (See http://www.boost.org/LICENSE_1_0.txt)

// `pqrs::local_datagram::server` can be used safely in a multi-threaded environment.

#include "impl/server_impl.hpp"
#include <nod/nod.hpp>
#include <pqrs/dispatcher.hpp>

namespace pqrs {
namespace local_datagram {
class server final : public dispatcher::extra::dispatcher_client {
public:
  // Signals (invoked from the dispatcher thread)

  nod::signal<void(void)> bound;
  nod::signal<void(const asio::error_code&)> bind_failed;
  nod::signal<void(void)> closed;
  nod::signal<void(std::shared_ptr<std::vector<uint8_t>>)> received;

  // Methods

  server(const server&) = delete;

  server(std::weak_ptr<dispatcher::dispatcher> weak_dispatcher,
         const std::string& path,
         size_t buffer_size) : dispatcher_client(weak_dispatcher),
                               path_(path),
                               buffer_size_(buffer_size),
                               server_send_entries_(std::make_shared<std::deque<std::shared_ptr<impl::send_entry>>>()),
                               reconnect_timer_(*this) {
  }

  virtual ~server(void) {
    detach_from_dispatcher([this] {
      stop();
    });
  }

  // You have to call `set_server_check_interval` before `async_start`.
  void set_server_check_interval(std::optional<std::chrono::milliseconds> value) {
    server_check_interval_ = value;
  }

  // You have to call `set_reconnect_interval` before `async_start`.
  void set_reconnect_interval(std::optional<std::chrono::milliseconds> value) {
    reconnect_interval_ = value;
  }

  void async_start(void) {
    enqueue_to_dispatcher([this] {
      bind();
    });
  }

  void async_stop(void) {
    enqueue_to_dispatcher([this] {
      stop();
    });
  }

private:
  // This method is executed in the dispatcher thread.
  void stop(void) {
    // We have to unset reconnect_interval_ before `close` to prevent `start_reconnect_timer` by `closed` signal.
    reconnect_interval_ = std::nullopt;

    close();
  }

  // This method is executed in the dispatcher thread.
  void bind(void) {
    if (server_impl_) {
      return;
    }

    server_impl_ = std::make_unique<impl::server_impl>(weak_dispatcher_,
                                                       server_send_entries_);

    server_impl_->bound.connect([this] {
      enqueue_to_dispatcher([this] {
        bound();
      });
    });

    server_impl_->bind_failed.connect([this](auto&& error_code) {
      enqueue_to_dispatcher([this, error_code] {
        bind_failed(error_code);
      });

      close();
      start_reconnect_timer();
    });

    server_impl_->closed.connect([this] {
      enqueue_to_dispatcher([this] {
        closed();
      });

      close();
      start_reconnect_timer();
    });

    server_impl_->received.connect([this](auto&& buffer) {
      enqueue_to_dispatcher([this, buffer] {
        received(buffer);
      });
    });

    server_impl_->async_bind(path_,
                             buffer_size_,
                             server_check_interval_);
  }

  // This method is executed in the dispatcher thread.
  void close(void) {
    if (!server_impl_) {
      return;
    }

    server_impl_ = nullptr;
  }

  // This method is executed in the dispatcher thread.
  void start_reconnect_timer(void) {
    if (reconnect_interval_) {
      enqueue_to_dispatcher(
          [this] {
            reconnect_timer_.start(
                [this] {
                  if (!reconnect_interval_) {
                    reconnect_timer_.stop();
                  }

                  bind();
                },
                *reconnect_interval_);
          },
          when_now() + *reconnect_interval_);
    } else {
      reconnect_timer_.stop();
    }
  }

  std::string path_;
  size_t buffer_size_;
  std::optional<std::chrono::milliseconds> server_check_interval_;
  std::optional<std::chrono::milliseconds> reconnect_interval_;
  std::shared_ptr<std::deque<std::shared_ptr<impl::send_entry>>> server_send_entries_;
  std::unique_ptr<impl::server_impl> server_impl_;
  dispatcher::extra::timer reconnect_timer_;
};
} // namespace local_datagram
} // namespace pqrs
