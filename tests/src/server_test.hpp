#include "test.hpp"
#include <boost/ut.hpp>
#include <fstream>

void run_server_test(void) {
  using namespace boost::ut;
  using namespace boost::ut::literals;

  "socket file"_test = [] {
    std::cout << "TEST_CASE(socket file)" << std::endl;

    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    {
      std::error_code error_code;
      std::filesystem::remove(test_constants::server_socket_file_path, error_code);
    }
    expect(!std::filesystem::exists(test_constants::server_socket_file_path));

    {
      auto server = std::make_shared<test_server>(dispatcher,
                                                  std::nullopt);

      expect(std::filesystem::exists(test_constants::server_socket_file_path));
    }

    expect(!std::filesystem::exists(test_constants::server_socket_file_path));

    dispatcher->terminate();
    dispatcher = nullptr;
  };

  "fail to create socket file"_test = [] {
    std::cout << "TEST_CASE(fail to create socket file)" << std::endl;

    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    size_t server_buffer_size(32 * 1024);
    auto server = std::make_unique<pqrs::local_datagram::server>(dispatcher,
                                                                 "not_found/server.sock",
                                                                 server_buffer_size);
    server->set_server_check_interval(std::chrono::milliseconds(100));
    server->set_reconnect_interval(std::chrono::milliseconds(100));

    auto wait = pqrs::make_thread_wait();
    bool failed = false;

    server->bind_failed.connect([&](auto&& error_code) {
      failed = true;
      wait->notify();
    });

    server->async_start();

    wait->wait_notice();

    expect(failed == true);

    dispatcher->terminate();
    dispatcher = nullptr;
  };

  "remove existing file"_test = [] {
    std::cout << "TEST_CASE(remove existing file)" << std::endl;

    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    std::filesystem::path regular_file_path("tmp/regular_file.sock");

    {
      std::ofstream file(regular_file_path);
      file << regular_file_path << std::endl;
    }

    expect(std::filesystem::exists(regular_file_path));

    {
      size_t server_buffer_size(32 * 1024);
      auto server = std::make_unique<pqrs::local_datagram::server>(dispatcher,
                                                                   regular_file_path,
                                                                   server_buffer_size);
      server->set_server_check_interval(std::chrono::milliseconds(100));
      server->set_reconnect_interval(std::chrono::milliseconds(100));

      auto wait = pqrs::make_thread_wait();

      server->bound.connect([&] {
        wait->notify();
      });

      server->async_start();

      wait->wait_notice();
    }

    expect(!std::filesystem::exists(regular_file_path));

    dispatcher->terminate();
    dispatcher = nullptr;
  };

  "permission error"_test = [] {
    std::cout << "TEST_CASE(permission error)" << std::endl;

    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    {
      auto server = std::make_unique<test_server>(dispatcher,
                                                  std::nullopt);

      {
        auto client = std::make_unique<test_client>(dispatcher,
                                                    std::nullopt,
                                                    false);
        expect(client->get_connected() == true);
      }

      // ----
      chmod(test_constants::server_socket_file_path.c_str(), 0000);

      {
        auto client = std::make_unique<test_client>(dispatcher,
                                                    std::nullopt,
                                                    false);
        expect(client->get_connected() == false);
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      expect(server->get_closed());
    }

    {
      auto server = std::make_unique<test_server>(dispatcher,
                                                  std::nullopt);

      // -r--
      chmod(test_constants::server_socket_file_path.c_str(), 0400);

      {
        auto client = std::make_unique<test_client>(dispatcher,
                                                    std::nullopt,
                                                    false);
        expect(client->get_connected() == false);
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      expect(server->get_closed());
    }

    {
      auto server = std::make_unique<test_server>(dispatcher,
                                                  std::nullopt);

      // -rw-
      chmod(test_constants::server_socket_file_path.c_str(), 0600);

      {
        auto client = std::make_unique<test_client>(dispatcher,
                                                    std::nullopt,
                                                    false);
        expect(client->get_connected() == true);
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      expect(!server->get_closed());
    }

    dispatcher->terminate();
    dispatcher = nullptr;
  };

  "close when socket erased"_test = [] {
    std::cout << "TEST_CASE(close when socket erased)" << std::endl;

    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    auto server = std::make_unique<test_server>(dispatcher,
                                                std::nullopt);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::error_code error_code;
    std::filesystem::remove(test_constants::server_socket_file_path, error_code);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    expect(server->get_closed());

    dispatcher->terminate();
    dispatcher = nullptr;
  };

  "local_datagram::server"_test = [] {
    std::cout << "TEST_CASE(local_datagram::server)" << std::endl;

    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    {
      auto server = std::make_unique<test_server>(dispatcher,
                                                  std::nullopt);
      auto client = std::make_unique<test_client>(dispatcher,
                                                  std::nullopt,
                                                  false);

      expect(client->get_connected() == true);

      client->async_send();
      client->async_send();

      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      expect(!client->get_closed());

      expect(server->get_received_count() == 64);

      // Destroy server

      server = nullptr;

      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      expect(client->get_closed());
    }

    // Send after server is destroyed.
    {
      expect(!std::filesystem::exists(test_constants::server_socket_file_path));

      auto client = std::make_unique<test_client>(dispatcher,
                                                  std::nullopt,
                                                  false);

      expect(client->get_connected() == false);

      client->async_send();

      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      expect(!client->get_closed());
    }

    // Create client before server
    {
      auto client = std::make_unique<test_client>(dispatcher,
                                                  std::nullopt,
                                                  false);

      expect(client->get_connected() == false);

      auto server = std::make_unique<test_server>(dispatcher,
                                                  std::nullopt);

      expect(server->get_received_count() == 0);

      client->async_send();

      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      expect(server->get_received_count() == 0);
    }

    dispatcher->terminate();
    dispatcher = nullptr;
  };

  "local_datagram::server reconnect"_test = [] {
    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    {
      size_t bound_count = 0;
      size_t bind_failed_count = 0;
      size_t closed_count = 0;

      auto server = std::make_unique<pqrs::local_datagram::server>(dispatcher,
                                                                   test_constants::server_socket_file_path,
                                                                   test_constants::server_buffer_size);
      server->set_server_check_interval(std::chrono::milliseconds(100));
      server->set_reconnect_interval(std::chrono::milliseconds(100));

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

      expect(bound_count == 1);
      expect(bind_failed_count == 0);
      expect(closed_count == 0);

      std::error_code error_code;
      std::filesystem::remove(test_constants::server_socket_file_path, error_code);

      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      expect(bound_count == 2);
      expect(bind_failed_count == 0);
      expect(closed_count == 1);
    }

    dispatcher->terminate();
    dispatcher = nullptr;
  };
}
