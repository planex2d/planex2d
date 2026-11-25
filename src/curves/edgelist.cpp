// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/curves/edgelist.hpp"

#include <cmath>

namespace planex2d {

    EdgeList EdgeList::Expanded() const {
        EdgeList r;
        for (const Edge &e : *this) {
            if (const Circle *c = std::get_if<Circle>(&e))
                for (const Arc &half : c->Halves())
                    r.push_back(half);
            else
                r.push_back(e);
        }
        return r;
    }

    bool EdgeList::IsOnOneCarrier() const {
        const double tol = Tol();
        const EdgeList parts = Expanded();
        if (const Line *first = std::get_if<Line>(&parts.front())) {
            for (const Edge &e : parts) {
                const Line *l = std::get_if<Line>(&e);
                if (!l || !first->OnCarrier(l->Start(), tol) || !first->OnCarrier(l->End(), tol))
                    return false;
            }
            return true;
        }
        if (const Arc *first = std::get_if<Arc>(&parts.front())) {
            for (const Edge &e : parts) {
                const Arc *a = std::get_if<Arc>(&e);
                if (!a || !a->Center().IsClose(first->Center(), tol) || std::abs(a->Radius() - first->Radius()) > tol)
                    return false;
            }
            return true;
        }
        return false;
    }

}
