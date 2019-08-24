#pragma once

// (C) Copyright Takayama Fumihiko 2018.
// Distributed under the Boost Software License, Version 1.0.
// (See http://www.boost.org/LICENSE_1_0.txt)

// `pqrs::local_datagram::impl::client_send_entry` can be used safely in a multi-threaded environment.

#include "../request_id.hpp"
#include <optional>
#include <vector>

namespace pqrs {
namespace local_datagram {
namespace impl {
class client_send_entry final {
public:
  // Sending empty data causes `No buffer space available` error after wake up on macOS.
  // We append `type` into the beginning of data in order to avoid this issue.

  enum class type : uint8_t {
    server_check,
    user_data,
  };

  client_send_entry(type t,
                    std::optional<request_id> request_id) : request_id_(request_id) {
    buffer_.push_back(static_cast<uint8_t>(t));
  }

  client_send_entry(type t,
                    std::optional<request_id> request_id,
                    const std::vector<uint8_t>& v) : request_id_(request_id) {
    buffer_.push_back(static_cast<uint8_t>(t));

    std::copy(std::begin(v),
              std::end(v),
              std::back_inserter(buffer_));
  }

  client_send_entry(type t,
                    std::optional<request_id> request_id,
                    const uint8_t* p,
                    size_t length) : request_id_(request_id) {
    buffer_.push_back(static_cast<uint8_t>(t));

    if (p && length > 0) {
      std::copy(p,
                p + length,
                std::back_inserter(buffer_));
    }
  }

  const std::optional<request_id>& get_request_id(void) const {
    return request_id_;
  }

  const std::vector<uint8_t>& get_buffer(void) const {
    return buffer_;
  }

private:
  std::optional<request_id> request_id_;
  std::vector<uint8_t> buffer_;
};
} // namespace impl
} // namespace local_datagram
} // namespace pqrs
