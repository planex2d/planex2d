// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/shape/shapeops.hpp"

#include <map>
#include <stdexcept>

namespace planex2d {

    Shape WireUnion(const ShapeList &shapes) {
        const AxisSystem axes = shapes.CommonAxes();
        Wire c(axes);
        for (const Shape &s : shapes)
            for (const Edge &e : s.Edges())
                c.Add(e);
        std::vector<CutState> state(c.Size());
        Arrange(c, state);
        c.MergeOnCarriers();
        return Shape(axes, {}, c.Wires());
    }

    Shape SharingFaces(const AxisSystem &axes, const FaceList &faces, const WireList &wires) {
        Wire all(axes);
        std::vector<uint32_t> of;
        std::vector<Properties> props;
        for (size_t k = 0; k < faces.size(); ++k) {
            props.push_back(faces[k].Props());
            for (const Edge &e : faces[k].Edges()) {
                all.Add(e);
                of.push_back((uint32_t)k);
            }
        }
        for (size_t k = 0; k < wires.size(); ++k)
            for (const Edge &e : wires[k]) {
                all.Add(e);
                of.push_back((uint32_t)(faces.size() + k));
            }
        const double tol = all.Tol();
        // faces from independent booleans (one per face) can put a vertex they share a rounding apart
        SnapEndpoints(all, tol, [](size_t) { return true; });
        auto key = [](const Edge &e) {
            return std::visit([](const auto &x) { return std::array<Vertex, 3>{x.Start(), x.End(), x.MidPoint()}; }, e);
        };
        std::vector<std::array<Vertex, 3>> keys;
        std::vector<Vertex> mids;
        for (const Edge &e : all) {
            keys.push_back(key(e));
            mids.push_back(keys.back()[2]);
        }
        Grid grid = Grid::Of(mids, tol);
        Wire kept(axes);
        std::vector<std::vector<uint32_t>> owners;
        std::vector<size_t> into(all.Size(), all.Size());
        for (size_t i = 0; i < all.Size(); ++i) {
            size_t same = all.Size();
            grid.ForNear(mids[i], [&](size_t j) {
                if (same == all.Size() && j < i && into[j] < all.Size() && keys[j][2].IsClose(keys[i][2], tol) &&
                    ((keys[j][0].IsClose(keys[i][0], tol) && keys[j][1].IsClose(keys[i][1], tol)) ||
                     (keys[j][0].IsClose(keys[i][1], tol) && keys[j][1].IsClose(keys[i][0], tol))))
                    same = j;
            });
            if (same < all.Size()) {
                into[i] = into[same];
                owners[into[i]].push_back(of[i]);
                // the later face's edge properties come first
                SetProps(kept[into[i]], MergedProperties(PropsOf(all[i]), PropsOf(kept[into[i]])));
                continue;
            }
            into[i] = kept.Size();
            kept.Add(all[i]);
            owners.push_back({of[i]});
        }
        // the later face names a shared edge: its group first
        for (std::vector<uint32_t> &o : owners) {
            std::sort(o.begin(), o.end());
            auto groups = std::lower_bound(o.begin(), o.end(), (uint32_t)faces.size());
            if (groups != o.begin())
                std::rotate(o.begin(), groups - 1, groups);
        }
        return Shape(std::move(kept), owners, std::move(props), wires.size());
    }

    static std::vector<GluePart> GlueParts(const ShapeList &shapes) {
        std::vector<GluePart> parts;
        for (const Shape &s : shapes) {
            WireList groups = s.Groups();
            for (size_t k = 0; k < groups.size(); ++k)
                parts.push_back({groups[k], true, s.GroupProps(k)});
            for (Wire &w : s.WireParts())
                parts.push_back({std::move(w), false, nullptr});
        }
        return parts;
    }

    static Wire ArrangedParts(const Wire &input, const std::vector<uint32_t> &source) {
        Wire c = input;
        c.SetOwners(source, 0);
        std::vector<CutState> state(c.Size());
        for (size_t k = 0; k < c.Size(); ++k)
            state[k].part = source[k];
        Arrange(c, state);
        return c;
    }

    // per face of the arrangement, in list order; the entry after the last face (no face) stays empty
    static std::vector<std::vector<uint32_t>> CoveringParts(const Wire &c, const std::vector<HalfEdges> &faces, const std::vector<GluePart> &parts) {
        std::vector<BBox> boxes;
        for (const GluePart &p : parts)
            boxes.push_back(p.edges.Box());
        std::vector<std::vector<uint32_t>> cover(faces.size() + 1);
        for (size_t f = 0; f < faces.size(); ++f)
            if (std::optional<Vertex> p = EdgesOf(c, faces[f]).InteriorPoint())
                for (size_t k = 0; k < parts.size(); ++k)
                    if (parts[k].face && boxes[k].IsInside(*p) && parts[k].edges.Contains(*p))
                        cover[f].push_back((uint32_t)k);
        return cover;
    }

    // a part is whole if every face it covers has the same cover
    static std::vector<bool> WholeParts(const std::vector<std::vector<uint32_t>> &cover, size_t parts) {
        std::vector<size_t> firstFace(parts, cover.size());
        std::vector<bool> whole(parts, false);
        for (size_t f = 0; f < cover.size(); ++f)
            for (uint32_t k : cover[f]) {
                if (firstFace[k] == cover.size()) {
                    firstFace[k] = f;
                    whole[k] = true;
                } else if (cover[firstFace[k]] != cover[f]) {
                    whole[k] = false;
                }
            }
        return whole;
    }

    // ofFace: props.size() for a face no part covers
    struct GlueGroups {
        std::vector<Properties> props;
        std::vector<uint32_t> ofFace;
    };

    static GlueGroups GroupsOfCovers(const std::vector<std::vector<uint32_t>> &cover, const std::vector<bool> &whole, const std::vector<GluePart> &parts) {
        std::map<std::vector<uint32_t>, uint32_t> groupOf;
        for (const std::vector<uint32_t> &set : cover)
            if (!set.empty())
                groupOf.emplace(set, 0);
        GlueGroups groups;
        for (auto &[set, g] : groupOf) {
            g = (uint32_t)groups.props.size();
            Properties props;
            const bool hasWhole = std::any_of(set.begin(), set.end(), [&](uint32_t k) { return whole[k]; });
            for (uint32_t k : set)
                if (whole[k] == hasWhole)
                    props = MergedProperties(props, parts[k].props);
            groups.props.push_back(props);
        }
        groups.ofFace.assign(cover.size(), (uint32_t)groups.props.size());
        for (size_t f = 0; f < cover.size(); ++f)
            if (!cover[f].empty())
                groups.ofFace[f] = groupOf[cover[f]];
        return groups;
    }

    // the wires lying on a piece (numbered from none) and the groups on its two sides; a wire's stretch that separates
    // two faces of one group lists the group twice
    static std::vector<uint32_t> OwnersOf(const std::vector<uint32_t> &lying, const std::vector<GluePart> &parts, const std::vector<uint32_t> &wireIndex,
                                          uint32_t left, uint32_t right, bool separates, uint32_t none) {
        std::vector<uint32_t> set;
        for (uint32_t k : lying)
            if (!parts[k].face)
                set.push_back(none + wireIndex[k]);
        if (left != right) {
            for (uint32_t g : {left, right})
                if (g != none)
                    set.push_back(g);
        } else if (left != none && separates && !set.empty()) {
            set.push_back(left);
            set.push_back(left);
        }
        std::sort(set.begin(), set.end());
        set.erase(std::unique(set.begin(), set.end(), [&](uint32_t x, uint32_t y) { return x == y && x >= none; }), set.end());
        return set;
    }

    // every area covered by a different set of face parts is a group of its own (A - B, A * B, B - A), in the order of the
    // sets; the group on the side of the part whose edge comes first is an edge's first owner, its normal points out of it
    Shape Glue(const ShapeList &shapes) {
        const AxisSystem axes = shapes.CommonAxes();
        const std::vector<GluePart> parts = GlueParts(shapes);
        std::vector<uint32_t> wireIndex(parts.size());
        size_t wires = 0;
        for (size_t k = 0; k < parts.size(); ++k)
            if (!parts[k].face)
                wireIndex[k] = (uint32_t)(wires++);
        Wire input(axes);
        std::vector<uint32_t> source;
        for (size_t k = 0; k < parts.size(); ++k)
            for (const Edge &e : parts[k].edges) {
                input.Add(e);
                source.push_back((uint32_t)k);
            }
        Wire c = ArrangedParts(input, source);
        const double tol = input.Tol();
        const std::vector<HalfEdges> faces = ArrangementFaces(c);
        const std::vector<std::vector<uint32_t>> cover = CoveringParts(c, faces, parts);
        const std::vector<bool> whole = WholeParts(cover, parts.size());
        GlueGroups groups = GroupsOfCovers(cover, whole, parts);
        const auto none = (uint32_t)groups.props.size();
        const std::vector<size_t> faceOf = SideFaces(faces, c.Size());
        Grid grid = input.EdgeGrid();
        std::vector<uint32_t> nearby;
        std::vector<std::vector<uint32_t>> owners;
        std::vector<bool> drop(c.Size(), false);
        for (size_t i = 0; i < c.Size(); ++i) {
            const Vertex m = MidPointOf(c[i]);
            std::vector<size_t> on;
            nearby.clear();
            grid.InBox(BBox::Of(m, m).Grown(tol), nearby);
            for (uint32_t j : nearby)
                if (Distance(m, input[j]) <= tol)
                    on.push_back(j);
            std::vector<uint32_t> lying{c.Owner(i)};
            for (size_t j : on)
                lying.push_back(source[j]);
            const size_t fl = faceOf[2 * i], fr = faceOf[2 * i + 1];
            const uint32_t left = groups.ofFace[fl], right = groups.ofFace[fr];
            std::vector<uint32_t> set = OwnersOf(lying, parts, wireIndex, left, right, fl != fr, none);
            if (set.empty()) {
                drop[i] = true;
                continue;
            }
            auto covers = [&](size_t f, size_t k) { return std::find(cover[f].begin(), cover[f].end(), k) != cover[f].end(); };
            auto belongs = [&](size_t k) {
                return parts[k].face ? covers(fl, k) || covers(fr, k) : std::find(lying.begin(), lying.end(), k) != lying.end();
            };
            std::vector<size_t> from;
            for (size_t j : on)
                if (belongs(source[j]))
                    from.push_back(j);
            if (!from.empty()) {
                std::stable_sort(from.begin(), from.end(), [&](size_t x, size_t y) {
                    return whole[source[x]] != whole[source[y]] ? whole[source[x]] : source[x] < source[y];
                });
                Properties props;
                for (size_t j : from)
                    props = MergedProperties(props, PropsOf(input[j]));
                // the cut remapped the partition onto this piece
                const Properties &piece = PropsOf(c[i]);
                props = ChangedProperties(props, [&](ShapeProperties &q) { q.partition = piece ? piece->partition : std::nullopt; });
                SetProps(c[i], props);
                // the group on the side of the part that names the edge comes first
                const uint32_t namer = source[from.front()];
                if (parts[namer].face && covers(fl, namer) != covers(fr, namer)) {
                    auto at = std::find(set.begin(), set.end(), covers(fl, namer) ? left : right);
                    std::rotate(set.begin(), at, at + 1);
                }
            }
            owners.push_back(std::move(set));
        }
        c.Erase(drop);
        Shape glued(std::move(c), owners, std::move(groups.props), wires);
        return glued.JoinOnCarriers().Compacted();
    }

    // region is clean; on its boundary counts as inside; one wire part per connected chain
    Shape WiresIn(const Shape &w, const Wire &region, bool inside) {
        WireList chains;
        for (const Wire &part : w.WireParts()) {
            Wire c = part;
            std::vector<CutState> state(c.Size(), CutState{1});
            for (const Edge &e : region) {
                c.Append(e, 0);
                state.push_back({2});
            }
            Arrange(c, state);
            const double tol = c.Tol();
            Wire kept(w.Axes());
            for (size_t k = 0; k < c.Size(); ++k) {
                if (!(state[k].mask & 1))
                    continue;
                const Vertex m = MidPointOf(c[k]);
                bool on = (state[k].mask & 2) != 0;
                for (const Edge &e : region)
                    on = on || Distance(m, e) <= tol;
                if ((on || region.Contains(m)) == inside)
                    kept.Add(c[k]);
            }
            for (Wire &chain : kept.Wires())
                chains.push_back(std::move(chain));
        }
        return Shape(w.Axes(), {}, chains);
    }

    double SharedLength(const Face &a, const Face &b) {
        double sum = 0.0;
        for (const Edge &e : a.Edges()) {
            const auto [p, q, m, l] = std::visit([](const auto &x) { return std::tuple{x.Start(), x.End(), x.MidPoint(), x.Length()}; }, e);
            const double tol = Tol(std::max({NormInf(p), NormInf(q), 1.0}));
            for (const Edge &f : b.Edges()) {
                const auto [r, t, n] = std::visit([](const auto &x) { return std::tuple{x.Start(), x.End(), x.MidPoint()}; }, f);
                if (m.IsClose(n, tol) && ((p.IsClose(r, tol) && q.IsClose(t, tol)) || (p.IsClose(t, tol) && q.IsClose(r, tol)))) {
                    sum += l;
                    break;
                }
            }
        }
        return sum;
    }

    Shape Shape::WithoutSmallFaces(double area, bool merge) const {
        FaceList kept, smallFaces;
        for (const Face &f : Faces())
            (std::abs(f.Area()) < area ? smallFaces : kept).push_back(f);
        if (smallFaces.empty())
            return *this;
        for (const Face &f : smallFaces) {
            if (!merge)
                break;
            size_t best = kept.size();
            double longest = 0.0;
            for (size_t k = 0; k < kept.size(); ++k)
                if (const double shared = SharedLength(f, kept[k]); shared > longest) {
                    longest = shared;
                    best = k;
                }
            if (best == kept.size())
                continue;
            const FaceList united = Unify({Shape(kept[best]), Shape(f)}, false).Faces();
            if (united.size() == 1) {
                Face merged = united.front();
                merged.SetProps(kept[best].Props());
                kept[best] = merged;
            }
        }
        const Wire wires = WireEdges();
        return SharingFaces(Axes(), kept, wires.Empty() ? WireList{} : WireList{wires});
    }

    Shape Shape::WithoutSmallHoles(double area) const {
        const FaceList faces = Faces();
        std::vector<std::optional<Vertex>> inner;
        for (const Face &f : faces)
            inner.push_back(f.Edges().InteriorPoint());
        FaceList kept;
        bool filled = false;
        for (size_t k = 0; k < faces.size(); ++k) {
            const WireList holes = faces[k].Holes();
            Wire w = faces[k].Outer();
            for (const Wire &h : holes) {
                const BBox box = h.Box();
                bool island = false;
                for (size_t j = 0; j < faces.size() && !island; ++j)
                    island = j != k && inner[j] && box.IsInside(*inner[j]) && h.Contains(*inner[j]);
                if (island || std::abs(h.Area()) >= area) {
                    for (const Edge &e : h)
                        w.Add(e);
                } else
                    filled = true;
            }
            kept.push_back(Face(std::move(w), faces[k].Props()));
        }
        if (!filled)
            return *this;
        const Wire wires = WireEdges();
        return SharingFaces(Axes(), kept, wires.Empty() ? WireList{} : WireList{wires});
    }

    Shape Intersect(const ShapeList &shapes, bool multithreaded) {
        if (std::count_if(shapes.begin(), shapes.end(), std::mem_fn(&Shape::HasOnlyWires)) == 1 && shapes.size() > 1 &&
            std::all_of(shapes.begin(), shapes.end(), [](const Shape &s) { return s.HasOnlyWires() || s.WireCount() == 0; })) {
            const auto w = std::find_if(shapes.begin(), shapes.end(), std::mem_fn(&Shape::HasOnlyWires));
            WireList faces;
            for (auto s = shapes.begin(); s != shapes.end(); ++s)
                if (s != w)
                    faces.push_back(s->FaceEdges());
            shapes.CommonAxes();
            return WiresIn(*w, faces.size() == 1 ? faces.front() : BalancedBoolean(std::move(faces), BooleanOp::Intersection, multithreaded), true);
        }
        shapes.CheckParts(false);
        const AxisSystem axes = shapes.CommonAxes();
        if (!shapes.empty() && shapes.front().HasSharedFaces())
            return FaceByFace(axes, shapes, BalancedBoolean(shapes.Rest().Outlines(), BooleanOp::Intersection, multithreaded),
                              BooleanOp::Intersection, &FaceList::InheritFirstOperandProps);
        return Result(axes, BalancedBoolean(shapes.Outlines(), BooleanOp::Intersection, multithreaded), shapes,
                      &FaceList::InheritFirstOperandProps);
    }

    Shape Subtract(const ShapeList &shapes, bool multithreaded) {
        if (shapes.size() > 1 && shapes.front().HasOnlyWires() &&
            std::all_of(shapes.begin() + 1, shapes.end(), [](const Shape &s) { return s.WireCount() == 0; })) {
            shapes.CommonAxes();
            return WiresIn(shapes.front(), BalancedBoolean(shapes.Rest().Outlines(), BooleanOp::Union, multithreaded), false);
        }
        shapes.CheckParts(false);
        const AxisSystem axes = shapes.CommonAxes();
        if (shapes.empty())
            return Shape();
        WireList rest = shapes.Rest().Outlines();
        if (shapes.front().HasSharedFaces())
            return FaceByFace(axes, shapes, BalancedBoolean(std::move(rest), BooleanOp::Union, multithreaded), BooleanOp::Difference,
                              [](FaceList &regions, const std::vector<FaceList> &operands) {
                                  for (Face &r : regions)
                                      r.SetProps(operands.front().front().Props());
                              });
        if (rest.empty())
            return Result(axes, shapes.front().FaceEdges(), {shapes.front()}, &FaceList::InheritProps);
        Wire region = Boolean(shapes.front().FaceEdges(), BalancedBoolean(std::move(rest), BooleanOp::Union, multithreaded),
                              BooleanOp::Difference);
        Snap(region);
        return Result(axes, std::move(region), {shapes.front()}, &FaceList::InheritProps);
    }

    Shape SymmetricDifference(const ShapeList &shapes, bool multithreaded) {
        shapes.CheckParts(false);
        const AxisSystem axes = shapes.CommonAxes();
        if (shapes.empty())
            return Shape();
        Shape r = shapes.front();
        for (size_t k = 1; k < shapes.size(); ++k) {
            FaceList faces = Subtract({r, shapes[k]}, multithreaded).Faces();
            const FaceList other = Subtract({shapes[k], r}, multithreaded).Faces();
            faces.insert(faces.end(), other.begin(), other.end());
            r = SharingFaces(axes, faces);
        }
        return r;
    }

    // Liang-Barsky: t0 > t1 where the line misses the box
    std::pair<double, double> RangeInBox(const Line &l, const BBox &b) {
        const Vertex p = l.Start(), d = l.End() - l.Start();
        double t0 = 0.0, t1 = 1.0;
        for (const auto [dp, q] : {std::pair{-d.x, p.x - b.x0}, std::pair{d.x, b.x1 - p.x}, std::pair{-d.y, p.y - b.y0}, std::pair{d.y, b.y1 - p.y}}) {
            if (dp == 0.0) {
                if (q < 0.0)
                    return {1.0, 0.0};
            } else if (dp < 0.0)
                t0 = std::max(t0, q / dp);
            else
                t1 = std::min(t1, q / dp);
        }
        return {t0, t1};
    }

    // start: the length of its loop before it
    struct BoundaryEdge {
        const Edge *edge;
        uint32_t loop;
        double start, length;
    };

    static std::vector<BoundaryEdge> BoundaryEdges(const WireList &loops, std::vector<double> &loopLength) {
        std::vector<BoundaryEdge> edges;
        loopLength.assign(loops.size(), 0.0);
        for (uint32_t l = 0; l < loops.size(); ++l)
            for (const Edge &e : loops[l]) {
                edges.push_back({&e, l, loopLength[l], LengthOf(e)});
                loopLength[l] += edges.back().length;
            }
        return edges;
    }

    // where a part (or a gap) no wider than width can be: at two boundary points within width of each other, on two loops
    // or more than 3 width apart along one (a residue is a sliver only along more than 4 width), or anywhere on a loop
    // no longer than 8 width (the corners of its box); samples every width / 4, so a pair counts within 1.25 width
    std::vector<Vertex> ThinSpotsOf(const WireList &loops, double width) {
        static Timer t("planex ThinSpotsOf");
        Region reg(t);
        std::vector<double> loopLength;
        const std::vector<BoundaryEdge> edges = BoundaryEdges(loops, loopLength);
        std::vector<Vertex> spots;
        for (uint32_t l = 0; l < loops.size(); ++l)
            if (loopLength[l] <= 8.0 * width) {
                const BBox b = loops[l].Box();
                spots.push_back({b.x0, b.y0});
                spots.push_back({b.x1, b.y1});
            }
        if (edges.empty())
            return spots;
        std::vector<BBox> boxes;
        for (const BoundaryEdge &e : edges)
            boxes.push_back(BoxOf(*e.edge).Grown(1.25 * width));
        Grid grid = Grid::OfBoxes(boxes);
        std::vector<uint32_t> nearby;
        std::vector<Vertex> samples;
        for (uint32_t a = 0; a < edges.size(); ++a) {
            const BoundaryEdge &ea = edges[a];
            const Line *line = std::get_if<Line>(ea.edge);
            const size_t n = std::max<size_t>(1, (size_t)std::ceil(ea.length / (0.25 * width)));
            samples.clear();
            nearby.clear();
            grid.InBox(boxes[a], nearby);
            for (uint32_t b : nearby) {
                if (b == a)
                    continue;
                const BoundaryEdge &eb = edges[b];
                if (ea.loop == eb.loop && std::max(std::abs(ea.start + ea.length - eb.start), std::abs(eb.start + eb.length - ea.start)) <= 3.0 * width)
                    continue;
                size_t k0 = 0, k1 = n;
                if (line) {
                    const auto [t0, t1] = RangeInBox(*line, boxes[b]);
                    if (t0 > t1)
                        continue;
                    k0 = (size_t)std::ceil(t0 * (double)n);
                    k1 = std::min(n, (size_t)std::floor(t1 * (double)n));
                }
                if (samples.empty())
                    std::visit([&](const auto &x) {
                        for (size_t k = 0; k <= n; ++k)
                            samples.push_back(x.PointAtFraction((double)k / (double)n));
                    }, *ea.edge);
                for (size_t k = k0; k <= k1; ++k) {
                    const Vertex &p = samples[k];
                    if (!boxes[b].IsInside(p))
                        continue;
                    const auto [q, f] = std::visit([&](const auto &x) {
                        const Vertex q = x.NearestPoint(p);
                        return std::pair{q, x.LengthFraction(q)};
                    }, *eb.edge);
                    if (!p.IsClose(q, 1.25 * width))
                        continue;
                    const double d = std::abs(ea.start + ea.length * (double)k / (double)n - eb.start - eb.length * f);
                    if (ea.loop != eb.loop || std::min(d, loopLength[ea.loop] - d) > 3.0 * width) {
                        spots.push_back(p);
                        spots.push_back(q);
                    }
                }
            }
        }
        return spots;
    }

    // a box around each group of thin spots, grown by 2 width: a residue piece there is decided by the part within width of
    // it, so one opened inside the box grown by 2 width more is the same; spots within 4 width are one group, boxes that
    // overlap are one box
    std::vector<BBox> SpotWindows(const std::vector<Vertex> &points, double width) {
        DisjointSets groups(points.size());
        Grid grid = Grid::Of(points, 4.0 * width);
        std::vector<uint32_t> nearby;
        for (uint32_t i = 0; i < points.size(); ++i) {
            nearby.clear();
            grid.InBox(BBox::Of(points[i], points[i]).Grown(4.0 * width), nearby);
            for (uint32_t j : nearby)
                groups.Join(i, j);
        }
        std::vector<BBox> windows;
        std::vector<size_t> windowOf(points.size(), points.size());
        for (uint32_t i = 0; i < points.size(); ++i) {
            size_t &w = windowOf[groups.Find(i)];
            if (w == points.size()) {
                w = windows.size();
                windows.push_back(BBox::Of(points[i], points[i]));
            }
            windows[w].Grow(points[i]);
        }
        for (BBox &w : windows)
            w = w.Grown(2.0 * width);
        for (bool merged = true; merged;) {
            merged = false;
            for (size_t i = 0; i < windows.size() && !merged; ++i)
                for (size_t j = i + 1; j < windows.size() && !merged; ++j)
                    if (windows[i].Overlaps(windows[j], 0.0)) {
                        windows[i].Grow(windows[j]);
                        windows.erase(windows.begin() + (std::ptrdiff_t)j);
                        merged = true;
                    }
        }
        return windows;
    }

    bool IsSliver(const Face &piece, const Properties &mark, double width) {
        std::vector<double> runs;
        bool offset = false;
        for (const Wire &loop : piece.Loops()) {
            const size_t n = loop.Size();
            if (n == 0)
                continue;
            size_t first = 0;
            while (first < n && PropsOf(loop[first]) != mark)
                ++first;
            double run = 0.0;
            for (size_t j = 1; j <= n; ++j) {
                const Edge &e = loop[(first + j) % n];
                if (PropsOf(e) != mark) {
                    run += LengthOf(e);
                } else if (run > 0.0) {
                    runs.push_back(run);
                    run = 0.0;
                }
            }
            if (first == n)
                runs.push_back(run);
            offset = offset || first < n;
        }
        return !(offset && runs.size() == 1 && runs.front() <= 4.0 * width);
    }

    FaceShares FaceEdgeIndex::SharedLengths(const Face &piece, const Properties &mark) {
        FaceShares shared;
        for (const Edge &e : piece.Edges()) {
            if (PropsOf(e) == mark)
                continue;
            const Vertex m = MidPointOf(e);
            const double length = LengthOf(e), tol = Tol(NormInf(m));
            nearby.clear();
            grid.InBox(BBox::Of(m, m).Grown(tol), nearby);
            for (uint32_t q : nearby)
                if (Distance(m, faces[at[q].face].Edges()[at[q].edge]) <= tol)
                    shared.Add(at[q].face, length);
        }
        return shared;
    }

    FaceList Reassigned(const FaceList &faces, const std::vector<ShapeList> &taken, const std::vector<ShapeList> &given) {
        static Timer t("planex Reassigned");
        Region reg(t);
        FaceList out;
        for (uint32_t k = 0; k < faces.size(); ++k) {
            if (taken[k].empty() && given[k].empty()) {
                out.push_back(faces[k]);
                continue;
            }
            Shape s(faces[k]);
            if (!taken[k].empty()) {
                ShapeList parts{s};
                parts.insert(parts.end(), taken[k].begin(), taken[k].end());
                s = Subtract(parts);
            }
            if (!given[k].empty()) {
                ShapeList parts{s};
                parts.insert(parts.end(), given[k].begin(), given[k].end());
                s = Unify(parts);
            }
            for (Face g : s.Faces()) {
                g.SetProps(faces[k].Props());
                out.push_back(std::move(g));
            }
        }
        return out;
    }

    // face: unused for the gaps, which are those of all faces together
    struct ThinSpots {
        uint32_t face;
        std::vector<Vertex> spots;
    };

    static std::vector<ThinSpots> ThinParts(const FaceList &faces, double width) {
        std::vector<ThinSpots> thin;
        for (uint32_t k = 0; k < faces.size(); ++k)
            if (std::vector<Vertex> spots = ThinSpotsOf(faces[k].Loops(), width); !spots.empty())
                thin.push_back({k, std::move(spots)});
        return thin;
    }

    static std::vector<ThinSpots> ThinGaps(const Shape &united, double width) {
        WireList loops;
        for (const Face &f : united.Faces())
            for (Wire &loop : f.Loops())
                loops.push_back(std::move(loop));
        std::vector<ThinSpots> thin;
        if (std::vector<Vertex> spots = ThinSpotsOf(loops, width); !spots.empty())
            thin.push_back({0, std::move(spots)});
        return thin;
    }

    // kept: the slivers left between faces of different properties
    struct SliverMoves {
        std::vector<ShapeList> taken, given;
        size_t moved = 0, kept = 0;
        bool deferred = false;
    };

    static SliverMoves SliverMovesOf(const FaceList &faces, const Shape &united, const std::vector<ThinSpots> &thin, const SliverStep &step) {
        FaceEdgeIndex index(faces);
        const Properties mark = std::make_shared<ShapeProperties>();
        const double r = step.width / 2.0;
        SliverMoves moves{std::vector<ShapeList>(faces.size()), std::vector<ShapeList>(faces.size())};
        std::vector<std::pair<uint32_t, BBox>> applied;
        for (const auto &[k, spots] : thin) {
            const FaceList pieces = step.gaps ? ResidueAtSpots(united, spots, step.width, [&](const Shape &v) { return ClosingResidue(v, r, mark); })
                                              : ResidueAtSpots(Shape(faces[k]), spots, step.width, [&](const Shape &f) { return OpeningResidue(f, r, mark); });
            for (const Face &piece : pieces) {
                if (!IsSliver(piece, mark, step.width))
                    continue;
                FaceShares shared = index.SharedLengths(piece, mark);
                if (!step.gaps)
                    std::erase_if(shared, [k = k](const FaceShare &s) { return s.face == k; });
                if (shared.IsMixed(faces)) {
                    ++moves.kept;
                    continue;
                }
                if (shared.empty() && (step.gaps || step.keepOutline))
                    continue;
                // pieces of two faces that touch were found on the same faces and meet only to rounding: the second
                // waits for a round on what the first leaves
                const BBox box = piece.Box().Grown(step.width);
                bool touches = false;
                for (const auto &[from, other] : applied)
                    touches = touches || (from != k && box.Overlaps(other, 0.0));
                if (touches) {
                    moves.deferred = true;
                    continue;
                }
                applied.emplace_back(k, box);
                const Shape s = Unmarked(piece, mark);
                if (!step.gaps)
                    moves.taken[k].push_back(s);
                if (!shared.empty())
                    moves.given[shared.FaceSharingMost()].push_back(s);
                ++moves.moved;
            }
        }
        return moves;
    }

    SliverRound Shape::SliversMoved(const SliverStep &step) const {
        if (!HasFaces())
            return {*this, false};
        const FaceList faces = Faces();
        const Shape united = step.gaps ? Shape(Outline(), nullptr) : Shape(Axes());
        const std::vector<ThinSpots> thin = step.gaps ? ThinGaps(united, step.width) : ThinParts(faces, step.width);
        if (thin.empty())
            return {*this, false};
        const SliverMoves moves = SliverMovesOf(faces, united, thin, step);
        if (step.warn && moves.kept > 0)
            Warning() << "merge_slivers_thinner: " << moves.kept << (step.gaps ? " gap" : " part") << (moves.kept == 1 ? "" : "s") << " no wider than "
                      << step.width << " between faces of different properties kept";
        if (moves.moved == 0)
            return {*this, moves.deferred};
        ShapeList parts;
        for (const Face &f : Reassigned(faces, moves.taken, moves.given))
            parts.emplace_back(f);
        if (const Wire wires = WireEdges(); !wires.Empty())
            parts.push_back(SharingFaces(Axes(), {}, {wires}));
        return {Glue(parts), moves.deferred};
    }

    // at most 4 rounds
    Shape Shape::WithoutSlivers(double width, bool keepOutline) const {
        static Timer t("planex WithoutSlivers");
        Region reg(t);
        SliverRound r{*this, false};
        bool waiting = false;
        for (bool gaps : {false, true}) {
            if (gaps && keepOutline)
                break;
            r.deferred = true;
            for (int round = 0; round < 4 && r.deferred; ++round)
                r = r.shape.SliversMoved({.width = width, .keepOutline = keepOutline, .gaps = gaps, .warn = round == 0});
            waiting = waiting || r.deferred;
        }
        if (waiting)
            Warning() << "merge_slivers_thinner: thin parts that touch each other are left after 4 rounds; the cleanup run again "
                         "goes on with them";
        return r.shape;
    }

    // d > 0: the band added, d < 0: taken away
    Shape OffsetFaces(const Shape &s, double d, OffsetCorner corners) {
        if (s.HasSharedFaces())
            throw std::invalid_argument("Offset: the faces of the shape share edges (glued); px.Unify(shape) merges them first");
        const Wire edges = s.FaceEdges();
        if (std::abs(d) <= edges.Tol())
            return s;
        const ShapeList parts{s, Shape(edges.Offset(std::abs(d), OffsetEnd::Round, corners), nullptr)};
        Shape r = d > 0.0 ? Unify(parts) : Subtract(parts);
        r.Ids() = parts.MergedIds();
        return Fresh(std::move(r));
    }

}
