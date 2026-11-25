// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"

Properties Props(const Name &name, const Size &maxh, const Col &col, const Size &minh, const Size &hpref, const Layer &layer, const Partition &partition) {
    return ChangedProperties(nullptr, [&](ShapeProperties &p) {
        p.name = CheckedName(name);
        p.maxh = SizeOf(maxh, "maxh");
        p.col = ColOf(col);
        p.minh = SizeOf(minh, "minh");
        p.hpref = SizeOf(hpref, "hpref", true);
        p.layer = layer;
        p.partition = PartitionOf(partition);
    });
}
