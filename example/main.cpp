#include <csignal>
#include <iostream>
#include <pqrs/local_datagram.hpp>

namespace {
auto global_wait = pqrs::make_thread_wait();

void output_received_data(std::shared_ptr<std::vector<uint8_t>> buffer) {
  if (!buffer->empty()) {
    std::cout << "buffer: `";
    int count = 0;
    for (const auto& c : *buffer) {
      std::cout << c;
      ++count;
      if (count > 40) {
        std::cout << "... (" << buffer->size() << "bytes)";
        break;
      }
    }
    std::cout << "`";
    std::cout << std::endl;
  }
}
} // namespace

int main(void) {
  std::signal(SIGINT, [](int) {
    global_wait->notify();
  });

  auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
  auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

  std::filesystem::path server_socket_file_path("tmp/server.sock");
  std::filesystem::path client_socket_file_path("tmp/client.sock");

  // server
  size_t server_buffer_size = 32 * 1024;
  auto server = std::make_shared<pqrs::local_datagram::server>(dispatcher,
                                                               server_socket_file_path,
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
  server->received.connect([&server](auto&& buffer, auto&& sender_endpoint) {
    std::cout << "server received size:" << buffer->size() << std::endl;
    output_received_data(buffer);

    if (pqrs::local_datagram::non_empty_filesystem_endpoint_path(*sender_endpoint)) {
      server->async_send(*buffer, sender_endpoint);
    }
  });
  server->next_heartbeat_deadline_exceeded.connect([](auto&& sender_endpoint) {
    std::cout << "server next_heartbeat_deadline_exceeded " << *sender_endpoint << std::endl;
  });

  server->async_start();

  // client

  size_t client_buffer_size = 64 * 1024;
  auto client = std::make_shared<pqrs::local_datagram::client>(dispatcher,
                                                               server_socket_file_path,
                                                               client_socket_file_path,
                                                               client_buffer_size);
  client->set_server_check_interval(std::chrono::milliseconds(3000));
  client->set_next_heartbeat_deadline(std::chrono::milliseconds(10000));
  client->set_reconnect_interval(std::chrono::milliseconds(1000));

  client->connected.connect([&client](auto&& peer_pid) {
    std::cout << "client connected" << std::endl;
    std::cout << "peer_pid: " << peer_pid.value_or(-1) << std::endl;

    std::string s = "Type control-c to quit.";
    std::vector<uint8_t> buffer(std::begin(s), std::end(s));
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
  client->received.connect([](auto&& buffer, auto&& sender_endpoint) {
    std::cout << "client received size:" << buffer->size() << std::endl;
    output_received_data(buffer);
  });

  client->async_start();
  {
    std::vector<uint8_t> buffer;
    buffer.push_back('1');
    client->async_send(buffer, [] {
      std::cout << "processed `1`" << std::endl;
    });
  }
  {
    std::vector<uint8_t> buffer;
    buffer.push_back('1');
    buffer.push_back('2');
    client->async_send(buffer);
  }
  {
    std::vector<uint8_t> buffer(30 * 1024, '3');
    client->async_send(buffer);
  }
  {
    // This message is failed since client_buffer_size > server_buffer_size.
    std::vector<uint8_t> buffer(client_buffer_size, '4');
    client->async_send(buffer, [] {
      std::cout << "processed `4`" << std::endl;
    });
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
