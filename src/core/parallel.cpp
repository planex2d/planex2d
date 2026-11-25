// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/core/parallel.hpp"

#ifdef PLANEX_THREADS
#include <taskflow/taskflow.hpp>

namespace planex2d {

    tf::Executor &OwnExecutor() {
        static tf::Executor executor;
        return executor;
    }

}
#endif
