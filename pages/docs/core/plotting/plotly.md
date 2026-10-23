# plotly

With `backend="plotly"`, `px.Plot` draws an interactive figure that can be zoomed and panned, and returns the plotly
Figure. Passing the Figure as `ax` adds more objects to it, as with matplotlib. Sketch plots also show the id of the
element under the mouse, and field plots the value there. See [Sketch plots](sketches.md) and [Fields](fields.md) for
details.

The example is the bell crank from [matplotlib](matplotlib.md):

```python
import planex as px

bosses = [px.Circle((0, 0), 9).Face(), px.Circle((40, 0), 6).Face(), px.Circle((0, 30), 6).Face()]
arms = [px.Line((0, 0), (40, 0)).Offset(4), px.Line((0, 0), (0, 30)).Offset(4)]
holes = [px.Circle((0, 0), 4).Face(), px.Circle((40, 0), 3).Face(), px.Circle((0, 30), 3).Face()]
crank = px.Subtract([px.Unify(bosses + arms), *holes])

fig = crank.Plot(backend="plotly", face_col="lightsteelblue", legend=False)
fig
```

*[interactive plotly figure; see the html page]*

## A second position

The bell crank, swung by 30° about its pivot, is added to the same figure:

```python
crank.Rotated((0, 0), 30).Plot(ax=fig, face_col="wheat", legend=False)
```

*[interactive plotly figure; see the html page]*

## Saving

`write_html` saves the figure as an HTML file that opens in any browser and stays interactive:

```python
fig.write_html("bell-crank.html")
```
