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

	//default value for satelliteTileEndpoint
    wchar_t satelliteTileEndpoint[256] = L"https://mt1.google.com/vt/lyrs=s&x={x}&y={y}&z={z}";
    
	//default user agent
    wchar_t userAgent[128] = L"GPXLister";
    
    int requestDelayMs = 75; // throttle
    int backoffStartMs = 500;
    int backoffMaxMs = 4000;
    bool showElevationProfile = true;
	bool showSlopeColouringOnTrack = false; // Progressive slope colouring for the map track polyline.
    float trackLineWidth = 2.0f; // Stroke width for drawing the track polyline on the map.
};

void LoadOptions(Options& o);
#endif
