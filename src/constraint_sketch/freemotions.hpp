// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <Eigen/Core>
#include <Eigen/QR>

#include "constraint_sketch/reports.hpp"
#include "constraint_sketch/system.hpp"
#include "core/config.hpp"
#include "core/disjointsets.hpp"
#include "core/vertex.hpp"

// The motions the constraints of a sketch leave, one per degree of freedom, made readable: per connected part its shifts and its rotation where they are free, then one motion per further value
// in drag order, each moving its own value with the values all the others drag fixed.

namespace planex2d::cs {

    class FreeMotionFinder {
    public:
        // labels: per value (reference, coordinate), as ConstraintSketch::UnknownLabels gives them; ids: per element
        FreeMotionFinder(System &s, std::vector<std::pair<std::string, std::string>> labels, const std::vector<std::string> &ids)
            : s(s), labels(std::move(labels)), ids(ids) {}

        std::vector<FreeMotion> Find() {
            std::vector<FreeMotion> out;
            for (const Motion &m : Ordered())
                out.push_back(Readable(m));
            return out;
        }

        // of the candidates (one row each, their values as the geometry has them), in order, those that each remove a
        // motion the ones before them left: the index of each and the drag of the motion it removes
        std::vector<std::pair<size_t, std::string>> Removing(const std::vector<Constraint> &candidates) {
            std::vector<Motion> motions = Ordered();
            const size_t n = x.value.size();
            System trial = s;
            std::vector<size_t> at;
            for (const Constraint &c : candidates)
                at.push_back(trial.Add(c));
            trial.Build();
            std::vector<std::pair<size_t, std::string>> out;
            std::vector<double> r, local;
            for (size_t k = 0; k < candidates.size() && !motions.empty(); ++k) {
                const std::vector<size_t> &idx = trial.Indices(at[k]);
                r.assign(trial.Rows(at[k]), 0.0);
                local.assign(r.size() * idx.size(), 0.0);
                trial.Evaluate(at[k], trial.values, r.data(), local.data());
                double row = 0.0;
                std::vector<double> moved(motions.size(), 0.0);
                for (size_t j = 0; j < idx.size(); ++j) {
                    const double d = local[j] * trial.scale[idx[j]];
                    row += d * d;
                    if (idx[j] < n)
                        for (size_t m = 0; m < motions.size(); ++m)
                            moved[m] += d * motions[m].m[Eigen::Index(idx[j])];
                }
                size_t p = sketch::none;
                double most = zero * std::sqrt(row);
                for (size_t m = 0; m < motions.size(); ++m)
                    if (std::abs(moved[m]) / motions[m].m.norm() > most) {
                        most = std::abs(moved[m]) / motions[m].m.norm();
                        p = m;
                    }
                if (p == sketch::none)
                    continue;
                for (size_t m = 0; m < motions.size(); ++m)
                    if (m != p)
                        motions[m].m -= moved[m] / moved[p] * motions[p].m;
                out.push_back({k, Name(motions[p].lead)});
                motions.erase(motions.begin() + (std::ptrdiff_t)p);
            }
            return out;
        }

    private:
        enum class Role { Other, X, Y, Turn };
        // a motion in the solver's scaled values, led by the value lead
        struct Motion {
            size_t lead;
            Eigen::VectorXd m;
        };
        // free values that move together, in drag order; the x values of its elements' points, free or not; the
        // columns of N that move it
        struct MotionPart {
            std::vector<size_t> values, points;
            std::vector<Eigen::Index> columns;
        };

        System &s;
        std::vector<std::pair<std::string, std::string>> labels;
        const std::vector<std::string> &ids;
        Unknowns x;
        // the motions as System::Motions gives them; what is left of them while the leads are taken out
        Eigen::MatrixXd N, W;
        // null vectors are accurate to about eps / rank_tol: a value moved by less, relative to the motion, stays
        double zero = 0.0;
        std::vector<Role> role;
        // the values of the elements other than point elements; a curve's frame, moved but not shown
        std::vector<char> onElement, frame;
        std::vector<size_t> order;
        std::vector<MotionPart> parts;

        // per part its rigid motions, then one per further value in reduced form
        std::vector<Motion> Ordered() {
            std::tie(x, N) = s.Motions();
            W = N;
            zero = std::sqrt(config::state->rank_tol);
            Roles();
            DragOrder();
            Parts();
            std::vector<Motion> rigid, dragged;
            for (const MotionPart &part : parts)
                RigidMotions(part, rigid);
            for (size_t u : order) {
                if (W.cols() == 0)
                    break;
                if (std::optional<Eigen::VectorXd> m = Eliminate(u))
                    dragged.push_back({u, std::move(*m)});
            }
            for (size_t k = dragged.size(); k-- > 0;)
                for (size_t j = 0; j < k; ++j)
                    dragged[j].m -= dragged[j].m[Eigen::Index(dragged[k].lead)] * dragged[k].m;
            rigid.insert(rigid.end(), std::make_move_iterator(dragged.begin()), std::make_move_iterator(dragged.end()));
            return rigid;
        }

        // a point's coordinates, and the angles a rotation turns (a spline's tangents, an axis)
        void Roles() {
            role.assign(x.value.size(), Role::Other);
            onElement.assign(x.value.size(), 0);
            frame.assign(x.value.size(), 0);
            for (const sketch::Element &e : s.elements) {
                const std::vector<size_t> u = e.Values(s);
                if (e.kind != sketch::ElementKind::Point)
                    for (size_t value : u)
                        onElement[value] = 1;
                const sketch::Element::Names names = e.ValueNames(s);
                for (size_t k = 0; k < u.size(); ++k) {
                    frame[u[k]] = frame[u[k]] || names[k].first == ".origin";
                    if (names[k].second == "x")
                        role[u[k]] = Role::X;
                    else if (names[k].second == "y")
                        role[u[k]] = Role::Y;
                    else if (names[k].second == "angle")
                        role[u[k]] = Role::Turn;
                }
            }
        }

        static int DragRank(const std::string &coordinate) { return coordinate == "x" || coordinate == "y" ? 0 : coordinate == "r" ? 1 : 2; }
        // the free values in the order a drag is looked for: the points' coordinates, the radii and the elements' other
        // values (a curve's frame among them), each in the order drawn; then the constraints' own
        void DragOrder() {
            std::vector<char> taken = x.fixed;
            const auto take = [&](size_t u) {
                if (!taken[u]) {
                    taken[u] = 1;
                    order.push_back(u);
                }
            };
            for (const int rank : {0, 1, 2})
                for (const sketch::Element &e : s.elements) {
                    const std::vector<size_t> u = e.Values(s);
                    const sketch::Element::Names names = e.ValueNames(s);
                    for (size_t k = 0; k < u.size(); ++k)
                        if ((frame[u[k]] ? 2 : DragRank(names[k].second)) == rank)
                            take(u[k]);
                }
            for (const Constraint &c : s.constraints) {
                for (size_t u : c.params)
                    take(u);
                if (c.valueAt != sketch::none)
                    take(c.valueAt);
            }
            for (size_t u = 0; u < x.value.size(); ++u)
                take(u);
        }

        // joined by the elements and the constraints; in the order of their first value
        void Parts() {
            const size_t n = x.value.size();
            DisjointSets sets(n);
            const auto join = [&](const std::vector<size_t> &values) {
                size_t first = n;
                for (size_t u : values)
                    if (!x.fixed[u]) {
                        if (first == n)
                            first = u;
                        else
                            sets.Join(u, first);
                    }
            };
            for (const sketch::Element &e : s.elements)
                join(e.Values(s));
            for (size_t c = 0; c < s.Count(); ++c)
                join(s.Indices(c));
            std::vector<size_t> partOf(n, sketch::none);
            for (size_t u : order) {
                const size_t root = sets.Find(u);
                if (partOf[root] == sketch::none) {
                    partOf[root] = parts.size();
                    parts.emplace_back();
                }
                parts[partOf[root]].values.push_back(u);
            }
            // a fixed point (the origin) can be in several parts
            std::vector<size_t> listedIn(n, sketch::none);
            for (const sketch::Element &e : s.elements) {
                const std::vector<size_t> u = e.Values(s);
                size_t free = n;
                for (size_t v : u)
                    if (!x.fixed[v])
                        free = v;
                if (free == n)
                    continue;
                const size_t p = partOf[sets.Find(free)];
                for (size_t v : u)
                    if (role[v] == Role::X && listedIn[v] != p) {
                        listedIn[v] = p;
                        parts[p].points.push_back(v);
                    }
            }
            for (Eigen::Index c = 0; c < N.cols(); ++c) {
                Eigen::Index u = 0;
                N.col(c).cwiseAbs().maxCoeff(&u);
                parts[partOf[sets.Find((size_t)u)]].columns.push_back(c);
            }
        }

        // the part moved as a rigid body, in the solver's scaled values: its points shifted by shift and turned by turn
        // (radians) about centre, its angles turned with them
        Eigen::VectorXd Rigid(const MotionPart &part, Vertex shift, double turn, Vertex centre) const {
            Eigen::VectorXd r = Eigen::VectorXd::Zero(Eigen::Index(x.value.size()));
            for (size_t u : part.points) {
                r[Eigen::Index(u)] = shift.x - turn * (x.value[u + 1] - centre.y);
                r[Eigen::Index(u + 1)] = shift.y + turn * (x.value[u] - centre.x);
            }
            for (size_t u : part.values)
                if (role[u] == Role::Turn)
                    r[Eigen::Index(u)] = turn / x.scale[u];
            return r;
        }

        // its shifts, led by its first free point, if nothing of it is fixed; its rotation about its fixed point, or
        // about that first point, led by the point coordinate it moves most. Each kept if the constraints allow it: as
        // the motion that moves the points and angles as a rigid body does and the part's other values least
        void RigidMotions(const MotionPart &part, std::vector<Motion> &out) {
            size_t first = sketch::none, pinned = sketch::none;
            for (size_t u : part.points) {
                if (frame[u])
                    continue;
                size_t &found = x.fixed[u] || x.fixed[u + 1] ? pinned : first;
                if (found == sketch::none)
                    found = u;
            }
            if (first == sketch::none || part.columns.empty())
                return;
            std::vector<size_t> rows;
            for (size_t u : part.points)
                rows.insert(rows.end(), {u, u + 1});
            for (size_t u : part.values)
                if (role[u] == Role::Turn)
                    rows.push_back(u);
            Eigen::MatrixXd A(Eigen::Index(rows.size()), Eigen::Index(part.columns.size()));
            for (size_t i = 0; i < rows.size(); ++i)
                for (size_t j = 0; j < part.columns.size(); ++j)
                    A(Eigen::Index(i), Eigen::Index(j)) = N(Eigen::Index(rows[i]), part.columns[j]);
            Eigen::CompleteOrthogonalDecomposition<Eigen::MatrixXd> least(A.rows(), A.cols());
            least.setThreshold(zero);
            least.compute(A);
            const auto keep = [&](const Eigen::VectorXd &r, size_t lead) {
                Eigen::VectorXd b(Eigen::Index(rows.size()));
                for (size_t i = 0; i < rows.size(); ++i)
                    b[Eigen::Index(i)] = r[Eigen::Index(rows[i])];
                const Eigen::VectorXd c = least.solve(b);
                if (!((A * c - b).norm() <= zero * b.norm()))
                    return;
                Eigen::VectorXd m = Eigen::VectorXd::Zero(N.rows());
                for (size_t j = 0; j < part.columns.size(); ++j)
                    m += c[Eigen::Index(j)] * N.col(part.columns[j]);
                if (Eliminate(lead))
                    out.push_back({lead, std::move(m)});
            };
            if (pinned == sketch::none) {
                keep(Rigid(part, {1.0, 0.0}, 0.0, {}), first);
                keep(Rigid(part, {0.0, 1.0}, 0.0, {}), first + 1);
            }
            const size_t centre = pinned == sketch::none ? first : pinned;
            const Eigen::VectorXd turn = Rigid(part, {}, 1.0, {x.value[centre], x.value[centre + 1]});
            size_t lead = sketch::none;
            double most = 0.0;
            for (size_t u : part.points)
                for (size_t v : {u, u + 1})
                    if (!x.fixed[v] && !frame[v] && std::abs(turn[Eigen::Index(v)]) > most) {
                        most = std::abs(turn[Eigen::Index(v)]);
                        lead = v;
                    }
            if (lead != sketch::none)
                keep(turn, lead);
        }

        // the motion left in W that moves u most, scaled to move it by 1, taken out of W and u eliminated from the
        // others; nothing if none moves it
        std::optional<Eigen::VectorXd> Eliminate(size_t u) {
            Eigen::Index p = -1;
            double most = zero;
            for (Eigen::Index c = 0; c < W.cols(); ++c)
                if (std::abs(W(Eigen::Index(u), c)) > most) {
                    most = std::abs(W(Eigen::Index(u), c));
                    p = c;
                }
            if (p < 0)
                return std::nullopt;
            const Eigen::VectorXd m = W.col(p) / W(Eigen::Index(u), p);
            W.col(p) = W.col(W.cols() - 1);
            W.conservativeResize(Eigen::NoChange, W.cols() - 1);
            for (Eigen::Index c = 0; c < W.cols(); ++c) {
                W.col(c) -= W(Eigen::Index(u), c) * m;
                W.col(c).normalize();
            }
            return m;
        }

        std::string Name(size_t u) const {
            const auto &[reference, coordinate] = labels[u];
            return reference + (role[u] == Role::X ? ".h" : role[u] == Role::Y ? ".v" : "." + coordinate);
        }

        // per unit of the lead: the points by (h, v), the other values (angles in degrees) that move, the elements (a
        // point element on another element's point moves with it, it is not counted)
        FreeMotion Readable(const Motion &motion) const {
            const size_t n = x.value.size();
            const double largest = motion.m.cwiseAbs().maxCoeff();
            std::vector<double> v(n, 0.0);
            for (size_t u = 0; u < n; ++u)
                if (u == motion.lead || std::abs(motion.m[Eigen::Index(u)]) > zero * largest)
                    v[u] = x.scale[u] * motion.m[Eigen::Index(u)] * (s.isAngle[u] ? Degrees(1.0) : 1.0);
            const double per = v[motion.lead];
            for (double &value : v)
                value /= per;
            FreeMotion out{Name(motion.lead), {}, {}, {}};
            for (size_t u = 0; u < n; ++u)
                if (frame[u])
                    continue;
                else if (role[u] == Role::X && (v[u] != 0.0 || v[u + 1] != 0.0))
                    out.points.push_back({labels[u].first, {v[u], v[u + 1]}});
                else if (role[u] != Role::X && role[u] != Role::Y && v[u] != 0.0)
                    out.values.push_back({labels[u].first + "." + labels[u].second, v[u]});
            for (size_t e = 0; e < s.elements.size(); ++e) {
                const std::vector<size_t> u = s.elements[e].Values(s);
                if (s.elements[e].kind == sketch::ElementKind::Point && onElement[u[0]])
                    continue;
                for (size_t value : u)
                    if (v[value] != 0.0) {
                        out.moves.push_back(ids[e]);
                        break;
                    }
            }
            return out;
        }
    };

}
