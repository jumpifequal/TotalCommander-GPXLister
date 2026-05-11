#ifndef GPXPARSER_H
#define GPXPARSER_H
#include <windows.h>
#include <vector>
#include <string>

struct GpxPoint { 
    double lat = 0, lon = 0, ele = 0; 
    double time = 0; // Unix timestamp (seconds)
};

struct GpxTrack {
    std::wstring name; //Track name from GPX
    std::vector<GpxPoint> pts;
};

// holds waypoint data from GPX file
struct GpxWaypoint {
    double lat = 0;
    double lon = 0;
    double ele = 0;
	double time = 0; // Unix timestamp (seconds)
    std::wstring name;
    std::wstring sym;
};

bool ParseGpxWaypoints(const wchar_t* path, std::vector<GpxWaypoint>& out);
bool ParseGpxFile(const wchar_t* path, std::vector<GpxTrack>& out);

#endif
