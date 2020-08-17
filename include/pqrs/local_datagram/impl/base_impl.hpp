#pragma once

// (C) Copyright Takayama Fumihiko 2020.
// Distributed under the Boost Software License, Version 1.0.
// (See http://www.boost.org/LICENSE_1_0.txt)

// `pqrs::local_datagram::impl::base_impl` can be used safely in a multi-threaded environment.

#include "asio_helper.hpp"
#include "send_entry.hpp"
#include <deque>
#include <nod/nod.hpp>
#include <optional>
#include <pqrs/dispatcher.hpp>

namespace pqrs {
namespace local_datagram {
namespace impl {
class base_impl : public dispatcher::extra::dispatcher_client {
protected:
  base_impl(const base_impl&) = delete;

  base_impl(std::weak_ptr<dispatcher::dispatcher> weak_dispatcher,
            std::shared_ptr<std::deque<std::shared_ptr<send_entry>>> send_entries) : dispatcher_client(weak_dispatcher),
                                                                                     send_entries_(send_entries),
                                                                                     io_service_(),
                                                                                     work_(std::make_unique<asio::io_service::work>(io_service_)) {
    io_service_thread_ = std::thread([this] {
      this->io_service_.run();
    });
  }

  virtual ~base_impl(void) {
  }

  // We have to terminate asio and pqrs::dispatcher while all instance variables of child class are alive.
  // Thus, `base_impl::terminate` is provided to terminate in the decstructor of child class.
  void terminate_base_impl(void) {
    //
    // asio
    //

    io_service_.post([this] {
      work_ = nullptr;
    });

    if (io_service_thread_.joinable()) {
      io_service_thread_.join();
    }

    //
    // pqrs::dispatcher
    //

    detach_from_dispatcher();
  }

  std::shared_ptr<std::deque<std::shared_ptr<send_entry>>> send_entries_;
  asio::io_service io_service_;
  std::unique_ptr<asio::io_service::work> work_;
  std::thread io_service_thread_;
  std::unique_ptr<asio::local::datagram_protocol::socket> socket_;
};
} // namespace impl
} // namespace local_datagram
} // namespace pqrs
