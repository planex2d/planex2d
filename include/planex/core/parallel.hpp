// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#ifdef PLANEX_THREADS
namespace tf {
    class Executor;
}
#endif

namespace planex2d {

#ifdef PLANEX_THREADS
    tf::Executor &OwnExecutor();
    // every extension module has its own statics: the OCC bindings point executorOf at the core's thread pool
    inline tf::Executor &(*executorOf)() = OwnExecutor;
    inline tf::Executor &Executor() { return executorOf(); }
#endif

}
