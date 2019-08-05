#include <catch2/catch.hpp>

#include "test.hpp"
#include <pqrs/filesystem.hpp>

TEST_CASE("local_datagram::client") {
  std::cout << "TEST_CASE(local_datagram::client)" << std::endl;

  auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
  auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

  {
    size_t connected_count = 0;
    size_t connect_failed_count = 0;
    size_t closed_count = 0;
    std::string last_error_message;

    auto client = std::make_unique<pqrs::local_datagram::client>(dispatcher,
                                                                 socket_path);
    client->set_server_check_interval(server_check_interval);
    client->set_reconnect_interval(std::chrono::milliseconds(100));

    client->connected.connect([&] {
      std::cout << "client connected: " << connected_count << std::endl;

      ++connected_count;
    });

    client->connect_failed.connect([&](auto&& error_code) {
      std::cout << "client connect_failed: " << connect_failed_count << std::endl;

      ++connect_failed_count;
    });

    client->closed.connect([&] {
      std::cout << "client closed: " << closed_count << std::endl;

      ++closed_count;
    });

    client->error_occurred.connect([&](auto&& error_code) {
      last_error_message = error_code.message();
    });

    // Create client before server

    client->async_start();

    for (int i = 0; i < 10; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      REQUIRE(connected_count == i * 2);
      REQUIRE(connect_failed_count > 2);
      REQUIRE(closed_count == i * 2);
      REQUIRE(last_error_message == "");

      // Create server

      auto server = std::make_unique<test_server>(dispatcher,
                                                  std::nullopt);

      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      REQUIRE(connected_count == i * 2 + 1);
      REQUIRE(last_error_message == "");

      auto previous_received_count = server->get_received_count();

      std::vector<uint8_t> buffer(1024);
      int loop_count = 20;
      for (int j = 0; j < loop_count; ++j) {
        client->async_send(buffer);
      }

      while (server->get_received_count() < previous_received_count + buffer.size() * loop_count) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

      REQUIRE(last_error_message == "");

      // Shutdown servr

      connect_failed_count = 0;

      server = nullptr;

      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      REQUIRE(connected_count == i * 2 + 1);
      REQUIRE(connect_failed_count > 2);
      REQUIRE(closed_count == i * 2 + 1);
      // last_error_message == "Connection reset by peer" ||
      // last_error_message == "Socket is not connected"
      REQUIRE(last_error_message != "");
      last_error_message = "";

      // Send data while server is down (data will be sent after reconnection.)

      client->async_send(buffer);

      // Recreate server

      server = std::make_unique<test_server>(dispatcher,
                                             std::nullopt);

      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      REQUIRE(server->get_received_count() == buffer.size());

      // Shutdown server

      server = nullptr;

      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      connect_failed_count = 0;
      last_error_message = "";
    }
  }

  dispatcher->terminate();
  dispatcher = nullptr;
}
