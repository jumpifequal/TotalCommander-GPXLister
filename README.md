# GPXLister (WLX) for Total Commander

A high-performance Lister plugin for Total Commander that renders GPX, FIT, KML, and KMZ tracks using Direct2D and WIC, bypassing GDI+ to avoid header conflicts and ensure hardware acceleration.

**Note**: binaries are falsely marked with some malicious signatures by VirusTotal. This is because of 3 reasons: I used v145 libraries from VS 2026, I compressed the program, and the program fetches online map tiles and elevation.

**Latest release:** `v2.8` adds optional perspective and real DEM terrain views while preserving the native flat 2D startup view.

## Key Features

- **Direct2D Rendering**: Smooth, anti-aliased track lines and high-quality map rendering.
- **Optional 3D Rendering**: Press `D` for perspective or real terrain according to `3d_model`; Terrarium works without a key and MapTiler is supported with an API key.
- **FIT Support**: `.fit` files are converted transparently through `Fit2Gpx.exe` into a temporary GPX file and cleaned up after loading.
- **KML/KMZ Support**: `.kml` and `.kmz` files are converted transparently through `kml2gpx.exe` into a temporary GPX file and cleaned up after loading.
- **Map Style Cycle**: Switch forward through configured map styles with `T`, backward with `Shift+T`, or choose a style directly from the map context menu.
- **Asynchronous Tiles**: PNG tiles are handled via **WIC** and drawn as D2D bitmaps; background loading ensures the UI remains responsive.
- **Multi-Track Support**: Full support for GPX files with multiple tracks, including individual track names and colours.
- **Temporary Track Append**: Add extra GPX/FIT/KML/KMZ files to the current view from the map context menu.
- **Resizable Sidebar**: A lateral panel allows for easy track selection; resize the panel by dragging its right boundary.
- **Overlay Information**: Real-time display of cursor coordinates and the active track name.
- **Zero Disk Footprint**: All tile caching and bitmap decoding happen in memory (RAM).
- **Altitude/Speed Profiles**: Toggle altitude and speed profiles independently; speed uses its own scale and a configurable blue profile colour.
- **Copy View to Clipboard**: Capture the current visible Lister view as PNG with `Ctrl+C`, `Ctrl+Ins`, the standard Lister copy command, or the map context menu.
- **Robust Summaries**: The `I` information dialog uses smoothed elevation, robust sustained slope windows, stitched moving time for patched tracks, and a DPI-aware card layout.
- **Slope-based Track Colouring (optional)**: Progressive colouring of the track polyline based on gradient (slope), designed to remain stable on dense GPX data.

## Build Requirements

- **Environment**: Visual Studio 2026 distribution with the `v145` toolset, **Win32/x64 Release**. 
  - **NOTE:** v143 is supported too, just change vsproj files
- **Output folders**: Win32 builds write to `x32\<Configuration>\`; x64 builds write to `x64\<Configuration>\`.
- **Libraries**: `d2d1.lib`, `windowscodecs.lib`, `urlmon.lib`, `wininet.lib`, `msxml6.lib`, `Shell32.lib`, `Shlwapi.lib`, `gdi32.lib`

## Installation (Total Commander 10/11+)

1. Copy `GPXLister.wlx` and/or `GPXLister.wlx64` to your plugin directory. Keep the built `web` folder beside the plugin for 3D support. Add `Fit2Gpx.exe` for `.fit` support and `kml2gpx.exe` for `.kml`/`.kmz` support.
2. Add the DLL as a Lister plugin in Total Commander settings.
3. Recommended Detect String: `EXT="GPX" | EXT="FIT" | EXT="KML" | EXT="KMZ"`

3D mode uses the Microsoft Edge WebView2 Runtime. Current Windows 10/11 installations normally already include it; otherwise install the Evergreen Runtime from [Microsoft's WebView2 page](https://developer.microsoft.com/en-us/microsoft-edge/webview2). GPXLister's native loader is linked into the plugin, so no separate `WebView2Loader.dll` is needed. If the runtime is unavailable, GPXLister remains in flat 2D.

## Controls & Shortcuts

- **Zoom**: Mouse wheel or `+`/`-` keys (zooms toward cursor).
- **Pan**: Left-click drag or **Arrow Keys**.
- **3D view**: Press `D` or choose **3D view (D)** from the map context menu. Every file still opens in flat 2D.
- **3D navigation**: Left-drag pans, wheel or `+`/`-` zooms, right-drag or `Ctrl`/`Shift`+left-drag rotates and pitches, arrows pan, `Shift`+arrows rotate/pitch, `N` points north, `U` returns top-down, and `F` fits the track.
- **Fit to Window**: Press `F` or **Double-click** (fits the selected track or all tracks; `F` is handled reliably through Total Commander's Lister command path).
- **Sidebar**: Drag the right edge to resize; select tracks to filter the view.
- **Toggles**:
  - `T`: Cycle forward through the configured map styles from `mapTypeOrder`.
  - `Shift+T`: Cycle backward through the configured map styles.
  - `M`: Map tiles on/off.
  - `G`: Grid overlay on/off.
  - `E`: Elevation profile on/off.
  - `V`: Speed profile on/off (saved to `showSpeedProfile` in the INI file).
  - `S`: Slope-based track colouring on/off.
- **Information**: `I` opens the summary dialog.
- **Map context menu**: right-click the map in 2D or 3D to switch view, choose the map style, add tracks, fit, toggle overlays, open the summary, or copy the view.
- **Copy view**: `Ctrl+C` and `Ctrl+Ins` copy the current visible Lister view to the clipboard as PNG. The same action is available from the map right-click context menu as **Copy view to clipboard (Ctrl+C)**.
- **Hover**: Move the mouse over the map track or profile to see synchronised crosshairs/position lines.

## Configuration (`GPXLister.ini`)

You can place a `GPXLister.ini` file in the same directory as the plugin binaries to customise defaults.

- `showSlopeColouringOnTrack` (int, default `0`): Enables progressive slope-based colouring for the map track polyline.
- `showSpeedProfile` (int, default `0`): Default speed profile visibility. Runtime changes made with `V` or the map context menu are saved here.
- `trackLineWidth` (float, default `2.0`): Stroke width for drawing the map track polyline. Values are clamped to a safe range.
- `speedProfileColor` (string, default `#0059F2`): Speed profile colour as `#RRGGBB`.
- `mapTypeOrder` (string, default `standard,satellite,topo`): Display and cycle order for map styles. Valid values are `standard`, `satellite`, and `topo`.
- `standardTileEndpoint` (string, default OpenStreetMap): URL template for the standard map style. Legacy `tileEndpoint` is still accepted as an alias.
- `satelliteTileEndpoint` (string, default Google satellite): URL template for the satellite map style.
- `topoTileEndpoint` (string, default OpenTopoMap): URL template for the topo map style.
- `3d_model` (int, default `2`): Preferred renderer used only after the user requests 3D. `1` is perspective; `2` is real DEM terrain. Missing or invalid values are written back as `2`.
- `terrainProvider` (string, default `terrarium`): `terrarium` for the free AWS/Mapzen endpoint or `maptiler` for MapTiler Terrain RGB.
- `terrariumTerrainEndpoint` (string): Terrarium XYZ elevation URL template.
- `mapTilerTerrainEndpoint` (string): MapTiler Terrain RGB TileJSON URL; `{key}` is replaced with `mapTilerApiKey`.
- `mapTilerApiKey` (string): Required when the MapTiler endpoint contains `{key}`.
- `terrainExaggeration` (float, default `1.0`): Vertical terrain scale, clamped to `0.1` through `5.0`.
- `fitConverter` (string, default `Fit2Gpx.exe`): Converter executable for `.fit` files. Relative names are searched in the plugin folder first, then in `PATH`.
- `fitArgs` (string, default `{input} {output} --elevation-dataset srtm30m,eudem25m`): Fit2Gpx command-line template. Supported placeholders are `{converter}`, `{input}`, and `{output}`.
- `fitTimeoutSec` (int, default `60`): Maximum conversion time before the converter is terminated and an error is shown.
- `kmlConverter` (string, default `kml2gpx.exe`): Converter executable for `.kml` and `.kmz` files. Relative names are searched in the plugin folder first, then in `PATH`.
- `kmlArgs` (string, default `{input} {output} --elevation-dataset srtm30m,eudem25m`): kml2gpx command-line template. Supported placeholders are `{converter}`, `{input}`, and `{output}`.
- `kmlTimeoutSec` (int, default `60`): Maximum conversion time before the converter is terminated and an error is shown.
- When slope colouring is enabled, the gradient is computed from elevation change over distance and mapped to a blue->green->red ramp, then blended with the per-track base colour.
- Colour changes are computed in distance windows rather than per raw GPX segment to avoid visual noise. Track rendering uses rounded joins/caps to prevent visible seams when colours change.

## Supported Map Tile Providers

GPXLister can use raster web-map tiles that return PNG or JPEG images through a URL template containing `{z}`, `{x}`, and `{y}`. The `T` key cycles forward through the valid styles in `mapTypeOrder`, and `Shift+T` cycles backward; the map context menu offers the same styles for direct selection. Unknown map types are ignored, duplicates are removed, and the `mapTypeOrder` key is repaired when possible.

Example:

```ini
mapTypeOrder=standard,satellite,topo
standardTileEndpoint=https://tile.openstreetmap.org/{z}/{x}/{y}.png
satelliteTileEndpoint=https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}
topoTileEndpoint=https://a.tile.opentopomap.org/{z}/{x}/{y}.png
userAgent=GPXLister/2.8
```

## 3D Terrain Providers

The default real-terrain service is the public Terrarium dataset on AWS Open Data. It needs no API key and is the recommended free default. MapTiler Terrain RGB is fully supported through TileJSON and requires a MapTiler key/plan. Provider availability, quotas, attribution, and commercial-use terms remain controlled by the provider.

```ini
3d_model=2
terrainProvider=terrarium
terrariumTerrainEndpoint=https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png
terrainExaggeration=1.0

; Alternative:
;terrainProvider=maptiler
;mapTilerTerrainEndpoint=https://api.maptiler.com/tiles/terrain-rgb-v2/tiles.json?key={key}
;mapTilerApiKey=YOUR_KEY
```

The visual comparison, rendering expectations, and endpoint examples are collected in the single [Tile endpoints and 3D rendering guide](TileEndPoint_rendering_samples/README.md).

| Provider                    | INI endpoint example                                                                                                                                                | Pros                                                                                                  | Cons and limits                                                                                                                                                                                             |
| --------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| OpenStreetMap Standard      | `standardTileEndpoint=https://tile.openstreetmap.org/{z}/{x}/{y}.png`                                                                                               | Best general street map; very current OSM data; no API key.                                           | Community-funded service with a strict [tile usage policy](https://operations.osmfoundation.org/policies/tiles/). Use a stable `userAgent`, avoid bulk/offline downloading, and keep request volume modest. |
| OpenTopoMap                 | `topoTileEndpoint=https://a.tile.opentopomap.org/{z}/{x}/{y}.png`                                                                                                   | Topographic style with contours and hill shading; good low-impact way to get cliff/terrain shadows.   | Third-party service with its own availability and usage expectations; usually slower than large commercial CDNs; style is less clean for city navigation.                                                   |
| CARTO Voyager raster        | `standardTileEndpoint=https://a.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}.png`                                                                          | Clean, readable, high-contrast basemap that keeps GPX tracks visible.                                 | Third-party basemap; check CARTO terms for redistributed or heavy use. No terrain shadows.                                                                                                                  |
| Esri World Imagery          | `satelliteTileEndpoint=https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}`                                               | Good satellite/air-photo coverage; useful as the alternate `T` layer.                                 | Uses ArcGIS cached tile order `{z}/{y}/{x}`. Esri requires correct attribution for Esri and data providers; imagery age and resolution vary by area.                                                        |
| ArcGIS Static Basemap Tiles | `standardTileEndpoint=https://static-map-tiles-api.arcgis.com/arcgis/rest/services/static-basemap-tiles-service/v1/arcgis/outdoor/static/tile/{z}/{y}/{x}?token=YOUR_TOKEN` | Many professional street/outdoor/light/dark styles.                                                   | Requires an ArcGIS access token unless you use authorization headers; GPXLister can only place tokens in the URL. Uses `{z}/{y}/{x}` order and 512 px source tiles, so visual scaling may differ.           |
| Thunderforest               | `topoTileEndpoint=https://api.thunderforest.com/outdoors/{z}/{x}/{y}.png?apikey=YOUR_KEY`                                                                           | Outdoor, cycle, transport, and landscape styles.                                                      | Requires an API key and is plan/rate-limit based. Pick styles and limits from the [Thunderforest docs](https://www.thunderforest.com/docs/map-tiles-api/).                                                  |
| Stadia Maps raster          | `topoTileEndpoint=https://tiles.stadiamaps.com/tiles/outdoors/{z}/{x}/{y}.png?api_key=YOUR_KEY`                                                                     | CDN-backed raster styles, including outdoors and migrated Stamen styles.                              | Usually requires domain auth or an API key outside localhost; max zoom depends on style. Use the 256 px URL, not `{r}` retina placeholders.                                                                 |
| MapTiler raster             | `satelliteTileEndpoint=https://api.maptiler.com/tiles/satellite-v2/{z}/{x}/{y}.jpg?key=YOUR_KEY`                                                                    | Good commercial satellite and map styles; predictable service levels.                                 | Requires a MapTiler API key. Many MapTiler examples are TileJSON/vector-style URLs; GPXLister needs direct raster tile URLs.                                                                                |
| Self-hosted XYZ tiles       | `standardTileEndpoint=https://your-server.example/tiles/{z}/{x}/{y}.png`                                                                                            | Full control, best for offline/private/high-volume use, custom styles, or pre-rendered DEM hillshade. | You operate storage, rendering, caching, attribution, and uptime. Must use Web Mercator XYZ-compatible tile coordinates.                                                                                    |

Unsupported or limited cases:

- Vector tiles (`.pbf`, Mapbox/MapLibre style JSON, TileJSON-only endpoints) are not rendered by GPXLister.
- Provider templates using `{s}`, `{r}`, `{quadkey}`, signed headers, or POST-created sessions are not expanded by GPXLister.
- Official Google Map Tiles API 2D tiles require a session token created by a POST request and have strict caching/attribution rules, so they are not a simple INI-only provider. Older direct Google tile URLs may work technically, but they are not recommended unless your usage is explicitly allowed by Google terms.
- GPXLister keeps tiles in RAM only; it is an interactive viewer, not an offline tile downloader.
