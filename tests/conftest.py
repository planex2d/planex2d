# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import os
import re
import sys
import time

import pytest

FUZZ_SECONDS = 30.0
NETGEN_IMPORT = re.compile(r"^(?:import|from) (?:netgen|ngsolve)\b", re.M)


def pytest_addoption(parser):
    parser.addoption("--fuzz-seconds", type=float, default=None,
                     help="time budget of all randomized fuzz tests together, in seconds (default 30, or "
                          "$PLANEX_FUZZ_SECONDS)")


def pytest_configure(config):
    config.addinivalue_line("markers", "fuzz: a randomized test that shares the fuzz time budget")
    config.addinivalue_line("markers", "constraints: needs px.Sketch, the constraint sketch (CMake PLANEX_CONSTRAINTS)")
    config.addinivalue_line("markers", "occ: needs planex's OCC module (CMake PLANEX_OCC, netgen installed)")
    config._fuzz_cases = {}


def pytest_collection_modifyitems(config, items):
    config._fuzz_count = max(1, sum(1 for item in items if item.get_closest_marker("fuzz")))
    import planex
    if not hasattr(planex._planex, "Sketch"):
        skip = pytest.mark.skip(reason="planex is built without the constraint sketch (PLANEX_CONSTRAINTS=OFF)")
        for item in items:
            if item.get_closest_marker("constraints"):
                item.add_marker(skip)
    if not planex._occ.loaded:
        skip = pytest.mark.skip(reason="planex has no OCC module (PLANEX_OCC=OFF, or netgen not installed)")
        for item in items:
            if item.get_closest_marker("occ"):
                item.add_marker(skip)


def _occ_loaded():
    import planex
    return planex._occ.loaded


def _needs_occ(e):
    if isinstance(e, ModuleNotFoundError):
        return (e.name or "").split(".")[0] in ("netgen", "ngsolve")
    return "planex's OCC integration" in str(e)


# a planex without its OCC module (PLANEX_OCC=OFF, or no netgen): a test file that imports netgen or ngsolve, and a test
# that reaches OCC, are skipped
@pytest.hookimpl(wrapper=True)
def pytest_make_collect_report(collector):
    report = yield
    if (report.failed and isinstance(collector, pytest.Module) and not _occ_loaded() and
            NETGEN_IMPORT.search(collector.path.read_text())):
        report.outcome = "skipped"
        report.longrepr = (str(collector.path), 0, "Skipped: imports netgen or ngsolve, and planex has no OCC module")
    return report


def _skip_without_occ():
    try:
        return (yield)
    except Exception as e:
        if _needs_occ(e) and not _occ_loaded():
            pytest.skip(f"planex has no OCC module: {e}")
        raise


@pytest.hookimpl(wrapper=True)
def pytest_runtest_setup(item):
    return (yield from _skip_without_occ())


@pytest.hookimpl(wrapper=True)
def pytest_runtest_call(item):
    return (yield from _skip_without_occ())


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
