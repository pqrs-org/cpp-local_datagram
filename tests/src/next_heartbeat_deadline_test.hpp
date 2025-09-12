#include "test.hpp"
#include <boost/ut.hpp>

void run_next_heartbeat_deadline_test(void) {
  using namespace boost::ut;
  using namespace boost::ut::literals;
  using namespace std::string_literals;

  "next_heartbeat_deadline (server)"_test = [] {
    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    // No next_heartbeat_deadline
    {
      auto server = std::make_unique<test_server>(dispatcher,
                                                  std::nullopt);

      auto client1 = std::make_unique<pqrs::local_datagram::client>(dispatcher,
                                                                    test_constants::server_socket_file_path,
                                                                    test_constants::client_socket_file_path,
                                                                    test_constants::server_buffer_size);
      client1->set_server_check_interval(std::chrono::milliseconds(500));
      client1->async_start();

      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      expect(server->get_warning_message() == ""s);

      auto counts = server->get_next_heartbeat_deadline_exceeded_counts();
      expect(counts.size() == 0_i);
    }

    // Enough next_heartbeat_deadline
    {
      auto server = std::make_unique<test_server>(dispatcher,
                                                  std::nullopt);

      auto client1 = std::make_unique<pqrs::local_datagram::client>(dispatcher,
                                                                    test_constants::server_socket_file_path,
                                                                    test_constants::client_socket_file_path,
                                                                    test_constants::server_buffer_size);
      client1->set_server_check_interval(std::chrono::milliseconds(500));
      client1->set_next_heartbeat_deadline(std::chrono::milliseconds(1500));
      client1->async_start();

      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      expect(server->get_warning_message() == ""s);

      auto counts = server->get_next_heartbeat_deadline_exceeded_counts();
      expect(counts.size() == 0_i);
    }

    // Small next_heartbeat_deadline
    {
      auto server = std::make_unique<test_server>(dispatcher,
                                                  std::nullopt);

      auto client1 = std::make_unique<pqrs::local_datagram::client>(dispatcher,
                                                                    test_constants::server_socket_file_path,
                                                                    test_constants::client_socket_file_path,
                                                                    test_constants::server_buffer_size);
      client1->set_server_check_interval(std::chrono::milliseconds(500));
      client1->set_next_heartbeat_deadline(std::chrono::milliseconds(1500));
      client1->async_start();

      auto client2 = std::make_unique<pqrs::local_datagram::client>(dispatcher,
                                                                    test_constants::server_socket_file_path,
                                                                    test_constants::client_socket2_file_path,
                                                                    test_constants::server_buffer_size);
      client2->set_server_check_interval(std::chrono::milliseconds(800));
      client2->set_next_heartbeat_deadline(std::chrono::milliseconds(300));
      client2->async_start();

      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      expect(server->get_warning_message() == ""s);

      auto counts = server->get_next_heartbeat_deadline_exceeded_counts();
      expect(counts.size() == 1_i);
      expect(counts[test_constants::client_socket_file_path] == 0_i);
      expect(counts[test_constants::client_socket2_file_path] == 1_i);
    }

    dispatcher->terminate();
    dispatcher = nullptr;
  };

  "next_heartbeat_deadline (client)"_test = [] {
    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    {
      auto server = std::make_unique<pqrs::local_datagram::server>(dispatcher,
                                                                   test_constants::server_socket_file_path,
                                                                   test_constants::server_buffer_size);

      std::shared_ptr<pqrs::local_datagram::client> client_in_server;

      server->received.connect([&client_in_server, &dispatcher](auto&& buffer, auto&& sender_endpoint) {
        if (pqrs::local_datagram::non_empty_filesystem_endpoint_path(*sender_endpoint)) {
          client_in_server = std::make_shared<pqrs::local_datagram::client>(dispatcher,
                                                                            sender_endpoint->path(),
                                                                            test_constants::client_socket2_file_path,
                                                                            test_constants::server_buffer_size);
          client_in_server->set_server_check_interval(std::chrono::milliseconds(500));
          client_in_server->set_next_heartbeat_deadline(std::chrono::milliseconds(100));
          client_in_server->async_start();
        }
      });

      server->async_start();

      // Wait for a while until server is prepared
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      auto client = std::make_unique<pqrs::local_datagram::client>(dispatcher,
                                                                   test_constants::server_socket_file_path,
                                                                   test_constants::client_socket_file_path,
                                                                   test_constants::server_buffer_size);
      client->set_server_check_interval(std::chrono::milliseconds(500));

      client->connected.connect([&client](auto&& peer_pid) {
        std::vector<uint8_t> client_buffer(32);
        client_buffer[0] = 'c';
        client_buffer[1] = 'o';
        client_buffer[2] = 'n';
        client_buffer[3] = 'n';
        client_buffer[3] = '\0';
        client->async_send(client_buffer);
      });

      client->connect_failed.connect([](auto&& error_code) {
        std::cout << error_code.message() << std::endl;
      });

      int next_heartbeat_deadline_exceeded_count = 0;

      client->next_heartbeat_deadline_exceeded.connect([&next_heartbeat_deadline_exceeded_count](auto&& sender_endpoint) {
        if (sender_endpoint && sender_endpoint->path() == test_constants::client_socket2_file_path) {
          ++next_heartbeat_deadline_exceeded_count;
        }
      });

      client->async_start();

      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      expect(next_heartbeat_deadline_exceeded_count > 0_i);
    }

    dispatcher->terminate();
    dispatcher = nullptr;
  };

  "next_heartbeat_deadline (no sender_endpoint)"_test = [] {
    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    // No next_heartbeat_deadline
    {
      auto server = std::make_unique<test_server>(dispatcher,
                                                  std::nullopt);

      auto client1 = std::make_unique<pqrs::local_datagram::client>(dispatcher,
                                                                    test_constants::server_socket_file_path,
                                                                    std::nullopt,
                                                                    test_constants::server_buffer_size);
      client1->set_server_check_interval(std::chrono::milliseconds(500));
      client1->set_next_heartbeat_deadline(std::chrono::milliseconds(100));
      client1->async_start();

      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      expect(server->get_warning_message() == "sender endpoint is required when next_heartbeat_deadline is specified"s);
    }

    dispatcher->terminate();
    dispatcher = nullptr;
  };
}
