// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <taskflow/taskflow.hpp>

namespace planex2d {

    inline tf::Executor &OwnExecutor() {
        static tf::Executor executor;
        return executor;
    }
    // every extension module has its own statics: the OCC bindings point executorOf at the core's thread pool
    inline tf::Executor &(*executorOf)() = OwnExecutor;
    inline tf::Executor &Executor() { return executorOf(); }

}
