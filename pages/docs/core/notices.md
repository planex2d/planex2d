# License and notices

planex2d is Copyright (C) 2026 Sebastian Hirnschall.

```
planex2d is free software; you can redistribute it and/or modify it under the terms of the
GNU Lesser General Public License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

planex2d is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License (the file LICENSE) for more details.
```

Every source file of planex2d's own carries the short form of this notice:
`SPDX-FileCopyrightText: 2026 Sebastian Hirnschall` and `SPDX-License-Identifier: LGPL-2.1-or-later`.

Its compiled module contains code from three libraries and is linked to two more:

| Library | Version | In planex2d | License |
|---|---|---|---|
| Eigen | 3.4.0 | compiled in (sparse and dense linear algebra of the constraint solver) | MPL-2.0 (two files Apache-2.0 and BSD 3-clause) |
| pybind11 | as bundled with netgen-mesher | compiled in (the Python bindings) | BSD 3-clause |
| Taskflow | 4.1.0 | compiled in (parallel booleans) | MIT |
| Netgen | the netgen-mesher package | linked, installed separately | LGPL-2.1 |
| Open CASCADE Technology | the netgen-occt package | linked, installed separately | LGPL-2.1 with the Open CASCADE exception |

**planex2d makes use of facilities provided by the Open CASCADE Technology software.** The notices of every library
follow, as in the file `THIRD_PARTY_NOTICES.md` of planex2d's source and wheel.

The compiled module also contains one file under the Apache License 2.0, Eigen's `BFloat16.h`. The Apache License 2.0
is compatible with version 3 of the GNU LGPL but not with version 2.1, so the compiled module as a whole can be passed
on under version 3 or later of the LGPL, which the "or any later version" of planex2d's license allows.

## Eigen 3.4.0

- Project: Eigen, a C++ template library for linear algebra, https://gitlab.com/libeigen/eigen
- Version: 3.4.0 (git tag `3.4.0`, commit `3147391d`; identical to the release tarball with SHA-256
  `8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72`), unmodified.
- Source: the folder `external_dependencies/eigen` of planex2d's source and of its source distribution (sdist); the
  Source Code Form of every Eigen file planex2d compiles is available there and at the URL above.
- Used: the header-only modules `Eigen/Core`, `Eigen/SparseCore`, `Eigen/SparseCholesky`, `Eigen/OrderingMethods`,
  `Eigen/SparseQR` and the dense `Eigen/QR` and `Eigen/SVD` (with `Eigen/Cholesky`, `Eigen/Householder` and
  `Eigen/Jacobi`, which they include), compiled with `EIGEN_MPL2_ONLY` defined, so that no LGPL-licensed Eigen code can
  be included. Nothing from Eigen's `unsupported/` directory is used, and its BLAS, LAPACKE and MKL bindings are not
  enabled (`EIGEN_USE_BLAS`, `EIGEN_USE_LAPACKE` and `EIGEN_USE_MKL` are not defined).

### Licenses of the Eigen files planex2d compiles

**Mozilla Public License 2.0 (MPL-2.0)** applies to all of them except `BFloat16.h` (Apache-2.0) and `MKL_support.h`
(BSD 3-clause) below; the full text is `COPYING.MPL2` in `external_dependencies/eigen` (and in the `licenses` folder of
planex2d's
wheel). The files carry this notice, for example
`Eigen/src/Core/Array.h`, lines 1 to 8:

```
// This file is part of Eigen, a lightweight C++ template library
// for linear algebra.
//
// Copyright (C) 2009 Gael Guennebaud <gael.guennebaud@inria.fr>
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
```

The other licenses and the further notices some of the files contain, quoted verbatim:

**Apache License 2.0 (Apache-2.0)**: `Eigen/src/Core/arch/Default/BFloat16.h` (included by `Eigen/Core`); the full text
is `COPYING.APACHE` in `external_dependencies/eigen` (and in the wheel's `licenses` folder).

`Eigen/src/Core/arch/Default/BFloat16.h`, lines 1 to 14:

```
/* Copyright 2017 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
```

**BSD 3-clause (Intel Corporation)**: `Eigen/src/Core/util/MKL_support.h` (included by `Eigen/Core`; planex2d does not
use MKL); the same license text is `COPYING.BSD` in `external_dependencies/eigen` (and in the wheel's `licenses`
folder).

`Eigen/src/Core/util/MKL_support.h`, lines 1 to 31:

```
/*
 Copyright (c) 2011, Intel Corporation. All rights reserved.

 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
 * Neither the name of Intel Corporation nor the names of its contributors may
   be used to endorse or promote products derived from this software without
   specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 ********************************************************************************
 *   Content : Eigen bindings to Intel(R) MKL
 *   Include file with common MKL declarations
 ********************************************************************************
*/
```

**Half-precision conversion routines (Fabian Giesen)**: `Eigen/src/Core/arch/Default/Half.h` (included by
`Eigen/Core`).

`Eigen/src/Core/arch/Default/Half.h`, lines 1 to 25:

```
// This file is part of Eigen, a lightweight C++ template library
// for linear algebra.
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// The conversion routines are Copyright (c) Fabian Giesen, 2016.
// The original license follows:
//
// Copyright (c) Fabian Giesen, 2016
// All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted.
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

**COLAMD (University of Florida)**: `Eigen/src/OrderingMethods/Eigen_Colamd.h` (included by `Eigen/OrderingMethods`).

`Eigen/src/OrderingMethods/Eigen_Colamd.h`, lines 1 to 44:

```
// // This file is part of Eigen, a lightweight C++ template library
// for linear algebra.
//
// Copyright (C) 2012 Desire Nuentsa Wakam <desire.nuentsa_wakam@inria.fr>
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

// This file is modified from the colamd/symamd library. The copyright is below

//   The authors of the code itself are Stefan I. Larimore and Timothy A.
//   Davis (davis@cise.ufl.edu), University of Florida.  The algorithm was
//   developed in collaboration with John Gilbert, Xerox PARC, and Esmond
//   Ng, Oak Ridge National Laboratory.
//
//     Date:
//
//   September 8, 2003.  Version 2.3.
//
//     Acknowledgements:
//
//   This work was supported by the National Science Foundation, under
//   grants DMS-9504974 and DMS-9803599.
//
//     Notice:
//
//   Copyright (c) 1998-2003 by the University of Florida.
//   All Rights Reserved.
//
//   THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY
//   EXPRESSED OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
//
//   Permission is hereby granted to use, copy, modify, and/or distribute
//   this program, provided that the Copyright, this License, and the
//   Availability of the original version is retained on all copies and made
//   accessible to the end-user of any code or package that includes COLAMD
//   or any modified version of COLAMD.
//
//     Availability:
//
//   The colamd/symamd library is available at
//
//       http://www.suitesparse.com
```

**SuperLU (Xerox Corporation)**: `Eigen/src/SparseCore/SparseColEtree.h` (included by `Eigen/SparseQR`).

`Eigen/src/SparseCore/SparseColEtree.h`, lines 1 to 30:

```
// This file is part of Eigen, a lightweight C++ template library
// for linear algebra.
//
// Copyright (C) 2012 Désiré Nuentsa-Wakam <desire.nuentsa_wakam@inria.fr>
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.


/* 
 
 * NOTE: This file is the modified version of sp_coletree.c file in SuperLU 
 
 * -- SuperLU routine (version 3.1) --
 * Univ. of California Berkeley, Xerox Palo Alto Research Center,
 * and Lawrence Berkeley National Lab.
 * August 1, 2008
 *
 * Copyright (c) 1994 by Xerox Corporation.  All rights reserved.
 *
 * THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY
 * EXPRESSED OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 * Permission is hereby granted to use or copy this program for any
 * purpose, provided the above notices are retained on all copies.
 * Permission to modify the code and to distribute modified code is
 * granted, provided the above notices are retained, and a notice that
 * the code was modified is included with the above copyright notice.
 */
```

**Code adapted from LDL and CSparse**, relicensed by its author to MPL-2.0 for Eigen:
`Eigen/src/SparseCholesky/SimplicialCholesky_impl.h` and `Eigen/src/OrderingMethods/Amd.h`.

`Eigen/src/SparseCholesky/SimplicialCholesky_impl.h`, lines 1 to 18:

```
// This file is part of Eigen, a lightweight C++ template library
// for linear algebra.
//
// Copyright (C) 2008-2012 Gael Guennebaud <gael.guennebaud@inria.fr>
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

/*
NOTE: these functions have been adapted from the LDL library:

LDL Copyright (c) 2005 by Timothy A. Davis.  All Rights Reserved.

The author of LDL, Timothy A. Davis., has executed a license with Google LLC
to permit distribution of this code and derivative works as part of Eigen under
the Mozilla Public License v. 2.0, as stated at the top of this file.
 */
```

`Eigen/src/OrderingMethods/Amd.h`, lines 1 to 19:

```
// This file is part of Eigen, a lightweight C++ template library
// for linear algebra.
//
// Copyright (C) 2010 Gael Guennebaud <gael.guennebaud@inria.fr>
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

/*
NOTE: this routine has been adapted from the CSparse library:

Copyright (c) 2006, Timothy A. Davis.
http://www.suitesparse.com

The author of CSparse, Timothy A. Davis., has executed a license with Google LLC
to permit distribution of this code and derivative works as part of Eigen under
the Mozilla Public License v. 2.0, as stated at the top of this file.
*/
```

## pybind11

- Project: pybind11, https://github.com/pybind/pybind11
- Version: the one bundled in the include directory of the netgen-mesher package planex2d is built against (3.1.0a0 with
  netgen-mesher 6.2.2607); header-only, compiled in. It is not part of planex2d's source.
- License: BSD 3-clause, quoted verbatim from its `LICENSE` file:

```
Copyright (c) 2016 Wenzel Jakob <wenzel.jakob@epfl.ch>, All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Please also refer to the file .github/CONTRIBUTING.md, which clarifies licensing of
external contributions to this project including patches, pull requests, etc.
```

Its header `pybind11/detail/function_ref.h` (included by `pybind11/pybind11.h`) carries a further notice:

`pybind11/detail/function_ref.h`, lines 1 to 15:

```
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

//===- llvm/ADT/STLFunctionalExtras.h - Extras for <functional> -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
```

## Taskflow 4.1.0

- Project: Taskflow, https://github.com/taskflow/taskflow
- Version: 4.1.0 (`TF_VERSION 400100`), header-only, compiled in.
- Source: the folder `external_dependencies/taskflow` of planex2d's source and of its sdist.
- License: MIT, Copyright (c) 2018-2026 Dr. Tsung-Wei Huang; the full text is `external_dependencies/taskflow/LICENSE`
  (and in the `licenses` folder of planex2d's wheel).

## Libraries planex2d uses but does not include

`planex/_planex` is linked dynamically to these libraries and compiles material from their header files (inline
functions and templates). They are not part of planex2d's wheel: they are installed separately, as the netgen-mesher
and netgen-occt packages on PyPI.

- Netgen, including ngcore, by Joachim Schöberl and contributors, https://github.com/NGSolve/netgen: planex2d uses the
  Netgen library; Netgen and its use are covered by the GNU Lesser General Public License version 2.1, whose text is
  planex2d's `LICENSE`.
- Open CASCADE Technology (OCCT), https://dev.opencascade.org: planex2d makes use of facilities provided by the Open
  CASCADE Technology software. OCCT is covered by the GNU Lesser General Public License version 2.1 (the text:
  planex2d's `LICENSE`) with the Open CASCADE exception, quoted from the file `OCCT_LGPL_EXCEPTION.txt` of the OCCT
  source:

`OCCT_LGPL_EXCEPTION.txt`, lines 1 to 10:

```
Open CASCADE exception (version 1.0) to GNU LGPL version 2.1.

The object code (i.e. not a source) form of a "work that uses the Library"
can incorporate material from a header file that is part of the Library.
As a special exception to the GNU Lesser General Public License version 2.1,
you may distribute such object code incorporating material from header files
provided with the Open CASCADE Technology libraries (including code of CDL
generic classes) under terms of your choice, provided that you give
prominent notice in supporting documentation to this code that it makes use
of or is based on facilities provided by the Open CASCADE Technology software.
```

## Packages planex2d imports when they are installed

Some functions import further Python packages when they are called. These packages are not part of planex2d and are
not installed with it; of them, only numpy is a dependency of the planex2d package.

| Package | Imported by | License |
|---|---|---|
| numpy, https://numpy.org | the whole package (a dependency of planex2d) | BSD-3-Clause (parts of its wheels under 0BSD, MIT, Zlib and CC0-1.0) |
| NGSolve, https://ngsolve.org | `FromLevelSet`, `DeformableShape`, `planex.measurements`, field plots on NGSolve meshes | LGPL-2.1 |
| ngsolve-webgpu with webgpu, https://github.com/CERBSim/ngsolve_webgpu | `Plot3d` | LGPL-2.1-or-later |
| DOLFINx and Basix, https://fenicsproject.org | `Intersect3d` and field plots on DOLFINx meshes | LGPL-3.0-or-later (DOLFINx), MIT (Basix) |
| matplotlib, https://matplotlib.org | `Plot` and the field and mesh plots | the matplotlib license (based on the PSF license) |
| plotly, https://plotly.com/python | `Plot(..., backend="plotly")` | MIT |
| shapely, https://shapely.readthedocs.io | `FromShapely`, `ToShapely` | BSD-3-Clause |
| ezdxf, https://ezdxf.mozman.at | `FromDXF`, `WriteDXF` | MIT |

## Data files under other licenses

The test board `tests/data/kicad/pic_programmer.kicad_pcb` (part of planex2d's source and sdist, not of its wheel) and
the documentation's copy `docs/integrations/pic_programmer.kicad_pcb` (in the repository only) are KiCad's demo board
`demos/pic_programmer`, https://gitlab.com/kicad/code/kicad, Copyright the KiCad developers, licensed under CC BY-SA 4.0
(https://creativecommons.org/licenses/by-sa/4.0/), unmodified; see `tests/data/kicad/NOTICE`.
