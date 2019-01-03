#include <catch2/catch.hpp>

#define ASIO_STANDALONE
#include <pqrs/local_datagram.hpp>

TEST_CASE("asio_standalone") {
#ifdef ASIO_STANDALONE
  REQUIRE(true);
#else
  REQUIRE(false);
#endif
}
