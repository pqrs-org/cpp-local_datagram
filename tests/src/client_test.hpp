#include "test.hpp"
#include <boost/ut.hpp>

void run_client_test(void) {
  using namespace boost::ut;
  using namespace boost::ut::literals;

  "local_datagram::client"_test = [] {
    std::cout << "TEST_CASE(local_datagram::client)" << std::endl;

    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    {
      size_t connected_count = 0;
      size_t connect_failed_count = 0;
      size_t closed_count = 0;
      std::string last_error_message;

      auto client = std::make_unique<pqrs::local_datagram::client>(dispatcher,
                                                                   test_constants::server_socket_file_path,
                                                                   std::nullopt,
                                                                   test_constants::server_buffer_size);
      client->set_server_check_interval(test_constants::server_check_interval);
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

        expect(connected_count == i * 2);
        expect(connect_failed_count > 2);
        expect(closed_count == i * 2);
        expect(last_error_message == "");

        // Create server

        auto server = std::make_unique<test_server>(dispatcher,
                                                    std::nullopt);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        expect(connected_count == i * 2 + 1);
        expect(last_error_message == "");

        auto previous_received_count = server->get_received_count();

        std::vector<uint8_t> buffer(1024);
        int loop_count = 20;
        int processed_count = 0;
        for (int j = 0; j < loop_count; ++j) {
          if (j < loop_count / 2) {
            client->async_send(buffer);
          } else {
            client->async_send(buffer, [&processed_count] {
              ++processed_count;
            });
          }
        }

        while (server->get_received_count() < previous_received_count + buffer.size() * loop_count) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        expect(last_error_message == "");
        expect(processed_count == loop_count / 2);

        // Shutdown servr

        connect_failed_count = 0;

        server = nullptr;

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        expect(connected_count == i * 2 + 1);
        expect(connect_failed_count > 2);
        expect(closed_count == i * 2 + 1);
        // last_error_message == "Connection reset by peer" ||
        // last_error_message == "Socket is not connected"
        expect(last_error_message != "");
        last_error_message = "";

        // Send data while server is down (data will be sent after reconnection.)

        client->async_send(buffer);

        // Recreate server

        server = std::make_unique<test_server>(dispatcher,
                                               std::nullopt);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        expect(server->get_received_count() == buffer.size());

        // Shutdown server

        server = nullptr;

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        connect_failed_count = 0;
        last_error_message = "";
      }
    }

    dispatcher->terminate();
    dispatcher = nullptr;
  };

  "local_datagram::client large_buffer"_test = [] {
    std::cout << "TEST_CASE(local_datagram::client large_buffer)" << std::endl;

    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    {
      auto server = std::make_unique<test_server>(dispatcher,
                                                  std::nullopt);

      std::string last_error_message;

      auto client = std::make_unique<pqrs::local_datagram::client>(dispatcher,
                                                                   test_constants::server_socket_file_path,
                                                                   std::nullopt,
                                                                   test_constants::server_buffer_size);

      client->error_occurred.connect([&](auto&& error_code) {
        last_error_message = error_code.message();
      });

      client->async_start();

      //
      // buffer.size() == test_constants::server_buffer_size
      //

      {
        std::vector<uint8_t> buffer(test_constants::server_buffer_size, '1');
        client->async_send(buffer);

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        expect(server->get_received_count() == test_constants::server_buffer_size);
        expect(last_error_message == "");
      }

      //
      // buffer.size() > test_constants::server_buffer_size
      //

      {
        std::vector<uint8_t> buffer(test_constants::server_buffer_size + 64, '2');
        client->async_send(buffer);

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        if (server->get_received_count() > test_constants::server_buffer_size) {
          // Linux
          // (31 is server buffer_margin - buffer::send_entry)
          expect(server->get_received_count() == test_constants::server_buffer_size * 2 + 31);
          expect(last_error_message == "");
        } else {
          // macOS
          expect(server->get_received_count() == test_constants::server_buffer_size);
          expect(last_error_message == "Message too long");
        }
      }
    }

    // client_buffer_size > test_constants::server_buffer_size
    {
      auto server = std::make_unique<test_server>(dispatcher,
                                                  std::nullopt);

      std::string last_error_message;

      size_t client_buffer_size = test_constants::server_buffer_size + 32;
      auto client = std::make_unique<pqrs::local_datagram::client>(dispatcher,
                                                                   test_constants::server_socket_file_path,
                                                                   std::nullopt,
                                                                   client_buffer_size);

      client->error_occurred.connect([&](auto&& error_code) {
        last_error_message = error_code.message();
      });

      client->async_start();

      //
      // buffer.size() == test_constants::server_buffer_size
      //

      {
        std::vector<uint8_t> buffer(test_constants::server_buffer_size, '1');
        client->async_send(buffer);

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        expect(server->get_received_count() == test_constants::server_buffer_size);
        expect(last_error_message == "");
      }

      //
      // buffer.size() == client_buffer_size
      //

      {
        std::vector<uint8_t> buffer(client_buffer_size, '2');
        client->async_send(buffer);

        std::this_thread::sleep_for(std::chrono::milliseconds(2000));

        if (server->get_received_count() > test_constants::server_buffer_size) {
          // Linux
          // (31 is server buffer_margin - buffer::send_entry)
          expect(server->get_received_count() == test_constants::server_buffer_size * 2 + 31);
          expect(last_error_message == "");
        } else {
          // macOS
          expect(server->get_received_count() == test_constants::server_buffer_size);
          expect(last_error_message == "No buffer space available");
        }
      }
    }

    dispatcher->terminate();
    dispatcher = nullptr;
  };

  "local_datagram::client processed"_test = [] {
    std::cout << "TEST_CASE(local_datagram::client processed)" << std::endl;

    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    //
    // async_send after client is stopped
    //

    {
      auto server = std::make_unique<test_server>(dispatcher,
                                                  std::nullopt);

      auto client = std::make_unique<pqrs::local_datagram::client>(dispatcher,
                                                                   test_constants::server_socket_file_path,
                                                                   std::nullopt,
                                                                   test_constants::server_buffer_size);

      client->async_start();

      client->async_stop();

      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      int processed_count = 0;
      std::vector<uint8_t> buffer(8, '0');
      client->async_send(buffer, [&] {
        ++processed_count;
      });

      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      expect(processed_count == 1);
    }

    //
    // async_send with error (message_size)
    //

    {
      auto server = std::make_unique<test_server>(dispatcher,
                                                  std::nullopt);

      asio::error_code last_error_code;

      auto client = std::make_unique<pqrs::local_datagram::client>(dispatcher,
                                                                   test_constants::server_socket_file_path,
                                                                   std::nullopt,
                                                                   test_constants::server_buffer_size);

      client->error_occurred.connect([&](auto&& error_code) {
        last_error_code = error_code;
      });

      client->async_start();

      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      int processed_count = 0;
      std::vector<uint8_t> buffer(test_constants::server_buffer_size * 2, '0');
      client->async_send(buffer, [&] {
        ++processed_count;
      });

      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      expect(processed_count == 1);
      expect(last_error_code == asio::error::message_size);
    }

    dispatcher->terminate();
    dispatcher = nullptr;
  };

  "local_datagram::client bidirectional"_test = [] {
    std::cout << "TEST_CASE(local_datagram::client bidirectional)" << std::endl;

    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    {
      auto server = std::make_unique<test_server>(dispatcher,
                                                  std::nullopt);

      auto client = std::make_unique<test_client>(dispatcher,
                                                  std::nullopt,
                                                  true);

      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      for (int i = 0; i < 1000; ++i) {
        client->async_send();
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      expect(client->get_received_count() == 32 * 1000);
    }

    dispatcher->terminate();
    dispatcher = nullptr;
  };

  "local_datagram::client bidirectional check_client_endpoint"_test = [] {
    std::cout << "TEST_CASE(local_datagram::client bidirectional check_client_endpoint)" << std::endl;

    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    {
      auto server = std::make_unique<test_server>(dispatcher,
                                                  std::nullopt);

      auto client = std::make_unique<test_client>(dispatcher,
                                                  std::nullopt,
                                                  true);

      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      std::error_code error_code;
      std::filesystem::remove(test_constants::client_socket_file_path, error_code);

      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      expect(client->get_closed() == true);
    }

    dispatcher->terminate();
    dispatcher = nullptr;
  };

  "local_datagram::client bind_failed"_test = [] {
    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    {
      size_t connected_count = 0;
      size_t connect_failed_count = 0;
      size_t closed_count = 0;

      auto client = std::make_unique<pqrs::local_datagram::client>(dispatcher,
                                                                   test_constants::server_socket_file_path,
                                                                   "/not_found/client_socket.sock",
                                                                   test_constants::server_buffer_size);

      client->connected.connect([&] {
        ++connected_count;
      });

      client->connect_failed.connect([&](auto&& error_code) {
        ++connect_failed_count;
      });

      client->closed.connect([&] {
        ++closed_count;
      });

      client->async_start();

      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      expect(connected_count == 0);
      expect(connect_failed_count == 1);
      expect(closed_count == 0);
    }

    dispatcher->terminate();
    dispatcher = nullptr;
  };

  "local_datagram::client::server_socket_file_path_resolver"_test = [] {
    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    {
      size_t connected_count = 0;
      size_t connect_failed_count = 0;
      size_t closed_count = 0;

      auto server = std::make_unique<test_server>(dispatcher,
                                                  std::nullopt);

      auto client = std::make_unique<pqrs::local_datagram::client>(dispatcher,
                                                                   "/not_found/server_socket.sock",
                                                                   std::nullopt,
                                                                   test_constants::server_buffer_size);
      client->set_server_socket_file_path_resolver([] {
        return test_constants::server_socket_file_path;
      });

      client->connected.connect([&] {
        ++connected_count;
      });

      client->connect_failed.connect([&](auto&& error_code) {
        ++connect_failed_count;
      });

      client->closed.connect([&] {
        ++closed_count;
      });

      client->async_start();

      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      expect(connected_count == 1);
      expect(connect_failed_count == 0);
      expect(closed_count == 0);
    }

    dispatcher->terminate();
    dispatcher = nullptr;
  };
}
