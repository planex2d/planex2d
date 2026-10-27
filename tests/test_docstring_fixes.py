# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""docstrings Sphinx reads as written: id once in a signature, no bare asterisks, no attribute listed twice"""
import re

import pytest

import planex as px


def test_move_lists_id_once():
    lines = [line for line in px.Move.__doc__.splitlines() if line.startswith("Move(")]
    assert lines
    for line in lines:
        assert len(re.findall(r"(?:\(|, )id: ", line)) == 1, line
    assert sum(line.startswith("    id:") for line in px.Move.__doc__.splitlines()) == 1


def emphasis(doc):
    """the asterisks outside double backticks that reST would read as the start of emphasis (signature lines are read
    as Python)"""
    body = "\n".join(line for line in doc.splitlines() if not re.match(r"^\w+\(.*\)( -> .+)?$", line))
    text = re.sub(r"``.*?``", "", body)
    return re.findall(r"(?:^|[\s(\[{'\"])\*+\S", text, flags=re.M)


def test_relate_and_expression_docstrings_have_no_bare_asterisks():
    for doc in (px.Face.Relate.__doc__, px.Shape.Relate.__doc__, px.Wire.Relate.__doc__, px.Sketch.Expression.__doc__):
        assert emphasis(doc) == [], doc


def test_workplane_lists_its_properties_once():
    assert "Attributes:" not in px.WorkPlane.__doc__
    for name in ("cur_loc", "cur_dir", "start_pnt", "sketch"):
        assert getattr(px.WorkPlane, name).__doc__


def test_the_types_module_docstring_is_clean_rst():
    """the API reference documents planex.types: its table is a definition list Sphinx reads without a warning"""
    import inspect
    import io

    core = pytest.importorskip("docutils.core")
    import planex.types

    warnings = io.StringIO()
    core.publish_doctree(inspect.cleandoc(planex.types.__doc__),
                         settings_overrides={"warning_stream": warnings, "report_level": 1})
    assert warnings.getvalue() == ""
