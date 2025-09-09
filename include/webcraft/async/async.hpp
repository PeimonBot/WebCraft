#pragma once

///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Aditya Rao
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////

#include "async_event.hpp"
#include "async_generator.hpp"
#include "awaitable.hpp"
#include "event_signal.hpp"
#include "fire_and_forget_task.hpp"
#include "generator.hpp"
#include "runtime.hpp"
#include "sync_wait.hpp"
#include "task_completion_source.hpp"
#include "task.hpp"
#include "thread_pool.hpp"
#include "when_all.hpp"
#include "when_any.hpp"
#include <webcraft/async/io/io.hpp>

#define co_async [&]() -> ::webcraft::async::task<void>
#define async_t(T) ::webcraft::async::task<T>