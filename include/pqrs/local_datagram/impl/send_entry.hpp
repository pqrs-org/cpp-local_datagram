#pragma once

// (C) Copyright Takayama Fumihiko 2018.
// Distributed under the Boost Software License, Version 1.0.
// (See http://www.boost.org/LICENSE_1_0.txt)

// `pqrs::local_datagram::impl::send_entry` can be used safely in a multi-threaded environment.

#include <optional>
#include <vector>

namespace pqrs {
namespace local_datagram {
namespace impl {
class send_entry final {
public:
  // Sending empty data causes `No buffer space available` error after wake up on macOS.
  // We append `type` into the beginning of data in order to avoid this issue.

  enum class type : uint8_t {
    server_check,
    user_data,
    response,
  };

  send_entry(type t,
             const std::function<void(void)>& processed = nullptr) : bytes_transferred_(0),
                                                                     processed_(processed) {
    buffer_.push_back(static_cast<uint8_t>(t));
  }

  send_entry(type t,
             const std::vector<uint8_t>& v,
             const std::function<void(void)>& processed = nullptr) : bytes_transferred_(0),
                                                                     processed_(processed) {
    buffer_.push_back(static_cast<uint8_t>(t));

    std::copy(std::begin(v),
              std::end(v),
              std::back_inserter(buffer_));
  }

  send_entry(type t,
             const uint8_t* p,
             size_t length,
             const std::function<void(void)>& processed = nullptr) : bytes_transferred_(0),
                                                                     processed_(processed) {
    buffer_.push_back(static_cast<uint8_t>(t));

    if (p && length > 0) {
      std::copy(p,
                p + length,
                std::back_inserter(buffer_));
    }
  }

  const std::vector<uint8_t>& get_buffer(void) const {
    return buffer_;
  }

  size_t get_bytes_transferred(void) const {
    return bytes_transferred_;
  }

  void set_bytes_transferred(size_t value) {
    bytes_transferred_ = value;
  }

  const std::function<void(void)>& get_processed(void) const {
    return processed_;
  }

private:
  std::vector<uint8_t> buffer_;
  size_t bytes_transferred_;
  std::function<void(void)> processed_;
};
} // namespace impl
} // namespace local_datagram
} // namespace pqrs
