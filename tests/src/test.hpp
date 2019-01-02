#pragma once

#include <iostream>
#include <pqrs/local_datagram.hpp>

const std::string socket_path("tmp/server.sock");
const size_t server_buffer_size(32 * 1024);
const std::chrono::milliseconds server_check_interval(100);

class test_server final {
public:
  test_server(std::weak_ptr<pqrs::dispatcher::dispatcher> weak_dispatcher,
              std::optional<std::chrono::milliseconds> reconnect_interval) : closed_(false),
                                                                             received_count_(0) {
    if (!reconnect_interval) {
      reconnect_interval = std::chrono::milliseconds(1000 * 1000);
    }

    auto wait = pqrs::make_thread_wait();

    unlink(socket_path.c_str());

    server_ = std::make_unique<pqrs::local_datagram::server>(weak_dispatcher,
                                                             socket_path,
                                                             server_buffer_size,
                                                             server_check_interval,
                                                             *reconnect_interval);

    server_->bound.connect([this, wait] {
      std::cout << "server bound" << std::endl;

      bound_ = true;

      wait->notify();
    });

    server_->bind_failed.connect([this, wait](auto&& error_code) {
      std::cout << "server bind_failed" << std::endl;

      bound_ = false;

      wait->notify();
    });

    server_->closed.connect([this] {
      std::cout << "server closed" << std::endl;

      closed_ = true;
    });

    server_->received.connect([this](auto&& buffer) {
      std::cout << "server received size:" << buffer->size() << std::endl;

      received_count_ += buffer->size();

      if (buffer->size() == 32) {
        REQUIRE((*buffer)[0] == 10);
        REQUIRE((*buffer)[1] == 20);
        REQUIRE((*buffer)[2] == 30);
      }
    });

    server_->async_start();

    wait->wait_notice();
  }

  ~test_server(void) {
    std::cout << "~test_server" << std::endl;

    server_ = nullptr;
  }

  std::optional<bool> get_bound(void) const {
    return bound_;
  }

  bool get_closed(void) const {
    return closed_;
  }

  size_t get_received_count(void) const {
    return received_count_;
  }

private:
  std::optional<bool> bound_;
  bool closed_;
  size_t received_count_;
  std::unique_ptr<pqrs::local_datagram::server> server_;
};

class test_client final {
public:
  test_client(std::weak_ptr<pqrs::dispatcher::dispatcher> weak_dispatcher,
              std::optional<std::chrono::milliseconds> reconnect_interval) : closed_(false) {
    if (!reconnect_interval) {
      reconnect_interval = std::chrono::milliseconds(1000 * 1000);
    }

    auto wait = pqrs::make_thread_wait();

    client_ = std::make_unique<pqrs::local_datagram::client>(weak_dispatcher,
                                                             socket_path,
                                                             server_check_interval,
                                                             *reconnect_interval);

    client_->connected.connect([this, wait] {
      connected_ = true;
      wait->notify();
    });

    client_->connect_failed.connect([this, wait](auto&& error_code) {
      connected_ = false;

      std::cout << error_code.message() << std::endl;

      wait->notify();
    });

    client_->closed.connect([this] {
      closed_ = true;
    });

    client_->async_start();

    wait->wait_notice();
  }

  ~test_client(void) {
    std::cout << "~test_client" << std::endl;

    client_ = nullptr;
  }

  std::optional<bool> get_connected(void) const {
    return connected_;
  }

  bool get_closed(void) const {
    return closed_;
  }

  void async_send(void) {
    std::vector<uint8_t> client_buffer(32);
    client_buffer[0] = 10;
    client_buffer[1] = 20;
    client_buffer[2] = 30;
    if (client_) {
      client_->async_send(client_buffer);
    }
  }

private:
  std::optional<bool> connected_;
  bool closed_;
  std::unique_ptr<pqrs::local_datagram::client> client_;
};
