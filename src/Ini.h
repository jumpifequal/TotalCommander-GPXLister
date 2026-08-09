#ifndef INI_H
#define INI_H
#include <windows.h>
#include <string>

struct Options {
    bool useTiles = true;
    bool showGridWhenNoTiles = true;
    bool showScale = true;
    bool showCoords = true;
    int  initialZoom = 13;
    int  workers = 4;
    int  maxBitmaps = 512;
    int  prefetchRings = 2;

	//default value for tileEndpoint
    wchar_t tileEndpoint[256] = L"https://tile.openstreetmap.org/{z}/{x}/{y}.png";

    //default value for standardTileEndpoint
    wchar_t standardTileEndpoint[256] = L"https://tile.openstreetmap.org/{z}/{x}/{y}.png";

	//default value for satelliteTileEndpoint
    wchar_t satelliteTileEndpoint[256] = L"https://mt1.google.com/vt/lyrs=s&x={x}&y={y}&z={z}";

    //default value for topoTileEndpoint
    wchar_t topoTileEndpoint[256] = L"https://a.tile.opentopomap.org/{z}/{x}/{y}.png";

    wchar_t mapTypeOrder[128] = L"standard,satellite,topo";
    bool hasMapTypeOrder = false;

    // Preferred renderer used only when the user switches from the default 2D view.
    // 1 = flat perspective, 2 = real DEM terrain.
    int preferred3dModel = 2;
    wchar_t terrainProvider[32] = L"terrarium";
    wchar_t terrariumTerrainEndpoint[512] = L"https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png";
    wchar_t mapTilerTerrainEndpoint[512] = L"https://api.maptiler.com/tiles/terrain-rgb-v2/tiles.json?key={key}";
    wchar_t mapTilerApiKey[256] = L"";
    float terrainExaggeration = 1.0f;
    
	//default user agent
    wchar_t userAgent[128] = L"GPXLister";
    
    int requestDelayMs = 75; // throttle
    int backoffStartMs = 500;
    int backoffMaxMs = 4000;
    bool showElevationProfile = true;
    bool showSpeedProfile = false;
    bool showSlopeColouringOnTrack = false; // Progressive slope colouring for the map track polyline.
    float trackLineWidth = 2.0f; // Stroke width for drawing the track polyline on the map.
    wchar_t speedProfileColor[16] = L"#0059F2"; // Speed profile colour as #RRGGBB.
    wchar_t fitConverter[MAX_PATH] = L"Fit2Gpx.exe";
    wchar_t fitArgs[512] = L"";
    int fitTimeoutSec = 60;
    wchar_t kmlConverter[MAX_PATH] = L"kml2gpx.exe";
    wchar_t kmlArgs[512] = L"";
    int kmlTimeoutSec = 60;
};

void LoadOptions(Options& o);
void SaveOptionBool(const wchar_t* key, bool value);
void SaveOptionString(const wchar_t* key, const wchar_t* value);
#endif
