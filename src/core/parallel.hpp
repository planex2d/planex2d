// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <taskflow/taskflow.hpp>

namespace planex2d {

    inline tf::Executor &Executor() {
        static tf::Executor executor;
        return executor;
    }

}
