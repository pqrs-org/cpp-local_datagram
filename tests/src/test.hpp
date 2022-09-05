#pragma once

#include <boost/ut.hpp>
#include <iostream>
#include <pqrs/local_datagram.hpp>

namespace test_constants {
const std::filesystem::path server_socket_file_path("tmp/server.sock");
const std::filesystem::path client_socket_file_path("tmp/client.sock");
const std::filesystem::path client_socket2_file_path("tmp/client2.sock");
const size_t server_buffer_size(32 * 1024);
const std::chrono::milliseconds server_check_interval(100);
const std::chrono::milliseconds client_socket_check_interval(100);
} // namespace test_constants

class test_server final {
public:
  test_server(std::weak_ptr<pqrs::dispatcher::dispatcher> weak_dispatcher,
              std::optional<std::chrono::milliseconds> reconnect_interval) : closed_(false),
                                                                             received_count_(0) {
    auto wait = pqrs::make_thread_wait();

    unlink(test_constants::server_socket_file_path.c_str());

    server_ = std::make_unique<pqrs::local_datagram::server>(weak_dispatcher,
                                                             test_constants::server_socket_file_path,
                                                             test_constants::server_buffer_size);
    server_->set_server_check_interval(test_constants::server_check_interval);
    server_->set_reconnect_interval(reconnect_interval);

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

    server_->received.connect([this](auto&& buffer, auto&& sender_endpoint) {
      using namespace boost::ut;

      std::cout << "server received size:" << buffer->size() << std::endl;

      received_count_ += buffer->size();

      if (buffer->size() == 32) {
        expect((*buffer)[0] == 10);
        expect((*buffer)[1] == 20);
        expect((*buffer)[2] == 30);
      }

      // echo
      if (!sender_endpoint->path().empty()) {
        server_->async_send(*buffer, sender_endpoint);
      }
    });

    server_->next_heartbeat_deadline_exceeded.connect([this](auto&& sender_endpoint) {
      next_heartbeat_deadline_exceeded_counts_[sender_endpoint->path()] += 1;
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

  const std::unordered_map<std::string, int>& get_next_heartbeat_deadline_exceeded_counts(void) {
    return next_heartbeat_deadline_exceeded_counts_;
  }

private:
  std::optional<bool> bound_;
  bool closed_;
  size_t received_count_;
  std::unique_ptr<pqrs::local_datagram::server> server_;
  std::unordered_map<std::string, int> next_heartbeat_deadline_exceeded_counts_;
};

class test_client final {
public:
  test_client(std::weak_ptr<pqrs::dispatcher::dispatcher> weak_dispatcher,
              std::optional<std::chrono::milliseconds> reconnect_interval,
              bool bidirectional) : closed_(false),
                                    received_count_(0) {
    auto wait = pqrs::make_thread_wait();

    std::optional<std::filesystem::path> client_socket_file_path;
    if (bidirectional) {
      client_socket_file_path = test_constants::client_socket_file_path;
      unlink(client_socket_file_path->c_str());
    }

    client_ = std::make_unique<pqrs::local_datagram::client>(weak_dispatcher,
                                                             test_constants::server_socket_file_path,
                                                             client_socket_file_path,
                                                             test_constants::server_buffer_size);
    client_->set_server_check_interval(test_constants::server_check_interval);
    client_->set_client_socket_check_interval(test_constants::client_socket_check_interval);
    client_->set_reconnect_interval(reconnect_interval);

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

    client_->received.connect([this](auto&& buffer, auto&& sender_endpoint) {
      received_count_ += buffer->size();
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

  void set_closed(bool value) {
    closed_ = value;
  }

  size_t get_received_count(void) const {
    return received_count_;
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
  size_t received_count_;
  std::unique_ptr<pqrs::local_datagram::client> client_;
};
