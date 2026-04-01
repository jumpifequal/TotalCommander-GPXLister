# GPXLister — Lister Plugin for Total Commander

**Binary names:** `GPXLister.wlx` and `GPXLister.wlx64`

**Purpose:** Fast preview of `.gpx` tracks in Total Commander’s Lister (10/11+), featuring interactive maps, elevation profiles, and multi-track selection.

## 1) Intent & Overview

**GPXLister** is a high-performance WLX (Lister) plugin for **Total Commander** designed to render GPX tracks directly within the Lister window. It provides a visual alternative to plain text, plotting coordinates onto a responsive map.

- **Visual Rendering:** Displays GPX **tracks** (`trk`/`trkseg`/`trkpt`) with anti-aliased lines and elevation profiles. Waypoints and multiple tracks GPX are supported 
- **Map Background:** Uses Slippy Map tiles (OpenStreetMap by default) with an automatic fallback to a cartographic grid when offline.
- **Satellite Toggle:** Quickly switch between standard map tiles and satellite imagery.
- **Multi-Track Support:** Handles files with multiple tracks using a selectable and **resizable lateral sidebar**.
- **Performance:** Capable of rendering large GPX files (100,000+ points) smoothly without blocking the UI.
- **Zero Disk Footprint:** All tiles and bitmaps are managed in RAM; no temporary files are written to disk.
- **Clean Lifecycle:** Full resource release (threads, COM, D2D) upon closing the Lister window.

## 2) Technical Design

- **Projection:** Uses **Web Mercator (EPSG:3857)** to project geographic coordinates into pixel space.
- **Rendering Engine:** Utilises **Direct2D** for hardware-accelerated, high-quality drawing.
- **DPI Awareness:** Fully compatible with Windows scaling settings for crisp rendering on high-resolution displays.
- **Asynchronous Loading:** PNG tiles are downloaded via background worker threads and promoted to the UI thread once decoded.
- **Antimeridian Handling:** Correctly handles tracks that cross the 180° longitude line.

## 3) Installation

### A) Manual Installation

1. Copy **`GPXLister.wlx`** (and/or **`GPXLister.wlx64`**) to a dedicated folder (e.g. `%COMMANDER_PATH%\Plugins\WLX\GPXLister\`).
2. In **Total Commander**: Navigate to `Configuration → Options… → Plugins → Lister plugins (WLX) → Add…`.
3. Select the plugin binary and confirm the detect string.
4. *(Optional)* Place a customised **`GPXLister.ini`** in the same folder as the plugin.

### B) Recommended Detect String

EXT="GPX" | (FORCE & FIND("<gpx"))

## 4) Usage & Controls

### Keyboard Shortcuts

- **Arrow Keys** — Pan the map.

- **+ / −** — Zoom in and out.

- **X / x** — **Fit to window** (recentre and choose the best zoom for the active selection).

- **M/m**— Toggle **map tiles** on/off.

- **T / t** — Toggle **Satellite Mode** (switches between standard and satellite tile servers).

- **G/g** — Toggle **grid overlay** on/off.

- **E/e** — Toggle **elevation profile** (the state is saved automatically to the INI file).

- **V/v** — Toggle **speed profile** (into the elevation window, if it's shown, it not press E before or after)

- **S/s** — Toggle **slope-based track colouring** on/off (progressive colouring based on gradient).
  
  ### Mouse Interactions

- **Left-drag** — Pan the map.

- **Mouse wheel** — Zoom in/out toward the cursor position.

- **Double-click** — Fit to window the track

- **Right-click (map view)** - Open a context menu with direct access to existing actions.
  
  - Toggle tiles (M)
  - Toggle tile server (T)
  - Fit to window (X)
  - Toggle grid when tiles are off (G)
  - Toggle elevation profile (E)
  - Toggle speed profile visibility (V)
  - Toggle slope colouring on track (S)
  
  The menu is shown only when the pointer is inside the map view.
  It is not shown when right-clicking the sidebar or the elevation profile area.

- **Sidebar drag** — Drag the right edge of the lateral panel to resize the track list.
* **Mouse over** — Go near the track or on the elevation window to activate mouse over.
  
  ### Overlays
- **Top-left:** Active **track name**, latitude, and longitude.

- **Bottom-left:** Mandatory map data attribution.

- **Bottom-centre:** Dynamic **scale bar**.

- **Elevation Profile:** Synchronised hover line showing elevation at specific points along the track.

### Slope-based track colouring

GPXLister can render the track polyline with progressive colouring driven by the local gradient (slope). This is designed to be readable and stable rather than flickery on dense GPX samples.

- **Enable/disable:** Press **S** or use the map right-click context menu item.

- **How it is calculated:** The gradient is computed from elevation change divided by horizontal distance, then mapped to a colour ramp (downhill→blue, flat→green, uphill→red) and blended with the per-track base colour.

- **Noise control:** The colour is updated in distance windows (not on every raw GPX segment). The window length is adaptive, using longer windows on near-flat sections and shorter windows on steeper sections, while staying above GPX noise.

- **Rendering quality:** Track stroke uses rounded joins and caps to avoid visible seams when colours change between windows.
  
  ## 5) INI Configuration (`GPXLister.ini`)
  
  | Key                         | Type   | Default      | Description                                                                        |
  | --------------------------- | ------ | ------------ | ---------------------------------------------------------------------------------- |
  | `useTiles`                  | bool   | `1`          | Enable map tile background.                                                        |
  | `showGridWhenNoTiles`       | bool   | `1`          | Draw grid lines when tiles are unavailable.                                        |
  | `showScale`                 | bool   | `1`          | Show the dynamic scale bar.                                                        |
  | `showCoords`                | bool   | `1`          | Show latitude, longitude, and track name.                                          |
  | `initialZoom`               | int    | `13`         | Default zoom level (3..19).                                                        |
  | `showElevationProfile`      | int    | `1`          | Default visibility of the elevation window.                                        |
  | `showSlopeColouringOnTrack` | int    | `0`          | Default visibility of slope-based progressive colouring on the map track polyline. |
  | `trackLineWidth`            | float  | `2.0`        | Stroke width used to draw the track polyline on the map (clamped to a safe range). |
  | `tileEndpoint`              | string | `OSM URL`    | Standard Slippy URL template ({z}, {x}, {y}).                                      |
  | `satelliteTileEndpoint`     | string | `Google Sat` | URL template used when Satellite Mode is toggled. Same standard of tileEndpoint    |
  | `userAgent`                 | string | `GPXLister`  | HTTP User-Agent for requests.                                                      |
  | `workers`                   | int    | `4`          | Concurrent download threads (1..8).                                                |
  | `requestDelayMs`            | int    | `75`         | Base inter-request delay (throttling).                                             |
  | `backoffStartMs`            | int    | `500`        | Initial exponential backoff after network errors.                                  |
  | `backoffMaxMs`              | int    | `4000`       | Maximum backoff delay for retries.                                                 |
  | `prefetchRings`             | int    | `2`          | Number of tile rings to pre-load around the view.                                  |
  | `maxBitmaps`                | int    | `512`        | LRU capacity for in-memory bitmaps.                                                |
  
  #### 6) Alternative Map Providers
  
  The following servers are compatible with the `{z}`, `{x}`, and `{y}` placeholder system. You can switch between your primary and satellite views by configuring these in the INI file.
  
  - **OpenStreetMap (Standard)**
    
    - `https://tile.openstreetmap.org/{z}/{x}/{y}.png`
    - **Best for:** General-purpose navigation. It features the most up-to-date community-verified street data, points of interest (POIs), and cycling paths.
  
  - **Google Satellite**
    
    - `https://mt1.google.com/vt/lyrs=s&x={x}&y={y}&z={z}`
    - **Best for:** Photorealistic terrain analysis. Ideal for identifying physical landmarks, forest density, or specific buildings that are not yet mapped as vectors.
  
  - **OpenTopoMap**
    
    - `https://a.tile.opentopomap.org/{z}/{x}/{y}.png`
    - **Best for:** Hiking and outdoor sports. It provides excellent topographic detail, including contour lines (elevation) and hill shading to represent slope steepness.
  
  - **Esri World Imagery**
    
    - `https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}`
    - **Best for:** Professional GIS rendering. Often offers more consistent high-resolution imagery in rural or remote areas compared to consumer-grade providers.
  
  - **CartoDB Voyager**
    
    - `https://a.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}.png`
    - **Best for:** Clean presentations. It uses a high-contrast, "noise-free" design that makes the GPX track line stand out as the primary visual element.

- ## 7) Troubleshooting

- **Blank background:** Verify internet connectivity and ensure map tiles are enabled (**M** key).

- **Fit not working:** Ensure a track is actually selected. Press **X** to recentre the view.

- **Slow loading:** Increase the number of `workers` or `prefetchRings` in the INI file if your connection permits.
  
  ## 8) License & Attribution

- Map data © **OpenStreetMap contributors**.

- Developed utilising Windows **Direct2D**, **WIC**, and **WinINet** APIs.

- Largely coded via Gemini Pro.
  
  ## Versions

- **v1.0** — Initial public release.

- **v1.3** — Added elevation profiles and support for multiple track colours.

- **v1.4** — Added multi-track support, resizable sidebar, and track name overlays.

- **v1.5** — Added **Satellite Mode toggle ('T' key)** with customisable `satelliteTileEndpoint` in INI. Improved Google Maps tile support. Added mouse hover on the track window

- v1.6 — Added mouse contextual menu 

- v1.7 — Added support for waypoints

- v1.8 — Added slope-based track colouring and configurable track stroke width.

- v1.9 — A bunch of functions and fixes
  
  - Added Speed Profile support ('V' key) with locale-independent parsing fix. Supports ISO 8601 time format (used by GPX ) 
  
  - Improved mouse interactions: Smooth Zoom on scroll/touchpad, Double-Click on Map to zoom in, and Double-Click on  Profile to jump to the corresponding track location. 
  
  - Added smart smoothing for speed data to reduce GPS noise. 
  
  - Fixed elevation calculation. Now the Ascent and Descent are correct
  
  - Added Maximum and Minimum elevation in the Elevation window
  
  - Added to the tooltips the time, meaning when I passed in that point
  
  - Improved colouring of tooltips in Satellite mode

- **v2.0 — High-DPI Support & Precision Update**
  
  - **Added** full **High-DPI (Per-Monitor V2) support**. The plugin now renders crisp text and lines at any Windows scaling factor (125%, 150%, 175%+) without blurring or virtualization.
  
  - **Fixed** critical mouse misalignment ("cursor drift"). The red alignment circle now stays perfectly under the mouse cursor regardless of zoom level or screen scaling.
  
  - **Fixed** the Elevation Profile window appearing flattened/invisible on high-resolution screens by enforcing correct monitor-aware scaling.
  
  - **Improved** "Fit to Window" ('X' key) algorithm: increased map usage (tighter margins) and corrected vertical centering logic for a perfect view of the track.
  
  - **Fixed**: Switched internal coordinate systems to floating-point precision to prevent sub-pixel rounding errors during panning and zooming.
  
  - **Fixed:** Implemented dynamic linking for modern DPI APIs to ensure continued stability on older systems (Windows 7).

- **v2.1 - security increase**
  
  - increased handling of lock conditions (e.g. too long tracks, not responding or badly formed tiles servers, switches in between a tile download, locks)
  - added recap window with tracks statistics, invoked also with "I"
