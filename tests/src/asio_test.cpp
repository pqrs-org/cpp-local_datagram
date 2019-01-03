#include <catch2/catch.hpp>

#include <pqrs/local_datagram.hpp>

TEST_CASE("asio") {
#ifdef ASIO_STANDALONE
  REQUIRE(false);
#else
  REQUIRE(true);
#endif
}
