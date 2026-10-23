# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import planex as px

# trace end from a KiCad Gerber layer (e15 B_Cu) whose least-squares spline ran 0.12 mm past its end and came back
CHAIN = [(117.19080, -82.34879), (117.16692, -82.34838), (117.16692, -82.34857), (117.16677, -82.34857),
         (117.16529, -82.34835), (117.15421, -82.34816), (117.14891, -82.34728), (117.14109, -82.34630),
         (117.08832, -82.34848), (117.08630, -82.34857)]


def test_spline_fit_is_validated_against_its_chain():
    wp = px.Move((117.30921, -82.33320))
    for p in CHAIN:
        wp.Line(p)
    wp.Line((116.936, -82.34857))
    w = wp.Wire()
    before = w.mass
    w.Simplify(remove_edges_smaller=0, fit_splines=dict(max_edge_length=0.1, max_edge_angle=25, tol=2e-2, ignore_edges_smaller=5e-3))
    occ = w.ToOCCWires()
    assert abs(sum(e.mass for e in occ.edges) - before) < 0.02 * before
