# GPXLister (WLX) for Total Commander

A high-performance Lister plugin for Total Commander that renders GPX, FIT, KML, and KMZ tracks using Direct2D and WIC, bypassing GDI+ to avoid header conflicts and ensure hardware acceleration.

**Note**: binaries are falsely marked with some malicious signatures by VirusTotal. This is because of 3 reasons: I used v145 libraries from VS 2026, I compressed the program, and the program fetches online map tiles and elevation.

## Key Features

- **Direct2D Rendering**: Smooth, anti-aliased track lines and high-quality map rendering.
- **FIT Support**: `.fit` files are converted transparently through `Fit2Gpx.exe` into a temporary GPX file and cleaned up after loading.
- **KML/KMZ Support**: `.kml` and `.kmz` files are converted transparently through `kml2gpx.exe` into a temporary GPX file and cleaned up after loading.
- **Satellite Mode Toggle**: Instantly switch between map providers (e.g., OSM to Google Satellite) using the `T` key.
- **Asynchronous Tiles**: PNG tiles are handled via **WIC** and drawn as D2D bitmaps; background loading ensures the UI remains responsive.
- **Multi-Track Support**: Full support for GPX files with multiple tracks, including individual track names and colours.
- **Resizable Sidebar**: A lateral panel allows for easy track selection; resize the panel by dragging its right boundary.
- **Overlay Information**: Real-time display of cursor coordinates and the active track name.
- **Zero Disk Footprint**: All tile caching and bitmap decoding happen in memory (RAM).
- **Altitude/Speed Profiles**: Toggle altitude and speed profiles independently; speed uses its own scale and a configurable blue profile colour.
- **Robust Summaries**: The `I` information dialog uses smoothed elevation, robust sustained slope windows, stitched moving time for patched tracks, and a DPI-aware card layout.
- **Slope-based Track Colouring (optional)**: Progressive colouring of the track polyline based on gradient (slope), designed to remain stable on dense GPX data.

## Build Requirements

- **Environment**: Visual Studio 2026 distribution with the `v145` toolset, **Win32/x64 Release**. 
  - **NOTE:** v143 is supported too, just change vsproj files
- **Output folders**: Win32 builds write to `x32\<Configuration>\`; x64 builds write to `x64\<Configuration>\`.
- **Libraries**: `d2d1.lib`, `windowscodecs.lib`, `urlmon.lib`, `wininet.lib`, `msxml6.lib`, `Shell32.lib`, `Shlwapi.lib`, `gdi32.lib`

## Installation (Total Commander 10/11+)

1. Copy `GPXLister.wlx` and/or `GPXLister.wlx64` to your plugin directory, together with `Fit2Gpx.exe` if you want `.fit` support and `kml2gpx.exe` if you want `.kml` and `.kmz` support.
2. Add the DLL as a Lister plugin in Total Commander settings.
3. Recommended Detect String: `EXT="GPX" | EXT="FIT" | EXT="KML" | EXT="KMZ"`

## Controls & Shortcuts

- **Zoom**: Mouse wheel or `+`/`-` keys (zooms toward cursor).
- **Pan**: Left-click drag or **Arrow Keys**.
- **Fit to Window**: Press `F` or **Double-click** (fits the selected track or all tracks).
- **Sidebar**: Drag the right edge to resize; select tracks to filter the view.
- **Toggles**:
  - `T`: Satellite Mode on/off (switches between tile servers).
  - `M`: Map tiles on/off.
  - `G`: Grid overlay on/off.
  - `E`: Elevation profile on/off.
  - `V`: Speed profile on/off.
  - `S`: Slope-based track colouring on/off.
- **Information**: `I` opens the summary dialog.
- **Hover**: Move the mouse over the map track or profile to see synchronised crosshairs/position lines.

## Configuration (`GPXLister.ini`)

You can place a `GPXLister.ini` file in the same directory as the plugin binaries to customise defaults.

- `showSlopeColouringOnTrack` (int, default `0`): Enables progressive slope-based colouring for the map track polyline.
- `trackLineWidth` (float, default `2.0`): Stroke width for drawing the map track polyline. Values are clamped to a safe range.
- `speedProfileColor` (string, default `#0059F2`): Speed profile colour as `#RRGGBB`.
- `fitConverter` (string, default `Fit2Gpx.exe`): Converter executable for `.fit` files. Relative names are searched in the plugin folder first, then in `PATH`.
- `fitArgs` (string, default `{input} {output} --elevation-dataset srtm30m,eudem25m`): Fit2Gpx command-line template. Supported placeholders are `{converter}`, `{input}`, and `{output}`.
- `fitTimeoutSec` (int, default `60`): Maximum conversion time before the converter is terminated and an error is shown.
- `kmlConverter` (string, default `kml2gpx.exe`): Converter executable for `.kml` and `.kmz` files. Relative names are searched in the plugin folder first, then in `PATH`.
- `kmlArgs` (string, default `{input} {output} --elevation-dataset srtm30m,eudem25m`): kml2gpx command-line template. Supported placeholders are `{converter}`, `{input}`, and `{output}`.
- `kmlTimeoutSec` (int, default `60`): Maximum conversion time before the converter is terminated and an error is shown.
- When slope colouring is enabled, the gradient is computed from elevation change over distance and mapped to a blue→green→red ramp, then blended with the per-track base colour.
- Colour changes are computed in distance windows rather than per raw GPX segment to avoid visual noise. Track rendering uses rounded joins/caps to prevent visible seams when colours change.
