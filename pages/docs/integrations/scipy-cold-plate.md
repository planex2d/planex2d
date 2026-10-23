---
jupyter:
  description: 'A cold plate in 3D: a constraint sketch as the path of a cooling pipe,
    swept with OpenCASCADE and glued into the plate, oil flow and heat solved with
    NGSolve, and SciPy''s COBYLA moving four spline points to cool the plate at a
    limited pressure drop.'
  nbsphinx:
    timeout: 2400
---

# A 3D cold plate

planex2d sketches lie on planes in 3D. Here, a constraint sketch on a plane is the path of a cooling pipe.
OpenCASCADE sweeps the pipe's cross-section along it and joins the pipe to a plate, netgen meshes the result, and
NGSolve solves the oil flow in the pipe and the temperature in the plate and the oil. COBYLA from SciPy moves four
points of the path, which are eight dimensions of the sketch, to make the plate cooler at a limited pressure drop.

The example is an aluminum cold plate, `WP` = 160 wide, 120 deep and `wall` = 2 thick, that carries 50 W of
electronics spread evenly over its middle `W` = 120 × 120, while the strips of `side` = 20 on the left and right are
not heated. A pipe of outer radius `Ro` = 5 and inner radius `Ri` = 3, sunk 1 into the plate's top, carries oil at
0.2 L/min that comes in at 20 °C. It enters in the middle of the front
edge and leaves in the middle of the back edge, and in between, it runs wherever the design puts it. Lengths are in
mm, pressure in kPa and heat in W.

```python
import math
import netgen.occ as occ
import ngsolve as ngs
import numpy as np
import planex as px
from scipy.optimize import minimize
```

```python nbsphinx="hidden"
from docs_figures import FACE, MAP, Chart, Row, Views
```

## The path

The path is a constraint sketch on the plane of the pipe's axis, `zc` = 4 above the plate's top, with its origin at the
plate's front left corner. A fixed construction line of 10 leads in along y to the middle of the front edge, another
leads out from the middle of the back edge, and the spline `meander` runs from the end of one to the start of the
other through four points, constrained tangent to both lines. The four points are `meander.p1` to `meander.p4`,
counted with the start as `p0`, and each has two dimensions: `p1_h` is the distance of the first point from the
sketch's vertical axis `px.V`, the plate's left edge, and `p1_v` its distance from `px.H`, the front edge. With the
eight dimensions nothing can move. They are the design, and `Path` builds the sketch for given values.

```python
W = 120.0
side = 20.0
wall = 2.0
WP = W + 2 * side
Ro = 5.0
Ri = 3.0
zc = Ro - wall / 2
corner = px.Plane(origin=(-WP / 2, -W / 2, zc))


def Path(points):
    path = px.Sketch(corner, id="path")
    path.Move((WP / 2, -10)).Line(0, 10, id="in", construction=True)
    path.Move("in.end", px.Coincident).Spline([*points, (WP / 2, W)], id="meander")
    path.Line(0, 10, id="out", construction=True)
    path.Constrain("in", "meander", px.Tangent)
    path.Constrain("meander", "out", px.Tangent)
    for p in ("in.start", "in.end", "out.start", "out.end"):
        path.Constrain(p, px.Fix)
    for k, (h, v) in enumerate(points, start=1):
        path.Constrain(f"meander.p{k}", px.V, px.Distance, h, id=f"p{k}_h")
        path.Constrain(f"meander.p{k}", px.H, px.Distance, v, id=f"p{k}_v")
    return path


initial = [(WP / 2 + 25 * math.sin(2 * math.pi * v / W), v) for v in (24.0, 48.0, 72.0, 96.0)]
path = Path(initial)
```

The path that the optimization starts from is an S: the four points lie on x = 80 + 25 sin(2π y / 120), so that the
pipe swings out to the right in the front half of the plate and to the left in the back half. Figure 1 shows the
sketch as drawn by planex2d, with a sign for each constraint, and the path on the plate, with the heated area dashed
and the four design points as dots.

```python nbsphinx="hide-input"
plate = px.QuickSketch().Rectangle(WP, W).Face()
heated = px.QuickSketch((side, 0)).Rectangle(W, W).Wire()
Row((path, "The path sketch and its constraints", {"constraints": True}),
    (path, "On the plate: the heated area dashed, the four points",
     {"fill": False, "fills": [(plate, FACE)], "dashed": [heated], "names": False,
      "dots": initial}),
    caption="The path of the start")
```

## The pipe and the plate

The pipe's cross-section lies on the plane across the start of the path: a disc of radius `Ro` for the pipe and one of
radius `Ri` for the oil in it, each drawn as two half circles. OpenCASCADE sweeps both along the path with `occ.Pipe`:
the outer disc gives the pipe as a solid rod, the inner disc the oil. The rod is fused to the plate. Where it dips into
the plate's top, it leaves a sharp re-entrant edge on either side, the edges in the plane z = 0 inside the plate, and a
chamfer of 2 fills them. An edge between two faces comes up once for each of them, so each is kept once. Gluing the
oil into the body (`occ.Glue`) splits the rod into the pipe wall and the oil: one shape of two solids, `metal` and
`coolant`, that share the bore, so that their meshes match there.

The names for the solver are assigned after the glue, first to the metal's faces and then to the coolant's. The bore
is a face of both, and the last name wins. The coolant's faces are the `bore`, its two ends the `inlet` and the
`outlet`; the plate's underside is the `bottom`, and everything else of the metal is `outside`.

```python
profile_plane = px.Plane(origin=(0, -W / 2, zc), normal=(0, 1, 0), axis=(1, 0, 0))


def disc(r):
    return px.QuickSketch(profile_plane).Move((r, 0), dir=(0, 1)).Arc(r, 180).Arc(r, 180).Face().ToOCCShape()


outer = disc(Ro)
inner = disc(Ri)


def on_top(edge):
    t0, t1 = edge.parameter_interval
    points = [edge.Value(t0 + (t1 - t0) * k / 8) for k in range(9)]
    inside = all(abs(p.x) < WP / 2 - 1e-3 and abs(p.y) < W / 2 - 1e-3 for p in points[1:-1])
    return inside and all(abs(p.z) < 1e-4 for p in points)


def ColdPlate(path, chamfer=2.0):
    spine = path.ToOCCWire()
    rod, oil = occ.Pipe(spine, outer), occ.Pipe(spine, inner)
    plate = occ.Box(occ.Pnt(-WP / 2, -W / 2, -wall), occ.Pnt(WP / 2, W / 2, 0))
    body = plate + rod
    seam, seen = [], set()
    for e in body.edges:
        t0, t1 = e.parameter_interval
        m = e.Value((t0 + t1) / 2)
        key = (round(e.mass, 4), round(m.x, 4), round(m.y, 4))
        if on_top(e) and key not in seen:
            seen.add(key)
            seam.append(e)
    body = body.MakeChamfer(seam, chamfer)
    glued = occ.Glue([body, oil])
    metal, coolant = sorted(glued.solids, key=lambda s: s.mass, reverse=True)
    metal.name = "metal"
    coolant.name = "coolant"
    metal.faces.name = "outside"
    coolant.faces.name = "bore"
    coolant.faces.Min(occ.Y).name = "inlet"
    coolant.faces.Max(occ.Y).name = "outlet"
    metal.faces.Min(occ.Z).name = "bottom"
    return glued, metal, coolant


glued, metal, coolant = ColdPlate(path)
names = sorted({f.name for f in glued.faces})
print(len(glued.solids), "solids, face names", names)
```

Figure 2 shows the path and the profile at its start, and the cold plate seen from the same side. Figure 3 is a thin
slice of metal and oil across the pipe at y = −20, seen from the front, with the pipe sunk 1 into the plate, the
chamfers on either side of it, and the oil inside.

```python nbsphinx="hide-input"
plate_top = occ.Box(occ.Pnt(-WP / 2, -W / 2, -wall), occ.Pnt(WP / 2, W / 2, 0)).faces.Max(occ.Z)
Views(("The path and the profile at its start",
       [(plate_top, "soft"), (path.ToOCCWire(), "accent"), (outer, "ink"), (inner, "ink")]),
      ("plate and pipe in one, chamfered, the oil glued in", [(glued, "shade"), (glued, "ink")]),
      caption="The path and the cold plate")
across = px.Plane(origin=(0, -20, 0), normal=(0, -1, 0), axis=(1, 0, 0))
spot = max(px.Intersect3d(across, coolant).faces, key=lambda f: f.bounding_box[1][0])
(u0, _), (u1, _) = spot.bounding_box
u = (u0 + u1) / 2
thin = occ.Box(occ.Pnt(u - 14, -20.01, -10), occ.Pnt(u + 14, -19.99, 20))
Views(("A slice at y = −20 through the pipe, seen from the front",
       [(metal * thin, "shade"), (metal * thin, "ink"), (coolant * thin, "accent")]),
      angles=(-90, 0, 0), key={"ink": "metal", "accent": "oil"})
```

## Rules before solving

The design moves the points freely, and not every path makes a valid pipe. `Rules` checks the path alone, with its
centerline sampled from the sketch's OpenCASCADE wire, in a few milliseconds. It measures:

- the tightest bend, which must be at least 1.2 Ro: a pipe bent tighter than Ro folds into itself, and the chamfers
  reach Ro from its axis;
- the closest approach of two passes, at least 2 Ro + 2 between their axes, so that 2 is left between the pipes;
- the margin to the plate's edges: the pipe stays 1 inside, except near the ends, where it leaves the plate;
- the length of the path.

```python
def centerline(path, n=1500):
    points = []
    for e in path.ToOCCWire().edges:
        t0, t1 = e.parameter_interval
        points += [(q.x, q.y) for q in (e.Value(t0 + (t1 - t0) * k / n) for k in range(n + 1))]
    return np.array(points)


def Rules(path):
    p = centerline(path)
    d1 = np.gradient(p, axis=0)
    d2 = np.gradient(d1, axis=0)
    cross = np.abs(d1[:, 0] * d2[:, 1] - d1[:, 1] * d2[:, 0])
    speed = np.linalg.norm(d1, axis=1)
    bend = (speed ** 3 / np.maximum(cross, 1e-300))[3:-3].min()
    s = np.r_[0, np.cumsum(np.hypot(*np.diff(p, axis=0).T))]
    q, sq = p[::5], s[::5]
    dist = np.hypot(q[:, None, 0] - q[None, :, 0], q[:, None, 1] - q[None, :, 1])
    far = np.abs(sq[:, None] - sq[None, :]) > 4 * (Ro + 1) * math.pi / 2
    clearance = dist[far].min() if far.any() else 1e9
    away = (s > 15) & (s < s[-1] - 15)
    margin = min((WP / 2 - Ro - 1) - np.abs(p[:, 0]).max(),
                 (W / 2 - Ro - 1) - np.abs(p[away, 1]).max())
    return {"bend": bend, "clearance": clearance, "margin": margin, "length": s[-1]}


r = Rules(path)
print("tightest bend", round(r["bend"], 1), "passes", round(r["clearance"], 1), "apart")
print("margin", round(r["margin"], 1), "length", round(r["length"], 1))
```

## The mesh

`Mesh` has netgen use the names. The bore gets elements of 1, so that the thin layer of oil along the wall, where the
heat goes in, is resolved, and everything else gets elements of 5 at most. The elements are curved to order 3, so that
the bore stays round.

```python
def Mesh(glued):
    glued.faces["bore"].maxh = 1.0
    mesh = ngs.Mesh(occ.OCCGeometry(glued).GenerateMesh(maxh=5.0, minh=0.3))
    mesh.Curve(3)
    return mesh


mesh = Mesh(ColdPlate(path)[0])
print(mesh.ne, "tetrahedra in", mesh.GetMaterials())
print("boundaries", sorted(set(mesh.GetBoundaries())))
```

## Flow and heat

The oil is an ISO VG 32 oil:

- a kinematic viscosity of 32 mm²/s, so a dynamic viscosity `mu` of 32 times the density;
- a density `rho` of 870 kg/m³;
- a heat capacity `cp` of 2000 J/(kg K);
- a conductivity `k_oil` of 0.13 W/(m K).

Plate and pipe are aluminum, `k_metal` = 200 W/(m K). The flow `Q` is 0.2 L/min, the heat `P` 50 W and the oil comes
in at `T_in` = 20 °C. In the units of the page:

```python
rho = 870e-9
mu = rho * 32.0
cp = 2000.0
k_oil = 0.13e-3
k_metal = 0.2
Q = 0.2e6 / 60
P = 50.0
T_in = 20.0


def poiseuille(length):
    return 8 * mu * length * Q / (math.pi * Ri ** 4)
```

`Solve` takes the mesh and has NGSolve solve the flow and then the temperature. How it does that does not matter for
the geometry, so it sits in a hidden cell. In the coolant: Stokes flow, scaled to 0.2 L/min, at a Reynolds number of
about 22. In metal and coolant together: the temperature, carried along by the flow, with the 50 W going into the
heated part of the bottom and the oil coming in at 20 °C. It returns the pressure drop, the hottest point of the heated
bottom, its mean temperature, a p-norm of it (p = 20: close to the hottest point, but smooth, so that an optimizer can
follow it) and the share of the 50 W that the oil carries out. `poiseuille` is the pressure drop of a straight pipe
of the given length.

```python nbsphinx="hidden"
import os
import time


def Solve(mesh, keep=False):
    X = ngs.VectorH1(mesh, order=2, definedon="coolant", dirichlet="bore") * ngs.H1(mesh, order=1, definedon="coolant")
    (u, p), (v, q) = X.TnT()
    stokes = ngs.BilinearForm(X, symmetric=True)
    stokes += (mu * ngs.InnerProduct(ngs.grad(u), ngs.grad(v)) + 1e-6 * mu * u * v
               - ngs.div(u) * q - ngs.div(v) * p) * ngs.dx("coolant")
    stokes.Assemble()
    n = ngs.specialcf.normal(3)
    push = ngs.LinearForm(-(v * n) * ngs.ds("inlet")).Assemble()
    up = ngs.GridFunction(X)
    up.vec.data = stokes.mat.Inverse(X.FreeDofs(), inverse="pardiso") * push.vec
    scale = Q / ngs.Integrate(up.components[0] * n * ngs.ds("outlet"), mesh)
    U = up.components[0] * scale
    k = mesh.MaterialCF({"metal": k_metal, "coolant": k_oil})
    fes = ngs.H1(mesh, order=3)
    T, w = fes.TnT()
    tau = mesh.MaterialCF({"coolant": 1.0}, default=0.0) * ngs.specialcf.mesh_size / (2 * 3 * ngs.Norm(U) + 1e-9)
    a = ngs.BilinearForm(fes)
    a += k * ngs.grad(T) * ngs.grad(w) * ngs.dx
    a += -rho * cp * T * (U * ngs.grad(w)) * ngs.dx("coolant")
    a += rho * cp * (U * n) * T * w * ngs.ds("outlet")
    a += rho * cp * tau * (U * ngs.grad(T)) * (U * ngs.grad(w)) * ngs.dx("coolant")
    a.Assemble()
    f = ngs.LinearForm(fes)
    heated = ngs.IfPos(W / 2 - ngs.sqrt(ngs.x ** 2), 1.0, 0.0)
    f += P / (W * W) * heated * w * ngs.ds("bottom")
    f += -rho * cp * (U * n) * T_in * w * ngs.ds("inlet")
    f.Assemble()
    gf = ngs.GridFunction(fes)
    gf.vec.data = a.mat.Inverse(inverse="pardiso") * f.vec
    hottest = max(gf(mesh(*v.point)) for v in mesh.vertices
                  if abs(v.point[2] + wall) < 1e-6 and abs(v.point[0]) <= W / 2)
    pnorm = (ngs.Integrate(heated * gf ** 20 * ngs.ds("bottom"), mesh) / (W * W)) ** (1 / 20)
    carried = rho * cp * ngs.Integrate((U * n) * (gf - T_in) * ngs.ds("outlet"), mesh)
    mean = ngs.Integrate(heated * gf * ngs.ds("bottom"), mesh) / (W * W)
    out = {"dp": scale, "pnorm": pnorm, "max": hottest, "mean": mean, "balance": carried / P, "tets": mesh.ne}
    progress = os.environ.get("PLANEX_DOCS_PROGRESS")
    if progress:
        Solve.count = getattr(Solve, "count", 0) + 1
        with open(progress, "a") as log:
            log.write(f"{time.strftime('%H:%M:%S')} solve {Solve.count} p-norm {pnorm:.2f} dp {scale:.3f}\n")
    if keep:
        out.update({"mesh": mesh, "U": U, "p": up.components[1] * scale, "T": gf})
    return out
```

```python
start = Solve(mesh)
print("hottest", round(start["max"], 1), "°C, p-norm", round(start["pnorm"], 1), "°C")
print("mean", round(start["mean"], 1), "°C, pressure drop", round(start["dp"], 3), "kPa")
print("Poiseuille for a straight pipe as long", round(poiseuille(r["length"]), 3), "kPa")
print("the oil carries out", round(100 * start["balance"], 1), "% of the heat")
```

**Checks:**

- the oil carries out the heat that goes in, to within a fraction of a percent;
- the pressure drop equals that of a straight pipe as long as the path: in a flow this slow, the bends cost next to
  nothing. This makes the limit on the pressure drop a limit on the length of the pipe.

## The optimization

COBYLA starts from the S of Figure 1, which already spreads the pipe over the heated area. A straight pipe down the
middle would be a poor start, because all the heat would have to reach one pass.

The objective is the p-norm of the bottom's temperature. The constraints, each scaled to about 1, are:

- the three rules on bend, clearance and margin;
- a pressure drop of at most `dp_max` = 0.95 kPa, almost three times that of the straight pipe: room for a pipe almost
  three times as long.

`evaluate` builds a layout, checks the rules and solves only if they pass, and it ends the run after `budget` = 80
solves. A layout that breaks a rule, or cannot be
built or solved, gets a high temperature and the pressure drop of a straight pipe as long as its path, so that COBYLA
still sees which way is out. `evaluate` remembers every layout, as COBYLA asks for the objective and each constraint at
the same point.

COBYLA works on the eight dimensions scaled to 0 to 1 over the box from 10 to 10 short of the plate's edges, so that
one step size suits all of them. It starts from the S with steps of a tenth of the box. Layouts that break
a rule cost no solve and do not count toward the 80 solves, and COBYLA may stop earlier on its own, when its steps get
small. It may end a hair over a constraint, so the best layout is the coolest one within 0.1 % of the pressure
limit.

```python
lo = np.array([10.0, 10.0] * 4)
hi = np.array([WP - 10, W - 10] * 4)
budget = 80
trail = []
known = {}


class Enough(Exception):
    pass


def pairs(x):
    return [(x[2 * k], x[2 * k + 1]) for k in range(4)]


def evaluate(x):
    key = tuple(np.round(x, 6))
    if key in known:
        return known[key]
    e = {"x": np.array(x), "pnorm": 150.0, "solved": False,
         "bend": 0.0, "clearance": 0.0, "margin": -10.0, "length": 1e3}
    try:
        path = Path(pairs(x))
        e.update(Rules(path))
    except Exception:
        path = None
    e["dp"] = poiseuille(e["length"])
    if e["bend"] >= 1.2 * Ro and e["clearance"] >= 2 * Ro + 2 and e["margin"] >= 0:
        if sum(t["solved"] for t in trail) == budget:
            raise Enough
        try:
            e.update(Solve(Mesh(ColdPlate(path)[0])), solved=True)
            e["solve"] = sum(t["solved"] for t in trail) + 1
        except Exception:
            pass
    trail.append(e)
    known[key] = e
    return e


def layout(u):
    return lo + np.clip(u, 0, 1) * (hi - lo)


def temperature(u):
    return evaluate(layout(u))["pnorm"]


def bend_rule(u):
    return evaluate(layout(u))["bend"] / (1.2 * Ro) - 1


def clearance_rule(u):
    return evaluate(layout(u))["clearance"] / (2 * Ro + 2) - 1


def margin_rule(u):
    return evaluate(layout(u))["margin"] / 5


def pressure_rule(u):
    return 1 - evaluate(layout(u))["dp"] / dp_max


x0 = np.array(initial).ravel()
dp_max = 0.95
try:
    stop = minimize(temperature, (x0 - lo) / (hi - lo), method="COBYLA", bounds=[(0, 1)] * 8,
                    constraints=[{"type": "ineq", "fun": c}
                                 for c in (bend_rule, clearance_rule, margin_rule, pressure_rule)],
                    options={"rhobeg": 0.1, "maxiter": 1000, "tol": 1e-3}).message
except Enough:
    stop = "Stopped after the budget of solves."
solved = [e for e in trail if e["solved"]]
within = [e for e in solved if e["dp"] <= dp_max * 1.001]
best = min(within, key=lambda e: e["pnorm"])
print(stop, len(trail), "layouts,", len(solved), "solved")
print("start: p-norm", round(trail[0]["pnorm"], 2), "°C, hottest", round(trail[0]["max"], 2), "°C")
print("best: solve", best["solve"], "p-norm", round(best["pnorm"], 2), "°C")
print("best: hottest", round(best["max"], 2), "°C")
print("best: pressure drop", round(best["dp"], 3), "kPa, points", np.round(best["x"], 1).tolist())
```

## The result

Figure 4 counts every real solve; layouts that broke a rule cost no solve and are not included. Seen from above,
Figure 5 shows the start, the path of the best layout after every five solves, and the best one. The pipe bends
out to both sides, so that its passes reach more of the heated area.

```python nbsphinx="hide-input"
over = [e for e in solved if e["dp"] > dp_max * 1.001]
line = np.minimum.accumulate([e["pnorm"] if e["dp"] <= dp_max * 1.001 else math.inf for e in solved]).tolist()
Chart("Real 3D solves", "plate bottom, p-norm 20 of the temperature [°C]",
      dots={"within the pressure limit": ([e["solve"] for e in within], [e["pnorm"] for e in within])},
      others={"over the pressure limit": ([e["solve"] for e in over], [e["pnorm"] for e in over])},
      accent={"best so far": (list(range(1, len(line) + 1)), line)},
      title="The temperature of every solved layout")

history, low = [], None
for e in solved:
    if e["dp"] <= dp_max * 1.001 and (low is None or e["pnorm"] < low["pnorm"]):
        low = e
    if e["solve"] % 5 == 0:
        history.append(low)
heated_top = occ.Box(occ.Pnt(-W / 2, -W / 2, -wall), occ.Pnt(W / 2, W / 2, 0)).faces.Max(occ.Z)
shapes = [(plate_top, "ink"), (heated_top, "soft"), (Path(initial).ToOCCWire(), "guide")]
shapes += [(Path(pairs(e["x"])).ToOCCWire(), "light") for e in history]
shapes += [(Path(pairs(best["x"])).ToOCCWire(), "accent")]
Views((f"The best layout after every 5 solves, and the best: solve {best['solve']}", shapes),
      angles=(0, 0, 0),
      key={"guide": "start: the S", "light": "best after 5, 10, ... solves", "accent": "best",
           "soft": "heated area"})
```

Figure 6 shows the underside of the plate from below, for the start and the best layout on one color scale,
and for the best layout again on a scale of its own, which shows where it is still warmest. The lines are the pipe's
outline and the feet of the chamfers.

```python nbsphinx="hide-input"
shown = {}
for name, e in (("start", trail[0]), ("best", best)):
    p = Path(pairs(e["x"]))
    shown[name] = (p, Solve(Mesh(ColdPlate(p)[0]), keep=True))


def outline(path):
    q = centerline(path, 600)
    d = np.gradient(q, axis=0)
    normal = np.c_[-d[:, 1], d[:, 0]] / np.linalg.norm(d, axis=1)[:, None]
    foot = math.sqrt(Ro ** 2 - zc ** 2) + 2.0
    lines = []
    for off in (Ro, -Ro, foot, -foot):
        o = q + off * normal
        o = o[(np.abs(o[:, 0]) <= WP / 2) & (np.abs(o[:, 1]) <= W / 2)]
        if len(o) > 1:
            lines.append(px.FromPolyline([tuple(r) for r in o]))
    return lines


def bottom(result):
    def field(face, plot=True, **_):
        (u0, v0), (u1, v1) = face.bounding_box
        gu, gv = np.meshgrid(np.linspace(u0, u1, 241), np.linspace(v0, v1, 241))
        uv = np.c_[gu.ravel(), gv.ravel()]
        values = result["T"](result["mesh"](uv[:, 0], uv[:, 1], np.full(len(uv), -wall + 1e-3))).ravel()
        return {"points": [tuple(r) for r in uv], "values": values, "label": "°C"}
    return field


plate2d = px.QuickSketch().RectangleC(WP, W).Face()
clim = (min(s["mean"] for _, s in shown.values()) - 8, max(s["max"] for _, s in shown.values()))
panels = [(plate2d, f"{name}: hottest {s['max']:.1f} °C",
           {"field": bottom(s), "cmap": MAP.reversed(), "clim": clim, "paths": outline(p), "fill": False})
          for name, (p, s) in shown.items()]
p, s = shown["best"]
panels.append((plate2d, "Best, on its own scale",
               {"field": bottom(s), "cmap": MAP.reversed(), "paths": outline(p), "fill": False}))
Row(*panels, caption="The temperature of the plate's underside")
```

Figure 7 shows the pressure in the mid-plane of the pipe for the start and the best layout on one color
scale. The cut is a planex2d face where the plane meets the coolant (`px.Intersect3d`). The best layout spends more
pressure on a longer pipe. Figure 8 shows the two cold plates in 3D.

```python nbsphinx="hide-input"
def pressure(result):
    def field(face, plot=True, **_):
        (u0, v0), (u1, v1) = face.bounding_box
        gu, gv = np.meshgrid(np.linspace(u0, u1, 400), np.linspace(v0, v1, 400))
        uv = np.c_[gu.ravel(), gv.ravel()]
        uv = uv[face.Distance(uv, signed=True) < -0.02]
        values = result["p"](result["mesh"](uv[:, 0], uv[:, 1], np.full(len(uv), zc))).ravel()
        return {"points": [tuple(r) for r in uv], "values": values, "label": "kPa"}
    return field


cuts = {name: px.Intersect3d(px.XY.Offset(zc), ColdPlate(p)[2]) for name, (p, s) in shown.items()}
p_all = [s["p"](s["mesh"](0.0, y, zc)) for _, s in shown.values() for y in (-W / 2 + 0.5, W / 2 - 0.5)]
rim = plate2d.outer
Row(*((cuts[name], f"{name}: pressure",
       {"field": pressure(s), "cmap": MAP.reversed(), "clim": (min(p_all), max(p_all)), "paths": [rim],
        "view": ((-WP / 2 - 2, WP / 2 + 2), (-W / 2 - 2, W / 2 + 2))})
      for name, (p, s) in shown.items()), caption="The pressure in the mid-plane of the pipe")
solids = {name: ColdPlate(p)[0] for name, (p, s) in shown.items()}
Views(*((f"{name} in 3D", [(solid, "shade"), (solid, "ink")]) for name, solid in solids.items()),
      caption="The two cold plates")
```

The best layout as a solid, to turn and zoom:

```python
from ngsolve_webgpu.jupyter import Draw

glued, metal, coolant = ColdPlate(Path(pairs(best["x"])))
metal.faces.col = (0.867, 0.902, 0.941)
coolant.faces.col = (0.353, 0.663, 0.812)
Draw(glued, euler_angles=[-60, 0, 30])
```

See the [SciPy page](scipy.md) for the same pattern in 2D, with planex2d measurements as the objective.
