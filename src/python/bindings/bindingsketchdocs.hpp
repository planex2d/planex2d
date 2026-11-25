// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

const char *SketchCornerDoc(bool chamfer, const std::string &sk, bool constraints);

const char *ConnectDoc(const std::string &sk, bool constraints);

const char *SketchMoveDoc(const std::string &sk, bool constraints);
const char *SketchLineDoc(const std::string &sk);
const char *SketchArcDoc(const std::string &sk);
const char *SketchSlotDoc(const std::string &sk);
const char *SketchArcSlotDoc(const std::string &sk);
const char *SketchKeyholeDoc(const std::string &sk);
const char *SketchPolygonDoc(const std::string &sk);
const char *SketchHexagonDoc(const std::string &sk);
const char *SketchCircleDoc(const std::string &sk);
const char *SketchSplineDoc(const std::string &sk);
const char *SketchBSplineDoc(const std::string &sk);
const char *SketchCurveDoc(const std::string &sk, bool constraints);
const char *SketchRectangleDoc(const std::string &sk);
const char *SketchRectangleCDoc(const std::string &sk);
const char *SketchDirectionDoc(const std::string &sk);
const char *SketchTurnDoc(const std::string &sk);
const char *SketchCloseDoc(const std::string &sk);
const char *SketchFinishDoc(const std::string &sk);
const char *SketchVertexDoc(const std::string &sk);
const char *SketchMeasureDoc();
const char *SketchSymmetryLineDoc(const std::string &sk);
const char *SketchMirrorDoc(const std::string &sk);
const char *SketchMirroredDoc(const std::string &sk, bool constraints);
const char *SketchCircularPatternDoc(const std::string &sk, bool constraints);
const char *SketchLinearPatternDoc(const std::string &sk, bool constraints);
const char *SketchOffsetDoc(const std::string &sk);
const char *SketchExtendDoc(const std::string &sk, bool constraints);
const char *SketchTrimDoc(const std::string &sk);
const char *SketchSplitDoc(const std::string &sk);
const char *SketchSetOriginDoc(const std::string &sk);
const char *SketchLocationDoc();
const char *SketchGeometryDoc();
const char *SketchRemoveDoc(const std::string &sk);
const char *SketchFaceDoc();
const char *SketchShapeDoc();
const char *SketchDiagnoseDoc();
const char *SketchAddDoc(const std::string &sk);
const char *SketchWireDoc();
const char *SketchWiresDoc();
const char *SketchConstructionDoc();
