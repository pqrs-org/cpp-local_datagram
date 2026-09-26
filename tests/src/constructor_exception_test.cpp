#include <cstdio>
#include <cstdlib>
#include <new>
#include <pqrs/local_datagram.hpp>

namespace {
// Fail one allocation on the constructing thread only. Cleanup and background
// dispatcher/ASIO work must remain able to allocate after the injected failure.
thread_local long allocations_before_failure = -1;
} // namespace

void* operator new(std::size_t size) {
  if (allocations_before_failure >= 0 && allocations_before_failure-- == 0) {
    throw std::bad_alloc();
  }
  if (auto p = std::malloc(size ? size : 1)) {
    return p;
  }
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
  return ::operator new(size);
}

void operator delete(void* p) noexcept {
  std::free(p);
}

void operator delete[](void* p) noexcept {
  ::operator delete(p);
}

namespace {
template <typename Factory>
bool check_constructor(const char* name, Factory factory) {
  int failures = 0;
  for (long index = 0; index < 512; ++index) {
    allocations_before_failure = index;
    try {
      auto object = factory();
      // Do not inject failures into the normal destructor after construction.
      allocations_before_failure = -1;
      if (failures == 0) {
        return false;
      }
      std::printf("%s: recovered from %d allocation failures\n", name, failures);
      return true;
    } catch (const std::bad_alloc&) {
      allocations_before_failure = -1;
      ++failures;
    }
  }
  allocations_before_failure = -1;
  std::fprintf(stderr, "%s: never completed construction\n", name);
  return false;
}
} // namespace

int main() {
  auto source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
  auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(source);
  auto entries = std::make_shared<std::deque<pqrs::not_null_shared_ptr_t<pqrs::local_datagram::impl::send_entry>>>();
  auto endpoint = std::make_shared<asio::local::datagram_protocol::endpoint>();
  bool passed = true;
  passed &= check_constructor("client", [&] {
    return std::make_unique<pqrs::local_datagram::client>(dispatcher, "unused.sock", std::nullopt, 1024);
  });
  passed &= check_constructor("server", [&] {
    return std::make_unique<pqrs::local_datagram::server>(dispatcher, "unused.sock", 1024);
  });
  // Exercise failures in derived members after base_impl starts its I/O thread.
  passed &= check_constructor("client_impl", [&] {
    return std::make_unique<pqrs::local_datagram::impl::client_impl>(dispatcher, entries);
  });
  passed &= check_constructor("server_impl", [&] {
    return std::make_unique<pqrs::local_datagram::impl::server_impl>(dispatcher, entries);
  });
  // Exercise constructor-body failure while a debounced_task already exists.
  passed &= check_constructor("next_heartbeat_deadline_timer", [&] {
    return std::make_unique<pqrs::local_datagram::impl::next_heartbeat_deadline_timer>(dispatcher, endpoint, std::chrono::hours(1));
  });
  return passed ? 0 : 1;
}
