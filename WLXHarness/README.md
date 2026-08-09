# WLXHarness

WLXHarness loads a WLX plugin in a Total Commander-like parent window. It supports interactive file/reload testing and automated 3D regression cycles.

```text
WLXHarness.exe <plugin.wlx|plugin.wlx64> <track.gpx|fit|kml|kmz>
WLXHarness.exe <plugin> <track> --auto3d <mode> <cycles> <log-file>
```

Automated modes:

- `1`: perspective renderer
- `2`: real terrain renderer
- `0`: expect terrain-provider failure and safe 2D fallback

Each normal cycle verifies flat-2D startup, enters the requested 3D mode, exercises profile mouse selection/double-click, fit, slope colouring, tile toggles, map-style cycling, resize, return to 2D, and reload. The log records process-tree private memory and rejects unbounded cycle-to-cycle growth. The final cycle allows time for WebView2 child-process cleanup.

Use an INI beside the test plugin to select the provider. Isolated plugin/INI folders make it possible to test perspective, Terrarium, MapTiler, and invalid-provider configurations without altering the main development INI.
