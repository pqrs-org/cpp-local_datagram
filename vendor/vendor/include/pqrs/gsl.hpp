#pragma once

// pqrs::gsl v1.0

// (C) Copyright Takayama Fumihiko 2025.
// Distributed under the Boost Software License, Version 1.0.
// (See https://www.boost.org/LICENSE_1_0.txt)

#include <gsl/gsl>

namespace pqrs {
template <typename T>
using not_null_shared_ptr_t = gsl::not_null<std::shared_ptr<T>>;
}
