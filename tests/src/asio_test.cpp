#include <boost/ut.hpp>
#include <pqrs/local_datagram.hpp>

int main(void) {
  using namespace boost::ut;
  using namespace boost::ut::literals;

  "asio"_test = [] {
#ifdef ASIO_STANDALONE
    expect(false);
#else
    expect(true);
#endif
  };

  return 0;
}
