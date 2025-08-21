#pragma once

#include "core.hpp"
#include "adaptors.hpp"
#include "fs.hpp"
#include "socket.hpp"

#if !defined(__linux__)

#if !defined(_WIN32)
#define WEBCRAFT_MOCK_FS_TESTS
#endif

#define WEBCRAFT_MOCK_SOCKET_TESTS
#define WEBCRAFT_MOCK_LISTENER_TESTS
#endif