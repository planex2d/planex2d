# Units, settings and warnings

See [Units](../core/basics/units.md) and [Tolerances](../core/basics/tolerances.md) for the guides.

m\
cm\
mm\
um\
inch\
mil  
Length factors in meters: 1, 0.01, 0.001, 10⁻⁶, 0.0254 and 2.54 · 10⁻⁵.

&nbsp;

*class* config  
The defaults of planex: the absolute tolerance k \* eps \* S (S the largest coordinate magnitude involved), and the fit and cleanup values the OCC exports take by default.

default_sketch_type  
alias of [`QuickSketch`](sketches.md#planex.QuickSketch "planex._planex.QuickSketch")

&nbsp;

*class* PlanexWarning  
A warning of the planex kernel.

&nbsp;

ResetIdCounters() → None  
Starts every kind of auto id at 1 again (Sketch.1, Shape.1, …), so a script run from the top gets the same ids.

Examples

``` python
px.ResetIdCounters()
```
