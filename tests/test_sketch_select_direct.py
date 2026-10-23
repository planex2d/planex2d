# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px

PARTS = {"Line": ("start", "end", "mid"), "Arc": ("start", "end", "center"), "Circle": ("center",)}


def sketch():
    sk = px.Sketch()
    sk.Rectangle(20, 10, id="plate")
    sk.Offset("plate", 2, id="wall")
    sk.Slot((40, 0), (40, 10), 2, id="groove")
    return sk


def keys(sk):
    out = []
    for kind, ids in sk.elements.items():
        if kind not in ("Point", "Line", "Arc", "Circle", "Spline"):
            continue
        ids = [i for i in ids if i != "origin"]
        out += ids
        out += [i + "." + part for i in ids for part in PARTS.get(kind, ())]
    return out


def answers(sk):
    return {key: [x.id for x in sk(key)] for key in keys(sk)}


def test_the_direct_lookup_gives_what_the_full_list_gives(monkeypatch):
    sk = sketch()
    for step in ("whole", "removed", "renamed"):
        if step == "removed":
            sk.Remove("wall")
        if step == "renamed":
            sk.Remove("groove")
            sk.Line(3)
        fast = answers(sk)
        with monkeypatch.context() as m:
            m.setattr(type(sk), "_KindById", lambda self, key: None)
            slow = answers(sk)
        assert fast == slow
        assert len(fast) > 20


def test_an_id_with_ids_under_it_gives_them_too():
    sk = sketch()
    parent = "wall.plate.Line.1"
    assert [x.id for x in sk(parent)][-1].endswith(parent + ".corner")
    assert sk._KindById("plate.Line.1") == "Line"
    sk.Remove("wall")
    assert sk._KindById("groove.spine") == "Line"
    sk.Remove("groove")
    sk.Offset("plate", 1, id="again")
    assert [x.local for x in sk("again.plate.Line.1")][0] == "again.plate.Line.1"
