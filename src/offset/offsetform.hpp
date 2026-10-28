// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

namespace planex2d {

    // how an offset closes an open chain's ends: a half circle, the line across, or flat d beyond the end
    enum class OffsetEnd { Round, Flat, Square };
    // how it joins the offsets of two edges around the outside of a corner: an arc of radius d about the corner, the
    // two extended along their end tangents until they meet, or the line between them
    enum class OffsetCorner { Round, Sharp, Chamfer };
    // an offset's ends and corners
    struct OffsetForms {
        OffsetEnd ends = OffsetEnd::Round;
        OffsetCorner corners = OffsetCorner::Round;
    };

}
