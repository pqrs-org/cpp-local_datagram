#include "test.hpp"
#include <boost/ut.hpp>

void run_extra_peer_manager_test(void) {
  using namespace boost::ut;
  using namespace boost::ut::literals;

  "peer_manager_test"_test = [] {
    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    auto peer_manager = std::make_shared<pqrs::local_datagram::extra::peer_manager>(dispatcher,
                                                                                    test_constants::server_buffer_size,
                                                                                    [](std::optional<pid_t> peer_pid) {
                                                                                      // verified
                                                                                      return true;
                                                                                    });

    //
    // Create server
    //

    auto server = std::make_unique<pqrs::local_datagram::server>(dispatcher,
                                                                 test_constants::server_socket_file_path,
                                                                 test_constants::server_buffer_size);

    {
      auto wait = pqrs::make_thread_wait();

      server->bound.connect([wait] {
        wait->notify();
      });

      server->received.connect([peer_manager](auto&& buffer, auto&& sender_endpoint) {
        peer_manager->async_send(sender_endpoint->path(),
                                 {42});
      });

      server->async_start();

      wait->wait_notice();
    }

    //
    // Create client
    //

    {
      auto client = std::make_unique<pqrs::local_datagram::client>(dispatcher,
                                                                   test_constants::server_socket_file_path,
                                                                   test_constants::client_socket_file_path,
                                                                   test_constants::server_buffer_size);

      auto connected_wait = pqrs::make_thread_wait();
      auto received_wait = pqrs::make_thread_wait();
      int received_count = 0;

      client->connected.connect([connected_wait](auto&& peer_pid) {
        connected_wait->notify();
      });

      client->received.connect([received_wait, &received_count](auto&& buffer, auto&& sender_endpoint) {
        expect(1 == buffer->size());
        expect(42 == (*buffer)[0]);

        ++received_count;
        if (received_count == 2) {
          received_wait->notify();
        }
      });

      client->async_start();

      connected_wait->wait_notice();

      client->async_send(std::vector<uint8_t>({0}));
      client->async_send(std::vector<uint8_t>({0}));

      received_wait->wait_notice();
    }

    server = nullptr;
    peer_manager = nullptr;

    dispatcher->terminate();
    dispatcher = nullptr;
  };
}
