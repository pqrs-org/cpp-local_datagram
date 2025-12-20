#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <pqrs/local_datagram.hpp>
#include <stdlib.h>

namespace extra_peer_manager_example {

enum class payload_type {
  handshake,
  shared_secret,
  message,
  message_response,
};

NLOHMANN_JSON_SERIALIZE_ENUM(
    payload_type,
    {
        {payload_type::handshake, "handshake"},
        {payload_type::shared_secret, "shared_secret"},
        {payload_type::message, "message"},
        {payload_type::message_response, "message_response"},
    });

void run(std::shared_ptr<pqrs::dispatcher::dispatcher> dispatcher) {
  size_t server_buffer_size = 32 * 1024;

  auto peer_manager = std::make_shared<pqrs::local_datagram::extra::peer_manager>(
      dispatcher,
      server_buffer_size,
      [](auto&& peer_pid,
         auto&& peer_socket_file_path) {
        // Note: Peer verification should be added here.
        // Otherwise, anyone could obtain a valid secret simply by sending a payload_type::handshake.
        return true;
      });

  std::filesystem::path server_socket_file_path("tmp/extra_peer_manager_example_server.sock");
  std::filesystem::path client_socket_file_path("tmp/extra_peer_manager_example_client.sock");

  //
  // Server
  //

  auto server = std::make_unique<pqrs::local_datagram::server>(dispatcher,
                                                               server_socket_file_path,
                                                               server_buffer_size);

  {
    auto wait = pqrs::make_thread_wait();

    server->bound.connect([wait] {
      wait->notify();
    });

    server->received.connect([peer_manager](auto&& buffer, auto&& sender_endpoint) {
      try {
        std::string json_string(std::begin(*buffer), std::end(*buffer));
        auto json = nlohmann::json::parse(json_string);

        std::cout << "server received:" << json << std::endl;

        switch (json["type"].get<payload_type>()) {
          case payload_type::handshake: {
            std::vector<uint8_t> secret(32);
            arc4random_buf(secret.data(), secret.size());

            peer_manager->insert_shared_secret(sender_endpoint->path(),
                                               secret);

            nlohmann::json payload({
                {"type", payload_type::shared_secret},
                {"secret", secret},
            });
            auto s = payload.dump();
            peer_manager->async_send(sender_endpoint->path(),
                                     std::vector<uint8_t>(std::begin(s),
                                                          std::end(s)));
            break;
          }

          case payload_type::message: {
            std::string message_response;
            if (peer_manager->verify_shared_secret(sender_endpoint->path(),
                                                   json["secret"].get<std::vector<uint8_t>>())) {
              message_response = "world";
            } else {
              message_response = "invalid secret";
            }

            nlohmann::json payload({
                {"type", payload_type::message_response},
                {"message_response", message_response},
            });
            auto s = payload.dump();
            peer_manager->async_send(sender_endpoint->path(),
                                     std::vector<uint8_t>(std::begin(s),
                                                          std::end(s)));
            break;
          }

          default:
            break;
        }
      } catch (std::exception& ex) {
        std::cerr << ex.what() << std::endl;
      }
    });

    server->async_start();

    wait->wait_notice();
  }

  //
  // Client
  //

  auto client = std::make_unique<pqrs::local_datagram::client>(dispatcher,
                                                               server_socket_file_path,
                                                               client_socket_file_path,
                                                               server_buffer_size);

  {
    auto wait = pqrs::make_thread_wait();

    client->connected.connect([&client](auto&& peer_pid) {
      nlohmann::json payload({
          {"type", payload_type::handshake},
      });
      auto s = payload.dump();
      client->async_send(std::vector<uint8_t>(std::begin(s),
                                              std::end(s)));
    });

    client->received.connect([wait, &client](auto&& buffer, auto&& sender_endpoint) {
      try {
        std::string json_string(std::begin(*buffer), std::end(*buffer));
        auto json = nlohmann::json::parse(json_string);

        std::cout << "client received:" << json << std::endl;

        switch (json["type"].get<payload_type>()) {
          case payload_type::shared_secret: {
            auto client_shared_key = json["secret"].get<std::vector<uint8_t>>();

            // Enable this code to corrupt the shared secret:
            // client_shared_key[0] = ~client_shared_key[0];

            nlohmann::json payload({
                {"type", payload_type::message},
                {"message", "hello"},
                {"secret", client_shared_key},
            });
            auto s = payload.dump();
            client->async_send(std::vector<uint8_t>(std::begin(s),
                                                    std::end(s)));
            break;
          }

          case payload_type::message_response:
            wait->notify();
            break;

          default:
            break;
        }
      } catch (std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        wait->notify();
      }
    });

    client->async_start();

    wait->wait_notice();
  }
}
} // namespace extra_peer_manager_example
