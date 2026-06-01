# Tile Endpoint Rendering Samples

Generated for the same area and zoom so the visual style differences are easy to compare.

- Center: latitude 46.5405, longitude 12.1357
- Zoom: 13
- Tile grid: 3x3 tiles, 768x768 px
- Center tile: z 13, x 4372, y 2896
- User-Agent used by generator: GPXLister-doc-sample/1.0

| Image | Endpoint | Notes |
| --- | --- | --- |
| `Esri_World_Street_Map_standard_street_map.png` | `https://server.arcgisonline.com/ArcGIS/rest/services/World_Street_Map/MapServer/tile/{z}/{y}/{x}` | Esri, OpenStreetMap contributors, and data providers |
| `OpenTopoMap_topographic_contours_hillshade.png` | `https://a.tile.opentopomap.org/{z}/{x}/{y}.png` | OpenTopoMap, OpenStreetMap contributors, SRTM |
| `CARTO_Voyager_clean_street_map.png` | `https://a.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}.png` | CARTO, OpenStreetMap contributors |
| `Esri_World_Imagery_satellite.png` | `https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}` | Esri and imagery data providers |
| `Esri_World_Topographic_Map.png` | `https://server.arcgisonline.com/ArcGIS/rest/services/World_Topo_Map/MapServer/tile/{z}/{y}/{x}` | Esri, OpenStreetMap contributors, and data providers |

Providers that require private keys or session creation, such as MapTiler, Thunderforest, Stadia Maps, and the official Google Map Tiles API, are documented in the project README/MANUAL but are not rendered here with placeholder credentials.

These samples are for visual comparison and documentation only. Follow each provider's attribution, caching, and usage policy before redistribution.
