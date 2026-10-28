// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <taskflow/taskflow.hpp>

namespace planex2d {

    // Emscripten without threads (Pyodide) cannot start the executor's workers: everything runs in the calling thread
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    inline constexpr bool threadsAvailable = false;
#else
    inline constexpr bool threadsAvailable = true;
#endif

    inline tf::Executor &OwnExecutor() {
        static tf::Executor executor;
        return executor;
    }
    // every extension module has its own statics: the OCC bindings point executorOf at the core's thread pool
    inline tf::Executor &(*executorOf)() = OwnExecutor;
    inline tf::Executor &Executor() { return executorOf(); }

}
