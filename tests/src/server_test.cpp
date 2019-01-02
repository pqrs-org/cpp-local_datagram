#include <catch2/catch.hpp>

#include "test.hpp"
#include <pqrs/filesystem.hpp>

TEST_CASE("socket file") {
  std::cout << "TEST_CASE(socket file)" << std::endl;

  auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
  auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

  unlink(socket_path.c_str());
  REQUIRE(!pqrs::filesystem::exists(socket_path));

  {
    auto server = std::make_shared<test_server>(dispatcher,
                                                std::nullopt);

    REQUIRE(pqrs::filesystem::exists(socket_path));
  }

  REQUIRE(!pqrs::filesystem::exists(socket_path));

  dispatcher->terminate();
  dispatcher = nullptr;
}

TEST_CASE("fail to create socket file") {
  std::cout << "TEST_CASE(fail to create socket file)" << std::endl;

  auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
  auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

  size_t server_buffer_size(32 * 1024);
  std::chrono::milliseconds server_check_interval(100);
  std::chrono::milliseconds reconnect_interval(100);

  auto server = std::make_unique<pqrs::local_datagram::server>(dispatcher,
                                                               "not_found/server.sock",
                                                               server_buffer_size,
                                                               server_check_interval,
                                                               reconnect_interval);

  auto wait = pqrs::make_thread_wait();
  bool failed = false;

  server->bind_failed.connect([&](auto&& error_code) {
    failed = true;
    wait->notify();
  });

  server->async_start();

  wait->wait_notice();

  REQUIRE(failed == true);

  dispatcher->terminate();
  dispatcher = nullptr;
}

TEST_CASE("keep existing file in destructor") {
  std::cout << "TEST_CASE(keep existing file in destructor)" << std::endl;

  auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
  auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

  std::string regular_file_path("tmp/regular_file");

  {
    std::ofstream file(regular_file_path);
    file << regular_file_path << std::endl;
  }

  REQUIRE(pqrs::filesystem::exists(regular_file_path));

  {
    size_t server_buffer_size(32 * 1024);
    std::chrono::milliseconds server_check_interval(100);
    std::chrono::milliseconds reconnect_interval(100);

    auto server = std::make_unique<pqrs::local_datagram::server>(dispatcher,
                                                                 regular_file_path,
                                                                 server_buffer_size,
                                                                 server_check_interval,
                                                                 reconnect_interval);

    auto wait = pqrs::make_thread_wait();
    bool failed = false;

    server->bind_failed.connect([&](auto&& error_code) {
      failed = true;
      wait->notify();
    });

    server->async_start();

    wait->wait_notice();

    REQUIRE(failed == true);
  }

  REQUIRE(pqrs::filesystem::exists(regular_file_path));

  dispatcher->terminate();
  dispatcher = nullptr;
}

TEST_CASE("permission error") {
  std::cout << "TEST_CASE(permission error)" << std::endl;

  auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
  auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

  {
    auto server = std::make_unique<test_server>(dispatcher,
                                                std::nullopt);

    {
      auto client = std::make_unique<test_client>(dispatcher,
                                                  std::nullopt);
      REQUIRE(client->get_connected() == true);
    }

    // ----
    chmod(socket_path.c_str(), 0000);

    {
      auto client = std::make_unique<test_client>(dispatcher,
                                                  std::nullopt);
      REQUIRE(client->get_connected() == false);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    REQUIRE(server->get_closed());
  }

  {
    auto server = std::make_unique<test_server>(dispatcher,
                                                std::nullopt);

    // -r--
    chmod(socket_path.c_str(), 0400);

    {
      auto client = std::make_unique<test_client>(dispatcher,
                                                  std::nullopt);
      REQUIRE(client->get_connected() == false);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    REQUIRE(server->get_closed());
  }

  {
    auto server = std::make_unique<test_server>(dispatcher,
                                                std::nullopt);

    // -rw-
    chmod(socket_path.c_str(), 0600);

    {
      auto client = std::make_unique<test_client>(dispatcher,
                                                  std::nullopt);
      REQUIRE(client->get_connected() == true);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    REQUIRE(!server->get_closed());
  }

  dispatcher->terminate();
  dispatcher = nullptr;
}

TEST_CASE("close when socket erased") {
  std::cout << "TEST_CASE(close when socket erased)" << std::endl;

  auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
  auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

  auto server = std::make_unique<test_server>(dispatcher,
                                              std::nullopt);

  unlink(socket_path.c_str());

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  REQUIRE(server->get_closed());

  dispatcher->terminate();
  dispatcher = nullptr;
}

TEST_CASE("local_datagram::server") {
  std::cout << "TEST_CASE(local_datagram::server)" << std::endl;

  auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
  auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

  {
    auto server = std::make_unique<test_server>(dispatcher,
                                                std::nullopt);
    auto client = std::make_unique<test_client>(dispatcher,
                                                std::nullopt);

    REQUIRE(client->get_connected() == true);

    client->async_send();
    client->async_send();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    REQUIRE(!client->get_closed());

    REQUIRE(server->get_received_count() == 64);

    // Destroy server

    server = nullptr;

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    REQUIRE(client->get_closed());
  }

  // Send after server is destroyed.
  {
    REQUIRE(!pqrs::filesystem::exists(socket_path));

    auto client = std::make_unique<test_client>(dispatcher,
                                                std::nullopt);

    REQUIRE(client->get_connected() == false);

    client->async_send();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    REQUIRE(!client->get_closed());
  }

  // Create client before server
  {
    auto client = std::make_unique<test_client>(dispatcher,
                                                std::nullopt);

    REQUIRE(client->get_connected() == false);

    auto server = std::make_unique<test_server>(dispatcher,
                                                std::nullopt);

    REQUIRE(server->get_received_count() == 0);

    client->async_send();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    REQUIRE(server->get_received_count() == 0);
  }

  dispatcher->terminate();
  dispatcher = nullptr;
}

TEST_CASE("local_datagram::server reconnect") {
  auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
  auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

  {
    size_t bound_count = 0;
    size_t bind_failed_count = 0;
    size_t closed_count = 0;

    std::chrono::milliseconds reconnect_interval(100);

    auto server = std::make_unique<pqrs::local_datagram::server>(dispatcher,
                                                                 socket_path,
                                                                 server_buffer_size,
                                                                 server_check_interval,
                                                                 reconnect_interval);

    server->bound.connect([&] {
      std::cout << "server bound: " << bound_count << std::endl;

      ++bound_count;
    });

    server->bind_failed.connect([&](auto&& error_code) {
      std::cout << "server bind_failed: " << bind_failed_count << std::endl;

      ++bind_failed_count;
    });

    server->closed.connect([&] {
      std::cout << "server closed: " << closed_count << std::endl;

      ++closed_count;
    });

    server->async_start();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    REQUIRE(bound_count == 1);
    REQUIRE(bind_failed_count == 0);
    REQUIRE(closed_count == 0);

    unlink(socket_path.c_str());

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    REQUIRE(bound_count == 2);
    REQUIRE(bind_failed_count == 0);
    REQUIRE(closed_count == 1);
  }

  dispatcher->terminate();
  dispatcher = nullptr;
}
