# Changelog

## v2.3 - GPX viewing polish

- Changed Fit to Window to the usual `F` shortcut and removed the old `X` compatibility shortcut.
- Reworked the `I` information dialog with a cleaner DPI-aware card layout and translucent map-integrated presentation.
- Fixed summary calculations for live-recorded GPX files:
  - elapsed time and average speed now ignore large timestamp gaps in patched multi-track data;
  - ascent/descent use smoothed GPX elevation without external elevation correction;
  - sustained slope uses robust distance windows and excludes invalid zero-distance/noisy samples.
- Improved single-track naming so a named single GPX track is shown by name instead of `All tracks`.
- Fixed mouse-wheel zoom anchoring so the map point under the cursor remains stable across DPI scaling.
- Made altitude and speed profiles independently toggleable; `E` toggles altitude and `V` toggles speed.
- Decoupled speed profile scaling from elevation scaling, and changed the default speed profile colour to blue.
- Added `speedProfileColor=#0059F2` to `GPXLister.ini` for user-configurable speed profile colour.
- Added transparent `.fit` support through hidden `Fit2Gpx.exe` conversion, with `fitConverter`, template-based `fitArgs`, `fitTimeoutSec`, and unconditional temporary GPX cleanup.
- Tightened the detect string to extension-only `GPX`/`FIT` matching so unrelated text/config files fall back to Total Commander's normal lister.
- Synced the profile position line from map-track hover and improved overlay contrast with translucent backgrounds.
- Fixed sidebar mouse resizing under DPI scaling.
- Repaired the Visual Studio solution configuration and made build outputs explicit:
  - Win32: `x32\<Configuration>\GPXLister.wlx`
  - x64: `x64\<Configuration>\GPXLister.wlx64`
- Confirmed the project targets the Visual Studio 2026 Insiders `v145` toolset.

## v2.2

- Previous packaged release.
