# GPXLister - Lister Plugin for Total Commander

**Binary names:** `GPXLister.wlx` and `GPXLister.wlx64`

**FIT/KML support:** `.fit` files are converted transparently through `Fit2Gpx.exe`, and `.kml` or `.kmz` files through `kml2gpx.exe`, into a temporary GPX file, rendered through the existing GPX engine, and cleaned up after loading.

**Purpose:** Fast preview of .gpx tracks in Total Commander's Lister (10/11+), featuring interactive maps, elevation profiles, and multi-track selection.

## 1) Intent & Overview

**GPXLister** is a high-performance WLX (Lister) plugin for **Total Commander** designed to render GPX tracks directly within the Lister window. It provides a visual alternative to plain text, plotting coordinates onto a responsive map.

- **Visual Rendering:** Displays GPX **tracks** (`trk`/`trkseg`/`trkpt`) with anti-aliased lines and elevation profiles. Waypoints and multiple tracks GPX are supported 
- **Map Background:** Uses Slippy Map tiles (OpenStreetMap by default) with an automatic fallback to a cartographic grid when offline.
- **Map Style Cycle:** Quickly switch forward or backward between configured map styles, or select a specific style from the map context menu.
- **Multi-Track Support:** Handles files with multiple tracks using a selectable and **resizable lateral sidebar**.
- **Performance:** Capable of rendering large GPX files (100,000+ points) smoothly without blocking the UI.
- **Zero Disk Footprint:** All tiles and bitmaps are managed in RAM; no temporary files are written to disk.
- **Clean Lifecycle:** Full resource release (threads, COM, D2D) upon closing the Lister window.

## 2) Technical Design

- **Projection:** Uses **Web Mercator (EPSG:3857)** to project geographic coordinates into pixel space.
- **Rendering Engine:** Utilises **Direct2D** for hardware-accelerated, high-quality drawing.
- **DPI Awareness:** Fully compatible with Windows scaling settings for crisp rendering on high-resolution displays.
- **Asynchronous Loading:** PNG tiles are downloaded via background worker threads and promoted to the UI thread once decoded.
- **Antimeridian Handling:** Correctly handles tracks that cross the 180Â° longitude line.

## 3) Installation

### Build Output Folders

- The Visual Studio project targets the **v145** toolset from the Visual Studio 2026 Insiders distribution.
- Win32 builds write to `x32\<Configuration>\GPXLister.wlx`.
- x64 builds write to `x64\<Configuration>\GPXLister.wlx64`.

### A) Manual Installation

1. Copy **`x32\Release\GPXLister.wlx`** (and/or **`x64\Release\GPXLister.wlx64`**) to a dedicated folder (e.g. `%COMMANDER_PATH%\Plugins\WLX\GPXLister\`). Keep **`Fit2Gpx.exe`** in the same folder if you want `.fit` support and **`kml2gpx.exe`** in the same folder if you want `.kml` and `.kmz` support.
2. In **Total Commander**: Navigate to `Configuration -> Options... -> Plugins -> Lister plugins (WLX) -> Add...`.
3. Select the plugin binary and confirm the detect string.
4. *(Optional)* Place a customised **`GPXLister.ini`** in the same folder as the plugin.

### B) Recommended Detect String

EXT="GPX" | EXT="FIT" | EXT="KML" | EXT="KMZ"

## 4) Usage & Controls

### Keyboard Shortcuts

- **Arrow Keys** - Pan the map.

- **+ / -** - Zoom in and out.

- **F / f** - **Fit to window** (recentre and choose the best zoom for the active selection).

- **M/m** - Toggle **map tiles** on/off.

- **T / t** - Cycle forward through the configured map styles from `mapTypeOrder`.

- **Shift+T** - Cycle backward through the configured map styles.

- **G/g** - Toggle **grid overlay** on/off.

- **E/e** - Toggle **elevation profile** (the state is saved automatically to the INI file).

- **V/v** - Toggle **speed profile** independently from the elevation profile.

- **Ctrl+C / Ctrl+Ins** - Copy the current visible Lister view to the clipboard as PNG.

- **I/i** - Open the DPI-aware track information dialog.

- **S/s** - Toggle **slope-based track colouring** on/off (progressive colouring based on gradient).
  
  ### Mouse Interactions

- **Left-drag** - Pan the map.

- **Mouse wheel** - Zoom in/out toward the cursor position.

- **Double-click** - Fit to window the track

- **Right-click (map view)** - Open a context menu with direct access to existing actions.
  
  - Toggle tiles (M)
  - Map style submenu (uses `mapTypeOrder`)
  - Fit to window (F)
  - Toggle grid when tiles are off (G)
  - Add track file...
  - Track summary (I)
  - Copy view to clipboard (Ctrl+C)
  - Toggle elevation profile (E)
  - Toggle speed profile visibility (V)
  - Toggle slope colouring on track (S)
  
  The menu is shown only when the pointer is inside the map view.
  It is not shown when right-clicking the sidebar or the elevation profile area.

- **Sidebar drag** - Drag the right edge of the lateral panel to resize the track list.
* **Mouse over** - Go near the track or on the elevation window to activate mouse over.
  
  ### Overlays
- **Top-left:** Active **track name**, latitude, and longitude.

- **Bottom-left:** Mandatory map data attribution.

- **Bottom-centre:** Dynamic **scale bar**.

- **Elevation Profile:** Synchronised hover line showing elevation at specific points along the track.

### Slope-based track colouring

GPXLister can render the track polyline with progressive colouring driven by the local gradient (slope). This is designed to be readable and stable rather than flickery on dense GPX samples.

- **Enable/disable:** Press **S** or use the map right-click context menu item.

- **How it is calculated:** The gradient is computed from elevation change divided by horizontal distance, then mapped to a colour ramp (downhill->blue, flat->green, uphill->red) and blended with the per-track base colour.

- **Noise control:** The colour is updated in distance windows (not on every raw GPX segment). The window length is adaptive, using longer windows on near-flat sections and shorter windows on steeper sections, while staying above GPX noise.

- **Rendering quality:** Track stroke uses rounded joins and caps to avoid visible seams when colours change between windows.

## 5) INI Configuration (`GPXLister.ini`)

  | Key                         | Type   | Default                                                 | Description                                                                                                    |
  | --------------------------- | ------ | ------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------- |
  | `useTiles`                  | bool   | `1`                                                     | Enable map tile background.                                                                                    |
  | `showGridWhenNoTiles`       | bool   | `1`                                                     | Draw grid lines when tiles are unavailable.                                                                    |
  | `showScale`                 | bool   | `1`                                                     | Show the dynamic scale bar.                                                                                    |
  | `showCoords`                | bool   | `1`                                                     | Show latitude, longitude, and track name.                                                                      |
  | `initialZoom`               | int    | `13`                                                    | Default zoom level (3..19).                                                                                    |
  | `showElevationProfile`      | int    | `1`                                                     | Default visibility of the elevation window.                                                                    |
  | `showSpeedProfile`          | int    | `0`                                                     | Default visibility of the speed profile. Runtime changes made with V or the context menu are saved here.       |
  | `showSlopeColouringOnTrack` | int    | `0`                                                     | Default visibility of slope-based progressive colouring on the map track polyline.                             |
  | `trackLineWidth`            | float  | `2.0`                                                   | Stroke width used to draw the track polyline on the map (clamped to a safe range).                             |
  | `speedProfileColor`         | string | `#0059F2`                                               | Speed profile colour as `#RRGGBB`. Invalid values fall back to the default blue.                               |
  | `fitConverter`              | string | `Fit2Gpx.exe`                                           | Converter executable for `.fit` files. Relative names search the plugin folder first, then `PATH`.             |
  | `fitArgs`                   | string | `{input} {output} --elevation-dataset srtm30m,eudem25m` | Fit2Gpx command-line template. Supports `{converter}`, `{input}`, and `{output}`.                              |
  | `fitTimeoutSec`             | int    | `60`                                                    | Maximum FIT conversion time before the converter is terminated and an error is shown.                          |
  | `kmlConverter`              | string | `kml2gpx.exe`                                           | Converter executable for `.kml` and `.kmz` files. Relative names search the plugin folder first, then `PATH`.  |
  | `kmlArgs`                   | string | `{input} {output} --elevation-dataset srtm30m,eudem25m` | kml2gpx command-line template. Supports `{converter}`, `{input}`, and `{output}`.                              |
  | `kmlTimeoutSec`             | int    | `60`                                                    | Maximum KML/KMZ conversion time before the converter is terminated and an error is shown.                      |
  | `mapTypeOrder`              | string | `standard,satellite,topo`                               | Startup, forward/backward keyboard-cycle, and context-menu order for map styles. Valid values: `standard`, `satellite`, `topo`. |
  | `standardTileEndpoint`      | string | `OSM URL`                                               | URL template for the standard map style. Legacy `tileEndpoint` is still accepted as an alias.                  |
  | `satelliteTileEndpoint`     | string | `Google Sat`                                            | URL template for the satellite map style.                                                                      |
  | `topoTileEndpoint`          | string | `OpenTopoMap URL`                                       | URL template for the topo map style.                                                                           |
  | `userAgent`                 | string | `GPXLister`                                             | HTTP User-Agent for requests.                                                                                  |
  | `workers`                   | int    | `4`                                                     | Concurrent download threads (1..8).                                                                            |
  | `requestDelayMs`            | int    | `75`                                                    | Base inter-request delay (throttling).                                                                         |
  | `backoffStartMs`            | int    | `500`                                                   | Initial exponential backoff after network errors.                                                              |
  | `backoffMaxMs`              | int    | `4000`                                                  | Maximum backoff delay for retries.                                                                             |
  | `prefetchRings`             | int    | `2`                                                     | Number of tile rings to pre-load around the view.                                                              |
  | `maxBitmaps`                | int    | `512`                                                   | LRU capacity for in-memory bitmaps.                                                                            |

## 6) Supported Map Tile Providers

GPXLister supports raster map tiles that return PNG or JPEG images from a URL template containing `{z}`, `{x}`, and `{y}`. Use `mapTypeOrder` to choose the startup, keyboard-cycle, and context-menu order for `standard`, `satellite`, and `topo`; `T` moves forward and `Shift+T` moves backward through that order. Unknown values are ignored, duplicates are removed, and the `mapTypeOrder` key is repaired when possible.

Example:

```ini
mapTypeOrder=standard,satellite,topo
standardTileEndpoint=https://tile.openstreetmap.org/{z}/{x}/{y}.png
satelliteTileEndpoint=https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}
topoTileEndpoint=https://a.tile.opentopomap.org/{z}/{x}/{y}.png
userAgent=GPXLister/2.7.1
```

| Provider                    | INI endpoint example                                                                                                                                                        | Pros                                                                                                                   | Cons and limits                                                                                                                                                                                             |
| --------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| OpenStreetMap Standard      | `standardTileEndpoint=https://tile.openstreetmap.org/{z}/{x}/{y}.png`                                                                                                       | Best general street map; current OSM road, path, and POI data; no API key.                                             | Community-funded service with a strict [tile usage policy](https://operations.osmfoundation.org/policies/tiles/). Use a stable `userAgent`, avoid bulk/offline downloading, and keep request volume modest. |
| OpenTopoMap                 | `topoTileEndpoint=https://a.tile.opentopomap.org/{z}/{x}/{y}.png`                                                                                                           | Topographic map with contours and hill shading; lowest-impact way to get cliff/terrain shadowing without code changes. | Third-party service with its own availability and usage expectations; can be slower than commercial CDNs; style is less clean for city navigation.                                                          |
| CARTO Voyager raster        | `standardTileEndpoint=https://a.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}.png`                                                                                  | Clean, high-contrast presentation map; GPX tracks remain easy to see.                                                  | Check CARTO terms for redistributed or heavy use; no terrain shadows.                                                                                                                                       |
| Esri World Imagery          | `satelliteTileEndpoint=https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}`                                                       | Good satellite/air-photo coverage; useful as the alternate **T** layer.                                                | Uses ArcGIS cached tile order `{z}/{y}/{x}`. Esri requires attribution for Esri and data providers; imagery age and resolution vary by area.                                                                |
| ArcGIS Static Basemap Tiles | `standardTileEndpoint=https://static-map-tiles-api.arcgis.com/arcgis/rest/services/static-basemap-tiles-service/v1/arcgis/outdoor/static/tile/{z}/{y}/{x}?token=YOUR_TOKEN` | Professional street, outdoor, light, and dark styles.                                                                  | Requires an ArcGIS token unless authorization headers are used; GPXLister can only pass tokens in the URL. Uses `{z}/{y}/{x}` and 512 px source tiles, so visual scaling may differ.                        |
| Thunderforest               | `topoTileEndpoint=https://api.thunderforest.com/outdoors/{z}/{x}/{y}.png?apikey=YOUR_KEY`                                                                                   | Outdoor, cycle, transport, landscape, and other specialist OSM-derived styles.                                         | Requires an API key and is plan/rate-limit based. See the [Thunderforest tile docs](https://www.thunderforest.com/docs/map-tiles-api/).                                                                     |
| Stadia Maps raster          | `topoTileEndpoint=https://tiles.stadiamaps.com/tiles/outdoors/{z}/{x}/{y}.png?api_key=YOUR_KEY`                                                                             | CDN-backed raster styles, including outdoors and migrated Stamen styles.                                               | Usually requires domain auth or an API key outside localhost; max zoom depends on style. Use the 256 px URL, not `{r}` retina placeholders.                                                                 |
| MapTiler raster             | `satelliteTileEndpoint=https://api.maptiler.com/tiles/satellite-v2/{z}/{x}/{y}.jpg?key=YOUR_KEY`                                                                            | Commercial satellite and map styles with predictable service levels.                                                   | Requires a MapTiler API key. Many examples are TileJSON or vector style URLs; GPXLister needs a direct raster tile URL.                                                                                     |
| Self-hosted XYZ tiles       | `standardTileEndpoint=https://your-server.example/tiles/{z}/{x}/{y}.png`                                                                                                    | Full control; best for offline/private/high-volume use, custom styles, or pre-rendered DEM hillshade.                  | You operate storage, rendering, caching, attribution, and uptime. Tiles must use Web Mercator XYZ-compatible coordinates.                                                                                   |

Unsupported or limited cases:

- Vector tiles (`.pbf`), Mapbox/MapLibre style JSON, and TileJSON-only endpoints are not rendered.

- Provider templates using `{s}`, `{r}`, `{quadkey}`, signed headers, or POST-created sessions are not expanded.

- Official Google Map Tiles API 2D tiles require a session token created by a POST request and have strict caching/attribution rules, so they are not a simple INI-only provider. Older direct Google tile URLs may work technically, but they are not recommended unless your usage is explicitly allowed by Google terms.

- Tiles are cached in RAM only. GPXLister is an interactive viewer, not an offline tile downloader.

## 7) Troubleshooting

- **Blank background:** Verify internet connectivity and ensure map tiles are enabled (**M** key).

- **Fit not working:** Ensure a track is actually selected. Press **F** to recentre the view.

- **Slow loading:** Increase the number of `workers` or `prefetchRings` in the INI file if your connection permits.

## 8) License & Attribution

- Map data Â© **OpenStreetMap contributors**.

- Developed utilising Windows **Direct2D**, **WIC**, and **WinINet** APIs.

- Largely coded via Gemini Pro.

## Versions

- **v2.7.1 - Reverse map style cycling**

  - Added **Shift+T** to cycle backward through configured map styles for quick side-by-side comparisons such as Standard vs Topo.

- **v2.7 - Configurable map styles**

  - Added `mapTypeOrder` to control startup map style, keyboard cycle order, and context-menu order.
  - Added standard, satellite, and topo map style slots with `standardTileEndpoint`, `satelliteTileEndpoint`, and `topoTileEndpoint`.
  - Replaced the old context-menu tile-server toggle with a **Map style** submenu for direct style selection.
  - Added **Add track file...** to append GPX/FIT/KML/KMZ files temporarily to the current view.
  - Added safe repair for invalid or duplicate `mapTypeOrder` values, while preserving legacy `tileEndpoint` compatibility.

- **v2.6 - Clipboard copy and persistent speed profile**

  - Added **Ctrl+C / Ctrl+Ins / Copy view to clipboard** for the current visible Lister view.
  - Added the same copy action to the map right-click context menu.
  - Persisted **V / speed profile** visibility in `showSpeedProfile`.

- **v2.5.1 - Reliable Fit to Window shortcut**

  - Fixed unreliable **F / Fit to window** activation in Total Commander by handling its WLX host command path.
  - Preserved matching behavior for the map keyboard shortcut and contextual-menu action.

- **v2.5 - KMZ import support and recompile via VS 2026**

  * Added transparent `.kmz` support through hidden `kml2gpx.exe` conversion, with template-based `kmlArgs` and temporary GPX cleanup.

  * Recompilation via stable VS 2026 to avoid false VirusTotal positive warnings
* **v2.4 - KML import support**

  - Added transparent `.kml` support through hidden `kml2gpx.exe` conversion, with template-based `kmlArgs` and temporary GPX cleanup.
- **v2.3 - GPX viewing polish**

  - Changed **Fit to Window** to the standard **F** shortcut and removed the old X shortcut.
  - Redesigned the **I** information dialog with a DPI-aware card layout and clearer spacing.
  - Fixed summary elapsed time and average speed for patched/multi-track files with large timestamp gaps.
  - Made ascent/descent and sustained slope calculations robust against noisy live-recorded GPX samples.
  - Made speed and elevation profiles independently toggleable (**V** and **E**) and gave the speed profile its own scale.
  - Added configurable `speedProfileColor` with blue as the default.
  - Added transparent `.fit` support through hidden `Fit2Gpx.exe` conversion, with template-based `fitArgs` and temporary GPX cleanup.
  - Improved mouse-wheel zoom anchoring, sidebar resizing, profile/map hover synchronisation, and overlay readability under DPI scaling.
  - Build outputs are now organised under `x32` and `x64`.

- **v1.0** - Initial public release.

- **v1.3** - Added elevation profiles and support for multiple track colours.

- **v1.4** - Added multi-track support, resizable sidebar, and track name overlays.

- **v1.5** - Added **Satellite Mode toggle ('T' key)** with customisable satelliteTileEndpoint in INI. Improved Google Maps tile support. Added mouse hover on the track window

- v1.6 - Added mouse contextual menu

- v1.7 - Added support for waypoints

- v1.8 - Added slope-based track colouring and configurable track stroke width.

- v1.9 - A bunch of functions and fixes
  
  - Added Speed Profile support ('V' key) with locale-independent parsing fix. Supports ISO 8601 time format (used by GPX ) 
  
  - Improved mouse interactions: Smooth Zoom on scroll/touchpad, Double-Click on Map to zoom in, and Double-Click on  Profile to jump to the corresponding track location. 
  
  - Added smart smoothing for speed data to reduce GPS noise. 
  
  - Fixed elevation calculation. Now the Ascent and Descent are correct
  
  - Added Maximum and Minimum elevation in the Elevation window
  
  - Added to the tooltips the time, meaning when I passed in that point
  
  - Improved colouring of tooltips in Satellite mode

- **v2.0 - High-DPI Support & Precision Update**
  
  - **Added** full **High-DPI (Per-Monitor V2) support**. The plugin now renders crisp text and lines at any Windows scaling factor (125%, 150%, 175%+) without blurring or virtualization.
  
  - **Fixed** critical mouse misalignment ("cursor drift"). The red alignment circle now stays perfectly under the mouse cursor regardless of zoom level or screen scaling.
  
  - **Fixed** the Elevation Profile window appearing flattened/invisible on high-resolution screens by enforcing correct monitor-aware scaling.
  
  - **Improved** "Fit to Window" ('F' key) algorithm: increased map usage (tighter margins) and corrected vertical centering logic for a perfect view of the track.
  
  - **Fixed**: Switched internal coordinate systems to floating-point precision to prevent sub-pixel rounding errors during panning and zooming.
  
  - **Fixed:** Implemented dynamic linking for modern DPI APIs to ensure continued stability on older systems (Windows 7).

- **v2.1 - security increase**
  
  - increased handling of lock conditions (e.g. too long tracks, not responding or badly formed tiles servers, switches in between a tile download, locks)
  - added recap window with tracks statistics, invoked also with "I"
