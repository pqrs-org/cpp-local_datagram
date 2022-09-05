#include "test.hpp"
#include <boost/ut.hpp>

void run_next_heartbeat_deadline_test(void) {
  using namespace boost::ut;
  using namespace boost::ut::literals;

  "next_heartbeat_deadline"_test = [] {
    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

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

      auto counts = server->get_next_heartbeat_deadline_exceeded_counts();
      expect(counts.size() == 1_i);
      expect(counts[test_constants::client_socket_file_path] == 0_i);
      expect(counts[test_constants::client_socket2_file_path] == 1_i);
    }

    dispatcher->terminate();
    dispatcher = nullptr;
  };
}
