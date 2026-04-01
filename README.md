# TotalCommander-GPXLister
A high-performance Lister plugin for Total Commander that renders GPX tracks using Direct2D and WIC, bypassing GDI+ to avoid header conflicts and ensure hardware acceleration.

## Key Features

- **Direct2D Rendering**: Smooth, anti-aliased track lines and high-quality map rendering.
- **Satellite Mode Toggle**: Instantly switch between map providers (e.g., OSM to Google Satellite) using the `T` key.
- **Asynchronous Tiles**: PNG tiles are handled via **WIC** and drawn as D2D bitmaps; background loading ensures the UI remains responsive.
- **Multi-Track Support**: Full support for GPX files with multiple tracks, including individual track names and colours.
- **Resizable Sidebar**: A lateral panel allows for easy track selection; resize the panel by dragging its right boundary.
- **Overlay Information**: Real-time display of cursor coordinates and the active track name.
- **Zero Disk Footprint**: All tile caching and bitmap decoding happen in memory (RAM).
- **Slope-based Track Colouring (optional)**: Progressive colouring of the track polyline based on gradient (slope), designed to remain stable on dense GPX data.

## Build Requirements

- **Environment**: Visual Studio 2022 (v143 toolset), **Win32/x64 Release**
- **Libraries**: `d2d1.lib`, `windowscodecs.lib`, `urlmon.lib`, `wininet.lib`, `msxml6.lib`, `Shell32.lib`, `Shlwapi.lib`

## Installation (Total Commander 10/11+)

1. Copy `GPXLister.wlx` and/or `GPXLister.wlx64` to your plugin directory.
2. Add the DLL as a Lister plugin in Total Commander settings.
3. Recommended Detect String: `EXT="GPX" | (FORCE & FIND("<gpx"))`

## Controls & Shortcuts

- **Zoom**: Mouse wheel or `+`/`-` keys (zooms toward cursor).
- **Pan**: Left-click drag or **Arrow Keys**.
- **Fit to Window**: Press `X` or **Double-click** (fits the selected track or all tracks).
- **Sidebar**: Drag the right edge to resize; select tracks to filter the view.
- **Toggles**:
  - `T`: Satellite Mode on/off (switches between tile servers).
  - `M`: Map tiles on/off.
  - `G`: Grid overlay on/off.
  - `E`: Elevation profile on/off.
  - `T`: speed profile on/off.
  - `S`: Slope-based track colouring on/off.
- **Hover**: Move mouse over the elevation profile to see synchronised crosshairs on the map.

## Configuration (`GPXLister.ini`)

You can place a `GPXLister.ini` file in the same directory as the plugin binaries to customise defaults.

- `showSlopeColouringOnTrack` (int, default `0`): Enables progressive slope-based colouring for the map track polyline.
- `trackLineWidth` (float, default `2.0`): Stroke width for drawing the map track polyline. Values are clamped to a safe range.
- When slope colouring is enabled, the gradient is computed from elevation change over distance and mapped to a blue→green→red ramp, then blended with the per-track base colour.
- Colour changes are computed in distance windows rather than per raw GPX segment to avoid visual noise. Track rendering uses rounded joins/caps to prevent visible seams when colours change.
