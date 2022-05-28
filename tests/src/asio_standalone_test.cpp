#define ASIO_STANDALONE

#include <boost/ut.hpp>
#include <pqrs/local_datagram.hpp>

int main(void) {
  using namespace boost::ut;
  using namespace boost::ut::literals;

  "asio_standalone"_test = [] {
#ifdef ASIO_STANDALONE
    expect(true);
#else
    expect(false);
#endif
  };

  return 0;
}
