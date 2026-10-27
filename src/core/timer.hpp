// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace planex2d {

    using Clock = std::chrono::steady_clock;

    // trace = false for timers that run per edge or ray: counted, but a trace would get an event per call
    class Timer {
        private:
            int id;
            bool trace;

        public:
            explicit Timer(const std::string &name, bool trace = true);
            int Id() const { return id; }
            bool Traced() const { return trace; }
    };

    // every thread sums into its own table, Totals/Reset/traces are meant for when nothing is computing
    class Profiler {
        private:
            struct Event {
                int timer;
                Clock::time_point start;
                Clock::time_point stop;
            };
            struct ThreadData {
                int id = 0;
                std::vector<Clock::duration> time;
                std::vector<size_t> counts;
                std::vector<Event> events;
            };

            std::mutex mutex;
            std::vector<std::string> names;
            std::vector<std::unique_ptr<ThreadData>> threads;
            std::atomic<bool> tracing = false;
            Clock::time_point traceStart;

            ThreadData &Local() {
                thread_local ThreadData *data = nullptr;
                if (!data) {
                    std::lock_guard lock(mutex);
                    data = threads.emplace_back(std::make_unique<ThreadData>()).get();
                    data->id = (int)threads.size() - 1;
                }
                return *data;
            }

        public:
            struct Total {
                double time = 0.0;
                size_t counts = 0;
            };

            static Profiler &Own() {
                static Profiler p;
                return p;
            }
            // every extension module has its own statics: the OCC bindings point of at the core's profiler
            static inline Profiler &(*of)() = Own;
            static Profiler &Get() { return of(); }
            int Register(const std::string &name) {
                std::lock_guard lock(mutex);
                names.push_back(name);
                return (int)names.size() - 1;
            }
            void Record(const Timer &t, Clock::time_point start, Clock::time_point stop) {
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
            std::map<std::string, Total> Totals() {
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
            void Reset() {
                std::lock_guard lock(mutex);
                for (const auto &d : threads) {
                    std::fill(d->time.begin(), d->time.end(), Clock::duration::zero());
                    std::fill(d->counts.begin(), d->counts.end(), 0);
                    d->events.clear();
                }
            }
            void StartTrace() {
                std::lock_guard lock(mutex);
                for (const auto &d : threads)
                    d->events.clear();
                traceStart = Clock::now();
                tracing = true;
            }
            // Chrome trace event format: ui.perfetto.dev or chrome://tracing, one track per thread
            void StopTrace(const std::string &filename) {
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
    };

    inline Timer::Timer(const std::string &name, bool trace) : id(Profiler::Get().Register(name)), trace(trace) {}

    class Region {
        private:
            const Timer &timer;
            Clock::time_point start;

        public:
            explicit Region(const Timer &t) : timer(t), start(Clock::now()) {}
            ~Region() { Profiler::Get().Record(timer, start, Clock::now()); }
            Region(const Region &) = delete;
            Region &operator=(const Region &) = delete;
    };

}
