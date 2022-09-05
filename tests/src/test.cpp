#include "client_test.hpp"
#include "next_heartbeat_deadline_test.hpp"
#include "server_test.hpp"

int main(void) {
  run_client_test();
  run_next_heartbeat_deadline_test();
  run_server_test();

  return 0;
}
