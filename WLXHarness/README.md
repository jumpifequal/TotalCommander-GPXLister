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

Each normal cycle verifies flat-2D startup, hovers a profile point without clicking, enters the requested 3D mode, checks the one-time point/guide handoff, and requires the WebView to acknowledge the exact initial DOM marker before any 3D camera movement. It then selects a different profile point, requires a second marker acknowledgement, refreshes the 3D style, explicitly ends the hover, and fails unless both the profile guide and rendered marker clear without reverting to the handoff point. It also exercises fit, slope colouring, tile toggles, map-style cycling, resize, return to 2D, reload, and process-tree memory bounds.

Use an INI beside the test plugin to select the provider. Isolated plugin/INI folders make it possible to test perspective, Terrarium, MapTiler, and invalid-provider configurations without altering the main development INI.
