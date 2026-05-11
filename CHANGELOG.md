# Changelog

## v2.3 - GPX viewing polish and support for .fit files

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

## v2.1 - Security increase

- Increased handling of lock conditions, including very long tracks, non-responding or malformed tile servers, tile-server failures, and switching modes during tile downloads.
- Added a recap window with track statistics, also invoked with the `I` key.

## v2.0 - High-DPI Support & Precision Update

- Added full High-DPI (Per-Monitor V2) support. The plugin now renders crisp text and lines at any Windows scaling factor, including 125%, 150%, and 175%+, without blurring or virtualization.
- Fixed critical mouse misalignment / cursor drift. The red alignment circle now stays under the mouse cursor regardless of zoom level or screen scaling.
- Fixed the Elevation Profile window appearing flattened or invisible on high-resolution screens by enforcing correct monitor-aware scaling.
- Improved the Fit to Window algorithm, originally bound to the `X` key, with tighter margins and corrected vertical centering logic for a better track view.
- Switched internal coordinate systems to floating-point precision to prevent sub-pixel rounding errors during panning and zooming.
- Implemented dynamic linking for modern DPI APIs to ensure continued stability on older systems such as Windows 7.

## v1.9 - Functions and fixes

- Added Speed Profile support with the `V` key.
- Added locale-independent parsing for speed data.
- Added support for ISO 8601 time format used by GPX files.
- Improved mouse interactions:
  - smooth zoom on scroll wheel and touchpad;
  - double-click on the map to zoom in;
  - double-click on the profile to jump to the corresponding track location.
- Added smart smoothing for speed data to reduce GPS noise.
- Fixed elevation calculation so ascent and descent values are correct.
- Added maximum and minimum elevation to the Elevation window.
- Added time information to tooltips, showing when the track point was reached.
- Improved tooltip colouring in Satellite Mode.

## v1.8

- Added slope-based track colouring.
- Added configurable track stroke width.

## v1.7

- Added support for waypoints.

## v1.6

- Added mouse contextual menu.

## v1.5

- Added Satellite Mode toggle with the `T` key.
- Added customisable `satelliteTileEndpoint` in `GPXLister.ini`.
- Improved Google Maps tile support.
- Added mouse hover on the track window.

## v1.4

- Added multi-track support.
- Added resizable sidebar.
- Added track name overlays.

## v1.3

- Added elevation profiles.
- Added support for multiple track colours.

## v1.0

- Initial public release.
