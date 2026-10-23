// planex sets a plotly figure's size from the drawing's scale, so a drawing can be wider than the page column or
// taller than the other figures (custom.css: 360 px): such figures are laid out again within both, the plot area
// keeping its aspect ratio, and centered like the images
(function () {
  var MAX_HEIGHT = 360;

  function fit(div) {
    var layout = div.layout, area = div.closest(".output_area") || div.closest(".rst-content");
    if (!layout || !layout.width || !area || !window.Plotly) return;
    var prompt = area.querySelector(".prompt");
    var room = area.getBoundingClientRect().width - (prompt ? prompt.getBoundingClientRect().width : 0) - 4;
    var m = layout.margin || {}, l = m.l || 0, r = m.r || 0, t = m.t || 0, b = m.b || 0;
    var scale = Math.min(1, (room - l - r) / (layout.width - l - r));
    if (layout.height > MAX_HEIGHT) scale = Math.min(scale, (MAX_HEIGHT - t - b) / (layout.height - t - b));
    var width = scale < 1 ? (layout.width - l - r) * scale + l + r : layout.width;
    var height = layout.height ? (layout.height - t - b) * scale + t + b : undefined;
    // the wrappers keep the figure's first size (style height and width): they follow the new one
    for (var up = div.parentElement; up && up !== area; up = up.parentElement) {
      up.style.maxWidth = "100%";
      up.style.marginLeft = up.style.marginRight = "auto";
      if (scale < 1) {
        up.style.width = width + "px";
        if (height) up.style.height = height + "px";
      }
    }
    if (scale >= 1) return;
    if (height) div.style.height = height + "px";
    div.style.width = width + "px";
    Plotly.relayout(div, {width: width, height: height});
  }

  // page_speed.py turns each figure's plotting script into type="text/plotly-lazy": plotly.js (from _static/vendor)
  // is loaded once, async, when the first figure comes near the screen, and each figure is drawn when it does
  var base = document.currentScript.src.replace(/custom\.js.*$/, ""), loading;

  function plotly() {
    loading = loading || new Promise(function (done) {
      var s = document.createElement("script");
      s.src = base + "vendor/plotly.min.js";
      s.async = true;
      s.onload = done;
      document.head.appendChild(s);
    });
    return loading;
  }

  function draw(code) {
    plotly().then(function () {
      var s = document.createElement("script");
      s.textContent = code.textContent;
      code.replaceWith(s);
      var div = s.parentElement.querySelector(".plotly-graph-div"), tries = 0;
      (function wait() {
        if (div && div.layout) fit(div);
        else if (div && tries++ < 40) setTimeout(wait, 50);
      })();
    });
  }

  function lazy() {
    var codes = document.querySelectorAll('script[type="text/plotly-lazy"]');
    if (!codes.length) return;
    if (!("IntersectionObserver" in window)) return codes.forEach(draw);
    var seen = new IntersectionObserver(function (entries) {
      entries.forEach(function (e) {
        if (!e.isIntersecting) return;
        seen.unobserve(e.target);
        draw(e.target._code);
      });
    }, {rootMargin: "600px 0px"});
    codes.forEach(function (code) {
      var box = code.parentElement;
      box._code = code;
      seen.observe(box);
    });
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", lazy);
  else lazy();
})();
