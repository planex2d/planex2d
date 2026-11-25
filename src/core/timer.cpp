// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/core/timer.hpp"

#include <fstream>
#include <iomanip>

namespace planex2d {

    void Profiler::Record(const Timer &t, Clock::time_point start, Clock::time_point stop) {
        ThreadData &d = Local();
        size_t i = (size_t)t.Id();
        if (i >= d.time.size()) {
            d.time.resize(i + 1, Clock::duration::zero());
            d.counts.resize(i + 1, 0);
        }
        d.time[i] += stop - start;
        ++d.counts[i];
        if (t.Traced() && tracing.load(std::memory_order_relaxed))
            d.events.push_back({t.Id(), start, stop});
    }

    std::map<std::string, Profiler::Total> Profiler::Totals() {
        std::lock_guard lock(mutex);
        std::map<std::string, Total> totals;
        for (const std::string &name : names)
            totals[name];
        for (const auto &d : threads)
            for (size_t i = 0; i < d->time.size(); ++i) {
                Total &t = totals[names[i]];
                t.time += std::chrono::duration<double>(d->time[i]).count();
                t.counts += d->counts[i];
            }
        return totals;
    }

    // Chrome trace event format: ui.perfetto.dev or chrome://tracing, one track per thread
    void Profiler::StopTrace(const std::string &filename) {
        tracing = false;
        std::lock_guard lock(mutex);
        std::ofstream out(filename);
        out << std::fixed << std::setprecision(3) << "{\"traceEvents\": [";
        const char *sep = "\n";
        for (const auto &d : threads) {
            for (const Event &e : d->events) {
                double ts = std::chrono::duration<double, std::micro>(e.start - traceStart).count();
                double dur = std::chrono::duration<double, std::micro>(e.stop - e.start).count();
                out << sep << "{\"name\": \"" << names[(size_t)e.timer] << "\", \"ph\": \"X\", \"ts\": " << ts
                    << ", \"dur\": " << dur << ", \"pid\": 0, \"tid\": " << d->id << "}";
                sep = ",\n";
            }
            d->events.clear();
        }
        out << "\n]}\n";
    }

}
