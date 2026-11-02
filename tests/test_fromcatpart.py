# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px


def test_a_file_that_is_no_part_raises_naming_it(tmp_path):
    bad = tmp_path / "bad.CATPart"
    bad.write_bytes(b"not a part")
    with pytest.raises(ValueError, match="bad.CATPart.*not a .CATPart file"):
        px.FromCATPart(bad)
