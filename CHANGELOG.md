# Changelog

## v2.8.1 - Reliable 3D hover synchronisation

- Added direct track-point hovering in the 3D view: moving over a 3D track updates the marker and altitude/speed profile guide, and the active hover point stays synchronised when switching between 2D and 3D.
- Fixed the 3D hover marker appearing only after a resize or camera drag; the mouse, marker, and altitude/speed profile guide are now aligned as soon as the 3D view opens.
- Kept the 3D marker and altitude/speed profile guide synchronised when hovering a profile or using downsampled tracks.
- Fixed stale or duplicate hover indicators after switching views or moving the pointer outside the 3D view.
- Fixed 3D hover timestamps so they match the source GPX date and local time shown in 2D.
- Kept the 3D marker fully visible across camera angles.
- Prevented isolated terrain-tile download failures from unexpectedly closing an otherwise working 3D session.

## v2.8 - Optional perspective and real 3D terrain

- Added an optional **3D view (D)** context-menu action and `D` shortcut while keeping flat 2D as the startup view; the numeric `3` key remains available to Total Commander for Lister mode switching.
- Added `3d_model=1` for perspective rendering and `3d_model=2` for real DEM terrain; a missing or invalid setting is repaired to `2`.
- Added free Terrarium terrain as the default provider and MapTiler Terrain RGB support through an API key.
- Kept track names, distance/elevation statistics, cursor elevation/speed/time, waypoints, track filtering, slope colouring, altitude/speed profiles, and profile-to-map point selection available in 3D.
- Added Google Earth-style combined navigation: drag/wheel/keyboard pan and zoom, right-drag or `Ctrl`/`Shift`+left-drag rotation/pitch, `N` north-up, `U` top-down, and `F` fit.
- Added safe automatic fallback to flat 2D when WebView2, local assets, or the terrain provider fail.
- Distribution now includes the local MapLibre web assets required by both 3D modes; only the Microsoft WebView2 Evergreen Runtime may need separate installation.
- Fixed 3D shortcut routing (including `T`/`Shift+T`) and added reliable `Ctrl`/`Shift`+left-drag rotation and pitch.
- Fixed black patches caused by overlapping invisible hover targets on dense 3D tracks.
- Added bounded 3D tile/worker caches and release of the 3D renderer when returning to 2D.


## v2.7.1 - Reverse map style cycling

- Added `Shift+T` to cycle backward through the configured map styles, making Standard/Topo comparisons quicker when those styles are adjacent in `mapTypeOrder`.
- Updated distribution metadata to version `2.7.1`.

## v2.7 - Configurable map styles

- Added `mapTypeOrder` to control the startup map style, the `T` keyboard cycle, and the map context-menu order.
- Added named map style endpoints: `standardTileEndpoint`, `satelliteTileEndpoint`, and `topoTileEndpoint`.
- Replaced the context-menu tile-server toggle with a **Map style** submenu for direct style selection.
- Added **Add track file...** to append GPX/FIT/KML/KMZ files temporarily to the current view using the existing multi-track sidebar.
- Fixed tile-cache worker state so WinINet download handles remain thread-local during parallel tile downloads.
- Preserved legacy `tileEndpoint` as the old alias for the standard map style.
- Added safe `mapTypeOrder` repair for unknown, duplicate, empty, and backward-compatible `standard`-only configurations.
- Updated distribution metadata to version `2.7`.

## v2.6 - Clipboard copy and persistent speed profile

- Added `Ctrl+C`, `Ctrl+Ins`, and standard Lister copy support to copy the current Lister view to the clipboard as PNG, also available from the map context menu.
- Added persistent `showSpeedProfile` INI storage for the speed profile toggled with `V` or the context menu.
- Updated distribution metadata to version `2.6`.
- Changed default tile rendering to opentopomap with hill shadows
- Added tiles topo map server rendering samples

## v2.5.1 - Reliable Fit to Window shortcut

- Fixed the `F` shortcut in Total Commander Lister by handling the host-routed fit command through the WLX `ListSendCommand` interface.
- Kept the existing right-click **Fit to window** action and direct map keyboard handling consistent with the host shortcut.

## v2.5 - KMZ import support and recompile via VS 2026

- Recompiled using stable Visual Studio 2026 and no vcpkg to avoid Virus Total warnings
- added support for KMZ (zip compressed KML files)

## v2.4 - KML import support

- Added transparent `.kml` support through hidden `kml2gpx.exe` conversion, with `kmlConverter`, template-based `kmlArgs`, `kmlTimeoutSec`, and unconditional temporary GPX cleanup.
- Extended extension-only detection and installer defaults to `GPX`/`FIT`/`KML`.
- Updated distribution packaging to include `kml2gpx.exe` beside the plugin and existing `Fit2Gpx.exe`.

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
