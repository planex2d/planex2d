# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""The type stub of the compiled module ships with the package and is the one tools/stubgen.py writes from the
docstrings now."""
import importlib.util
import sys
from pathlib import Path

import pytest

import planex

ROOT = Path(__file__).resolve().parents[1]


def test_the_package_ships_the_stub():
    package = Path(planex.__file__).parent
    assert (package / "_planex.pyi").is_file() and (package / "py.typed").is_file()


@pytest.mark.occ
@pytest.mark.skipif(sys.version_info < (3, 12), reason="Python 3.11 drops the docstrings of pybind11's static properties, "
                    "so stubgen cannot read the types of px.config's settings")
def test_the_stub_is_current():
    spec = importlib.util.spec_from_file_location("stubgen", ROOT / "tools" / "stubgen.py")
    stubgen = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(stubgen)
    committed = (ROOT / "src" / "python" / "planex" / "_planex.pyi").read_text()
    assert stubgen.Stub() == committed, "the stub is stale: run python tools/stubgen.py"
