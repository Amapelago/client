// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// The Archipelago net stack's configuration: apclientpp on wswrap on websocketpp on standalone asio, with C++11 threads and no Boost. apclientpp
// validates no packet schemas and keeps no data package cache of its own, so it writes nothing to disk. wswrap frees a closed socket on a thread of
// its own when the close happens inside a callback. Every file that uses any part of the stack includes this header before anything else, so all
// of them see the same configuration.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#define ASIO_STANDALONE
#define _WEBSOCKETPP_CPP11_STL_
#define _WEBSOCKETPP_CPP11_THREAD_
#define _WEBSOCKETPP_NO_BOOST_
#define AP_NO_SCHEMA
#define AP_NO_DEFAULT_DATA_PACKAGE_STORE
#define WSWRAP_ASYNC_CLEANUP
#include <apclient.hpp>
