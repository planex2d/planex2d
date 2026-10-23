# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import os
import sys
import time

import pytest

FUZZ_SECONDS = 30.0


def pytest_addoption(parser):
    parser.addoption("--fuzz-seconds", type=float, default=None,
                     help="time budget of all randomized fuzz tests together, in seconds (default 30, or "
                          "$PLANEX_FUZZ_SECONDS)")


def pytest_configure(config):
    config.addinivalue_line("markers", "fuzz: a randomized test that shares the fuzz time budget")
    config.addinivalue_line("markers", "constraints: needs px.Sketch, the constraint sketch (CMake PLANEX_CONSTRAINTS)")
    config._fuzz_cases = {}


def pytest_collection_modifyitems(config, items):
    config._fuzz_count = max(1, sum(1 for item in items if item.get_closest_marker("fuzz")))
    import planex
    if not hasattr(planex._planex, "Sketch"):
        skip = pytest.mark.skip(reason="planex is built without the constraint sketch (PLANEX_CONSTRAINTS=OFF)")
        for item in items:
            if item.get_closest_marker("constraints"):
                item.add_marker(skip)


@pytest.fixture
def fuzz_budget(request):
    """this test's share of the fuzz time budget in seconds, and a dict to report how many cases it ran"""
    config = request.config
    total = config.getoption("--fuzz-seconds")
    if total is None:
        total = float(os.environ.get("PLANEX_FUZZ_SECONDS", FUZZ_SECONDS))
    report = {"cases": 0, "start": time.perf_counter()}
    yield total / config._fuzz_count, report
    config._fuzz_cases[request.node.nodeid] = (report["cases"], time.perf_counter() - report["start"])


def pytest_terminal_summary(terminalreporter, config):
    cases = getattr(config, "_fuzz_cases", {})
    if not cases:
        return
    terminalreporter.section("fuzz cases")
    for nodeid, (n, seconds) in sorted(cases.items()):
        terminalreporter.write_line(f"{n:7d} cases in {seconds:6.1f} s  {nodeid}")
    fuzzlib = sys.modules.get("fuzzlib")
    refereed = getattr(fuzzlib, "REFEREED", [])
    if refereed:
        terminalreporter.section("fuzz: shapely disagreed, OCC sided with planex")
        for name, spec, off in refereed:
            terminalreporter.write_line(f"{name}: shapely off by {off:.3e}  spec={spec!r}")
    errors = getattr(fuzzlib, "GEOS_ERRORS", [])
    if errors:
        terminalreporter.section("fuzz: GEOS raised (triage: invalid sampled input or a GEOS bug)")
        for name, spec, message in errors:
            terminalreporter.write_line(f"{name}: {message}  spec={spec!r}")
