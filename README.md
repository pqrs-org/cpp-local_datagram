[![Build Status](https://github.com/pqrs-org/cpp-local_datagram/workflows/CI/badge.svg)](https://github.com/pqrs-org/cpp-local_datagram/actions)
[![License](https://img.shields.io/badge/license-Boost%20Software%20License-blue.svg)](https://github.com/pqrs-org/cpp-local_datagram/blob/master/LICENSE.md)

# cpp-local_datagram

local datagram server and client.

- Server and client work asynchronously. (using pqrs::dispatcher)
- Server and client can be used safely in a multi-threaded environment.
- Server will restart automatically when service is down. (e.g., when the socket file is removed.)
- Client will reconnect automatically when the connection is closed unintendedly. (e.g., when the server is down.)

## Requirements

cpp-local_datagram depends the following classes.

- [asio](https://github.com/chriskohlhoff/asio/)
- [Nod](https://github.com/fr00b0/nod)
- [pqrs::dispatcher](https://github.com/pqrs-org/cpp-dispatcher)

## Install

### Using package manager

You can install `include/pqrs` by using [cget](https://github.com/pfultz2/cget).

```shell
cget install pqrs-org/cget-recipes
cget install pqrs-org/cpp-local_datagram
```

### Manual install

Copy `include/pqrs` directory into your include directory.
