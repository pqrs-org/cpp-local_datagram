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
  unlink(socket_file_path.c_str());

  // server
  size_t server_buffer_size = 32 * 1024;
  auto server = std::make_shared<pqrs::local_datagram::server>(dispatcher,
                                                               socket_file_path,
                                                               server_buffer_size);
  server->set_server_check_interval(std::chrono::milliseconds(3000));
  server->set_reconnect_interval(std::chrono::milliseconds(1000));

  server->bound.connect([] {
    std::cout << "server bound" << std::endl;
  });
  server->bind_failed.connect([](auto&& error_code) {
    std::cout << "server bind_failed:" << error_code.message() << std::endl;
  });
  server->closed.connect([] {
    std::cout << "server closed" << std::endl;
  });
  server->received.connect([](auto&& buffer) {
    std::cout << "server received size:" << buffer->size() << std::endl;

    if (!buffer->empty()) {
      std::cout << "buffer: ";
      int count = 0;
      for (const auto& c : *buffer) {
        std::cout << c;
        ++count;
        if (count > 40) {
          std::cout << "... (" << buffer->size() << "bytes)";
          break;
        }
      }
      std::cout << std::endl;
    }
  });

  server->async_start();

  // client

  size_t client_buffer_size = 32 * 1024;
  auto client = std::make_shared<pqrs::local_datagram::client>(dispatcher,
                                                               socket_file_path,
                                                               client_buffer_size);
  client->set_server_check_interval(std::chrono::milliseconds(3000));
  client->set_reconnect_interval(std::chrono::milliseconds(1000));

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
    std::cout << "client connect_failed:" << error_code.message() << std::endl;
  });
  client->closed.connect([] {
    std::cout << "client closed" << std::endl;
  });
  client->error_occurred.connect([](auto&& error_code) {
    std::cout << "client error_occurred:" << error_code.message() << std::endl;
  });

  client->async_start();
  {
    std::vector<uint8_t> buffer;
    buffer.push_back('1');
    client->async_send(buffer);
  }
  {
    std::vector<uint8_t> buffer;
    buffer.push_back('1');
    buffer.push_back('2');
    client->async_send(buffer);
  }
  {
    std::vector<uint8_t> buffer(30 * 1024, '9');
    client->async_send(buffer);
  }
  {
    std::vector<uint8_t> buffer(client_buffer_size, '9');
    client->async_send(buffer);
  }

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
