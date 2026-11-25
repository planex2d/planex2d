// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"

// the stack level of the first frame outside the planex package: the warning names the user's line, not a
// wrapper of the package (1 where no Python frame calls, as in a worker thread)
int CallerStackLevel() {
    try {
        static const std::string package = [] {
            const std::string f = py::module_::import("planex").attr("__file__").cast<std::string>();
            return f.substr(0, f.find_last_of("/\\") + 1);
        }();
        int level = 1;
        for (py::object frame = py::module_::import("sys").attr("_getframe")(0); !frame.is_none(); frame = frame.attr("f_back")) {
            if (frame.attr("f_code").attr("co_filename").cast<std::string>().rfind(package, 0) != 0)
                break;
            ++level;
        }
        return level;
    } catch (const py::error_already_set &) {
        return 1;
    }
}
