#include <csignal>
#include <iostream>
#include <pqrs/local_datagram.hpp>

namespace {
auto global_wait = pqrs::make_thread_wait();
}

int main(void) {
  std::signal(SIGINT, [](int) {
    global_wait->notify();
  });

  auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
  auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

  std::string socket_file_path("tmp/server.sock");
  size_t buffer_size = 32 * 1024;
  std::chrono::milliseconds server_check_interval(3000);
  std::chrono::milliseconds reconnect_interval(1000);

  // server

  auto server = std::make_shared<pqrs::local_datagram::server>(dispatcher,
                                                               socket_file_path,
                                                               buffer_size,
                                                               server_check_interval,
                                                               reconnect_interval);
  server->bound.connect([] {
    std::cout << "server bound" << std::endl;
  });
  server->bind_failed.connect([](auto&& error_code) {
    std::cout << "server bind_failed" << std::endl;
  });
  server->closed.connect([] {
    std::cout << "server closed" << std::endl;
  });
  server->received.connect([](auto&& buffer) {
    std::cout << "server received size:" << buffer->size() << std::endl;

    if (!buffer->empty()) {
      std::cout << "buffer: ";
      for (const auto& c : *buffer) {
        std::cout << c;
      }
      std::cout << std::endl;
    }
  });
  server->async_start();

  // client

  auto client = std::make_shared<pqrs::local_datagram::client>(dispatcher,
                                                               socket_file_path,
                                                               server_check_interval,
                                                               reconnect_interval);
  client->connected.connect([&client] {
    std::cout << "client connected" << std::endl;

    std::vector<uint8_t> buffer;
    buffer.push_back('h');
    buffer.push_back('e');
    buffer.push_back('l');
    buffer.push_back('l');
    buffer.push_back('o');
    client->async_send(buffer);
  });
  client->connect_failed.connect([](auto&& error_code) {
    std::cout << "client connect_failed" << std::endl;
  });
  client->closed.connect([] {
    std::cout << "client closed" << std::endl;
  });
  client->async_start();

  // ============================================================

  global_wait->wait_notice();

  // ============================================================

  client = nullptr;
  server = nullptr;

  dispatcher->terminate();
  dispatcher = nullptr;

  std::cout << "finished" << std::endl;

  return 0;
}
