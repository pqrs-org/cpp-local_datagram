#pragma once

// (C) Copyright Takayama Fumihiko 2018.
// Distributed under the Boost Software License, Version 1.0.
// (See http://www.boost.org/LICENSE_1_0.txt)

// `pqrs::local_datagram::server` can be used safely in a multi-threaded environment.

#include "impl/server.hpp"
#include <nod/nod.hpp>
#include <pqrs/dispatcher.hpp>

namespace pqrs {
namespace local_datagram {
class server final : public dispatcher::extra::dispatcher_client {
public:
  // Signals (invoked from the shared dispatcher thread)

  nod::signal<void(void)> bound;
  nod::signal<void(const asio::error_code&)> bind_failed;
  nod::signal<void(void)> closed;
  nod::signal<void(const std::shared_ptr<std::vector<uint8_t>>)> received;

  // Methods

  server(std::weak_ptr<dispatcher::dispatcher> weak_dispatcher,
         const std::string& path,
         size_t buffer_size,
         std::optional<std::chrono::milliseconds> server_check_interval,
         std::chrono::milliseconds reconnect_interval) : dispatcher_client(weak_dispatcher),
                                                         path_(path),
                                                         buffer_size_(buffer_size),
                                                         server_check_interval_(server_check_interval),
                                                         reconnect_interval_(reconnect_interval),
                                                         reconnect_enabled_(false) {
  }

  virtual ~server(void) {
    detach_from_dispatcher([this] {
      stop();
    });
  }

  void async_start(void) {
    enqueue_to_dispatcher([this] {
      reconnect_enabled_ = true;

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
    // We have to unset reconnect_enabled_ before `close` to prevent `enqueue_reconnect` by `closed` signal.
    reconnect_enabled_ = false;

    close();
  }

  // This method is executed in the dispatcher thread.
  void bind(void) {
    if (impl_server_) {
      return;
    }

    impl_server_ = std::make_unique<impl::server>(weak_dispatcher_);

    impl_server_->bound.connect([this] {
      enqueue_to_dispatcher([this] {
        bound();
      });
    });

    impl_server_->bind_failed.connect([this](auto&& error_code) {
      enqueue_to_dispatcher([this, error_code] {
        bind_failed(error_code);
      });

      close();
      enqueue_reconnect();
    });

    impl_server_->closed.connect([this] {
      enqueue_to_dispatcher([this] {
        closed();
      });

      close();
      enqueue_reconnect();
    });

    impl_server_->received.connect([this](auto&& buffer) {
      enqueue_to_dispatcher([this, buffer] {
        received(buffer);
      });
    });

    impl_server_->async_bind(path_,
                             buffer_size_,
                             server_check_interval_);
  }

  // This method is executed in the dispatcher thread.
  void close(void) {
    if (!impl_server_) {
      return;
    }

    impl_server_ = nullptr;
  }

  // This method is executed in the dispatcher thread.
  void enqueue_reconnect(void) {
    enqueue_to_dispatcher(
        [this] {
          if (!reconnect_enabled_) {
            return;
          }

          bind();
        },
        when_now() + reconnect_interval_);
  }

  std::string path_;
  size_t buffer_size_;
  std::optional<std::chrono::milliseconds> server_check_interval_;
  std::chrono::milliseconds reconnect_interval_;

  std::unique_ptr<impl::server> impl_server_;
  bool reconnect_enabled_;
};
} // namespace local_datagram
} // namespace pqrs
