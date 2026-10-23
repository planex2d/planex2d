# Sketches

The quick sketch without a solver, the constraint sketch, and the constraint kinds. See [Quick sketch](../core/sketch/index.md) and [Constraint sketches](../core/constraints/index.md) for the guides.

*class* QuickSketch  
A sketch without constraints: drawing without solves or analysis, as fast as it gets.

Arc(*r: float*, *sweep: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Arc(*to: Point \| Id*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Arc(*to: Point \| Id*, *r: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Arc(*start: Point \| Id*, *end: Point \| Id*, *r: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Arc(*\*ends: Point \| Id \| float \| [Constraint](#planex.Constraint "planex.Constraint")*, *sweep: float \| None = None*, *center: Point \| None = None*, *concentric: Id \| None = None*, *coincident: Id \| None = None*, *tangent: tuple\[float, float\] \| Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| tuple\[Id, Id\] \| None = None*, *through: Point \| None = None*, *until: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| SketchUntil \| None = None*, *reverse: bool = False*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Draw an arc from the pen or between two ends.

The positional arguments are read in this order: start, end, then the sizes (r, sweep), then the flags. The circle of the arc: center= or concentric= with radius r (default through the start), coincident= (an arc’s circle), else continuing the pen’s direction with radius r (r \> 0 turns left). On a given circle the start is the given start (an element: where it meets the circle nearest the pen, or (element, point)), else the pen projected onto it; the arc runs counterclockwise between two given ends, along the pen’s direction from the pen, with sweep= by its sign. Its end: a positional to, sweep= or until=.

Parameters:  
- **r** – the radius; from the pen a negative r starts backwards, its centre on the other side; with two ends the arc runs counterclockwise the short way, -r clockwise.

- **sweep** – the turn in degrees from the pen, positive counterclockwise; a full turn (also sweep=360 with center=) draws one circle element (Circle.k, or id=) through the pen, its origin the pen’s point, and the pen stays; in a px.Sketch it touches the element before it; as sweep= the turn on a circle given by center=, concentric= or coincident=.

- **to** – the end, as for Line: a point (h, v), a point id, sk.pen, an element (until=px.first, the default, or px.last picks the intersection along the arc) or (element, point). Arc(to) alone leaves the pen along its direction. An element as an end needs the circle.

- **start** – the start of the two-end form, read as to.

- **end** – the end of the two-end form, read as to.

- **ends** – start and end, or a single start with sweep= or until=, then the sizes, then the flags.

- **flags** – as for Line.

- **center** – the centre, a point or a point id.

- **concentric** – a point id or an arc whose centre the arc shares.

- **coincident** – an arc whose circle the arc lies on.

- **tangent** – (dh, dv), the direction at the start; sk.pen, the pen’s direction; a target: the arc leaves the pen along its direction and ends touching it; (a, b) with r: the arc of radius r touching a and b, from a to b, by default in the corner they face. A target given as (target, point): of a line the side the point is on, of a circle a point inside puts the arc inside.

- **through** – a third point the arc passes.

- **until** – px.first / px.last until the first / last line, arc, circle or spline along the arc (the start not counted), or a target. With sweep= or until= a single positional is the from: sk.Arc(“tangent”, sweep=90, coincident=”c”).

- **reverse** – the other way round; with two ends and r the long way.

- **options** – id= (the arc’s id), and its properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **TypeError** – a wrong form of arguments, an unknown keyword, a keyword for an end (the ends are positional), r= or ccw=, near=.

- **ValueError** – the arc cannot be placed (the message names the form it needs); nothing is drawn.

Examples

``` python
sk.Arc(5, 90).Arc((3, 1), tangent=(0, 1)).Arc((0, 0), (2, 0), 5)
sk.Arc("lower", "upper", coincident="boss")
```

ArcSlot(*r: float*, *sweep: float*, *w: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
ArcSlot(*to: Point \| Id*, *w: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
ArcSlot(*to: Point \| Id*, *r: float*, *w: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
ArcSlot(*start: Point \| Id*, *end: Point \| Id*, *r: float*, *w: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
ArcSlot(*\*ends: Point \| Id \| float \| [Constraint](#planex.Constraint "planex.Constraint")*, *sweep: float \| None = None*, *center: Point \| None = None*, *concentric: Id \| None = None*, *coincident: Id \| None = None*, *tangent: tuple\[float, float\] \| Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| tuple\[Id, Id\] \| None = None*, *through: Point \| None = None*, *until: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| SketchUntil \| None = None*, *reverse: bool = False*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Draw an arc slot: an arc and its offset on both sides, closed by round caps.

Every form of Arc, with the width w as the last number before the flags. The arc is the slot’s spine, a construction element; the outline is its offset at w / 2 on both sides, the caps half circles around the spine’s ends. The pen goes on at the spine’s end in its direction (a new chain). The ids are those of Slot: ArcSlot.k is the wire, with ArcSlot.k.spine, .left, .right, .startcap and .endcap; a Sketch has the dimension ArcSlot.k.w and keeps the slot a slot when the spine moves. Remove(“ArcSlot.1”) removes the whole slot.

Parameters:  
- **w** – the width of the slot, across the spine; positive and below the diameter 2 r of the spine arc.

- **r** – as for Arc, which draws the spine.

- **sweep** – as for Arc, which draws the spine.

- **to** – as for Arc, which draws the spine.

- **start** – as for Arc, which draws the spine.

- **end** – as for Arc, which draws the spine.

- **ends** – as for Arc, which draws the spine.

- **flags** – as for Arc, which draws the spine.

- **center** – as for Arc, which draws the spine.

- **concentric** – as for Arc, which draws the spine.

- **coincident** – as for Arc, which draws the spine.

- **tangent** – as for Arc, which draws the spine.

- **through** – as for Arc, which draws the spine.

- **until** – as for Arc, which draws the spine.

- **reverse** – as for Arc, which draws the spine.

- **options** – id= replaces ArcSlot.k, and the outline’s properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **TypeError** – no width as the last number, another form of arguments, an unknown keyword.

- **ValueError** – w is not positive or not below 2 r; the spine has no length or cannot be placed; id is taken.

Examples

``` python
sk.ArcSlot(10, 90, 2)
sk.ArcSlot((10, 0), (0, 10), 10, 2, id="track")
```

Chamfer(*at: Id \| Point \| list\[Id \| Point\]*, *d: float*, */*, *d2: float \| None = None*, *angle: float \| None = None*, *reverse: bool = False*, *\**, *id: str \| list\[str\] \| None = None*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Cut corners with a straight line, in place.

A corner is a point that is the end of exactly two drawn elements (construction elements do not count) whose directions differ. The new line is the element ‘Chamfer.k’ (k counts per sketch, in the order the corners are given) with the properties of the two elements merged, the name of the first winning (the first element is the one drawn first), and the properties given in options set over them. The two elements keep their ids, cut back to the cut points. What is cut off stays as a construction element named as Trim names it (‘Line.1.trimmed.1’, ‘Line.2.trimmed.1’; the sharp corner is the end the two share, ‘Line.1.trimmed.1.end’). An element the cut reaches the far end of is used up: it becomes construction as a whole, and the new element ends at its far end. Lines and arcs are cut exactly; a spline is cut where the line meets its exact curve. Remove(‘Chamfer.1’) takes the line away; the elements stay cut.

A corner that cannot be cut (a tangent joint, a point on no vertex, more or fewer than two ends, a size that does not fit) is skipped with a PlanexWarning that names why and the largest size that fits; if none can be cut the call raises and changes nothing.

Parameters:  
- **at** – the corners: a point id (‘Line.1.end’, ‘A’), a sketch point or a selection of points, a point (h, v) on a corner, or a list of these; there is no default.

- **d** – the length on the first element, a length \> 0; also on the second if d2 and angle are None.

- **d2** – the length on the second element, a length \> 0.

- **angle** – with d, the angle in degrees between the first element and the new line, between 0 and 180. d2 and angle exclude each other.

- **reverse** – True makes the second element the first.

- **id** – the id of the new element for one corner (a str), or one id per corner (a list); default ‘Chamfer.k’.

- **options** – the new element’s properties name=, maxh=, minh=, hpref=, layer=, col=, each one value for all corners or a list of one per corner (col=\[(1, 0, 0), (0, 0, 1)\]); they replace the merged ones.

Returns:  
this sketch, so calls chain.

Raises:  
- **TypeError** – at is none of the forms above, id is neither a str nor a list of str, or an option is no property.

- **ValueError** – a size is not positive and finite, the angle is not between 0 and 180 or d2 and angle are both given, no corner is given, an id is no point, or no corner can be cut (the message names each corner and why); an id is taken, or the number of ids or of a property’s values differs from the number of corners.

Examples

``` python
sk.Chamfer("Line.2.end", 1)
sk.Chamfer([(4, 0), (4, 4)], 1, d2=2, id=["a", "b"])
```

Circle(*r: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Circle(*h: float*, *v: float*, *r: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Circle(*\*r: float \| [Constraint](#planex.Constraint "planex.Constraint")*, *center: Point \| None = None*, *concentric: Id \| None = None*, *coincident: Id \| None = None*, *through: Point \| None = None*, *tangent: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| tuple\[Id, Id\] \| None = None*, *reverse: bool = False*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Draw a circle around the pen’s position, around (h, v), or placed by the helpers.

With helpers the centre is center= or concentric=, else the pen’s position; the radius is r, coincident=, through= or tangent=. The circle is one circle edge starting at the point r from the centre along +h.

Parameters:  
- **r** – the radius; in the helper form at most one radius, then the flags.

- **h** – the centre’s h.

- **v** – the centre’s v.

- **flags** – px.Fix.

- **center** – the centre, a point or a point id.

- **concentric** – a point id or an arc whose centre is the circle’s.

- **coincident** – an arc or circle whose radius the circle takes.

- **through** – a point or point id on the circle.

- **tangent** – a target x the circle touches: of a line where the foot from the centre lies, of an arc where the line of the centres meets it; on the edge first, then nearest the target’s point, else the smaller radius. (a, b) with r: the circle touching both, as Arc(r, tangent=(a, b)). (target, point): the solution near the point.

- **reverse** – take the next solution.

- **options** – id= (the circle’s id), and its properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **TypeError** – a wrong form of arguments, an unknown keyword, near=.

- **ValueError** – the circle cannot be placed.

Examples

``` python
sk.Circle(5)
sk.Circle(2, center="Line.1.end", id="hole")
```

CircularPattern(*ids: Ids*, *center: Id*, *count: int*, *angle: float \| None = None*, *auto_constraint: bool \| None = None*, *internal_constraints: bool \| None = None*, *id: str \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Add count instances of the elements turned about a point, the original the first.

The pattern is ‘CircularPattern.N’ (or id), the copies named after their sources by their step (‘CircularPattern.1.3.bolt’).

Parameters:  
- **ids** – the elements: one id, a list or a selection.

- **center** – the point id they turn about.

- **count** – the number of instances, the original included.

- **angle** – the step in degrees; default 360 / count, and the pattern stays spread over the full turn when count changes.

- **auto_constraint** – tie the copies to the originals through the pattern, its step the dimension ‘CircularPattern.N.angle’ and its number of instances ‘CircularPattern.N.count’ (set like a dimension: copies are added or the last ones removed), each copy listed as one constraint ‘CircularPattern.N.k’; default whether the constraints are enabled.

- **internal_constraints** – show the constraints among the originals on the copies (‘CircularPattern.1.3.\<id\>’, derived), or with auto_constraint=False give the copies those constraints; default as auto_constraint.

- **id** – replaces CircularPattern.N.

Examples

``` python
sk.CircularPattern("bolt", "Circle.1.center", 6)
sk.CircularPattern(["Line.1", "Line.2"], px.origin, 4, angle=30, id="ribs")
```

Close(*\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *id_wire: str \| None = None*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Draw a line back to where the chain started; the chain ends.

Parameters:  
- **flags** – as for Line.

- **id_wire** – replaces the chain’s wire id Chain.k (for the patterns, RigidGroup, Remove, Edge, Measure).

- **options** – id= (the closing line’s id), and its properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
**TypeError** – a positional argument other than a flag, or an unknown keyword.

Examples

``` python
sk.Line(10).Turn(90).Line(5).Close()
sk.Close(id_wire="outline")
```

Construction(*\**, *id: str \| None = None*) → [Wire](geometry.md#planex.Wire "planex.Wire")  
The construction edges as a wire.

Parameters:  
**id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – in mode=’strict’ while the sketch can still move.

Examples

``` python
helpers = sk.Construction()
```

Copy(*\**, *id: str \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
An independent copy; copy.copy and copy.deepcopy give the same.

Parameters:  
**id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Examples

``` python
b = a.Copy()
```

Direction(*d: tuple\[float, float\]*, */*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Direction(*dh: float*, *dv: float*, */*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Set the pen’s direction.

Parameters:  
- **d** – the direction (dh, dv).

- **dh** – the direction’s h; with dv.

- **dv** – the direction’s v.

Raises:  
**TypeError** – another form of arguments.

Examples

``` python
sk.Direction((0, 1)).Line(5)
sk.Direction(1, 1)
```

Face(*\**, *id: str \| None = None*) → [Face](geometry.md#planex.Face "planex.Face")  
The region the drawn elements bound.

Construction elements are left out, crossings cut, stretches drawn twice by closed wires cancel, an open wire’s stretch on another edge is left out, holes by nesting.

Parameters:  
**id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – the region has several parts (Shape() keeps them all); in mode=’strict’ while the sketch can still move.

Examples

``` python
face = sk.Face()
```

Fillet(*at: Id \| Point \| list\[Id \| Point\]*, *r: float*, */*, *\**, *id: str \| list\[str\] \| None = None*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Round corners with an arc tangent to both elements, in place.

A corner is a point that is the end of exactly two drawn elements (construction elements do not count) whose directions differ. The new arc is the element ‘Fillet.k’ (k counts per sketch, in the order the corners are given) with the properties of the two elements merged, the name of the first winning (the first element is the one drawn first), and the properties given in options set over them. The two elements keep their ids, cut back to the touch points. What is cut off stays as a construction element named as Trim names it (‘Line.1.trimmed.1’, ‘Line.2.trimmed.1’; the sharp corner is the end the two share, ‘Line.1.trimmed.1.end’). An element the cut reaches the far end of is used up: it becomes construction as a whole, and the new element ends at its far end. Lines and arcs are cut exactly; a spline is cut where the arc meets its exact curve. Remove(‘Fillet.1’) takes the arc away; the elements stay cut.

A corner that cannot be cut (a tangent joint, a point on no vertex, more or fewer than two ends, a size that does not fit) is skipped with a PlanexWarning that names why and the largest size that fits; if none can be cut the call raises and changes nothing.

Parameters:  
- **at** – the corners: a point id (‘Line.1.end’, ‘A’), a sketch point or a selection of points, a point (h, v) on a corner, or a list of these; there is no default.

- **r** – the radius, a length \> 0.

- **id** – the id of the new element for one corner (a str), or one id per corner (a list); default ‘Fillet.k’.

- **options** – the new element’s properties name=, maxh=, minh=, hpref=, layer=, col=, each one value for all corners or a list of one per corner (col=\[(1, 0, 0), (0, 0, 1)\]); they replace the merged ones.

Returns:  
this sketch, so calls chain.

Raises:  
- **TypeError** – at is none of the forms above, id is neither a str nor a list of str, or an option is no property.

- **ValueError** – a size is not positive and finite, no corner is given, an id is no point, or no corner can be cut (the message names each corner and why); an id is taken, or the number of ids or of a property’s values differs from the number of corners.

Examples

``` python
sk.Fillet("Line.2.end", 0.5)
sk.Fillet([(4, 0), (4, 4)], 0.5, name=["r1", "r2"])
```

Finish(*id_wire: str \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
End the chain without closing it; the next one starts where the pen is.

Parameters:  
**id_wire** – replaces the chain’s wire id Chain.k (for the patterns, RigidGroup, Remove, Edge, Measure).

Raises:  
**ValueError** – id_wire is taken or invalid, or no chain is drawn.

Examples

``` python
sk.Line(10).Finish()
sk.Line(10).Turn(90).Line(5).Finish(id_wire="rail")
```

Geometry(*id: Id*) → dict  
An element’s solved geometry.

Parameters:  
**id** – the element.

Returns:  
a point its point; a line start, end, length, angle (degrees from h); a circle center, r; an arc center, r, start, end, ccw, sweep (degrees, positive counterclockwise); an axis normal_angle, d; a spline points, closed, tangents, path, start, end, projected, style (an offset curve also source).

Return type:  
kind, and

Examples

``` python
sk.Geometry("Line.1")["length"]
sk.Geometry("Arc.1")["sweep"]
```

Hexagon(*af: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Hexagon(*h: float*, *v: float*, *af: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Hexagon(*af: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *center: Point \| None = None*, *concentric: Id \| None = None*, *coincident: Id \| None = None*, *through: Point \| None = None*, *tangent: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| tuple\[Id, Id\] \| None = None*, *reverse: bool = False*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Draw a regular hexagon of width af across its flats, as for a nut.

A Polygon of six sides whose circle has the radius af / sqrt(3): the first corner is along the pen’s drawing direction from the centre, so two flats run parallel to it. Hexagon.k.Line.1 to .Line.6 run from the first corner counterclockwise, Hexagon.k.circle is the construction circle, and Hexagon.k is the wire of the lines. The pen and its direction stay. A Sketch keeps the hexagon regular and has the dimension Hexagon.k.af, the distance between two opposite flats (sk\[“Hexagon.1.af”\] = 17; sk.Update()). Remove(“Hexagon.1”) removes the whole hexagon.

Parameters:  
- **af** – the width across flats; positive.

- **h** – the centre’s h.

- **v** – the centre’s v.

- **flags** – as for Circle, which draws the circle.

- **center** – as for Circle.

- **concentric** – as for Circle.

- **coincident** – as for Circle.

- **through** – as for Circle.

- **tangent** – as for Circle.

- **reverse** – as for Circle.

- **options** – id= replaces Hexagon.k (id=’nut’ gives nut.circle, nut.Line.1, …), and the sides’ properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **TypeError** – af is no number or not the last number, another form of arguments, an unknown keyword.

- **ValueError** – af is not positive; the circle cannot be placed; id is taken.

Examples

``` python
sk.Hexagon(13)
sk.Hexagon(10, 5, 13, id="nut")
```

Intersect3d(*shape: occ.TopoDS_Shape*, */*, *construction: bool = True*, *name: str \| None = None*, *id: str \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Add where the sketch’s plane meets an OCC shape, as px.Intersect3d, as fixed elements under Intersect3d.N, with the points where 3d wires cross or touch the plane.

Parameters:  
- **shape** – the OCC shape.

- **construction** – references; False adds drawn edges.

- **name** – the name of them all; default they keep the names of the shape’s faces and edges.

- **id** – replaces Intersect3d.N.

Examples

``` python
sk.Intersect3d(box)
sk.Intersect3d(box, construction=False, name="cut")
```

Keyhole(*l: float*, *w: float*, *r: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Keyhole(*dh: float*, *dv: float*, *w: float*, *r: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Keyhole(*to: Point \| Id*, *w: float*, *r: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Keyhole(*start: Point \| Id*, *end: Point \| Id*, *w: float*, *r: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Keyhole(*\*ends: Point \| Id \| float \| [Constraint](#planex.Constraint "planex.Constraint")*, *until: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| SketchUntil \| None = None*, *parallel: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| None = None*, *perpendicular: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| None = None*, *angle: float \| tuple\[float, Id\] \| None = None*, *coincident: Id \| None = None*, *tangent: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| tuple\[Id, Id\] \| None = None*, *distance: float \| tuple\[float, Point\] \| None = None*, *through: Point \| None = None*, *reverse: bool = False*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Draw a keyhole: a straight slot that starts in a round hole.

Every form of Slot, with the hole’s radius r as the last number. The spine is a construction line; the hole is centred at its start. The outline is one loop: the two sides run from the hole’s circle to the round end cap, and the hole is the arc of its circle between the sides, the long way round. The pen goes on at the spine’s end in its direction (a new chain). Keyhole.k is the wire of the outline, with Keyhole.k.spine, Keyhole.k.left and Keyhole.k.right (left and right of the spine’s direction), Keyhole.k.endcap and Keyhole.k.hole. A Sketch keeps the keyhole a keyhole when the spine moves, and has the dimensions Keyhole.k.w and Keyhole.k.r (sk\[“Keyhole.1.r”\] = 4; sk.Update()); with auto_constraint the spine gets what Line gets. Remove(“Keyhole.1”) removes the whole keyhole.

Parameters:  
- **r** – the radius of the hole; larger than w / 2.

- **w** – the width of the slot, across the spine; positive.

- **l** – as for Line, which draws the spine.

- **dh** – as for Line, which draws the spine.

- **dv** – as for Line, which draws the spine.

- **to** – as for Line, which draws the spine.

- **start** – as for Line, which draws the spine.

- **end** – as for Line, which draws the spine.

- **ends** – as for Line, which draws the spine.

- **flags** – as for Line, which draws the spine.

- **until** – as for Line, which draws the spine.

- **parallel** – as for Line, which draws the spine.

- **perpendicular** – as for Line, which draws the spine.

- **angle** – as for Line, which draws the spine.

- **coincident** – as for Line, which draws the spine.

- **tangent** – as for Line, which draws the spine.

- **distance** – as for Line, which draws the spine.

- **through** – as for Line, which draws the spine.

- **reverse** – as for Line, which draws the spine.

- **options** – id= replaces Keyhole.k (id=’hook’ gives hook.spine, hook.left, …), and the outline’s properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **TypeError** – no width and radius as the last numbers, another form of arguments, an unknown keyword.

- **ValueError** – w is not positive; r is not larger than w / 2; the spine is not longer than sqrt(r^2 - (w / 2)^2) or cannot be placed; id is taken.

Examples

``` python
sk.Keyhole(10, 2, 3)
sk.Keyhole((0, 0), (0, 10), 2, 3, id="hook")
```

Line(*l: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Line(*dh: float*, *dv: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Line(*to: Point \| Id*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Line(*start: Point \| Id*, *end: Point \| Id*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Line(*\*ends: Point \| Id \| float \| [Constraint](#planex.Constraint "planex.Constraint")*, *until: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| SketchUntil \| None = None*, *parallel: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| None = None*, *perpendicular: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| None = None*, *angle: float \| tuple\[float, Id\] \| None = None*, *coincident: Id \| None = None*, *tangent: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| tuple\[Id, Id\] \| None = None*, *distance: float \| tuple\[float, Point\] \| None = None*, *through: Point \| None = None*, *reverse: bool = False*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Draw a line from the pen or between two ends.

The positional arguments are read in this order: start, end, then a length, then the flags. The helpers (the keywords from until to reverse) place the line from references: one of parallel, perpendicular, angle, coincident, tangent or through fixes its carrier (else the pen’s direction), and l, to or until its end. A target is an id, px.H, px.V, an edge, Wire, Face, Shape or OCC shape (projected). Of several solutions the one on the edges before their extensions, then the shortest line, then the one nearest the pen is taken. With auto_constraint the helpers also constrain the line.

Parameters:  
- **l** – the length along the pen’s direction, or with a helper the length along its carrier.

- **dh** – the line’s extent along h from the pen; with dv.

- **dv** – the line’s extent along v from the pen.

- **to** – the end: a point (h, v), a point id (‘A’, ‘Line.1.end’), sk.pen, an element or another target (where the carrier meets it; (target, point) takes the intersection nearest the point). An element as an end needs a helper that fixes the carrier.

- **start** – the start of the two-end form, read as to.

- **end** – the end of the two-end form, read as to.

- **ends** – start and end, or a single start with until=, or a length l, then the flags.

- **flags** – px.Horizontal, px.Vertical, px.Fix (of the line); px.Tangent, px.Perpendicular, px.Parallel, px.Equal (with the element before it).

- **until** – the end: px.first / px.last until the first / last line, arc, circle or spline ahead (the start not counted), or a target; with an element as to, px.first / px.last picks the first or last intersection with it ahead. With until= a single positional is the from: sk.Line(“c1”, tangent=”c2”, until=”wall”).

- **parallel** – a line or target the line is parallel to; sk.pen takes the pen’s direction.

- **perpendicular** – a line or target the line is perpendicular to; sk.pen takes the pen’s direction.

- **angle** – degrees from the h axis, or (degrees, reference) from the line reference.

- **coincident** – a line whose carrier the line lies on.

- **tangent** – one target the line touches, or two circles (the line goes from the first to the second).

- **distance** – with parallel, the offset from the reference: d \> 0 on the side of it the pen is on, d \< 0 the other side (pen on the reference: d \> 0 to its left); (d, point): d on the side of the point. Needs a length, an end target or until=; sk.Offset gives an offset copy.

- **through** – a point: with a direction helper the carrier passes it and the line starts there, else the line goes from the start through it.

- **reverse** – take the other solution; with none, run a forward line the other way.

- **options** – id= (the line’s id), and its properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **TypeError** – a wrong form of arguments, an unknown keyword, a keyword for an end (the ends are positional), near=.

- **ValueError** – the line cannot be placed (the message names what is missing); nothing is drawn.

Examples

``` python
sk.Line(5).Line((5, 3)).Line("A", "B", through=(2, 1))
sk.Line("wall", angle=30)
```

LinearPattern(*ids: Ids*, *dir: Id*, *count: int*, *spacing: float*, *auto_constraint: bool \| None = None*, *internal_constraints: bool \| None = None*, *id: str \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Add count instances of the elements in a row, the original the first.

The pattern is ‘LinearPattern.N’ (or id), the copies named after their sources by their step (‘LinearPattern.1.3.hole’).

Parameters:  
- **ids** – the elements: one id, a list or a selection.

- **dir** – px.H, px.V or a line (its direction start to end).

- **count** – the number of instances, the original included.

- **spacing** – how much further along dir each instance is.

- **auto_constraint** – tie the copies to the originals through the pattern, its spacing the dimension ‘LinearPattern.N.spacing’ and its number of instances ‘LinearPattern.N.count’ (set like a dimension: copies are added or the last ones removed), each copy listed as one constraint ‘LinearPattern.N.k’; default whether the constraints are enabled.

- **internal_constraints** – show the constraints among the originals on the copies (‘LinearPattern.1.3.\<id\>’, derived), or with auto_constraint=False give the copies those constraints; default as auto_constraint.

- **id** – replaces LinearPattern.N.

Examples

``` python
sk.LinearPattern("hole", px.H, 5, 10)
sk.LinearPattern(["Circle.1", "Circle.2"], "Line.1", 3, 8, id="row")
```

Location(*id: Id*, */*) → tuple\[float, float\]  
Where a point is, solved, as (h, v).

Parameters:  
**id** – a point id, or a point of an element: ‘Line.1.end’, ‘.start’, ‘.mid’, ‘.center’, a spline’s ‘.p2’.

Raises:  
**ValueError** – id names no point.

Examples

``` python
sk.Location("Line.1.end")
sk.Location("A")
```

Measure(*name: Id \| None = None*, *to: Point \| Point3 \| [Shape](geometry.md#planex.Shape "planex.Shape") \| Id \| None = None*, *project: bool = False*) → dict\
Measure(*f: Callable*, */*, *\*args*, *id: str \| None = None*, *\*\*params*) → object  
Measure an element, a point or named edges as Shape.Measure does, alone or against a target.

Parameters:  
- **name** – an element’s id, a point (‘Line.1.end’) or a name the edges carry (drawn and construction); default the region of Face()/Shape() and all drawn edges.

- **to** – an id, a name, or anything Distance takes (also OCC shapes).

- **project** – measure to the target projected into the sketch’s plane.

- **f** – a measurement, a callable f(obj, \*args, \*\*params) (see px.measurements): Measure returns its value.

- **args** – the further arguments of f.

- **id** – stores the measurement f under this id: obj.measures then holds it next to the standard measurements, RemoveMeasure(id) drops it.

- **params** – the keyword arguments of f.

Returns:  
type, faces, area, edges, length, bounding_box, and where it has mass center, inertia, Ih, Iv, Ihv, I1, I2, principal_angle (a circle or arc also radius and angle). With to: distance, du, dv, dx, dy, dz, points (the nearest pair) and, for two lines, angle.

Return type:  
Without to

Raises:  
- **ValueError** – no element, point or edge has the name.

- **TypeError** – id without a measurement f.

Examples

``` python
sk.Measure("Line.1")["length"]
sk.Measure(px.measurements.MinThickness(), id="wall")
```

Mirror(*axis: Id*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Mirror(*ids: Ids*, *axis: Id*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Mirror the whole sketch, or some of its elements in place, at the line axis.

The whole sketch: its plane is mirrored, so in 3d it is its mirror image (the plane’s normal turns over); its coordinates, constraints and dimensions stay. Elements: they move with the points they reach (what they share with other elements moves too); their constraints stay and the sketch solves again (Mirrored adds mirrored copies instead).

Parameters:  
- **axis** – px.H, px.V, a line or a symmetry line.

- **ids** – the elements: one id, a list or a selection.

Raises:  
**ValueError** – the constraints do not allow the mirrored place; nothing changes.

Examples

``` python
sk.Mirror(px.V)
sk.Mirror("slot", px.V)
```

Mirrored(*axis: Id*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Mirrored(*ids: Ids*, *axis: Id*, *auto_constraint: bool \| None = None*, *internal_constraints: bool \| None = None*, *id: str \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
A copy of the whole sketch mirrored at the line axis, or mirrored copies of elements added to this sketch.

Mirrored(axis) returns a new sketch, as Mirror(axis). Mirrored(ids, axis) adds copies of the elements, with the points their lines and arcs end at and their properties, under ‘Mirrored.N’ (or id), named after their sources (‘Mirrored.1.Line.1’); ‘Mirrored.N’ is the wire of the copies. The originals stay; it returns this sketch.

Parameters:  
- **axis** – px.H, px.V, a line or a symmetry line.

- **ids** – the elements: one id, a list or a selection.

- **auto_constraint** – keep the copies symmetric to the originals about the axis, listed as one constraint ‘Mirrored.N’; default whether the constraints are enabled.

- **internal_constraints** – show the constraints among the originals on the copies (‘Mirrored.1.\<id\>’, derived), or with auto_constraint=False give the copies those constraints; default as auto_constraint.

- **id** – replaces Mirrored.N.

Examples

``` python
right = sk.Mirrored(px.V)
sk.Mirrored("Chain.1", px.V)
```

Move(*point: Point*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *dir: tuple\[float, float\] \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Move(*dh: float*, *dv: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *dir: tuple\[float, float\] \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Move(*l: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *dir: tuple\[float, float\] \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Move the pen without drawing; a new chain starts there.

Parameters:  
- **point** – where the pen goes: (h, v), a point id (‘Line.1.end’), a selected point or sk.pen.

- **dh** – the move along h from the pen; with dv.

- **dv** – the move along v from the pen.

- **l** – the move along the pen’s direction.

- **flags** – px.Coincident (the start shares the point it moves to), px.Fix (the start is fixed).

- **dir** – the drawing direction (dh, dv) from there, or sk.pen (its direction); default: it stays.

Raises:  
- **TypeError** – another form of arguments, or an unknown keyword.

- **ValueError** – dir has no length.

Examples

``` python
sk.Move((10, 0)).Line(5)
sk.Move("Line.1.end", px.Coincident, dir=(0, 1))
```

Normal() → tuple\[float, float, float\]  
The normal of its plane in 3d.

Examples

``` python
sk.Normal()
```

Offset(*source: Ids*, */*, *d: float \| tuple\[float, Point\]*, *both_sides: bool \| None = None*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Add new elements at distance d from source that follow it: the exact offset, round around corners, sharp where the sides cross.

The offset of a spline is a curve at exactly d (kind ‘Spline’). With the constraints on, the offset follows the source and ‘\<id\>.d’; a change of d moves the geometry as little as it can, a free source too: fix or dimension the source to keep it. A QuickSketch makes the same elements without ties, a spline’s offset as an ordinary spline within config.spline_tol. The pens stay (a Rectangle after it follows the pen’s direction; dir= sets it). A source cannot be removed while the offset exists (sk(‘Line.2’).construction = True keeps it); the offset’s parts cannot be removed, trimmed, split, fixed or copied alone.

Parameters:  
- **source** – an id, a wire id or a list of ids forming one chain of lines, arcs and splines; or one circle or closed spline.

- **d** – the distance. A closed chain gets one loop, outside for d \> 0, inside for d \< 0; an open chain both sides with round caps (‘\<id\>.startcap’, ‘\<id\>.endcap’). (d, point): the side the point ((h, v) or a point id) lies on, d \> 0.

- **both_sides** – True both sides (a closed chain: both loops); False one side (an open chain: the left for d \> 0, the right for d \< 0, no caps); default one side for a closed chain, both for an open one.

- **options** – id= the offset’s id, default ‘Offset.N’. One side: ‘\<id\>.Line.3’ is the offset of Line.3, ‘\<id\>.Line.3.corner’ the round corner around the end of Line.3. Both sides: the side word first, ‘\<id\>.left.Line.3’, ‘\<id\>.right.Line.3.corner’, and ‘\<id\>.left’, ‘\<id\>.right’ the sides (a loop counts counterclockwise: left is its inside). ‘\<id\>’ is all of it; ‘\<id\>.d’ its distance, a dimension. name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=: the elements’ properties, by default those of the source.

Raises:  
- **ValueError** – d = 0; a source that is not one chain; d at or beyond the radius of a spline’s tightest bend towards the side (a cusp). Later, a change of d or of the source that needs another shape (an arc’s offset vanishing or coming back, a corner appearing, a cusp, sides crossing) raises and changes nothing: Remove(‘\<id\>’), then Offset again.

- **TypeError** – d is neither a distance nor (d, point), or an unknown keyword.

Examples

``` python
sk.Offset('plate', -2, id='wall')
sk.Offset('s', (1.5, (10, 40)), id='bead')
```

Plot(*ax: Any = None*, *show_constraints: bool \| None = None*, *show_ends: bool = False*, *show_direction: bool = False*, *show_parent: Any = None*, *backend: str \| None = None*) → Any  
Draws the sketch with matplotlib or plotly, every element in the colour of its state.

Green is fully constrained, black free, pink over-constrained (a constraint that contradicts the others and the elements it contradicts; all free while the constraints are disabled); construction elements are dashed, projected elements gold; in plotly an element shows its id when hovered. The signs, pieces and arrows keep their size on screen when zooming.

Parameters:  
- **ax** – a matplotlib Axes or a plotly Figure to draw into, which also selects the backend.

- **show_constraints** – True adds a sign per constraint (in plotly what it is when hovered; not while the constraints are disabled); a QuickSketch has none and warns.

- **show_ends** – True adds a blue start and an orange end piece on every line and arc.

- **show_direction** – True adds an arrow from start to end on every line and arc.

- **show_parent** – an OCC solid (e.g. the one whose face the sketch lies on) drawn projected into the sketch’s plane under the sketch, its faces lightly filled, its edges black (in plotly with their names when hovered); only in the plot.

- **backend** – “matplotlib” or “plotly” (hover) without ax; None is matplotlib.

Returns:  
the matplotlib Axes or the plotly Figure.

Raises:  
**ValueError** – backend is unknown.

Examples

``` python
sk.Plot(show_constraints=True)
sk.Plot(show_parent=box, backend="plotly")
```

Plot3d(*plane_size: float \| None = None*, *backend: str = 'webgpu'*, *\*\*kwargs: Any*) → Any  
Draws planex objects, planes and OCC shapes in one 3d view.

Everything is exported to OCC, put into one compound and drawn; a sketch contributes its drawn edges. A plane is drawn as a square of side plane_size with a line along its u axis. backend “webgpu” draws with ngsolve_webgpu.jupyter.Draw: it needs ngsolve_webgpu (pip install ngsolve_webgpu, which brings the full ngsolve) and a browser with WebGPU, with the kernel on the same machine as the browser or its websocket port forwarded. backend “webgui” draws with netgen.webgui.Draw and needs only netgen.

Parameters:  
- **objects** – a Wire, Face, Shape, Compound, sketch, Plane or OCC shape, or a list of them.

- **plane_size** – the side of a plane’s square; None takes the largest extent of everything else, 1 without.

- **backend** – ‘webgpu’ or ‘webgui’.

- **kwargs** – go to the backend’s Draw (width, height; settings, euler_angles only for webgui).

Returns:  
the backend’s scene.

Raises:  
- **ValueError** – backend is unknown, or there is nothing to draw.

- **ImportError** – backend ‘webgpu’ without ngsolve_webgpu installed.

Examples

``` python
px.Plot3d([face, px.XY], plane_size=2)
shape.Plot3d(backend="webgui", height=400)
```

Polygon(*r: float*, *n: int*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Polygon(*h: float*, *v: float*, *r: float*, *n: int*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Polygon(*n: int*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *center: Point \| None = None*, *concentric: Id \| None = None*, *coincident: Id \| None = None*, *through: Point \| None = None*, *tangent: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| tuple\[Id, Id\] \| None = None*, *reverse: bool = False*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Draw a regular polygon of n equal sides, its corners on a circle.

Every form of Circle, with the number of sides n as the last number. The circle is a construction element, Polygon.k.circle, of radius r around the pen’s position, around (h, v) or placed by the helpers; the first corner is on it along the pen’s drawing direction from its centre, the others follow counterclockwise. Polygon.k.Line.1 to .Line.n run from the first corner, and Polygon.k is their wire (sk(‘Polygon.1’), Offset, Remove, the patterns). The pen and its direction stay. A Sketch keeps the polygon regular (the corners on the circle, equal sides) and has the dimension Polygon.k.r (sk\[“Polygon.1.r”\] = 3; sk.Update()); with auto_constraint the circle gets what Circle gets. Remove(“Polygon.1”) removes the whole polygon.

Parameters:  
- **n** – the number of sides, an int of at least 3.

- **r** – the radius of the circle the corners are on.

- **h** – the centre’s h.

- **v** – the centre’s v.

- **flags** – as for Circle, which draws the circle.

- **center** – as for Circle.

- **concentric** – as for Circle.

- **coincident** – as for Circle.

- **through** – as for Circle.

- **tangent** – as for Circle.

- **reverse** – as for Circle.

- **options** – id= replaces Polygon.k (id=’nut’ gives nut.circle, nut.Line.1, …), and the sides’ properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **TypeError** – n is no int or not the last number, another form of arguments, an unknown keyword.

- **ValueError** – n is below 3; the circle cannot be placed; id is taken.

Examples

``` python
sk.Polygon(5, 6)
sk.Polygon(0, 0, 5, 8, id="gear")
```

Polylines(*tol: float*) → list\[np.ndarray\]  
The loops and open chains of the sketch’s Shape() as (n, 2) arrays of plane coordinates, as Shape.Polylines.

Parameters:  
**tol** – the largest distance of a chord from its edge, a length \> 0.

Raises:  
**ValueError** – tol is not positive and finite, or below the rounding of an arc’s or a spline’s coordinates.

Examples

``` python
sk.Polylines(0.01)
```

Project(*shape: occ.TopoDS_Shape*, */*, *construction: bool = True*, *name: str \| None = None*, *id: str \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Add the edges and points of an OCC shape projected straight onto the sketch’s plane, fixed where they are.

A curve becomes a spline through samples of it, within the spline tolerance. Its elements are Projection.N.Edge.X, X the edge’s index in shape.edges (the number the webgui shows), and Projection.N.Vertex.X; pieces of one edge Edge.X.1, Edge.X.2.

Parameters:  
- **shape** – the OCC shape.

- **construction** – references; False adds drawn edges.

- **name** – the name of the drawn edges.

- **id** – replaces Projection.N.

Examples

``` python
sk.Project(box)
sk.Project(box, construction=False, name="outline")
```

Rectangle(*l: float*, *w: float*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Rectangle(*p0: Point*, *p1: Point*, */*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Rectangle(*p0: Point*, *p1: Point*, *p2: Point*, */*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Draw a rectangle from the pen, l along the drawing direction and w to its left, or through its corners.

Its four lines are Rectangle.k.Line.1 to .Line.4 from the start corner along the drawing direction (counterclockwise), and Rectangle.k is their wire (sk(‘Rectangle.1’), Offset(‘Rectangle.1’, 2), Remove, the patterns). It is not part of a chain; its three right angles are Rectangle.k.Perpendicular.1 to .3; the pen stays at the start corner.

Through corners: two opposite corners p0, p1 (in any order) give a rectangle with its sides along h and v, starting at the corner of the smallest h and v (in a px.Sketch its first side is also Horizontal). Three points: p0 to p1 is a side and the opposite side runs through p2, which sets the width; it starts at p0 and runs counterclockwise (with p2 to the right of p0 -\> p1, Line.1 leaves p0 along its other side and Line.4 runs p1 -\> p0). A corner is a point (h, v) or a selected point such as sk(‘A’), which it is tied to (p2: the opposite side passes through it). The pen stays where it was.

Parameters:  
- **l** – the length along the drawing direction.

- **w** – the width to the left of it.

- **p0** – a corner.

- **p1** – the opposite corner (two points), or the corner after p0 (three points).

- **p2** – a point on the side opposite p0 -\> p1.

- **options** – id= replaces Rectangle.k (id=’plate’ gives plate.Line.1 to plate.Line.4 and the wire ‘plate’); the lines’ properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **ValueError** – id is taken or no valid id, a side within rounding of 0, a selection of more than one point.

- **TypeError** – id_wire= (the id names the wire), an unknown keyword, a corner that is no point or an id string, or four points.

Examples

``` python
sk.Rectangle(40, 20, id='plate', maxh=2)
sk.Rectangle((0, 0), (40, 20))
```

RectangleC(*l: float*, *w: float*, */*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
RectangleC(*center: Point*, *l: float*, *w: float*, */*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Draw a rectangle centred at the pen or at center, l along the drawing direction and w across it.

Its four lines are RectangleC.k.Line.1 to .Line.4 from the corner behind and to the right of the centre, along the drawing direction (counterclockwise), and RectangleC.k is their wire. The construction line RectangleC.k.diagonal runs from the first corner to the third, the construction point RectangleC.k.center sits at its middle; a selected point as center ties the centre to it. It is not part of a chain; the pen goes to the centre and keeps its direction.

Parameters:  
- **center** – the centre: a point (h, v) or a selected point such as sk(‘A’) (tied to it); default the pen.

- **l** – the length along the drawing direction.

- **w** – the width across it.

- **options** – id= replaces RectangleC.k (id=’plate’ gives plate.Line.1 to plate.Line.4, plate.center and the wire ‘plate’); the lines’ properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **ValueError** – a side within rounding of 0, a selection of more than one point, id is taken or no valid id.

- **TypeError** – l or w is no number, center is an id string or no point, id_wire= or an unknown keyword.

Examples

``` python
sk.RectangleC(40, 20)
sk.RectangleC((10, 5), 40, 20, id='plate')
```

Remove(*id: Id*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Remove a constraint, an element with its constraints (a line’s ends too, unless used), or the elements of a wire id; an expression that uses a dimension removed goes too, with a warning.

Parameters:  
**id** – the constraint, element or wire.

Examples

``` python
sk.Remove("Line.2")
sk.Remove("Chain.1")
```

RemoveMeasure(*id: str*) → None  
Drops the stored measurement id.

Parameters:  
**id** – the id the measurement was stored under (Measure(f, id=…)).

Raises:  
**ValueError** – no measurement is stored under id.

Examples

``` python
shape.RemoveMeasure("gap")
```

Rotate(*center: Point*, *angle: float*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Rotate(*point: Point3*, */*, *dir: tuple\[float, float, float\]*, *angle: float*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Rotates it in place by angle around center in its plane, or around the axis through point along dir in 3d.

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **center** – the centre of the rotation in the plane, (h, v).

- **angle** – the angle in degrees.

- **point** – a point of the axis in 3d, (x, y, z).

- **dir** – the direction of the axis in 3d, (dx, dy, dz).

Returns:  
this QuickSketch.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
sk.Rotate((0, 0), 90)
sk.Rotate((0, 0, 0), (0, 0, 1), 45)
```

Rotated(*center: Point*, *angle: float*, *\**, *id: str \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Rotated(*point: Point3*, */*, *dir: tuple\[float, float, float\]*, *angle: float*, *\**, *id: str \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
A copy rotated by angle around center in its plane, or around the axis through point along dir in 3d; this QuickSketch is not changed.

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **center** – the centre of the rotation in the plane, (h, v).

- **angle** – the angle in degrees.

- **point** – a point of the axis in 3d, (x, y, z).

- **dir** – the direction of the axis in 3d, (dx, dy, dz).

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
sk.Rotated((0, 0), 90)
sk.Rotated((0, 0, 0), (0, 0, 1), 45)
```

Scale(*center: Point*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Scale(*point: Point3*, */*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Scales it in place by factor around center in its plane, or around point in 3d.

The mesh sizes maxh and minh stay unless asked for.

Parameters:  
- **center** – the centre of the scaling in the plane, (h, v).

- **point** – the centre of the scaling in 3d, (x, y, z).

- **factor** – the scale factor, \> 0.

- **scale_maxh** – True also scales maxh.

- **scale_minh** – True also scales minh.

Returns:  
this QuickSketch.

Raises:  
**ValueError** – factor is not positive and finite, or a coordinate is not finite.

Examples

``` python
sk.Scale((0, 0), 2)
sk.Scale((0, 0), 0.5, scale_maxh=True)
```

Scaled(*center: Point*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*, *\**, *id: str \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Scaled(*point: Point3*, */*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*, *\**, *id: str \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
A copy scaled by factor around center in its plane, or around point in 3d; this QuickSketch is not changed.

The mesh sizes maxh and minh stay unless asked for.

Parameters:  
- **center** – the centre of the scaling in the plane, (h, v).

- **point** – the centre of the scaling in 3d, (x, y, z).

- **factor** – the scale factor, \> 0.

- **scale_maxh** – True also scales maxh.

- **scale_minh** – True also scales minh.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – factor is not positive and finite, or a coordinate is not finite.

Examples

``` python
sk.Scaled((0, 0), 2)
sk.Scaled((0, 0), 0.5, scale_maxh=True)
```

SetOrigin(*point: Point*, */*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Move the axes (H, V, the sketch’s origin) to a point; the drawing stays where it is, its coordinates shift, and constraints to the axes follow them.

Parameters:  
**point** – the new origin, (h, v) or a point id.

Examples

``` python
sk.SetOrigin((10, 5))
sk.SetOrigin("Circle.1.center")
```

Shape(*\**, *id: str \| None = None*) → [Shape](geometry.md#planex.Shape "planex.Shape")  
The regions the drawn elements bound, as Face() but all of them.

Parameters:  
**id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – in mode=’strict’ while the sketch can still move.

Examples

``` python
shape = sk.Shape()
```

Slot(*l: float*, *w: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Slot(*dh: float*, *dv: float*, *w: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Slot(*to: Point \| Id*, *w: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Slot(*start: Point \| Id*, *end: Point \| Id*, *w: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Slot(*\*ends: Point \| Id \| float \| [Constraint](#planex.Constraint "planex.Constraint")*, *until: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| SketchUntil \| None = None*, *parallel: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| None = None*, *perpendicular: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| None = None*, *angle: float \| tuple\[float, Id\] \| None = None*, *coincident: Id \| None = None*, *tangent: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| tuple\[Id, Id\] \| None = None*, *distance: float \| tuple\[float, Point\] \| None = None*, *through: Point \| None = None*, *reverse: bool = False*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Draw a straight slot: a line and its offset on both sides, closed by round caps.

Every form of Line, with the width w as the last number before the flags. The line is the slot’s spine, a construction element; the outline is its offset at w / 2 on both sides, the caps half circles around the spine’s ends. The pen goes on at the spine’s end in its direction (a new chain). Slot.k is the wire of the outline, with Slot.k.spine, Slot.k.left and Slot.k.right (left and right of the spine’s direction), Slot.k.startcap and Slot.k.endcap. A Sketch keeps the slot a slot when the spine moves, and has the dimension Slot.k.w (sk\[“Slot.1.w”\] = 6; sk.Update()); with auto_constraint the spine gets what Line gets. Remove(“Slot.1”) removes the whole slot.

Parameters:  
- **w** – the width of the slot, across the spine; positive.

- **l** – as for Line, which draws the spine.

- **dh** – as for Line, which draws the spine.

- **dv** – as for Line, which draws the spine.

- **to** – as for Line, which draws the spine.

- **start** – as for Line, which draws the spine.

- **end** – as for Line, which draws the spine.

- **ends** – as for Line, which draws the spine.

- **flags** – as for Line, which draws the spine.

- **until** – as for Line, which draws the spine.

- **parallel** – as for Line, which draws the spine.

- **perpendicular** – as for Line, which draws the spine.

- **angle** – as for Line, which draws the spine.

- **coincident** – as for Line, which draws the spine.

- **tangent** – as for Line, which draws the spine.

- **distance** – as for Line, which draws the spine.

- **through** – as for Line, which draws the spine.

- **reverse** – as for Line, which draws the spine.

- **options** – id= replaces Slot.k (id=’groove’ gives groove.spine, groove.left, …), and the outline’s properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **TypeError** – no width as the last number, another form of arguments, an unknown keyword.

- **ValueError** – w is not positive; the spine has no length or cannot be placed; id is taken.

Examples

``` python
sk.Slot(10, 4)
sk.Slot((0, 0), (0, 10), 2, id="groove")
```

Spline(*points: list\[tuple\[float, float\]\]*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *periodic: bool = False*, *tangents: dict\[int, tuple\[float, float\]\] \| None = None*, *start_from_localpos: bool = True*, *style: str \| None = None*, *tol: float \| None = None*, *\*\*options*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Draw a spline from the pen’s position through points; its points are point elements.

Parameters:  
- **points** – the points (h, v) it passes, after the start.

- **flags** – px.Fix; px.Tangent (with the element before it).

- **periodic** – closed back to its start.

- **tangents** – {index: (dh, dv)}, directions at some of the points, index 0 the start; they become angle unknowns.

- **start_from_localpos** – False starts at the first point instead of the pen’s position.

- **style** – ‘px’ (planex’s: closed without tangents the C2 periodic cubic) or ‘occ’ (OCC’s Geom2dAPI_Interpolate, netgen’s splines; a sketch keeps tangent directions only, so two points with one tangent of another length than 1 differ from netgen’s); default config.spline_style.

- **tol** – ignored.

- **options** – id= (the spline’s id), and its properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
**TypeError** – not one list of points, a point that is no (h, v), or an unknown keyword.

Examples

``` python
sk.Spline([(5, 2), (10, 0)])
sk.Spline([(5, 2), (10, 0), (5, -2)], periodic=True, tangents={0: (0, 1)})
```

Split(*a: Id*, *b: Ids \| None = None*, */*, *\**, *auto_constraint: bool = True*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Cut a into pieces where it meets b.

The pieces are ‘Line.3.1’, ‘Line.3.2’, … along it (a circle’s from the piece at 0 degrees, counterclockwise), and ‘Line.3’ is the wire of the pieces, which what referred to Line.3 (its ends, its length, a tangency) resolves through. The cut points stay on b.

Parameters:  
- **a** – the element.

- **b** – what cuts it: an id, a list or a selection; default every other line, arc and circle.

- **auto_constraint** – keep the pieces on one line or circle; False lets them move apart (an arc’s pieces get their own centres and radii).

Raises:  
- **ValueError** – a given as (a, point): every piece is numbered, none keeps a’s id.

- **TypeError** – a keyword other than auto_constraint.

Examples

``` python
sk.Split("Line.3", "Line.5")
sk.Split("Circle.1", ["Line.1", "Line.2"], auto_constraint=False)
```

SymmetryLine(*a: Point \| Id*, *b: Point \| Id*, */*, *\**, *reverse: bool = False*, *id: str \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Add the infinite construction line that mirrors a onto b.

Crossing lines: the bisector mapping a onto b best; parallel lines: the midline; points: their perpendicular bisector; circles and arcs: that of the centres, whatever radii drawn. The line is free to move and kept there by symmetry constraints: on the lines’ carriers (never equal lengths), the points, or the centres of the circles or arcs (with equal radii, never their ends).

Parameters:  
- **a** – a line, point, circle or arc.

- **b** – one of the same kind.

- **reverse** – of two crossing lines the other bisector.

- **id** – the line’s id, default SymmetryLine.N.

Raises:  
- **ValueError** – a and b are not two points, two lines or two circles, or two circles or arcs share their centre.

- **TypeError** – an unknown keyword.

Examples

``` python
sk.SymmetryLine("Line.1", "Line.3")
sk.SymmetryLine("Circle.1", "Circle.2", id="mid")
```

ToHV(*point: Point3*, */*) → tuple\[float, float\]\
ToHV(*points: list\[Point3\]*, */*) → list\[tuple\[float, float\]\]  
The plane coordinates (h, v) of the projection of a 3d point onto its plane, or of a list of points.

Parameters:  
- **point** – (x, y, z).

- **points** – a list of (x, y, z).

Examples

``` python
sk.ToHV((1, 2, 0))
sk.ToHV([(0, 0, 0), (1, 2, 0)])
```

ToOCCFace(*fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → occ.Face \| occ.Compound  
The region of the drawn elements as an OCC face, a compound of faces if it has several parts, made from a cleaned copy.

Parameters:  
- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges, False for a wire export. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
**ValueError** – in mode=’strict’ while the sketch can still move.

Examples

``` python
sk.ToOCCFace()
sk.ToOCCFace(fit_arcs=True)
```

ToOCCShape(*fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → occ.Face \| occ.Compound  
The same as ToOCCFace: the region as an OCC face, a compound of faces if it has several parts.

Parameters:  
- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges, False for a wire export. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
**ValueError** – in mode=’strict’ while the sketch can still move.

Examples

``` python
sk.ToOCCShape()
sk.ToOCCShape(remove_faces_smaller=0.01)
```

ToOCCWire(*approximation: bool = False*, *fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*, *unify: bool = True*) → occ.Wire  
The one wire of the drawn edges, Wire(unify=unify), as an OCC wire, made from a cleaned copy.

Parameters:  
- **approximation** – splines as their arcs.

- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges, False for a wire export. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

- **unify** – as for Wire().

Raises:  
**ValueError** – as Wire().

Examples

``` python
sk.ToOCCWire()
sk.ToOCCWire(unify=False, fit_arcs=True)
```

ToOCCWires(*approximation: bool = False*, *fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*, *unify: bool = True*) → occ.Compound  
The compound of the OCC wires of Wires(unify=unify), made from a cleaned copy.

Parameters:  
- **approximation** – splines as their arcs.

- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges, False for a wire export. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

- **unify** – as for Wires().

Raises:  
**ValueError** – as Wires().

Examples

``` python
sk.ToOCCWires()
sk.ToOCCWires(unify=False)
```

ToSVG(*size: float = 300*, *\*\*options: Any*) → str \| None  
The object as the text of an SVG picture in its plane coordinates, its faces filled.

A sketch is drawn in its own look: every element in the colour of its state (green fully constrained, black free, pink over-constrained) with its id when hovered, construction dashed, in a frame with numbered ticks on a grid.

Parameters:  
- **obj** – a Face, Shape, Wire, Compound, sketch or DeformableShape.

- **size** – the larger side of the picture in pixels.

- **options** – for a sketch: show_constraints=True adds a sign per constraint, show_ends=True a blue start and an orange end piece on every line and arc, show_direction=True an arrow from start to end; other objects ignore them.

Returns:  
the SVG text; None if the object has no extent.

Examples

``` python
face.ToSVG()
sk.ToSVG(size=500, show_constraints=True)
```

ToShapely(*tol: float = 0.001*) → Any  
Converts to a shapely geometry in plane coordinates, arcs and splines sampled within tol.

Faces that share an edge (a glued Shape) give a GeometryCollection of their polygons, since a MultiPolygon must not have parts sharing an edge.

Parameters:  
- **obj** – the Face, Shape, Wire, sketch (its Shape()) or Compound.

- **tol** – the largest distance of a chord from its arc or spline, a length. Where a face is thinner than tol its polygon is sampled again finer (to tol/1000), with a warning; still invalid, it is made valid (make_valid).

Returns:  
a Polygon or MultiPolygon for a Face, a Shape of faces or a sketch; a LineString or MultiLineString for a Wire or a Shape of wires; a GeometryCollection for a Compound or a Shape of both.

Examples

``` python
face.ToShapely()
px.ToShapely(shape, tol=1e-4)
```

ToXYZ(*point: Point*, */*) → tuple\[float, float, float\]\
ToXYZ(*points: list\[Point\]*, */*) → list\[tuple\[float, float, float\]\]  
The 3d coordinates of a point (h, v) of its plane, or of a list of points.

Parameters:  
- **point** – (h, v) in the plane.

- **points** – a list of (h, v).

Examples

``` python
sk.ToXYZ((1, 2))
sk.ToXYZ([(0, 0), (1, 2)])
```

Translate(*d: tuple\[float, float\]*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Translate(*dh: float*, *dv: float*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Translate(*d: tuple\[float, float, float\]*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Translate(*dx: float*, *dy: float*, *dz: float*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Moves it in place by d: in the plane by (dh, dv), in 3d by (dx, dy, dz).

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **d** – the displacement, (dh, dv) in the plane or (dx, dy, dz) in 3d.

- **dh** – the displacement along the plane’s first axis.

- **dv** – the displacement along the plane’s second axis.

- **dx** – the displacement along x in 3d.

- **dy** – the displacement along y in 3d.

- **dz** – the displacement along z in 3d.

Returns:  
this QuickSketch.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
sk.Translate((10, 0))
sk.Translate(0, 0, 5)
```

Translated(*d: tuple\[float, float\]*, *\**, *id: str \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Translated(*dh: float*, *dv: float*, *\**, *id: str \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Translated(*d: tuple\[float, float, float\]*, *\**, *id: str \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Translated(*dx: float*, *dy: float*, *dz: float*, *\**, *id: str \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
A copy moved by d: in the plane by (dh, dv), in 3d by (dx, dy, dz); this QuickSketch is not changed.

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **d** – the displacement, (dh, dv) in the plane or (dx, dy, dz) in 3d.

- **dh** – the displacement along the plane’s first axis.

- **dv** – the displacement along the plane’s second axis.

- **dx** – the displacement along x in 3d.

- **dy** – the displacement along y in 3d.

- **dz** – the displacement along z in 3d.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
sk.Translated((10, 0))
sk.Translated(0, 0, 5)
```

Trim(*a: Id \| tuple\[Id, Point\]*, *b: Ids \| None = None*, */*, *\**, *reverse: bool = False*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Cut a where it meets b and make one piece of it a construction element (‘Line.3.trimmed.1’).

The cut points stay on b. One piece left keeps a’s id; a cut out of the middle leaves ‘Line.3.1’, ‘Line.3.2’, and ‘Line.3’ is the wire of the pieces, which what referred to Line.3 resolves through. Without b and with nothing to cut at, all of a becomes construction.

Parameters:  
- **a** – the element; (id, point) picks the piece nearest the point ((h, v) or a point id), else the piece nearest the pen is trimmed.

- **b** – what cuts it: an id, a list or a selection; default every other line, arc and circle, construction ones included (RectangleC’s diagonal cuts a line through the rectangle’s center).

- **reverse** – trim the next nearest piece.

Raises:  
- **TypeError** – a keyword other than reverse (near=: the point goes with a).

- **ValueError** – a names a wire or group, not one element.

Examples

``` python
sk.Trim("Line.3", "Line.5")
sk.Trim(("Line.3", (4, 0)), "Line.5", reverse=True)
```

Turn(*angle: float*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Turn the pen’s direction.

Parameters:  
**angle** – the turn in degrees, positive counterclockwise.

Examples

``` python
sk.Line(5).Turn(90).Line(5)
```

Vertex(*id: str*, *point: tuple\[float, float\] \| None = None*) → [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
Add a construction point; it is constrained like any point.

Parameters:  
- **id** – the point’s id.

- **point** – where it is, (h, v) or sk.pen; default the pen’s position.

Raises:  
- **ValueError** – id is taken or no valid id.

- **TypeError** – point is no (h, v).

Examples

``` python
sk.Vertex("A", (3, 4))
sk.Line(5).Vertex("tip")
```

Wire(*element: Id \| None = None*, */*, *\**, *unify: bool = True*, *id: str \| None = None*) → [Wire](geometry.md#planex.Wire "planex.Wire")  
The drawn edges as one Wire, its ends within rounding joined; construction elements take no part. With element, that element’s solved edges (a line or arc one edge, a circle one circle edge, a spline its pieces) or a wire id’s elements in one chain.

Parameters:  
- **element** – an element, or a wire id (Chain.1, an id_wire); None the whole sketch.

- **unify** – for the whole sketch: True cuts the drawn edges at crossings and T-junctions, keeps an overlapping stretch once and merges the pieces on one carrier where nothing else meets (as wire + wire); False keeps every drawn element uncut.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – nothing is drawn, the edges form several wires (sk.Wires() returns them as a list), or, with unify=False or an element, the chain crosses, touches or overlaps itself; in mode=’strict’ while the sketch can still move.

Examples

``` python
sk.Wire()
sk.Wire("Chain.1")
```

Wires(*unify: bool = True*, *\**, *id: str \| None = None*) → list\[[Wire](geometry.md#planex.Wire "planex.Wire")\]  
The drawn edges as a list of Wire, each one chain in traversal order; ends within rounding are joined, a larger gap stays open; construction elements take no part.

Parameters:  
- **unify** – True (as px.Wires) cuts the drawn edges at crossings and T-junctions, keeps an overlapping stretch once and merges the pieces on one carrier where nothing else meets: an outline with a rib ending on it is one closed wire and the rib its own. False keeps every drawn element uncut (an overlap stays twice) and joins them at their ends.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – with unify=False a chain crosses, touches or overlaps itself (the message names the two elements); in mode=’strict’ while the sketch can still move.

Examples

``` python
sk.Wires()
sk.Wires(unify=False)
```

WriteBrep(*path: str \| os.PathLike*, */*, *\*\*options: Any*) → None  
Writes the OCC export (obj.ToOCCShape) as a BREP file.

Parameters:  
- **obj** – the Face, Shape, Wire, sketch or Compound to write.

- **path** – the file to write.

- **options** – the options of obj.ToOCCShape (fit_splines, fit_arcs, remove_edges_smaller, …).

Examples

``` python
face.WriteBrep("part.brep")
px.WriteBrep(shape, "part.brep", fit_arcs=True)
```

WriteDXF(*path: str \| os.PathLike*, */*, *unit: float = 1.0*) → None  
Writes the edges as LINE, ARC and SPLINE entities of a DXF file, each edge once.

An edge’s name becomes its layer (the characters a layer name cannot have, such as / and :, become \_, with a warning), its col the true colour. The coordinates are divided by unit and \$INSUNITS names it.

Parameters:  
- **obj** – the Face, Shape, Wire, sketch or Compound to write.

- **path** – the file to write.

- **unit** – the length the file counts in (px.m, px.mm, px.inch, …).

Raises:  
- **TypeError** – unit is a name, not a length.

- **ValueError** – unit is not a positive finite length.

Examples

``` python
face.WriteDXF("part.dxf", unit=px.mm)
px.WriteDXF(shape, "part.dxf")
```

WriteGeo(*path: str \| os.PathLike*, */*) → None  
Writes the edges and faces as a gmsh .geo file.

Points carry the maxh of their edges and faces as the characteristic length; lines, circle arcs (split at half a circle) and splines as B-splines through planex’s approximation (not exact: .geo has no NURBS), every curve once; plane surfaces with their holes. Physical groups: a curve group per edge name, a surface group per face name, the unnamed edges and faces in a curve and a surface group named “unnamed” (gmsh writes only elements in a group once there is one, and netgen’s ReadGmsh needs every group named and the boundary segments). Periodic identifications become Periodic Curve.

Parameters:  
- **obj** – the Face, Shape, Wire, sketch or Compound to write.

- **path** – the file to write.

Examples

``` python
face.WriteGeo("part.geo")
px.WriteGeo(shape, "part.geo")
```

WriteStep(*path: str \| os.PathLike*, */*, *\*\*options: Any*) → None  
Writes the OCC export (obj.ToOCCShape) as a STEP file.

Parameters:  
- **obj** – the Face, Shape, Wire, sketch or Compound to write.

- **path** – the file to write.

- **options** – the options of obj.ToOCCShape (fit_splines, fit_arcs, remove_edges_smaller, …).

Examples

``` python
face.WriteStep("part.step")
px.WriteStep(shape, "part.step", fit_arcs=True)
```

*property* bounding_box  
tuple\[tuple\[float, float\], tuple\[float, float\]\] \| None

Its bounding box in the plane, ((hmin, vmin), (hmax, vmax)); None if it is empty.

Type:  
bounding_box

*property* chain_start  
tuple\[float, float\]

Where the chain being drawn started, (h, v): Close() goes back there.

Type:  
chain_start

*property* elements  
Elements

The ids of the sketch by kind (Elements), in the order made.

Type:  
elements

*property* id  
str

The object’s id: the kind that made it and a number, given when first asked for (Sketch.1, Shape.3), or set (an ASCII letter, then letters, digits, \_ and single inner spaces); its elements’ ids follow it (Shape.3.Line.2).

Type:  
id

*property* locations  
dict\[str, tuple\[float, float\]\]

Every point reference with where it is, solved, {ref: (h, v)}.

Type:  
locations

*property* measures  
dict

The standard measurements and the stored ones, evaluated now, as a dict.

Type:  
measures

*property* origin  
str

The id of the sketch’s origin.

Type:  
origin

*property* pen  
SketchPen

The pen as it is now, a snapshot (sk.pen.position, sk.pen.direction); a drawing call takes it where a point (its position) or a direction (its direction) is asked for.

Type:  
pen

*property* points  
dict\[str, tuple\[float, float\]\]

The points with ids given to them, {id: (h, v)}.

Type:  
points

*property* wires  
dict\[str, list\[str\]\]

The wires, {wire id: \[element ids\]}: every pen chain (Chain.1, Chain.2, …, or its id_wire), every Rectangle and every Offset.

Type:  
wires

&nbsp;

*class* Sketch  
A sketch of elements and constraints between them, drawn with a pen. Ids (id=, id_wire=) start with a letter and contain none of + - \* / ^ ( ) = , %, the operators of expressions.

Arc(*r: float*, *sweep: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Arc(*to: Point \| Id*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Arc(*to: Point \| Id*, *r: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Arc(*start: Point \| Id*, *end: Point \| Id*, *r: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Arc(*\*ends: Point \| Id \| float \| [Constraint](#planex.Constraint "planex.Constraint")*, *sweep: float \| None = None*, *center: Point \| None = None*, *concentric: Id \| None = None*, *coincident: Id \| None = None*, *tangent: tuple\[float, float\] \| Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| tuple\[Id, Id\] \| None = None*, *through: Point \| None = None*, *until: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| SketchUntil \| None = None*, *reverse: bool = False*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")  
Draw an arc from the pen or between two ends.

The positional arguments are read in this order: start, end, then the sizes (r, sweep), then the flags. The circle of the arc: center= or concentric= with radius r (default through the start), coincident= (an arc’s circle), else continuing the pen’s direction with radius r (r \> 0 turns left). On a given circle the start is the given start (an element: where it meets the circle nearest the pen, or (element, point)), else the pen projected onto it; the arc runs counterclockwise between two given ends, along the pen’s direction from the pen, with sweep= by its sign. Its end: a positional to, sweep= or until=.

Parameters:  
- **r** – the radius; from the pen a negative r starts backwards, its centre on the other side; with two ends the arc runs counterclockwise the short way, -r clockwise.

- **sweep** – the turn in degrees from the pen, positive counterclockwise; a full turn (also sweep=360 with center=) draws one circle element (Circle.k, or id=) through the pen, its origin the pen’s point, and the pen stays; in a px.Sketch it touches the element before it; as sweep= the turn on a circle given by center=, concentric= or coincident=.

- **to** – the end, as for Line: a point (h, v), a point id, sk.pen, an element (until=px.first, the default, or px.last picks the intersection along the arc) or (element, point). Arc(to) alone leaves the pen along its direction. An element as an end needs the circle.

- **start** – the start of the two-end form, read as to.

- **end** – the end of the two-end form, read as to.

- **ends** – start and end, or a single start with sweep= or until=, then the sizes, then the flags.

- **flags** – as for Line.

- **center** – the centre, a point or a point id.

- **concentric** – a point id or an arc whose centre the arc shares.

- **coincident** – an arc whose circle the arc lies on.

- **tangent** – (dh, dv), the direction at the start; sk.pen, the pen’s direction; a target: the arc leaves the pen along its direction and ends touching it; (a, b) with r: the arc of radius r touching a and b, from a to b, by default in the corner they face. A target given as (target, point): of a line the side the point is on, of a circle a point inside puts the arc inside.

- **through** – a third point the arc passes.

- **until** – px.first / px.last until the first / last line, arc, circle or spline along the arc (the start not counted), or a target. With sweep= or until= a single positional is the from: sk.Arc(“tangent”, sweep=90, coincident=”c”).

- **reverse** – the other way round; with two ends and r the long way.

- **options** – id= (the arc’s id), and its properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **TypeError** – a wrong form of arguments, an unknown keyword, a keyword for an end (the ends are positional), r= or ccw=, near=.

- **ValueError** – the arc cannot be placed (the message names the form it needs); nothing is drawn.

Examples

``` python
sk.Arc(5, 90).Arc((3, 1), tangent=(0, 1)).Arc((0, 0), (2, 0), 5)
sk.Arc("lower", "upper", coincident="boss")
```

ArcSlot(*r: float*, *sweep: float*, *w: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
ArcSlot(*to: Point \| Id*, *w: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
ArcSlot(*to: Point \| Id*, *r: float*, *w: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
ArcSlot(*start: Point \| Id*, *end: Point \| Id*, *r: float*, *w: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
ArcSlot(*\*ends: Point \| Id \| float \| [Constraint](#planex.Constraint "planex.Constraint")*, *sweep: float \| None = None*, *center: Point \| None = None*, *concentric: Id \| None = None*, *coincident: Id \| None = None*, *tangent: tuple\[float, float\] \| Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| tuple\[Id, Id\] \| None = None*, *through: Point \| None = None*, *until: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| SketchUntil \| None = None*, *reverse: bool = False*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")  
Draw an arc slot: an arc and its offset on both sides, closed by round caps.

Every form of Arc, with the width w as the last number before the flags. The arc is the slot’s spine, a construction element; the outline is its offset at w / 2 on both sides, the caps half circles around the spine’s ends. The pen goes on at the spine’s end in its direction (a new chain). The ids are those of Slot: ArcSlot.k is the wire, with ArcSlot.k.spine, .left, .right, .startcap and .endcap; a Sketch has the dimension ArcSlot.k.w and keeps the slot a slot when the spine moves. Remove(“ArcSlot.1”) removes the whole slot.

Parameters:  
- **w** – the width of the slot, across the spine; positive and below the diameter 2 r of the spine arc.

- **r** – as for Arc, which draws the spine.

- **sweep** – as for Arc, which draws the spine.

- **to** – as for Arc, which draws the spine.

- **start** – as for Arc, which draws the spine.

- **end** – as for Arc, which draws the spine.

- **ends** – as for Arc, which draws the spine.

- **flags** – as for Arc, which draws the spine.

- **center** – as for Arc, which draws the spine.

- **concentric** – as for Arc, which draws the spine.

- **coincident** – as for Arc, which draws the spine.

- **tangent** – as for Arc, which draws the spine.

- **through** – as for Arc, which draws the spine.

- **until** – as for Arc, which draws the spine.

- **reverse** – as for Arc, which draws the spine.

- **options** – id= replaces ArcSlot.k, and the outline’s properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **TypeError** – no width as the last number, another form of arguments, an unknown keyword.

- **ValueError** – w is not positive or not below 2 r; the spine has no length or cannot be placed; id is taken.

Examples

``` python
sk.ArcSlot(10, 90, 2)
sk.ArcSlot((10, 0), (0, 10), 10, 2, id="track")
```

Chamfer(*at: Id \| Point \| list\[Id \| Point\]*, *d: float*, */*, *d2: float \| None = None*, *angle: float \| None = None*, *reverse: bool = False*, *\**, *id: str \| list\[str\] \| None = None*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")  
Cut corners with a straight line, in place.

A corner is a point that is the end of exactly two drawn elements (construction elements do not count) whose directions differ. The new line is the element ‘Chamfer.k’ (k counts per sketch, in the order the corners are given) with the properties of the two elements merged, the name of the first winning (the first element is the one drawn first), and the properties given in options set over them. The two elements keep their ids, cut back to the cut points. What is cut off stays as a construction element named as Trim names it (‘Line.1.trimmed.1’, ‘Line.2.trimmed.1’; the sharp corner is the end the two share, ‘Line.1.trimmed.1.end’). An element the cut reaches the far end of is used up: it becomes construction as a whole, and the new element ends at its far end. Lines and arcs are cut exactly; a spline is cut where the line meets its exact curve. Remove(‘Chamfer.1’) takes the line away; the elements stay cut.

A corner that cannot be cut (a tangent joint, a point on no vertex, more or fewer than two ends, a size that does not fit) is skipped with a PlanexWarning that names why and the largest size that fits; if none can be cut the call raises and changes nothing.

The line is held by its ends on the two elements and the dimensions ‘Chamfer.k.d’ (from the sharp corner along the first element, as the straight distance) and ‘Chamfer.k.d2’ (the second), or ‘Chamfer.k.angle’ if angle was given (the first element must be a line), and ‘Chamfer.k.reverse’ (0 or 1: which element takes d); the dimensions are changed like every sketch value: sk\[‘Chamfer.1.d’\] = 2 or sk\[‘Chamfer.1.reverse’\] = True, then Update(). A change of reverse moves d and d2 (angle) to the other elements.

Parameters:  
- **at** – the corners: a point id (‘Line.1.end’, ‘A’), a sketch point or a selection of points, a point (h, v) on a corner, or a list of these; there is no default.

- **d** – the length on the first element, a length \> 0; also on the second if d2 and angle are None.

- **d2** – the length on the second element, a length \> 0.

- **angle** – with d, the angle in degrees between the first element and the new line, between 0 and 180. d2 and angle exclude each other.

- **reverse** – True makes the second element the first.

- **id** – the id of the new element for one corner (a str), or one id per corner (a list); default ‘Chamfer.k’.

- **options** – the new element’s properties name=, maxh=, minh=, hpref=, layer=, col=, each one value for all corners or a list of one per corner (col=\[(1, 0, 0), (0, 0, 1)\]); they replace the merged ones.

Returns:  
this sketch, so calls chain.

Raises:  
- **TypeError** – at is none of the forms above, id is neither a str nor a list of str, or an option is no property.

- **ValueError** – a size is not positive and finite, the angle is not between 0 and 180 or d2 and angle are both given, no corner is given, an id is no point, or no corner can be cut (the message names each corner and why); an id is taken, or the number of ids or of a property’s values differs from the number of corners.

Examples

``` python
sk.Chamfer("Line.2.end", 1)
sk.Chamfer([(4, 0), (4, 4)], 1, d2=2, id=["a", "b"])
```

Circle(*r: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Circle(*h: float*, *v: float*, *r: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Circle(*\*r: float \| [Constraint](#planex.Constraint "planex.Constraint")*, *center: Point \| None = None*, *concentric: Id \| None = None*, *coincident: Id \| None = None*, *through: Point \| None = None*, *tangent: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| tuple\[Id, Id\] \| None = None*, *reverse: bool = False*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")  
Draw a circle around the pen’s position, around (h, v), or placed by the helpers.

With helpers the centre is center= or concentric=, else the pen’s position; the radius is r, coincident=, through= or tangent=. The circle is one circle edge starting at the point r from the centre along +h.

Parameters:  
- **r** – the radius; in the helper form at most one radius, then the flags.

- **h** – the centre’s h.

- **v** – the centre’s v.

- **flags** – px.Fix.

- **center** – the centre, a point or a point id.

- **concentric** – a point id or an arc whose centre is the circle’s.

- **coincident** – an arc or circle whose radius the circle takes.

- **through** – a point or point id on the circle.

- **tangent** – a target x the circle touches: of a line where the foot from the centre lies, of an arc where the line of the centres meets it; on the edge first, then nearest the target’s point, else the smaller radius. (a, b) with r: the circle touching both, as Arc(r, tangent=(a, b)). (target, point): the solution near the point.

- **reverse** – take the next solution.

- **options** – id= (the circle’s id), and its properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **TypeError** – a wrong form of arguments, an unknown keyword, near=.

- **ValueError** – the circle cannot be placed.

Examples

``` python
sk.Circle(5)
sk.Circle(2, center="Line.1.end", id="hole")
```

CircularPattern(*ids: Ids*, *center: Id*, *count: int*, *angle: float \| None = None*, *auto_constraint: bool \| None = None*, *internal_constraints: bool \| None = None*, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")  
Add count instances of the elements turned about a point, the original the first.

The pattern is ‘CircularPattern.N’ (or id), the copies named after their sources by their step (‘CircularPattern.1.3.bolt’).

Parameters:  
- **ids** – the elements: one id, a list or a selection.

- **center** – the point id they turn about.

- **count** – the number of instances, the original included.

- **angle** – the step in degrees; default 360 / count, and the pattern stays spread over the full turn when count changes.

- **auto_constraint** – tie the copies to the originals through the pattern, its step the dimension ‘CircularPattern.N.angle’ and its number of instances ‘CircularPattern.N.count’ (set like a dimension: copies are added or the last ones removed), each copy listed as one constraint ‘CircularPattern.N.k’; default whether the constraints are enabled.

- **internal_constraints** – show the constraints among the originals on the copies (‘CircularPattern.1.3.\<id\>’, derived), or with auto_constraint=False give the copies those constraints; default as auto_constraint.

- **id** – replaces CircularPattern.N.

Examples

``` python
sk.CircularPattern("bolt", "Circle.1.center", 6)
sk.CircularPattern(["Line.1", "Line.2"], px.origin, 4, angle=30, id="ribs")
```

Close(*\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *id_wire: str \| None = None*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")  
Draw a line back to where the chain started; the chain ends.

Parameters:  
- **flags** – as for Line.

- **id_wire** – replaces the chain’s wire id Chain.k (for the patterns, RigidGroup, Remove, Edge, Measure).

- **options** – id= (the closing line’s id), and its properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
**TypeError** – a positional argument other than a flag, or an unknown keyword.

Examples

``` python
sk.Line(10).Turn(90).Line(5).Close()
sk.Close(id_wire="outline")
```

Constrain(*\*args: Id \| [Constraint](#planex.Constraint "planex.Constraint") \| float*, *id: str \| None = None*, *reference: bool = False*) → [Sketch](#planex.Sketch "planex.Sketch")  
Add a constraint or dimension and solve at once.

Parameters:  
- **args** – the ids of the elements or their points (‘Line.1.end’, .start, .center, .mid), px.H, px.V, then the kind (px.Distance, …), then its value for a dimension. px.Angle of one arc is its sweep in degrees, negative for a clockwise arc.

- **id** – the constraint’s id.

- **reference** – a dimension without a value that measures and drives nothing (sk\[id\] gives what it measures).

Raises:  
- **TypeError** – no kind, two kinds, or arguments after the kind other than one value.

- **ValueError** – a wrong value, or an expression out of its domain. A constraint that contradicts the others is kept but not solved (PlanexWarning naming the constraints it contradicts): status is ‘over’, its elements are pink, and it is solved again once the other dimensions allow it.

Examples

``` python
sk.Constrain("Line.1", px.Horizontal)
sk.Constrain("Line.1", px.Distance, 10, id="w")
```

Construction(*\**, *id: str \| None = None*) → [Wire](geometry.md#planex.Wire "planex.Wire")  
The construction edges as a wire.

Parameters:  
**id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – in mode=’strict’ while the sketch can still move.

Examples

``` python
helpers = sk.Construction()
```

Copy(*\**, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")  
An independent copy; copy.copy and copy.deepcopy give the same.

Parameters:  
**id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Examples

``` python
b = a.Copy()
```

Direction(*d: tuple\[float, float\]*, */*) → [Sketch](#planex.Sketch "planex.Sketch")\
Direction(*dh: float*, *dv: float*, */*) → [Sketch](#planex.Sketch "planex.Sketch")  
Set the pen’s direction.

Parameters:  
- **d** – the direction (dh, dv).

- **dh** – the direction’s h; with dv.

- **dv** – the direction’s v.

Raises:  
**TypeError** – another form of arguments.

Examples

``` python
sk.Direction((0, 1)).Line(5)
sk.Direction(1, 1)
```

Expression(*text: str*, *rhs: str \| None = None*, *\**, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")  
Add an equation between dimensions and measures of the solved geometry, solved with all the other constraints.

Its terms: the ids of driving, driven or reference dimensions, and measures (Line.1.length; Line.1.angle, its direction from h; Arc.1.r, Circle.1.r; Arc.1.sweep; .h and .v of any point, e.g. Line.1.end.v; each a reference dimension Reference.N). A dimension alone on the left side is driven by it: it follows, and setting it raises (a reference stays a reference); any other equation is one more constraint, reported like one when it is redundant or contradicts. + - \* / ^ (or [\*\*](#id1)), brackets, sqrt, abs, min, max, sin, cos, tan, asin, acos, atan2 (angles in degrees), pi and the units mm, cm, m, um, inch, mil (as px.mm, …). Removing a dimension it uses removes it, with a warning.

Parameters:  
- **text** – the equation (“h = w/2”), or its left side when rhs is given.

- **rhs** – the right side.

- **id** – the expression’s id, default Expression.N.

Raises:  
**ValueError** – an expression out of its domain (the sqrt of a negative, …); an invalid id.

Examples

``` python
sk.Expression("h = w/2")
sk.Expression("h", "w/2 + 3", id="half")
```

Face(*\**, *id: str \| None = None*) → [Face](geometry.md#planex.Face "planex.Face")  
The region the drawn elements bound.

Construction elements are left out, crossings cut, stretches drawn twice by closed wires cancel, an open wire’s stretch on another edge is left out, holes by nesting.

Parameters:  
**id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – the region has several parts (Shape() keeps them all); in mode=’strict’ while the sketch can still move.

Examples

``` python
face = sk.Face()
```

Fillet(*at: Id \| Point \| list\[Id \| Point\]*, *r: float*, */*, *\**, *id: str \| list\[str\] \| None = None*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")  
Round corners with an arc tangent to both elements, in place.

A corner is a point that is the end of exactly two drawn elements (construction elements do not count) whose directions differ. The new arc is the element ‘Fillet.k’ (k counts per sketch, in the order the corners are given) with the properties of the two elements merged, the name of the first winning (the first element is the one drawn first), and the properties given in options set over them. The two elements keep their ids, cut back to the touch points. What is cut off stays as a construction element named as Trim names it (‘Line.1.trimmed.1’, ‘Line.2.trimmed.1’; the sharp corner is the end the two share, ‘Line.1.trimmed.1.end’). An element the cut reaches the far end of is used up: it becomes construction as a whole, and the new element ends at its far end. Lines and arcs are cut exactly; a spline is cut where the arc meets its exact curve. Remove(‘Fillet.1’) takes the arc away; the elements stay cut.

A corner that cannot be cut (a tangent joint, a point on no vertex, more or fewer than two ends, a size that does not fit) is skipped with a PlanexWarning that names why and the largest size that fits; if none can be cut the call raises and changes nothing.

The arc is held by its tangency to the two elements and the dimension ‘Fillet.k.r’, which drives it: sk\[‘Fillet.1.r’\] = 2, then Update() moves the touch points.

Parameters:  
- **at** – the corners: a point id (‘Line.1.end’, ‘A’), a sketch point or a selection of points, a point (h, v) on a corner, or a list of these; there is no default.

- **r** – the radius, a length \> 0.

- **id** – the id of the new element for one corner (a str), or one id per corner (a list); default ‘Fillet.k’.

- **options** – the new element’s properties name=, maxh=, minh=, hpref=, layer=, col=, each one value for all corners or a list of one per corner (col=\[(1, 0, 0), (0, 0, 1)\]); they replace the merged ones.

Returns:  
this sketch, so calls chain.

Raises:  
- **TypeError** – at is none of the forms above, id is neither a str nor a list of str, or an option is no property.

- **ValueError** – a size is not positive and finite, no corner is given, an id is no point, or no corner can be cut (the message names each corner and why); an id is taken, or the number of ids or of a property’s values differs from the number of corners.

Examples

``` python
sk.Fillet("Line.2.end", 0.5)
sk.Fillet([(4, 0), (4, 4)], 0.5, name=["r1", "r2"])
```

Finish(*id_wire: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")  
End the chain without closing it; the next one starts where the pen is.

Parameters:  
**id_wire** – replaces the chain’s wire id Chain.k (for the patterns, RigidGroup, Remove, Edge, Measure).

Raises:  
**ValueError** – id_wire is taken or invalid, or no chain is drawn.

Examples

``` python
sk.Line(10).Finish()
sk.Line(10).Turn(90).Line(5).Finish(id_wire="rail")
```

Geometry(*id: Id*) → dict  
An element’s solved geometry.

Parameters:  
**id** – the element.

Returns:  
a point its point; a line start, end, length, angle (degrees from h); a circle center, r; an arc center, r, start, end, ccw, sweep (degrees, positive counterclockwise); an axis normal_angle, d; a spline points, closed, tangents, path, start, end, projected, style (an offset curve also source).

Return type:  
kind, and

Examples

``` python
sk.Geometry("Line.1")["length"]
sk.Geometry("Arc.1")["sweep"]
```

Hexagon(*af: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Hexagon(*h: float*, *v: float*, *af: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Hexagon(*af: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *center: Point \| None = None*, *concentric: Id \| None = None*, *coincident: Id \| None = None*, *through: Point \| None = None*, *tangent: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| tuple\[Id, Id\] \| None = None*, *reverse: bool = False*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")  
Draw a regular hexagon of width af across its flats, as for a nut.

A Polygon of six sides whose circle has the radius af / sqrt(3): the first corner is along the pen’s drawing direction from the centre, so two flats run parallel to it. Hexagon.k.Line.1 to .Line.6 run from the first corner counterclockwise, Hexagon.k.circle is the construction circle, and Hexagon.k is the wire of the lines. The pen and its direction stay. A Sketch keeps the hexagon regular and has the dimension Hexagon.k.af, the distance between two opposite flats (sk\[“Hexagon.1.af”\] = 17; sk.Update()). Remove(“Hexagon.1”) removes the whole hexagon.

Parameters:  
- **af** – the width across flats; positive.

- **h** – the centre’s h.

- **v** – the centre’s v.

- **flags** – as for Circle, which draws the circle.

- **center** – as for Circle.

- **concentric** – as for Circle.

- **coincident** – as for Circle.

- **through** – as for Circle.

- **tangent** – as for Circle.

- **reverse** – as for Circle.

- **options** – id= replaces Hexagon.k (id=’nut’ gives nut.circle, nut.Line.1, …), and the sides’ properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **TypeError** – af is no number or not the last number, another form of arguments, an unknown keyword.

- **ValueError** – af is not positive; the circle cannot be placed; id is taken.

Examples

``` python
sk.Hexagon(13)
sk.Hexagon(10, 5, 13, id="nut")
```

Intersect3d(*shape: occ.TopoDS_Shape*, */*, *construction: bool = True*, *name: str \| None = None*, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")  
Add where the sketch’s plane meets an OCC shape, as px.Intersect3d, as fixed elements under Intersect3d.N, with the points where 3d wires cross or touch the plane.

Parameters:  
- **shape** – the OCC shape.

- **construction** – references; False adds drawn edges.

- **name** – the name of them all; default they keep the names of the shape’s faces and edges.

- **id** – replaces Intersect3d.N.

Examples

``` python
sk.Intersect3d(box)
sk.Intersect3d(box, construction=False, name="cut")
```

Keyhole(*l: float*, *w: float*, *r: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Keyhole(*dh: float*, *dv: float*, *w: float*, *r: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Keyhole(*to: Point \| Id*, *w: float*, *r: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Keyhole(*start: Point \| Id*, *end: Point \| Id*, *w: float*, *r: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Keyhole(*\*ends: Point \| Id \| float \| [Constraint](#planex.Constraint "planex.Constraint")*, *until: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| SketchUntil \| None = None*, *parallel: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| None = None*, *perpendicular: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| None = None*, *angle: float \| tuple\[float, Id\] \| None = None*, *coincident: Id \| None = None*, *tangent: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| tuple\[Id, Id\] \| None = None*, *distance: float \| tuple\[float, Point\] \| None = None*, *through: Point \| None = None*, *reverse: bool = False*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")  
Draw a keyhole: a straight slot that starts in a round hole.

Every form of Slot, with the hole’s radius r as the last number. The spine is a construction line; the hole is centred at its start. The outline is one loop: the two sides run from the hole’s circle to the round end cap, and the hole is the arc of its circle between the sides, the long way round. The pen goes on at the spine’s end in its direction (a new chain). Keyhole.k is the wire of the outline, with Keyhole.k.spine, Keyhole.k.left and Keyhole.k.right (left and right of the spine’s direction), Keyhole.k.endcap and Keyhole.k.hole. A Sketch keeps the keyhole a keyhole when the spine moves, and has the dimensions Keyhole.k.w and Keyhole.k.r (sk\[“Keyhole.1.r”\] = 4; sk.Update()); with auto_constraint the spine gets what Line gets. Remove(“Keyhole.1”) removes the whole keyhole.

Parameters:  
- **r** – the radius of the hole; larger than w / 2.

- **w** – the width of the slot, across the spine; positive.

- **l** – as for Line, which draws the spine.

- **dh** – as for Line, which draws the spine.

- **dv** – as for Line, which draws the spine.

- **to** – as for Line, which draws the spine.

- **start** – as for Line, which draws the spine.

- **end** – as for Line, which draws the spine.

- **ends** – as for Line, which draws the spine.

- **flags** – as for Line, which draws the spine.

- **until** – as for Line, which draws the spine.

- **parallel** – as for Line, which draws the spine.

- **perpendicular** – as for Line, which draws the spine.

- **angle** – as for Line, which draws the spine.

- **coincident** – as for Line, which draws the spine.

- **tangent** – as for Line, which draws the spine.

- **distance** – as for Line, which draws the spine.

- **through** – as for Line, which draws the spine.

- **reverse** – as for Line, which draws the spine.

- **options** – id= replaces Keyhole.k (id=’hook’ gives hook.spine, hook.left, …), and the outline’s properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **TypeError** – no width and radius as the last numbers, another form of arguments, an unknown keyword.

- **ValueError** – w is not positive; r is not larger than w / 2; the spine is not longer than sqrt(r^2 - (w / 2)^2) or cannot be placed; id is taken.

Examples

``` python
sk.Keyhole(10, 2, 3)
sk.Keyhole((0, 0), (0, 10), 2, 3, id="hook")
```

Line(*l: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Line(*dh: float*, *dv: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Line(*to: Point \| Id*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Line(*start: Point \| Id*, *end: Point \| Id*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Line(*\*ends: Point \| Id \| float \| [Constraint](#planex.Constraint "planex.Constraint")*, *until: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| SketchUntil \| None = None*, *parallel: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| None = None*, *perpendicular: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| None = None*, *angle: float \| tuple\[float, Id\] \| None = None*, *coincident: Id \| None = None*, *tangent: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| tuple\[Id, Id\] \| None = None*, *distance: float \| tuple\[float, Point\] \| None = None*, *through: Point \| None = None*, *reverse: bool = False*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")  
Draw a line from the pen or between two ends.

The positional arguments are read in this order: start, end, then a length, then the flags. The helpers (the keywords from until to reverse) place the line from references: one of parallel, perpendicular, angle, coincident, tangent or through fixes its carrier (else the pen’s direction), and l, to or until its end. A target is an id, px.H, px.V, an edge, Wire, Face, Shape or OCC shape (projected). Of several solutions the one on the edges before their extensions, then the shortest line, then the one nearest the pen is taken. With auto_constraint the helpers also constrain the line.

Parameters:  
- **l** – the length along the pen’s direction, or with a helper the length along its carrier.

- **dh** – the line’s extent along h from the pen; with dv.

- **dv** – the line’s extent along v from the pen.

- **to** – the end: a point (h, v), a point id (‘A’, ‘Line.1.end’), sk.pen, an element or another target (where the carrier meets it; (target, point) takes the intersection nearest the point). An element as an end needs a helper that fixes the carrier.

- **start** – the start of the two-end form, read as to.

- **end** – the end of the two-end form, read as to.

- **ends** – start and end, or a single start with until=, or a length l, then the flags.

- **flags** – px.Horizontal, px.Vertical, px.Fix (of the line); px.Tangent, px.Perpendicular, px.Parallel, px.Equal (with the element before it).

- **until** – the end: px.first / px.last until the first / last line, arc, circle or spline ahead (the start not counted), or a target; with an element as to, px.first / px.last picks the first or last intersection with it ahead. With until= a single positional is the from: sk.Line(“c1”, tangent=”c2”, until=”wall”).

- **parallel** – a line or target the line is parallel to; sk.pen takes the pen’s direction.

- **perpendicular** – a line or target the line is perpendicular to; sk.pen takes the pen’s direction.

- **angle** – degrees from the h axis, or (degrees, reference) from the line reference.

- **coincident** – a line whose carrier the line lies on.

- **tangent** – one target the line touches, or two circles (the line goes from the first to the second).

- **distance** – with parallel, the offset from the reference: d \> 0 on the side of it the pen is on, d \< 0 the other side (pen on the reference: d \> 0 to its left); (d, point): d on the side of the point. Needs a length, an end target or until=; sk.Offset gives an offset copy.

- **through** – a point: with a direction helper the carrier passes it and the line starts there, else the line goes from the start through it.

- **reverse** – take the other solution; with none, run a forward line the other way.

- **options** – id= (the line’s id), and its properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **TypeError** – a wrong form of arguments, an unknown keyword, a keyword for an end (the ends are positional), near=.

- **ValueError** – the line cannot be placed (the message names what is missing); nothing is drawn.

Examples

``` python
sk.Line(5).Line((5, 3)).Line("A", "B", through=(2, 1))
sk.Line("wall", angle=30)
```

LinearPattern(*ids: Ids*, *dir: Id*, *count: int*, *spacing: float*, *auto_constraint: bool \| None = None*, *internal_constraints: bool \| None = None*, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")  
Add count instances of the elements in a row, the original the first.

The pattern is ‘LinearPattern.N’ (or id), the copies named after their sources by their step (‘LinearPattern.1.3.hole’).

Parameters:  
- **ids** – the elements: one id, a list or a selection.

- **dir** – px.H, px.V or a line (its direction start to end).

- **count** – the number of instances, the original included.

- **spacing** – how much further along dir each instance is.

- **auto_constraint** – tie the copies to the originals through the pattern, its spacing the dimension ‘LinearPattern.N.spacing’ and its number of instances ‘LinearPattern.N.count’ (set like a dimension: copies are added or the last ones removed), each copy listed as one constraint ‘LinearPattern.N.k’; default whether the constraints are enabled.

- **internal_constraints** – show the constraints among the originals on the copies (‘LinearPattern.1.3.\<id\>’, derived), or with auto_constraint=False give the copies those constraints; default as auto_constraint.

- **id** – replaces LinearPattern.N.

Examples

``` python
sk.LinearPattern("hole", px.H, 5, 10)
sk.LinearPattern(["Circle.1", "Circle.2"], "Line.1", 3, 8, id="row")
```

Location(*id: Id*, */*) → tuple\[float, float\]  
Where a point is, solved, as (h, v).

Parameters:  
**id** – a point id, or a point of an element: ‘Line.1.end’, ‘.start’, ‘.mid’, ‘.center’, a spline’s ‘.p2’.

Raises:  
**ValueError** – id names no point.

Examples

``` python
sk.Location("Line.1.end")
sk.Location("A")
```

Measure(*name: Id \| None = None*, *to: Point \| Point3 \| [Shape](geometry.md#planex.Shape "planex.Shape") \| Id \| None = None*, *project: bool = False*) → dict\
Measure(*f: Callable*, */*, *\*args*, *id: str \| None = None*, *\*\*params*) → object  
Measure an element, a point or named edges as Shape.Measure does, alone or against a target.

Parameters:  
- **name** – an element’s id, a point (‘Line.1.end’) or a name the edges carry (drawn and construction); default the region of Face()/Shape() and all drawn edges.

- **to** – an id, a name, or anything Distance takes (also OCC shapes).

- **project** – measure to the target projected into the sketch’s plane.

- **f** – a measurement, a callable f(obj, \*args, \*\*params) (see px.measurements): Measure returns its value.

- **args** – the further arguments of f.

- **id** – stores the measurement f under this id: obj.measures then holds it next to the standard measurements, RemoveMeasure(id) drops it.

- **params** – the keyword arguments of f.

Returns:  
type, faces, area, edges, length, bounding_box, and where it has mass center, inertia, Ih, Iv, Ihv, I1, I2, principal_angle (a circle or arc also radius and angle). With to: distance, du, dv, dx, dy, dz, points (the nearest pair) and, for two lines, angle.

Return type:  
Without to

Raises:  
- **ValueError** – no element, point or edge has the name.

- **TypeError** – id without a measurement f.

Examples

``` python
sk.Measure("Line.1")["length"]
sk.Measure(px.measurements.MinThickness(), id="wall")
```

Mirror(*axis: Id*) → [Sketch](#planex.Sketch "planex.Sketch")\
Mirror(*ids: Ids*, *axis: Id*) → [Sketch](#planex.Sketch "planex.Sketch")  
Mirror the whole sketch, or some of its elements in place, at the line axis.

The whole sketch: its plane is mirrored, so in 3d it is its mirror image (the plane’s normal turns over); its coordinates, constraints and dimensions stay. Elements: they move with the points they reach (what they share with other elements moves too); their constraints stay and the sketch solves again (Mirrored adds mirrored copies instead).

Parameters:  
- **axis** – px.H, px.V, a line or a symmetry line.

- **ids** – the elements: one id, a list or a selection.

Raises:  
**ValueError** – the constraints do not allow the mirrored place; nothing changes.

Examples

``` python
sk.Mirror(px.V)
sk.Mirror("slot", px.V)
```

Mirrored(*axis: Id*) → [Sketch](#planex.Sketch "planex.Sketch")\
Mirrored(*ids: Ids*, *axis: Id*, *auto_constraint: bool \| None = None*, *internal_constraints: bool \| None = None*, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")  
A copy of the whole sketch mirrored at the line axis, or mirrored copies of elements added to this sketch.

Mirrored(axis) returns a new sketch, as Mirror(axis). Mirrored(ids, axis) adds copies of the elements, with the points their lines and arcs end at and their properties, under ‘Mirrored.N’ (or id), named after their sources (‘Mirrored.1.Line.1’); ‘Mirrored.N’ is the wire of the copies. The originals stay; it returns this sketch.

Parameters:  
- **axis** – px.H, px.V, a line or a symmetry line.

- **ids** – the elements: one id, a list or a selection.

- **auto_constraint** – keep the copies symmetric to the originals about the axis, listed as one constraint ‘Mirrored.N’; default whether the constraints are enabled.

- **internal_constraints** – show the constraints among the originals on the copies (‘Mirrored.1.\<id\>’, derived), or with auto_constraint=False give the copies those constraints; default as auto_constraint.

- **id** – replaces Mirrored.N.

Examples

``` python
right = sk.Mirrored(px.V)
sk.Mirrored("Chain.1", px.V)
```

Move(*point: Point*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *dir: tuple\[float, float\] \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")\
Move(*dh: float*, *dv: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *dir: tuple\[float, float\] \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")\
Move(*l: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *dir: tuple\[float, float\] \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")  
Move the pen without drawing; a new chain starts there.

Parameters:  
- **point** – where the pen goes: (h, v), a point id (‘Line.1.end’), a selected point or sk.pen.

- **dh** – the move along h from the pen; with dv.

- **dv** – the move along v from the pen.

- **l** – the move along the pen’s direction.

- **flags** – px.Coincident (the start shares the point it moves to), px.Fix (the start is fixed).

- **dir** – the drawing direction (dh, dv) from there, or sk.pen (its direction); default: it stays.

Raises:  
- **TypeError** – another form of arguments, or an unknown keyword.

- **ValueError** – dir has no length.

Examples

``` python
sk.Move((10, 0)).Line(5)
sk.Move("Line.1.end", px.Coincident, dir=(0, 1))
```

Normal() → tuple\[float, float, float\]  
The normal of its plane in 3d.

Examples

``` python
sk.Normal()
```

Offset(*source: Ids*, */*, *d: float \| tuple\[float, Point\]*, *both_sides: bool \| None = None*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")  
Add new elements at distance d from source that follow it: the exact offset, round around corners, sharp where the sides cross.

The offset of a spline is a curve at exactly d (kind ‘Spline’). With the constraints on, the offset follows the source and ‘\<id\>.d’; a change of d moves the geometry as little as it can, a free source too: fix or dimension the source to keep it. A QuickSketch makes the same elements without ties, a spline’s offset as an ordinary spline within config.spline_tol. The pens stay (a Rectangle after it follows the pen’s direction; dir= sets it). A source cannot be removed while the offset exists (sk(‘Line.2’).construction = True keeps it); the offset’s parts cannot be removed, trimmed, split, fixed or copied alone.

Parameters:  
- **source** – an id, a wire id or a list of ids forming one chain of lines, arcs and splines; or one circle or closed spline.

- **d** – the distance. A closed chain gets one loop, outside for d \> 0, inside for d \< 0; an open chain both sides with round caps (‘\<id\>.startcap’, ‘\<id\>.endcap’). (d, point): the side the point ((h, v) or a point id) lies on, d \> 0.

- **both_sides** – True both sides (a closed chain: both loops); False one side (an open chain: the left for d \> 0, the right for d \< 0, no caps); default one side for a closed chain, both for an open one.

- **options** – id= the offset’s id, default ‘Offset.N’. One side: ‘\<id\>.Line.3’ is the offset of Line.3, ‘\<id\>.Line.3.corner’ the round corner around the end of Line.3. Both sides: the side word first, ‘\<id\>.left.Line.3’, ‘\<id\>.right.Line.3.corner’, and ‘\<id\>.left’, ‘\<id\>.right’ the sides (a loop counts counterclockwise: left is its inside). ‘\<id\>’ is all of it; ‘\<id\>.d’ its distance, a dimension. name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=: the elements’ properties, by default those of the source.

Raises:  
- **ValueError** – d = 0; a source that is not one chain; d at or beyond the radius of a spline’s tightest bend towards the side (a cusp). Later, a change of d or of the source that needs another shape (an arc’s offset vanishing or coming back, a corner appearing, a cusp, sides crossing) raises and changes nothing: Remove(‘\<id\>’), then Offset again.

- **TypeError** – d is neither a distance nor (d, point), or an unknown keyword.

Examples

``` python
sk.Offset('plate', -2, id='wall')
sk.Offset('s', (1.5, (10, 40)), id='bead')
```

Plot(*ax: Any = None*, *show_constraints: bool \| None = None*, *show_ends: bool = False*, *show_direction: bool = False*, *show_parent: Any = None*, *backend: str \| None = None*) → Any  
Draws the sketch with matplotlib or plotly, every element in the colour of its state.

Green is fully constrained, black free, pink over-constrained (a constraint that contradicts the others and the elements it contradicts; all free while the constraints are disabled); construction elements are dashed, projected elements gold; in plotly an element shows its id when hovered. The signs, pieces and arrows keep their size on screen when zooming.

Parameters:  
- **ax** – a matplotlib Axes or a plotly Figure to draw into, which also selects the backend.

- **show_constraints** – True adds a sign per constraint (in plotly what it is when hovered; not while the constraints are disabled); a QuickSketch has none and warns.

- **show_ends** – True adds a blue start and an orange end piece on every line and arc.

- **show_direction** – True adds an arrow from start to end on every line and arc.

- **show_parent** – an OCC solid (e.g. the one whose face the sketch lies on) drawn projected into the sketch’s plane under the sketch, its faces lightly filled, its edges black (in plotly with their names when hovered); only in the plot.

- **backend** – “matplotlib” or “plotly” (hover) without ax; None is matplotlib.

Returns:  
the matplotlib Axes or the plotly Figure.

Raises:  
**ValueError** – backend is unknown.

Examples

``` python
sk.Plot(show_constraints=True)
sk.Plot(show_parent=box, backend="plotly")
```

Plot3d(*plane_size: float \| None = None*, *backend: str = 'webgpu'*, *\*\*kwargs: Any*) → Any  
Draws planex objects, planes and OCC shapes in one 3d view.

Everything is exported to OCC, put into one compound and drawn; a sketch contributes its drawn edges. A plane is drawn as a square of side plane_size with a line along its u axis. backend “webgpu” draws with ngsolve_webgpu.jupyter.Draw: it needs ngsolve_webgpu (pip install ngsolve_webgpu, which brings the full ngsolve) and a browser with WebGPU, with the kernel on the same machine as the browser or its websocket port forwarded. backend “webgui” draws with netgen.webgui.Draw and needs only netgen.

Parameters:  
- **objects** – a Wire, Face, Shape, Compound, sketch, Plane or OCC shape, or a list of them.

- **plane_size** – the side of a plane’s square; None takes the largest extent of everything else, 1 without.

- **backend** – ‘webgpu’ or ‘webgui’.

- **kwargs** – go to the backend’s Draw (width, height; settings, euler_angles only for webgui).

Returns:  
the backend’s scene.

Raises:  
- **ValueError** – backend is unknown, or there is nothing to draw.

- **ImportError** – backend ‘webgpu’ without ngsolve_webgpu installed.

Examples

``` python
px.Plot3d([face, px.XY], plane_size=2)
shape.Plot3d(backend="webgui", height=400)
```

Polygon(*r: float*, *n: int*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Polygon(*h: float*, *v: float*, *r: float*, *n: int*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Polygon(*n: int*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *center: Point \| None = None*, *concentric: Id \| None = None*, *coincident: Id \| None = None*, *through: Point \| None = None*, *tangent: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| tuple\[Id, Id\] \| None = None*, *reverse: bool = False*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")  
Draw a regular polygon of n equal sides, its corners on a circle.

Every form of Circle, with the number of sides n as the last number. The circle is a construction element, Polygon.k.circle, of radius r around the pen’s position, around (h, v) or placed by the helpers; the first corner is on it along the pen’s drawing direction from its centre, the others follow counterclockwise. Polygon.k.Line.1 to .Line.n run from the first corner, and Polygon.k is their wire (sk(‘Polygon.1’), Offset, Remove, the patterns). The pen and its direction stay. A Sketch keeps the polygon regular (the corners on the circle, equal sides) and has the dimension Polygon.k.r (sk\[“Polygon.1.r”\] = 3; sk.Update()); with auto_constraint the circle gets what Circle gets. Remove(“Polygon.1”) removes the whole polygon.

Parameters:  
- **n** – the number of sides, an int of at least 3.

- **r** – the radius of the circle the corners are on.

- **h** – the centre’s h.

- **v** – the centre’s v.

- **flags** – as for Circle, which draws the circle.

- **center** – as for Circle.

- **concentric** – as for Circle.

- **coincident** – as for Circle.

- **through** – as for Circle.

- **tangent** – as for Circle.

- **reverse** – as for Circle.

- **options** – id= replaces Polygon.k (id=’nut’ gives nut.circle, nut.Line.1, …), and the sides’ properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **TypeError** – n is no int or not the last number, another form of arguments, an unknown keyword.

- **ValueError** – n is below 3; the circle cannot be placed; id is taken.

Examples

``` python
sk.Polygon(5, 6)
sk.Polygon(0, 0, 5, 8, id="gear")
```

Polylines(*tol: float*) → list\[np.ndarray\]  
The loops and open chains of the sketch’s Shape() as (n, 2) arrays of plane coordinates, as Shape.Polylines.

Parameters:  
**tol** – the largest distance of a chord from its edge, a length \> 0.

Raises:  
**ValueError** – tol is not positive and finite, or below the rounding of an arc’s or a spline’s coordinates.

Examples

``` python
sk.Polylines(0.01)
```

Project(*shape: occ.TopoDS_Shape*, */*, *construction: bool = True*, *name: str \| None = None*, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")  
Add the edges and points of an OCC shape projected straight onto the sketch’s plane, fixed where they are.

A curve becomes a spline through samples of it, within the spline tolerance. Its elements are Projection.N.Edge.X, X the edge’s index in shape.edges (the number the webgui shows), and Projection.N.Vertex.X; pieces of one edge Edge.X.1, Edge.X.2.

Parameters:  
- **shape** – the OCC shape.

- **construction** – references; False adds drawn edges.

- **name** – the name of the drawn edges.

- **id** – replaces Projection.N.

Examples

``` python
sk.Project(box)
sk.Project(box, construction=False, name="outline")
```

Rectangle(*l: float*, *w: float*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Rectangle(*p0: Point*, *p1: Point*, */*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Rectangle(*p0: Point*, *p1: Point*, *p2: Point*, */*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")  
Draw a rectangle from the pen, l along the drawing direction and w to its left, or through its corners.

Its four lines are Rectangle.k.Line.1 to .Line.4 from the start corner along the drawing direction (counterclockwise), and Rectangle.k is their wire (sk(‘Rectangle.1’), Offset(‘Rectangle.1’, 2), Remove, the patterns). It is not part of a chain; its three right angles are Rectangle.k.Perpendicular.1 to .3; the pen stays at the start corner.

Through corners: two opposite corners p0, p1 (in any order) give a rectangle with its sides along h and v, starting at the corner of the smallest h and v (in a px.Sketch its first side is also Horizontal). Three points: p0 to p1 is a side and the opposite side runs through p2, which sets the width; it starts at p0 and runs counterclockwise (with p2 to the right of p0 -\> p1, Line.1 leaves p0 along its other side and Line.4 runs p1 -\> p0). A corner is a point (h, v) or a selected point such as sk(‘A’), which it is tied to (p2: the opposite side passes through it). The pen stays where it was.

Parameters:  
- **l** – the length along the drawing direction.

- **w** – the width to the left of it.

- **p0** – a corner.

- **p1** – the opposite corner (two points), or the corner after p0 (three points).

- **p2** – a point on the side opposite p0 -\> p1.

- **options** – id= replaces Rectangle.k (id=’plate’ gives plate.Line.1 to plate.Line.4 and the wire ‘plate’); the lines’ properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **ValueError** – id is taken or no valid id, a side within rounding of 0, a selection of more than one point.

- **TypeError** – id_wire= (the id names the wire), an unknown keyword, a corner that is no point or an id string, or four points.

Examples

``` python
sk.Rectangle(40, 20, id='plate', maxh=2)
sk.Rectangle((0, 0), (40, 20))
```

RectangleC(*l: float*, *w: float*, */*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
RectangleC(*center: Point*, *l: float*, *w: float*, */*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")  
Draw a rectangle centred at the pen or at center, l along the drawing direction and w across it.

Its four lines are RectangleC.k.Line.1 to .Line.4 from the corner behind and to the right of the centre, along the drawing direction (counterclockwise), and RectangleC.k is their wire. The construction line RectangleC.k.diagonal runs from the first corner to the third, the construction point RectangleC.k.center sits at its middle; a selected point as center ties the centre to it. It is not part of a chain; the pen goes to the centre and keeps its direction.

Parameters:  
- **center** – the centre: a point (h, v) or a selected point such as sk(‘A’) (tied to it); default the pen.

- **l** – the length along the drawing direction.

- **w** – the width across it.

- **options** – id= replaces RectangleC.k (id=’plate’ gives plate.Line.1 to plate.Line.4, plate.center and the wire ‘plate’); the lines’ properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **ValueError** – a side within rounding of 0, a selection of more than one point, id is taken or no valid id.

- **TypeError** – l or w is no number, center is an id string or no point, id_wire= or an unknown keyword.

Examples

``` python
sk.RectangleC(40, 20)
sk.RectangleC((10, 5), 40, 20, id='plate')
```

Remove(*id: Id*) → [Sketch](#planex.Sketch "planex.Sketch")  
Remove a constraint, an element with its constraints (a line’s ends too, unless used), or the elements of a wire id; an expression that uses a dimension removed goes too, with a warning.

Parameters:  
**id** – the constraint, element or wire.

Examples

``` python
sk.Remove("Line.2")
sk.Remove("Chain.1")
```

RemoveExpression(*id: Id*) → [Sketch](#planex.Sketch "planex.Sketch")  
Remove an expression, with the reference dimensions made for its measures if no other expression uses them; a dimension it drove drives again, with the value it has now.

Parameters:  
**id** – the expression.

Examples

``` python
sk.RemoveExpression("Expression.1")
```

RemoveMeasure(*id: str*) → None  
Drops the stored measurement id.

Parameters:  
**id** – the id the measurement was stored under (Measure(f, id=…)).

Raises:  
**ValueError** – no measurement is stored under id.

Examples

``` python
shape.RemoveMeasure("gap")
```

RigidGroup(*ids: Ids*, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")  
Keep the elements’ shape as it is now, together: they move and turn as one; remove it to change the shape.

Parameters:  
- **ids** – the elements: one id, a list or a selection.

- **id** – the constraint’s id.

Examples

``` python
sk.RigidGroup(["Line.1", "Arc.1"])
sk.RigidGroup("Chain.2", id="bracket")
```

Rotate(*center: Point*, *angle: float*) → [Sketch](#planex.Sketch "planex.Sketch")\
Rotate(*point: Point3*, */*, *dir: tuple\[float, float, float\]*, *angle: float*) → [Sketch](#planex.Sketch "planex.Sketch")  
Rotates it in place by angle around center in its plane, or around the axis through point along dir in 3d.

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **center** – the centre of the rotation in the plane, (h, v).

- **angle** – the angle in degrees.

- **point** – a point of the axis in 3d, (x, y, z).

- **dir** – the direction of the axis in 3d, (dx, dy, dz).

Returns:  
this Sketch.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
sk.Rotate((0, 0), 90)
sk.Rotate((0, 0, 0), (0, 0, 1), 45)
```

Rotated(*center: Point*, *angle: float*, *\**, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")\
Rotated(*point: Point3*, */*, *dir: tuple\[float, float, float\]*, *angle: float*, *\**, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")  
A copy rotated by angle around center in its plane, or around the axis through point along dir in 3d; this Sketch is not changed.

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **center** – the centre of the rotation in the plane, (h, v).

- **angle** – the angle in degrees.

- **point** – a point of the axis in 3d, (x, y, z).

- **dir** – the direction of the axis in 3d, (dx, dy, dz).

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
sk.Rotated((0, 0), 90)
sk.Rotated((0, 0, 0), (0, 0, 1), 45)
```

Scale(*center: Point*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*) → [Sketch](#planex.Sketch "planex.Sketch")\
Scale(*point: Point3*, */*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*) → [Sketch](#planex.Sketch "planex.Sketch")  
Scales it in place by factor around center in its plane, or around point in 3d.

The mesh sizes maxh and minh stay unless asked for.

Parameters:  
- **center** – the centre of the scaling in the plane, (h, v).

- **point** – the centre of the scaling in 3d, (x, y, z).

- **factor** – the scale factor, \> 0.

- **scale_maxh** – True also scales maxh.

- **scale_minh** – True also scales minh.

Returns:  
this Sketch.

Raises:  
**ValueError** – factor is not positive and finite, or a coordinate is not finite.

Examples

``` python
sk.Scale((0, 0), 2)
sk.Scale((0, 0), 0.5, scale_maxh=True)
```

Scaled(*center: Point*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*, *\**, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")\
Scaled(*point: Point3*, */*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*, *\**, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")  
A copy scaled by factor around center in its plane, or around point in 3d; this Sketch is not changed.

The mesh sizes maxh and minh stay unless asked for.

Parameters:  
- **center** – the centre of the scaling in the plane, (h, v).

- **point** – the centre of the scaling in 3d, (x, y, z).

- **factor** – the scale factor, \> 0.

- **scale_maxh** – True also scales maxh.

- **scale_minh** – True also scales minh.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – factor is not positive and finite, or a coordinate is not finite.

Examples

``` python
sk.Scaled((0, 0), 2)
sk.Scaled((0, 0), 0.5, scale_maxh=True)
```

SetOrigin(*point: Point*, */*) → [Sketch](#planex.Sketch "planex.Sketch")  
Move the axes (H, V, the sketch’s origin) to a point; the drawing stays where it is, its coordinates shift, and constraints to the axes follow them.

Parameters:  
**point** – the new origin, (h, v) or a point id.

Examples

``` python
sk.SetOrigin((10, 5))
sk.SetOrigin("Circle.1.center")
```

Shape(*\**, *id: str \| None = None*) → [Shape](geometry.md#planex.Shape "planex.Shape")  
The regions the drawn elements bound, as Face() but all of them.

Parameters:  
**id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – in mode=’strict’ while the sketch can still move.

Examples

``` python
shape = sk.Shape()
```

Slot(*l: float*, *w: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Slot(*dh: float*, *dv: float*, *w: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Slot(*to: Point \| Id*, *w: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Slot(*start: Point \| Id*, *end: Point \| Id*, *w: float*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")\
Slot(*\*ends: Point \| Id \| float \| [Constraint](#planex.Constraint "planex.Constraint")*, *until: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| SketchUntil \| None = None*, *parallel: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| None = None*, *perpendicular: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| None = None*, *angle: float \| tuple\[float, Id\] \| None = None*, *coincident: Id \| None = None*, *tangent: Id \| [Shape](geometry.md#planex.Shape "planex.Shape") \| tuple\[Id, Id\] \| None = None*, *distance: float \| tuple\[float, Point\] \| None = None*, *through: Point \| None = None*, *reverse: bool = False*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")  
Draw a straight slot: a line and its offset on both sides, closed by round caps.

Every form of Line, with the width w as the last number before the flags. The line is the slot’s spine, a construction element; the outline is its offset at w / 2 on both sides, the caps half circles around the spine’s ends. The pen goes on at the spine’s end in its direction (a new chain). Slot.k is the wire of the outline, with Slot.k.spine, Slot.k.left and Slot.k.right (left and right of the spine’s direction), Slot.k.startcap and Slot.k.endcap. A Sketch keeps the slot a slot when the spine moves, and has the dimension Slot.k.w (sk\[“Slot.1.w”\] = 6; sk.Update()); with auto_constraint the spine gets what Line gets. Remove(“Slot.1”) removes the whole slot.

Parameters:  
- **w** – the width of the slot, across the spine; positive.

- **l** – as for Line, which draws the spine.

- **dh** – as for Line, which draws the spine.

- **dv** – as for Line, which draws the spine.

- **to** – as for Line, which draws the spine.

- **start** – as for Line, which draws the spine.

- **end** – as for Line, which draws the spine.

- **ends** – as for Line, which draws the spine.

- **flags** – as for Line, which draws the spine.

- **until** – as for Line, which draws the spine.

- **parallel** – as for Line, which draws the spine.

- **perpendicular** – as for Line, which draws the spine.

- **angle** – as for Line, which draws the spine.

- **coincident** – as for Line, which draws the spine.

- **tangent** – as for Line, which draws the spine.

- **distance** – as for Line, which draws the spine.

- **through** – as for Line, which draws the spine.

- **reverse** – as for Line, which draws the spine.

- **options** – id= replaces Slot.k (id=’groove’ gives groove.spine, groove.left, …), and the outline’s properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
- **TypeError** – no width as the last number, another form of arguments, an unknown keyword.

- **ValueError** – w is not positive; the spine has no length or cannot be placed; id is taken.

Examples

``` python
sk.Slot(10, 4)
sk.Slot((0, 0), (0, 10), 2, id="groove")
```

Spline(*points: list\[tuple\[float, float\]\]*, */*, *\*flags: [Constraint](#planex.Constraint "planex.Constraint")*, *periodic: bool = False*, *tangents: dict\[int, tuple\[float, float\]\] \| None = None*, *start_from_localpos: bool = True*, *style: str \| None = None*, *tol: float \| None = None*, *\*\*options*) → [Sketch](#planex.Sketch "planex.Sketch")  
Draw a spline from the pen’s position through points; its points are point elements.

Parameters:  
- **points** – the points (h, v) it passes, after the start.

- **flags** – px.Fix; px.Tangent (with the element before it).

- **periodic** – closed back to its start.

- **tangents** – {index: (dh, dv)}, directions at some of the points, index 0 the start; they become angle unknowns.

- **start_from_localpos** – False starts at the first point instead of the pen’s position.

- **style** – ‘px’ (planex’s: closed without tangents the C2 periodic cubic) or ‘occ’ (OCC’s Geom2dAPI_Interpolate, netgen’s splines; a sketch keeps tangent directions only, so two points with one tangent of another length than 1 differ from netgen’s); default config.spline_style.

- **tol** – ignored.

- **options** – id= (the spline’s id), and its properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.

Raises:  
**TypeError** – not one list of points, a point that is no (h, v), or an unknown keyword.

Examples

``` python
sk.Spline([(5, 2), (10, 0)])
sk.Spline([(5, 2), (10, 0), (5, -2)], periodic=True, tangents={0: (0, 1)})
```

Split(*a: Id*, *b: Ids \| None = None*, */*, *\**, *auto_constraint: bool = True*) → [Sketch](#planex.Sketch "planex.Sketch")  
Cut a into pieces where it meets b.

The pieces are ‘Line.3.1’, ‘Line.3.2’, … along it (a circle’s from the piece at 0 degrees, counterclockwise), and ‘Line.3’ is the wire of the pieces, which what referred to Line.3 (its ends, its length, a tangency) resolves through. The cut points stay on b.

Parameters:  
- **a** – the element.

- **b** – what cuts it: an id, a list or a selection; default every other line, arc and circle.

- **auto_constraint** – keep the pieces on one line or circle; False lets them move apart (an arc’s pieces get their own centres and radii).

Raises:  
- **ValueError** – a given as (a, point): every piece is numbered, none keeps a’s id.

- **TypeError** – a keyword other than auto_constraint.

Examples

``` python
sk.Split("Line.3", "Line.5")
sk.Split("Circle.1", ["Line.1", "Line.2"], auto_constraint=False)
```

SymmetryLine(*a: Point \| Id*, *b: Point \| Id*, */*, *\**, *reverse: bool = False*, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")  
Add the infinite construction line that mirrors a onto b.

Crossing lines: the bisector mapping a onto b best; parallel lines: the midline; points: their perpendicular bisector; circles and arcs: that of the centres, whatever radii drawn. The line is free to move and kept there by symmetry constraints: on the lines’ carriers (never equal lengths), the points, or the centres of the circles or arcs (with equal radii, never their ends).

Parameters:  
- **a** – a line, point, circle or arc.

- **b** – one of the same kind.

- **reverse** – of two crossing lines the other bisector.

- **id** – the line’s id, default SymmetryLine.N.

Raises:  
- **ValueError** – a and b are not two points, two lines or two circles, or two circles or arcs share their centre.

- **TypeError** – an unknown keyword.

Examples

``` python
sk.SymmetryLine("Line.1", "Line.3")
sk.SymmetryLine("Circle.1", "Circle.2", id="mid")
```

ToHV(*point: Point3*, */*) → tuple\[float, float\]\
ToHV(*points: list\[Point3\]*, */*) → list\[tuple\[float, float\]\]  
The plane coordinates (h, v) of the projection of a 3d point onto its plane, or of a list of points.

Parameters:  
- **point** – (x, y, z).

- **points** – a list of (x, y, z).

Examples

``` python
sk.ToHV((1, 2, 0))
sk.ToHV([(0, 0, 0), (1, 2, 0)])
```

ToOCCFace(*fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → occ.Face \| occ.Compound  
The region of the drawn elements as an OCC face, a compound of faces if it has several parts, made from a cleaned copy.

Parameters:  
- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges, False for a wire export. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
**ValueError** – in mode=’strict’ while the sketch can still move.

Examples

``` python
sk.ToOCCFace()
sk.ToOCCFace(fit_arcs=True)
```

ToOCCShape(*fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → occ.Face \| occ.Compound  
The same as ToOCCFace: the region as an OCC face, a compound of faces if it has several parts.

Parameters:  
- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges, False for a wire export. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
**ValueError** – in mode=’strict’ while the sketch can still move.

Examples

``` python
sk.ToOCCShape()
sk.ToOCCShape(remove_faces_smaller=0.01)
```

ToOCCWire(*approximation: bool = False*, *fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*, *unify: bool = True*) → occ.Wire  
The one wire of the drawn edges, Wire(unify=unify), as an OCC wire, made from a cleaned copy.

Parameters:  
- **approximation** – splines as their arcs.

- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges, False for a wire export. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

- **unify** – as for Wire().

Raises:  
**ValueError** – as Wire().

Examples

``` python
sk.ToOCCWire()
sk.ToOCCWire(unify=False, fit_arcs=True)
```

ToOCCWires(*approximation: bool = False*, *fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*, *unify: bool = True*) → occ.Compound  
The compound of the OCC wires of Wires(unify=unify), made from a cleaned copy.

Parameters:  
- **approximation** – splines as their arcs.

- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges, False for a wire export. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

- **unify** – as for Wires().

Raises:  
**ValueError** – as Wires().

Examples

``` python
sk.ToOCCWires()
sk.ToOCCWires(unify=False)
```

ToSVG(*size: float = 300*, *\*\*options: Any*) → str \| None  
The object as the text of an SVG picture in its plane coordinates, its faces filled.

A sketch is drawn in its own look: every element in the colour of its state (green fully constrained, black free, pink over-constrained) with its id when hovered, construction dashed, in a frame with numbered ticks on a grid.

Parameters:  
- **obj** – a Face, Shape, Wire, Compound, sketch or DeformableShape.

- **size** – the larger side of the picture in pixels.

- **options** – for a sketch: show_constraints=True adds a sign per constraint, show_ends=True a blue start and an orange end piece on every line and arc, show_direction=True an arrow from start to end; other objects ignore them.

Returns:  
the SVG text; None if the object has no extent.

Examples

``` python
face.ToSVG()
sk.ToSVG(size=500, show_constraints=True)
```

ToShapely(*tol: float = 0.001*) → Any  
Converts to a shapely geometry in plane coordinates, arcs and splines sampled within tol.

Faces that share an edge (a glued Shape) give a GeometryCollection of their polygons, since a MultiPolygon must not have parts sharing an edge.

Parameters:  
- **obj** – the Face, Shape, Wire, sketch (its Shape()) or Compound.

- **tol** – the largest distance of a chord from its arc or spline, a length. Where a face is thinner than tol its polygon is sampled again finer (to tol/1000), with a warning; still invalid, it is made valid (make_valid).

Returns:  
a Polygon or MultiPolygon for a Face, a Shape of faces or a sketch; a LineString or MultiLineString for a Wire or a Shape of wires; a GeometryCollection for a Compound or a Shape of both.

Examples

``` python
face.ToShapely()
px.ToShapely(shape, tol=1e-4)
```

ToXYZ(*point: Point*, */*) → tuple\[float, float, float\]\
ToXYZ(*points: list\[Point\]*, */*) → list\[tuple\[float, float, float\]\]  
The 3d coordinates of a point (h, v) of its plane, or of a list of points.

Parameters:  
- **point** – (h, v) in the plane.

- **points** – a list of (h, v).

Examples

``` python
sk.ToXYZ((1, 2))
sk.ToXYZ([(0, 0), (1, 2)])
```

Translate(*d: tuple\[float, float\]*) → [Sketch](#planex.Sketch "planex.Sketch")\
Translate(*dh: float*, *dv: float*) → [Sketch](#planex.Sketch "planex.Sketch")\
Translate(*d: tuple\[float, float, float\]*) → [Sketch](#planex.Sketch "planex.Sketch")\
Translate(*dx: float*, *dy: float*, *dz: float*) → [Sketch](#planex.Sketch "planex.Sketch")  
Moves it in place by d: in the plane by (dh, dv), in 3d by (dx, dy, dz).

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **d** – the displacement, (dh, dv) in the plane or (dx, dy, dz) in 3d.

- **dh** – the displacement along the plane’s first axis.

- **dv** – the displacement along the plane’s second axis.

- **dx** – the displacement along x in 3d.

- **dy** – the displacement along y in 3d.

- **dz** – the displacement along z in 3d.

Returns:  
this Sketch.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
sk.Translate((10, 0))
sk.Translate(0, 0, 5)
```

Translated(*d: tuple\[float, float\]*, *\**, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")\
Translated(*dh: float*, *dv: float*, *\**, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")\
Translated(*d: tuple\[float, float, float\]*, *\**, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")\
Translated(*dx: float*, *dy: float*, *dz: float*, *\**, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")  
A copy moved by d: in the plane by (dh, dv), in 3d by (dx, dy, dz); this Sketch is not changed.

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **d** – the displacement, (dh, dv) in the plane or (dx, dy, dz) in 3d.

- **dh** – the displacement along the plane’s first axis.

- **dv** – the displacement along the plane’s second axis.

- **dx** – the displacement along x in 3d.

- **dy** – the displacement along y in 3d.

- **dz** – the displacement along z in 3d.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
sk.Translated((10, 0))
sk.Translated(0, 0, 5)
```

Trim(*a: Id \| tuple\[Id, Point\]*, *b: Ids \| None = None*, */*, *\**, *reverse: bool = False*) → [Sketch](#planex.Sketch "planex.Sketch")  
Cut a where it meets b and make one piece of it a construction element (‘Line.3.trimmed.1’).

The cut points stay on b. One piece left keeps a’s id; a cut out of the middle leaves ‘Line.3.1’, ‘Line.3.2’, and ‘Line.3’ is the wire of the pieces, which what referred to Line.3 resolves through. Without b and with nothing to cut at, all of a becomes construction.

Parameters:  
- **a** – the element; (id, point) picks the piece nearest the point ((h, v) or a point id), else the piece nearest the pen is trimmed.

- **b** – what cuts it: an id, a list or a selection; default every other line, arc and circle, construction ones included (RectangleC’s diagonal cuts a line through the rectangle’s center).

- **reverse** – trim the next nearest piece.

Raises:  
- **TypeError** – a keyword other than reverse (near=: the point goes with a).

- **ValueError** – a names a wire or group, not one element.

Examples

``` python
sk.Trim("Line.3", "Line.5")
sk.Trim(("Line.3", (4, 0)), "Line.5", reverse=True)
```

Turn(*angle: float*) → [Sketch](#planex.Sketch "planex.Sketch")  
Turn the pen’s direction.

Parameters:  
**angle** – the turn in degrees, positive counterclockwise.

Examples

``` python
sk.Line(5).Turn(90).Line(5)
```

Update(*values: dict\[str, float\] \| None = None*, *steps: int = 1*) → [Sketch](#planex.Sketch "planex.Sketch")  
Solve with the set values, walked there in steps, smaller ones where the geometry would jump (to stay in the assembly it was drawn in).

Parameters:  
- **values** – more values to set, {id: value}.

- **steps** – the fewest steps.

Raises:  
**ValueError** – there is no solution; nothing changes.

Examples

``` python
sk.Update()
sk.Update({"w": 12, "phi": 30}, steps=10)
```

Vertex(*id: str*, *point: tuple\[float, float\] \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch")  
Add a construction point; it is constrained like any point.

Parameters:  
- **id** – the point’s id.

- **point** – where it is, (h, v) or sk.pen; default the pen’s position.

Raises:  
- **ValueError** – id is taken or no valid id.

- **TypeError** – point is no (h, v).

Examples

``` python
sk.Vertex("A", (3, 4))
sk.Line(5).Vertex("tip")
```

Wire(*element: Id \| None = None*, */*, *\**, *unify: bool = True*, *id: str \| None = None*) → [Wire](geometry.md#planex.Wire "planex.Wire")  
The drawn edges as one Wire, its ends within rounding joined; construction elements take no part. With element, that element’s solved edges (a line or arc one edge, a circle one circle edge, a spline its pieces) or a wire id’s elements in one chain.

Parameters:  
- **element** – an element, or a wire id (Chain.1, an id_wire); None the whole sketch.

- **unify** – for the whole sketch: True cuts the drawn edges at crossings and T-junctions, keeps an overlapping stretch once and merges the pieces on one carrier where nothing else meets (as wire + wire); False keeps every drawn element uncut.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – nothing is drawn, the edges form several wires (sk.Wires() returns them as a list), or, with unify=False or an element, the chain crosses, touches or overlaps itself; in mode=’strict’ while the sketch can still move.

Examples

``` python
sk.Wire()
sk.Wire("Chain.1")
```

Wires(*unify: bool = True*, *\**, *id: str \| None = None*) → list\[[Wire](geometry.md#planex.Wire "planex.Wire")\]  
The drawn edges as a list of Wire, each one chain in traversal order; ends within rounding are joined, a larger gap stays open; construction elements take no part.

Parameters:  
- **unify** – True (as px.Wires) cuts the drawn edges at crossings and T-junctions, keeps an overlapping stretch once and merges the pieces on one carrier where nothing else meets: an outline with a rib ending on it is one closed wire and the rib its own. False keeps every drawn element uncut (an overlap stays twice) and joins them at their ends.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – with unify=False a chain crosses, touches or overlaps itself (the message names the two elements); in mode=’strict’ while the sketch can still move.

Examples

``` python
sk.Wires()
sk.Wires(unify=False)
```

WriteBrep(*path: str \| os.PathLike*, */*, *\*\*options: Any*) → None  
Writes the OCC export (obj.ToOCCShape) as a BREP file.

Parameters:  
- **obj** – the Face, Shape, Wire, sketch or Compound to write.

- **path** – the file to write.

- **options** – the options of obj.ToOCCShape (fit_splines, fit_arcs, remove_edges_smaller, …).

Examples

``` python
face.WriteBrep("part.brep")
px.WriteBrep(shape, "part.brep", fit_arcs=True)
```

WriteDXF(*path: str \| os.PathLike*, */*, *unit: float = 1.0*) → None  
Writes the edges as LINE, ARC and SPLINE entities of a DXF file, each edge once.

An edge’s name becomes its layer (the characters a layer name cannot have, such as / and :, become \_, with a warning), its col the true colour. The coordinates are divided by unit and \$INSUNITS names it.

Parameters:  
- **obj** – the Face, Shape, Wire, sketch or Compound to write.

- **path** – the file to write.

- **unit** – the length the file counts in (px.m, px.mm, px.inch, …).

Raises:  
- **TypeError** – unit is a name, not a length.

- **ValueError** – unit is not a positive finite length.

Examples

``` python
face.WriteDXF("part.dxf", unit=px.mm)
px.WriteDXF(shape, "part.dxf")
```

WriteGeo(*path: str \| os.PathLike*, */*) → None  
Writes the edges and faces as a gmsh .geo file.

Points carry the maxh of their edges and faces as the characteristic length; lines, circle arcs (split at half a circle) and splines as B-splines through planex’s approximation (not exact: .geo has no NURBS), every curve once; plane surfaces with their holes. Physical groups: a curve group per edge name, a surface group per face name, the unnamed edges and faces in a curve and a surface group named “unnamed” (gmsh writes only elements in a group once there is one, and netgen’s ReadGmsh needs every group named and the boundary segments). Periodic identifications become Periodic Curve.

Parameters:  
- **obj** – the Face, Shape, Wire, sketch or Compound to write.

- **path** – the file to write.

Examples

``` python
face.WriteGeo("part.geo")
px.WriteGeo(shape, "part.geo")
```

WriteStep(*path: str \| os.PathLike*, */*, *\*\*options: Any*) → None  
Writes the OCC export (obj.ToOCCShape) as a STEP file.

Parameters:  
- **obj** – the Face, Shape, Wire, sketch or Compound to write.

- **path** – the file to write.

- **options** – the options of obj.ToOCCShape (fit_splines, fit_arcs, remove_edges_smaller, …).

Examples

``` python
face.WriteStep("part.step")
px.WriteStep(shape, "part.step", fit_arcs=True)
```

*property* auto_constraint  
bool

Whether the drawing helpers (to, tangent=, …) also become constraints of what they draw (not set while the constraints are disabled; a disabled sketch’s helpers constrain nothing).

Type:  
auto_constraint

*property* bounding_box  
tuple\[tuple\[float, float\], tuple\[float, float\]\] \| None

Its bounding box in the plane, ((hmin, vmin), (hmax, vmax)); None if it is empty.

Type:  
bounding_box

*property* chain_start  
tuple\[float, float\]

Where the chain being drawn started, (h, v): Close() goes back there.

Type:  
chain_start

*property* condition  
float

The most a small change of the dimensions can move the geometry, relative to it; large (above config.sensitivity_warning) means nearly singular.

Type:  
condition

*property* constraints  
dict

The constraints by kind, each id with what made it.

Type:  
constraints

*property* dimensions  
dict\[str, float\]

Every dimension with its value, {id: value}: references measured, those an expression drives as it gives them, and the patterns’ counts.

Type:  
dimensions

*property* dof  
int

How many ways the geometry can still move.

Type:  
dof

*property* elements  
Elements

The ids of the sketch by kind (Elements), in the order made.

Type:  
elements

*property* error_on_branch_change  
bool

Whether a solve that turns a line round or an arc to its other side (it leaves the branch the sketch was drawn in, even in the smallest steps Update takes) is an error that changes nothing, else a warning; on by default in mode=’strict’.

Type:  
error_on_branch_change

*property* expressions  
dict\[str, str\]

The expressions, {id: the text as given}.

Type:  
expressions

*property* id  
str

The object’s id: the kind that made it and a number, given when first asked for (Sketch.1, Shape.3), or set (an ASCII letter, then letters, digits, \_ and single inner spaces); its elements’ ids follow it (Shape.3.Line.2).

Type:  
id

*property* jacobian  
dict

The solver’s Jacobian at the geometry now, as a dict.

‘matrix’: the derivatives of the residuals (rows) with respect to the solver’s scaled unknowns (columns), a scipy.sparse.csr_array when scipy is installed, else its arrays (data, indices, indptr), as scipy.sparse.csr_array((data, indices, indptr), shape=shape) takes them. ‘shape’: (rows, columns). ‘rows’: per row (constraint id, row in it); an arc’s own equations (arc id, row); the equation that holds a dimension an expression uses (dimension id, ‘value’). ‘columns’: per unknown (reference, coordinate), as (‘Line.1.end’, ‘x’), (‘Circle.1’, ‘r’), (‘Spline.1.p2’, ‘angle’), a linked dimension’s (‘w’, ‘value’) or a constraint’s own (‘RigidGroup.1’, ‘param 0’). ‘scale’: per column the unknown per scaled unknown: 1 for a length, 180 / (pi x the size of the sketch) for an angle (in degrees); matrix\[:, k\] / scale\[k\] is the derivative per unit (per degree) of unknown k. ‘row_scale’: per row the change of its residual per unit of its value: 1 for a length dimension and a row without a value; per degree for an angle dimension: its lever (the shortest element it relates) x pi/180, the size of the sketch x pi/180 on the ‘value’ row of a linked one or on a pattern’s angle.

The change of every unknown per unit (per degree) of the dimension in row i is scale \* spsolve(matrix, row_scale\[i\] \* e_i).

Reading it raises ValueError while the constraints are disabled.

How far every unknown moves per degree of the dimension phi, with scipy:

> from scipy.sparse.linalg import spsolve J = sk.jacobian i = J\[‘rows’\].index((‘phi’, 0)) e = np.zeros(J\[‘shape’\]\[0\]) e\[i\] = J\[‘row_scale’\]\[i\] per_degree = dict(zip(J\[‘columns’\], J\[‘scale’\] \* spsolve(J\[‘matrix’\].tocsc(), e))) per_degree\[(‘crank.end’, ‘x’)\] \# how far crank.end moves in x per degree of phi

Type:  
jacobian

*property* locations  
dict\[str, tuple\[float, float\]\]

Every point reference with where it is, solved, {ref: (h, v)}.

Type:  
locations

*property* measures  
dict

The standard measurements and the stored ones, evaluated now, as a dict.

Type:  
measures

*property* mode  
str

The mode, ‘normal’ or ‘strict’: setting ‘strict’ also switches error_on_branch_change on (not while the constraints are disabled).

Type:  
mode

*property* origin  
str

The id of the sketch’s origin.

Type:  
origin

*property* pen  
SketchPen

The pen as it is now, a snapshot (sk.pen.position, sk.pen.direction); a drawing call takes it where a point (its position) or a direction (its direction) is asked for.

Type:  
pen

*property* points  
dict\[str, tuple\[float, float\]\]

The points with ids given to them, {id: (h, v)}.

Type:  
points

*property* redundant  
list\[str\]

The ids of the constraints that follow from the others (consistently: the sketch is solved).

Type:  
redundant

*property* status  
str

Whether the constraints fix everything: ‘full’, or ‘under’ (it can still move), or ‘over’ while a constraint that contradicts the others is kept but not solved; redundant constraints are in redundant.

Type:  
status

*property* wires  
dict\[str, list\[str\]\]

The wires, {wire id: \[element ids\]}: every pen chain (Chain.1, Chain.2, …, or its id_wire), every Rectangle and every Offset.

Type:  
wires

&nbsp;

Move(*start: Point*, */*, *\*flags: Constraint*, *plane: Plane \| None = None*, *dir: tuple\[float*, *float\] \| None = None*, *auto_constraint: bool = False*, *error_on_branch_change: bool \| None = None*, *mode: str = "normal"*, *id: str \| None = None*, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch") \| [QuickSketch](#planex.QuickSketch "planex.QuickSketch")\
Move(*h: float*, *v: float*, */*, *\*flags: Constraint*, *plane: Plane \| None = None*, *dir: tuple\[float*, *float\] \| None = None*, *auto_constraint: bool = False*, *error_on_branch_change: bool \| None = None*, *mode: str = "normal"*, *id: str \| None = None*, *id: str \| None = None*) → [Sketch](#planex.Sketch "planex.Sketch") \| [QuickSketch](#planex.QuickSketch "planex.QuickSketch")  
A new sketch with its pen at a start point: of px.config.default_sketch_type, a QuickSketch by default.

Parameters:  
- **start** – the pen’s start (h, v), or (x, y, z) on the xy plane (z = 0).

- **h** – the start’s h; with v.

- **v** – the start’s v.

- **flags** – as for the sketch’s Move.

- **plane** – the plane it draws on, as for Sketch(); default the xy plane.

- **dir** – the drawing direction (dh, dv) from the start.

- **auto_constraint** – as for Sketch().

- **error_on_branch_change** – as for Sketch().

- **mode** – as for Sketch().

- **id** – the sketch’s id.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
- **TypeError** – no start point.

- **ValueError** – auto_constraint=True or mode=’strict’ for a QuickSketch; dir has no length.

Examples

``` python
px.Move((0, 0)).Line(10).Turn(90).Line(5)
px.Move(0, 0, dir=(0, 1), id="rib")
```

## Constraints and dimensions

*class* Constraint  
The kinds of constraints (px.Distance, …); the geometric ones are also flags of the drawing calls.

Members:

> Coincident
>
> Horizontal
>
> Vertical
>
> Parallel
>
> Perpendicular
>
> Tangent
>
> Equal
>
> Symmetric
>
> Concentric
>
> Fix
>
> Distance
>
> Radius
>
> Diameter
>
> Angle
>
> SymmetricCarriers
>
> RigidGroup

&nbsp;

Coincident\
Horizontal\
Vertical\
Parallel\
Perpendicular\
Tangent\
Equal\
Symmetric\
SymmetricCarriers\
Concentric\
Fix\
RigidGroup  
The geometric constraints, for `Sketch.Constrain` and as flags of the drawing calls.

&nbsp;

Distance\
Angle\
Radius\
Diameter  
The dimensions, for `Sketch.Constrain` with a value.

## References

H\
V\
origin  
The horizontal and vertical axis of a sketch and the global origin, as references for constraints.

&nbsp;

first\
last  
For `until=` of a sketch’s `Line` and `Arc`: until the first or the last element ahead.
