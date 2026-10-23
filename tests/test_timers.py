# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import json

import planex as px
from helpers import square


def test_timers_count_the_computing_functions():
    px.debug.ResetTimers()
    (square(0, 0, 2) + square(1, 1, 2)).ToOCCFace()
    counts = {t["name"]: t["counts"] for t in px.debug.Timers()}
    for name in ["Boolean", "Boolean classify", "CutAll", "Cut", "Crossing", "Export", "BuildLoops", "BuildFaces"]:
        assert counts["planex " + name] > 0


def test_timers_include_worker_threads():
    px.debug.ResetTimers()
    px.Unify([square(i, 0, 2) for i in range(16)])
    counts = {t["name"]: t["counts"] for t in px.debug.Timers()}
    assert counts["planex Boolean"] == 15


def test_trace_has_an_event_per_boolean(tmp_path):
    px.debug.StartTrace()
    px.Unify([square(i, 0, 2) for i in range(16)])
    px.debug.StopTrace(str(tmp_path / "trace.json"))
    events = json.loads((tmp_path / "trace.json").read_text())["traceEvents"]
    assert sum(e["name"] == "planex Boolean" for e in events) == 15
    assert all(e["ph"] == "X" and e["dur"] >= 0 for e in events)
