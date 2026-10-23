# Constraint sketches


[![Jansen&#x27;s walking leg solved by one constraint sketch, the crank angle updated frame by frame](index_files/index_1.gif)<span><em class="px-tag">Example</em> Jansen&#x27;s leg: one sketch, the crank angle updated</span>](../../examples/jansen-linkage.md)
[![A horizontal tank with its dipstick: every mark solved so that 10 % of the volume lies between two marks](index_files/index_2.svg)<span>A dipstick: every mark solved for 10 % of the volume</span>](../../core/constraints/formulas.md)
[![A plate with a fillet and a hole, fully constrained, every constraint shown by its sign](index_files/index_3.svg)<span>A plate, fully constrained, with every constraint shown</span>](../../core/constraints/kinds.md)


A constraint sketch, `px.Sketch`, supports every drawing call of `px.QuickSketch`: lines, arcs, circles and splines
from references, construction elements, offsets, trimming and mirroring (see [Quick sketch](../sketch/index.md)). In
addition, it keeps constraints that turn a rough drawing into an exact one, such as a line that must be horizontal, an
arc that must be tangent, or a hole 8 in diameter and 40 from the center. planex2d solves these constraints, moves
every element as little as they allow, and shows what is still free.

Every dimension has an id, and setting a dimension solves the sketch again, so the sketch becomes a parametric part.
Expressions tie dimensions together as design rules. The same solver also moves mechanisms: given a crank angle, it
returns the position of every joint. The solved sketch exports like any other, as faces, wires, OpenCASCADE shapes and
netgen geometries.

<p class="px-label">In this section</p>

## Constraining

- [Drawing with constraints](drawing.md)
- [Converting sketches](converting.md)
- [Constraints and dimensions](kinds.md)
- [Fully, under- and over-constrained](diagnosis.md)


[![Drawing with constraints](index_files/index_4.svg)<b>Drawing with constraints</b><span>Flags in the drawing calls, helpers as constraints, and how the drawing decides which tangent is meant.</span>](drawing.md)
[![Converting sketches](index_files/index_5.svg)<b>Converting sketches</b><span>A quick sketch turned into a constraint sketch with the constraints it shows, and back, and a sketch from a solid&#x27;s face.</span>](converting.md)
[![Constraints and dimensions](index_files/index_6.svg)<b>Constraints and dimensions</b><span>Every kind of constraint, reference dimensions, and how the sketch shows them.</span>](kinds.md)
[![Fully, under- and over-constrained](index_files/index_7.svg)<b>Fully, under- and over-constrained</b><span>Finding what is still free, what is redundant and what conflicts.</span>](diagnosis.md)


## Changing the design

- [Changing dimensions](update.md)
- [Expressions: design rules](expressions.md)
- [Formulas: a dipstick for a tank](formulas.md)


[![Changing dimensions](index_files/index_8.svg)<b>Changing dimensions</b><span>A parameter study with Update, walking in steps, and branch changes.</span>](update.md)
[![Expressions: design rules](index_files/index_9.svg)<b>Expressions: design rules</b><span>Equations between dimensions, so that one number drives a family of parts.</span>](expressions.md)
[![Formulas: a dipstick for a tank](index_files/index_10.svg)<b>Formulas: a dipstick for a tank</b><span>Every mark of a dipstick solved for an equal share of the volume.</span>](formulas.md)


## More geometry

- [Symmetry and patterns](symmetry.md)
- [Splines](splines.md)
- [Trim, split and project](trim.md)
- [Offsets and profiles revisited](offsets-profiles.md)


[![Symmetry and patterns](index_files/index_11.svg)<b>Symmetry and patterns</b><span>Mirrored halves, symmetry lines and circular patterns.</span>](symmetry.md)
[![Splines](index_files/index_12.svg)<b>Splines</b><span>Splines with constraints, from a bottle to a cam and its roller.</span>](splines.md)
[![Trim, split and project](index_files/index_13.svg)<b>Trim, split and project</b><span>Cuts that follow the dimensions, and OpenCASCADE geometry as references.</span>](trim.md)
[![Offsets and profiles revisited](index_files/index_14.svg)<b>Offsets and profiles revisited</b><span>Slot widths, fillet radii and offset distances as dimensions, with offsets that follow their sources.</span>](offsets-profiles.md)


## Beyond drawings

- [Mechanisms: the sketch as a solver](mechanisms.md)


[![Mechanisms: the sketch as a solver](index_files/index_15.svg)<b>Mechanisms: the sketch as a solver</b><span>A four-bar linkage and its coupler curve, solved position by position.</span>](mechanisms.md)
