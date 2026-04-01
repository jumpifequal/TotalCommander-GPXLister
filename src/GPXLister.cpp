#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <mutex> // Added for std::call_once (DPI Fix)
#include <string>

#define TILE_READY_MSG (WM_APP+1)
#define TIMER_DOWNLOAD 1 // Timer ID for download retries

// Context menu command identifiers. Each constant is commented for traceability.
#define ID_CTX_TOGGLE_TILES         40001 // Toggle tile rendering (keyboard: M)
#define ID_CTX_TOGGLE_SERVER        40002 // Toggle tile server (keyboard: T)
#define ID_CTX_FIT_TO_WINDOW        40003 // Fit selected/all tracks to window (keyboard: X)
#define ID_CTX_TOGGLE_GRID          40004 // Toggle grid when tiles are disabled (keyboard: G)
#define ID_CTX_TOGGLE_ELEVATION     40005 // Toggle elevation profile (keyboard: E)
#define ID_CTX_TOGGLE_SLOPE_COLOUR  40006 // Toggle slope colouring on track (keyboard: S)
#define ID_CTX_TOGGLE_SPEED         40007 // Toggle speed profile (keyboard: V)
#define ID_CTX_SHOW_INFO            40008 // Show track summary dialog (keyboard: I)

#define ID_MAX_CYCLING_SPEED        150.0 // km/h - used for speed profile scaling

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

#include "listplug.h"
#include "GPXParser.h"
#include "Mercator.h"
#include "TileCache.h"
#include "Ini.h"
#include "SlopeColouring.h"

// DPI awareness constants fallbacks for older SDKs
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

static HINSTANCE g_hInst = nullptr;

// --- DPI AWARENESS HELPERS ---

//mutex for one-time DPI awareness configuration
static std::once_flag g_dpiAwarenessOnce;

// Configure DPI awareness once per process. 
// Tries to force the process to be Per-Monitor Aware to avoid virtualization blur/scaling issues.
/* ======================================================================================
 * DPI AWARENESS STRATEGY
 * ======================================================================================
 * This function attempts to force the process into a "Per-Monitor" DPI awareness mode.
 * 
 * THE PROBLEM:
 * If the host application (Total Commander) is only "System Aware" (e.g. scaled to 120%),
 * but the monitor is at 175%, Windows will "virtualise" this plugin. It will report
 * fake coordinates (1.2x) and stretch the rendered bitmap, causing:
 * 1. Blurry graphics.
 * 2. "Drifting" mouse coordinates (physical vs logical mismatch).
 * 3. Incorrect window sizing (e.g. the Elevation Profile appearing too short).
 * 
 * THE SOLUTION:
 * I attempt to override the inherited DPI context using a hierarchy of APIs:
 * 1. SetProcessDpiAwareness (Windows 8.1+) -> Requests Per-Monitor (V1).
 * 2. SetProcessDpiAwarenessContext (Windows 10 v1607+) -> Requests Per-Monitor V2.
 * 3. SetProcessDPIAware (Vista/7) -> Legacy fallback to prevent complete scaling failure.
 * 
 * TECHNICAL NOTE:
 * I use LoadLibrary/GetProcAddress (Dynamic Linking) rather than static linking.
 * This ensures the plugin loads safely on Windows 7 without crashing due to missing
 * entry points in older system DLLs.
 * 
 * REASON:
 * These APIs reside in Shcore.dll (introduced in Windows 8.1) or newer User32 versions.
 * Windows 7 does not contain these libraries or export these functions.
 *
 * If I linked statically (calling the functions directly), the Windows Loader would
 * fail to load this plugin on Windows 7 with an "Entry Point Not Found" error,
 * causing a crash at startup before any code executes.
 *
 * This dynamic approach allows the plugin to:
 * 1. Be fully Per-Monitor V2 DPI aware on Windows 10/11.
 * 2. Run safely (graceful degradation) on Windows 7 without crashing.
 *
 * ======================================================================================
 */
static void ConfigureDpiAwarenessOnce() {
    std::call_once(g_dpiAwarenessOnce, []() 
        {
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            HMODULE shcore = LoadLibraryW(L"shcore.dll"); // Load dynamically

            // 1. Try SetProcessDpiAwareness (Win 8.1+) - Strongest override
            if (shcore) {
                using SetProcessDpiAwarenessFn = HRESULT(WINAPI*)(int);
                auto setAwareness = (SetProcessDpiAwarenessFn)GetProcAddress(shcore, "SetProcessDpiAwareness");
                if (setAwareness) {
                    // 2 == PROCESS_PER_MONITOR_DPI_AWARE
                    (void)setAwareness(2);
                }
                FreeLibrary(shcore);
            }

            // 2. Try SetProcessDpiAwarenessContext (Win 10 v1607+)
            if (user32) {
                using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
                auto setCtx = (SetProcessDpiAwarenessContextFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
                if (setCtx) {
                    if (setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                        return;
                    }
                }

                // 3. Legacy Fallback
                using SetProcessDPIAwareFn = BOOL(WINAPI*)();
                auto setLegacy = (SetProcessDPIAwareFn)GetProcAddress(user32, "SetProcessDPIAware");
                if (setLegacy) {
                    (void)setLegacy();
                }
            }
        }
    );
}

// Helper to retrieve the REAL Monitor DPI (pierces virtualization).
// This ensures we get correct DPI (e.g. 168) even if the Window reports less due to host virtualization.
static UINT GetWindowDpiSafe(HWND hwnd) {
    // 1. Try GetDpiForMonitor (Win 8.1+) - Correct for virtualized processes
    HMODULE hShcore = GetModuleHandleW(L"shcore.dll");
    if (!hShcore) hShcore = LoadLibraryW(L"shcore.dll");

    if (hShcore) {
        typedef HRESULT(WINAPI* GetDpiForMonitorProc)(HMONITOR, int, UINT*, UINT*);
        auto getDpiMonitor = (GetDpiForMonitorProc)GetProcAddress(hShcore, "GetDpiForMonitor");

        if (getDpiMonitor) {
            HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
            UINT dpiX = 0, dpiY = 0;
            // 0 = MDT_EFFECTIVE_DPI
            if (SUCCEEDED(getDpiMonitor(hMon, 0, &dpiX, &dpiY)) && dpiX > 0) {
                if (dpiX >= 48 && dpiX <= 480) return dpiX;
            }
        }
    }

    // 2. Try GetDpiForWindow (Win 10)
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        auto getDpi = (GetDpiForWindowFn)GetProcAddress(user32, "GetDpiForWindow");
        if (getDpi) {
            UINT dpi = getDpi(hwnd);
            if (dpi >= 48 && dpi <= 480) return dpi;
        }
    }

    // 3. Fallback: System DPI
    HDC hdc = GetDC(hwnd);
    if (!hdc) return 96;
    int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(hwnd, hdc);
    if (dpiX < 48 || dpiX > 480) return 96;
    return (UINT)dpiX;
}

// -------------------------------------

// Forward declarations for Sidebar support
static void UpdateSidebarContent(struct State& s);
static void OnSidebarSelChange(struct State& s);

// Helper to format Unix timestamp into localized OS date and time string.
static std::wstring FormatTimestampLocal(double unixTime) {
    // Unix epoch is 1970. Dates before this are negative but valid in Win32.
    // We check for 0.0 which usually indicates missing data in this project.
    if (unixTime == 0.0) {
        return L"";
    }

    // Convert Unix seconds to FILETIME (100-nanosecond intervals since 1601-01-01).
    ULONGLONG ticks = (ULONGLONG)(unixTime * 10000000.0) + 116444736000000000ULL;
    FILETIME ft;
    ft.dwLowDateTime = (DWORD)(ticks & 0xFFFFFFFF);
    ft.dwHighDateTime = (DWORD)(ticks >> 32);

    SYSTEMTIME stUtc, stLocal;
    if (!FileTimeToSystemTime(&ft, &stUtc)) return L"";

    // Convert UTC from GPX to the user's local timezone.
    if (!SystemTimeToTzSpecificLocalTime(NULL, &stUtc, &stLocal)) return L"";

    wchar_t dateBuf[64], timeBuf[64];
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &stLocal, NULL, dateBuf, 64);
    GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &stLocal, NULL, timeBuf, 64);

    return std::wstring(dateBuf) + L" " + timeBuf;
}

// Formats a duration in seconds as HH:MM:SS.
static std::wstring FormatDurationHhMmSs(double seconds) {
    if (!(seconds > 0.0)) {
        return L"";
    }
        // Clamp extreme values to avoid formatting nonsense if a file contains invalid timestamps.
        if (seconds > (double)60 * 60 * 24 * 365 * 100) {
        seconds = (double)60 * 60 * 24 * 365 * 100;
    }
    
    const unsigned long long total = (unsigned long long)(seconds + 0.5);
    const unsigned long long hh = total / 3600ULL;
    const unsigned long long mm = (total % 3600ULL) / 60ULL;
    const unsigned long long ss = total % 60ULL;
    
    wchar_t buf[32];
    swprintf(buf, 32, L"%02llu:%02llu:%02llu", hh, mm, ss);
    return buf;
}

// High-contrast colour palette for multiple tracks. If there are more than colours, they repeat.
static D2D1_COLOR_F GetTrackColor(size_t index) {
    static const D2D1_COLOR_F palette[] = {
        {1.0f, 0.0f, 0.0f, 1.0f},   // Red
        {0.0f, 0.6f, 0.0f, 1.0f},   // Green
        {0.0f, 0.0f, 1.0f, 1.0f},   // Blue
        {1.0f, 0.6f, 0.0f, 1.0f},   // Orange
        {0.6f, 0.0f, 0.8f, 1.0f},   // Violet
        {0.0f, 0.8f, 0.8f, 1.0f},   // Cyan
        {0.0f, 1.0f, 0.0f, 1.0f},   // Neon Green
        {0.0f, 0.4f, 1.0f, 1.0f},   // Azure Blue
        {1.0f, 0.9f, 0.0f, 1.0f},   // Bright Yellow
        {1.0f, 0.0f, 1.0f, 1.0f},   // Fuchsia
        {0.0f, 1.0f, 0.5f, 1.0f},   // Spring Green
        {1.0f, 0.6f, 0.8f, 1.0f},   // Hot Pink
        {0.5f, 1.0f, 0.0f, 1.0f},   // Chartreuse
        {1.0f, 0.0f, 0.5f, 1.0f},   // Bright Magenta (rarely in maps)
        {0.0f, 0.9f, 0.9f, 1.0f},   // Electric Cyan
        {1.0f, 0.4f, 0.0f, 1.0f},   // Vivid Orange
        {0.6f, 0.2f, 1.0f, 1.0f},   // Deep Purple
        {0.9f, 0.9f, 0.9f, 1.0f},   // Pure White (High contrast on Satellite/Dark areas)
        {0.2f, 0.2f, 0.2f, 1.0f},   // Charcoal Black (High contrast on light City maps)
        {0.6f, 1.0f, 0.2f, 1.0f},   // Lime/Lemon
        {0.2f, 0.6f, 1.0f, 1.0f},   // Sky Blue
        {1.0f, 0.5f, 0.5f, 1.0f}    // Salmon/Coral
    };
    return palette[index % (sizeof(palette) / sizeof(palette[0]))];
}

// Haversine distance between two points (in metres)
static double GetDistance(const GpxPoint& p1, const GpxPoint& p2) {
    double dLat = (p2.lat - p1.lat) * PI / 180.0;
    double dLon = (p2.lon - p1.lon) * PI / 180.0;
    double a = sin(dLat / 2) * sin(dLat / 2) +
        cos(p1.lat * PI / 180.0) * cos(p2.lat * PI / 180.0) *
        sin(dLon / 2) * sin(dLon / 2);
    return 2.0 * atan2(sqrt(a), sqrt(1.0 - a)) * 6371000.0;
}

// Track metadata for stats
struct TrackStats {
    double distance = 0; // metres
    double ascent = 0;   // metres
    double descent = 0;  // metres
};

struct Track {
    std::wstring name;           // Track name extracted from the GPX file
    std::vector<GpxPoint> pts;
    std::vector<double> cumDist; // Cumulative distance at each point
    std::vector<double> speed; // Speed at each point in km/h
    TrackStats stats;
};

struct State {
    HWND hwnd = nullptr, parent = nullptr;
    std::vector<Track> tracks;
    std::vector<GpxWaypoint> waypoints; //Container for waypoints
    double minLat = 0, maxLat = 0, minLon = 0, maxLon = 0;
    int zoom = 13;
    double cx = 0, cy = 0; // centre in pixel space at zoom
    bool panning = false; POINT panStart{}; double panCx = 0, panCy = 0;
    int wheelAccumulator = 0; // Accumulator for smooth/slower wheel zooming

    // Toggles
    bool tiles = true;
    bool showGridWhenNoTiles = true;
    bool showScale = true;
    bool showCoords = true;

    bool showElevationProfile = true; // toggle for elevation graph overlay
    bool showSpeedProfile = false; // Toggle for speed graph overlay
    bool showSlopeColouringOnTrack = false; // Progressive slope colouring for the map track polyline (opt-in).
    float trackLineWidth = 2.0f; // Stroke width for drawing the track polyline on the map.
    float profileHeight = 120.0f;
    double totalDist = 0, totalAsc = 0, totalDesc = 0; // overall stats
    double minE = 1e6, maxE = -1e6; // Cached elevation bounds

    int hoverTrackIdx = -1; // Index of the track containing the hovered point
    int hoverPointIdx = -1; // Index of the point within that track
    POINT mousePos = { 0, 0 }; // Persist mouse position for OnPaint (Physical Coords)

    // D2D/DWrite/WIC
    ID2D1Factory* d2d = nullptr;
    ID2D1HwndRenderTarget* rt = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    ID2D1StrokeStyle* gridDash = nullptr;
    ID2D1StrokeStyle* trackStrokeStyle = nullptr; // # Round caps/joins for track rendering to avoid micro discontinuities
    IDWriteFactory* dwrite = nullptr;
    IDWriteTextFormat* tf = nullptr;
    IWICImagingFactory* wic = nullptr;

    TileCache* cache = nullptr;
    Options opt{};

    // Factor to scale UI elements and coordinates on High-DPI displays.
    float dpiScale = 1.0f;

    // handle map provider switching
    bool isSatelliteMode = false;

    // Sidebar and Selection state
    HWND hwndList = nullptr;      // Handle of lateral listbox
    int selectedTrack = -1;       // -1 = All, 0..N = Index of selected track
    int sidebarWidth = 150;       // Current width of the sidebar
    bool resizingSidebar = false; // resize interaction state
};

struct TrackInfoSummary {
    double distanceM = 0.0;
    double minEleM = 0.0;
    double maxEleM = 0.0;
    double minSpeedKmh = 0.0;
    double maxSpeedKmh = 0.0;
    double avgSpeedKmh = 0.0;
    double minSlopePct = 0.0;
    double maxSlopePct = 0.0;
    double longestAscentM = 0.0;
    double longestDescentM = 0.0;
    double startTimeUnix = 0.0;
    double endTimeUnix = 0.0;
    size_t waypointCount = 0;
    size_t trackCount = 0;
    
};

// forward declarations for summary dialog
static void BuildElevationMedian3(const Track& t, std::vector<double>& out);

static bool ComputeTrackInfoSummary(const State& s, TrackInfoSummary& out) {
    out = TrackInfoSummary{};
    out.waypointCount = s.waypoints.size();
    
    const bool allTracks = (s.selectedTrack < 0);
    if (!allTracks) {
        if (s.selectedTrack >= (int)s.tracks.size()) {
            return false;            
        }       
    }
    
    bool anyPoint = false;
    bool anySpeed = false;
    bool anySlope = false;
    
    double speedSum = 0.0;
    size_t speedCount = 0;
    
    double minEle = 1e18;
    double maxEle = -1e18;
    
    double minSpeed = 1e18;
    double maxSpeed = -1e18;
    
    double minSlope = 1e18;
    double maxSlope = -1e18;
    
    double longestAsc = 0.0;
    double longestDesc = 0.0;
    
    double globalStart = 0.0;
    double globalEnd = 0.0;
    
    const size_t nTracks = s.tracks.size();
    for (size_t ti = 0; ti < nTracks; ++ti) {
        if (!allTracks && (int)ti != s.selectedTrack) {
            continue;
            
        }
        const Track & t = s.tracks[ti];
        out.trackCount++;
        
        if (t.pts.empty()) {
            continue;           
        }
        
        for (size_t i = 0; i < t.pts.size(); ++i) {
            const GpxPoint & p = t.pts[i];
            if (p.ele < minEle) minEle = p.ele;
            if (p.ele > maxEle) maxEle = p.ele;
            anyPoint = true;
            
            if (p.time > 0.0) {
                if (globalStart == 0.0 || p.time < globalStart) {
                    globalStart = p.time;
                    
                }
                 if (globalEnd == 0.0 || p.time > globalEnd) {
                    globalEnd = p.time;                    
                }             
            }           
        }
        
        out.distanceM += t.stats.distance;
        
        for (size_t i = 0; i < t.speed.size(); ++i) {
            const double spd = t.speed[i];
            if (spd > 0.0) {
                anySpeed = true;
                if (spd < minSpeed) minSpeed = spd;
                if (spd > maxSpeed) maxSpeed = spd;
                speedSum += spd;
                speedCount++;             
            }        
        }
        
        std::vector<double> smoothEle;
        BuildElevationMedian3(t, smoothEle);
        if (smoothEle.size() == t.pts.size() && t.pts.size() >= 2) {
            double curAscM = 0.0;
            double curDescM = 0.0;
            
            // compute grade over a distance window to avoid jitter spikes.
            const double kSlopeWindowM = 25.0;        // window length for grade calculation
            const double kMinRunSegmentM = 5.0;       // for ascent/descent run accumulation
            const double kSlopeEpsilonPct = 0.1;      // ignore near-flat noise

            std::vector<double> cumDistM;
            cumDistM.resize(t.pts.size());
            cumDistM[0] = 0.0;

            for (size_t i = 1; i < t.pts.size(); ++i) {
                const double d = GetDistance(t.pts[i - 1], t.pts[i]);
                cumDistM[i] = cumDistM[i - 1] + ((d > 0.0) ? d : 0.0);
            }

            size_t j = 0;

            for (size_t i = 1; i < t.pts.size(); ++i) {
                const double segDistM = GetDistance(t.pts[i - 1], t.pts[i]);
                if (!(segDistM > 0.0)) {
                    continue;
                }

                // Move j forward to keep a window of ~kSlopeWindowM behind i.
                while ((j + 1) < i) {
                    const double d0 = cumDistM[i] - cumDistM[j];
                    const double d1 = cumDistM[i] - cumDistM[j + 1];
                    if (d1 >= kSlopeWindowM) {
                        j++;
                        continue;
                    }
                    break;
                }

                const double winDistM = cumDistM[i] - cumDistM[j];
                if (!(winDistM >= kSlopeWindowM)) {
                    continue;
                }
                
                // some GPX files omit <ele>. My xparser defaults to 0 which creates huge negative grades.
                // Filter suspect windows when the track is clearly not a sea-level track.
                const bool trackNotSeaLevel = (maxEle > 50.0);
                const bool endpointEleIsZero = (t.pts[i].ele == 0.0) || (t.pts[j].ele == 0.0);
                if (trackNotSeaLevel && endpointEleIsZero) {
                    continue;                    
                }

                const double winEleDiffM = smoothEle[i] - smoothEle[j];
                const double slopePct = (winEleDiffM / winDistM) * 100.0;

                anySlope = true;
                if (slopePct < minSlope) minSlope = slopePct;
                if (slopePct > maxSlope) maxSlope = slopePct;

                // Use the window grade sign to stabilise ascent/descent runs too.
                if (segDistM >= kMinRunSegmentM) {
                    if (slopePct > kSlopeEpsilonPct) {
                        curAscM += segDistM;
                        if (curAscM > longestAsc) longestAsc = curAscM;
                        curDescM = 0.0;
                    }
                    else if (slopePct < -kSlopeEpsilonPct) {
                        curDescM += segDistM;
                        if (curDescM > longestDesc) longestDesc = curDescM;
                        curAscM = 0.0;
                    }
                    else {
                        curAscM = 0.0;
                        curDescM = 0.0;
                    }
                }
            }
        }       
    }
    
    if (!anyPoint || out.trackCount == 0) {
        return false;    
    }
    
    out.minEleM = (minEle < 1e17) ? minEle : 0.0;
    out.maxEleM = (maxEle > -1e17) ? maxEle : 0.0;
    out.startTimeUnix = globalStart;
    out.endTimeUnix = globalEnd;
    
        if (anySpeed && speedCount > 0) {
            out.minSpeedKmh = (minSpeed < 1e17) ? minSpeed : 0.0;
            out.maxSpeedKmh = (maxSpeed > -1e17) ? maxSpeed : 0.0;
            out.avgSpeedKmh = speedSum / (double)speedCount;      
        }    
        if (anySlope) {
            out.minSlopePct = (minSlope < 1e17) ? minSlope : 0.0;
            out.maxSlopePct = (maxSlope > -1e17) ? maxSlope : 0.0;
            out.longestAscentM = longestAsc;
            out.longestDescentM = longestDesc;        
        }    
        return true;
}

static void ShowTrackInfoDialog(State & s) {
    TrackInfoSummary sum;
    if (!ComputeTrackInfoSummary(s, sum)) {
        MessageBoxW(s.hwnd, L"No track data available.", L"Track summary", MB_OK | MB_ICONINFORMATION);
        return;        
    }
    
    const bool allTracks = (s.selectedTrack < 0);
    std::wstring title = L"Track summary";
    if (!allTracks && s.selectedTrack >= 0 && s.selectedTrack < (int)s.tracks.size()) {
        if (!s.tracks[s.selectedTrack].name.empty()) {
            title = L"Track summary. " + s.tracks[s.selectedTrack].name;        
        }        
    }
    
    const double distKm = sum.distanceM / 1000.0;
    const std::wstring startStr = FormatTimestampLocal(sum.startTimeUnix);
    const std::wstring endStr = FormatTimestampLocal(sum.endTimeUnix);
    const std::wstring durStr = (sum.startTimeUnix > 0.0 && sum.endTimeUnix > 0.0 && sum.endTimeUnix >= sum.startTimeUnix)
        ? FormatDurationHhMmSs(sum.endTimeUnix - sum.startTimeUnix)
        : L"";
    
    wchar_t line0[128];
    if (allTracks) {
        swprintf(line0, 128, L"Selection: All tracks (%zu)", sum.trackCount);        
    }
    else {
        swprintf(line0, 128, L"Selection: Track %d", s.selectedTrack + 1);        
    }
    
    wchar_t line1[128];
    swprintf(line1, 128, L"Length: %.2f km", distKm);
    
    wchar_t line2[128];
    swprintf(line2, 128, L"Elevation: min %.0f m. max %.0f m", sum.minEleM, sum.maxEleM);
    
    wchar_t line3[160];
    if (sum.maxSpeedKmh > 0.0) {
        swprintf(line3, 160, L"Speed (km/h): min %.1f. avg %.1f. max %.1f", sum.minSpeedKmh, sum.avgSpeedKmh, sum.maxSpeedKmh);        
    }
    else {
        swprintf(line3, 160, L"Speed (km/h): not available");        
    }
    
    wchar_t line4[256];
    if (!startStr.empty() && !endStr.empty()) {
        if (!durStr.empty()) {
            swprintf(line4, 256, L"Time: start %s. end %s. elapsed %s", startStr.c_str(), endStr.c_str(), durStr.c_str());            
        }
        else {
            swprintf(line4, 256, L"Time: start %s. end %s", startStr.c_str(), endStr.c_str());            
        }   
    }
    else {
        swprintf(line4, 256, L"Time: not available");        
    }
    
    wchar_t line5[128];
    swprintf(line5, 128, L"Waypoints: %zu", sum.waypointCount);
    
    wchar_t line6[192];
    if (sum.longestAscentM > 0.0 || sum.longestDescentM > 0.0 || sum.maxSlopePct != 0.0 || sum.minSlopePct != 0.0) {
        swprintf(line6, 192, L"Slope: min %.1f%%. max %.1f%%", sum.minSlopePct, sum.maxSlopePct);        
    }
    else {
        swprintf(line6, 192, L"Slope: not available");
    }
    
    wchar_t line7[128];
    swprintf(line7, 128, L"Longest ascent: %.2f km", sum.longestAscentM / 1000.0);
    
    wchar_t line8[128];
    swprintf(line8, 128, L"Longest descent: %.2f km", sum.longestDescentM / 1000.0);
    
    std::wstring text;
    text.reserve(1024);
    text += line0;
    text += L"\r\n";
    text += line1;
    text += L"\r\n";
    text += line2;
    text += L"\r\n";
    text += line3;
    text += L"\r\n";
    text += line4;
    text += L"\r\n";
    text += line5;
    text += L"\r\n";
    text += line6;
    text += L"\r\n";
    text += line7;
    text += L"\r\n";
    text += line8;
    
    MessageBoxW(s.hwnd, text.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
}

static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// Conservative elevation smoothing for ascent/descent.
// Purpose: avoid bias from a per-sample elevation threshold on quantised elevation data.
// Method: apply a median-of-3 filter to elevation samples, then sum all positive/negative deltas.
static double MedianOf3(double a, double b, double c) {
    if (a > b) {
        std::swap(a, b);
    }
    if (b > c) {
        std::swap(b, c);
    }
    if (a > b) {
        std::swap(a, b);
    }
    return b;
}

static void BuildElevationMedian3(const Track& t, std::vector<double>& out) {
    out.clear();
    out.reserve(t.pts.size());

    if (t.pts.empty()) {
        return;
    }
    const size_t n = t.pts.size();
    for (size_t i = 0; i < n; ++i) {
        const double prev = (i > 0) ? t.pts[i - 1].ele : t.pts[i].ele;
        const double curr = t.pts[i].ele;
        const double next = (i + 1 < n) ? t.pts[i + 1].ele : t.pts[i].ele;
        out.push_back(MedianOf3(prev, curr, next));

    }
}

static void ComputeBounds(State& s) {
    bool init = false;
    s.totalDist = 0; s.totalAsc = 0; s.totalDesc = 0;
    s.minE = 1e6; s.maxE = -1e6;

    for (auto& t : s.tracks) {
        if (t.pts.empty()) continue;

        t.cumDist.clear(); t.cumDist.push_back(0);
        t.speed.clear();   t.speed.push_back(0.0);
        t.stats = { 0, 0, 0 };

        // Smoothed elevation series used for ascent/descent.
        std::vector<double> smoothEle;
        BuildElevationMedian3(t, smoothEle);

        // 1. Calculate Raw Speed
        for (size_t i = 0; i < t.pts.size(); ++i) {
            const auto& p = t.pts[i];
            if (!init) { s.minLat = s.maxLat = p.lat; s.minLon = s.maxLon = p.lon; init = true; }
            else {
                s.minLat = (std::min<double>)(s.minLat, p.lat); s.maxLat = (std::max<double>)(s.maxLat, p.lat);
                s.minLon = (std::min<double>)(s.minLon, p.lon); s.maxLon = (std::max<double>)(s.maxLon, p.lon);
            }
            if (p.ele < s.minE) s.minE = p.ele;
            if (p.ele > s.maxE) s.maxE = p.ele;

            if (i > 0) {
                double d = GetDistance(t.pts[i - 1], p);
                double eleDiff = p.ele - t.pts[i - 1].ele;
                const double tPrev = t.pts[i - 1].time;
                const double tCurr = p.time;
                double tDiff = tCurr - tPrev;

                t.stats.distance += d;

                // Use median-smoothed elevation to reduce noise without undercounting.
                if (smoothEle.size() == t.pts.size()) {
                    eleDiff = smoothEle[i] - smoothEle[i - 1];
                }
                if (eleDiff > 0.0) {
                    t.stats.ascent += eleDiff;
                }
                else if (eleDiff < 0.0) {
                    t.stats.descent += -eleDiff;
                }

                t.cumDist.push_back(t.stats.distance);

                double spd = 0.0;
                // Avoid infinite speed on duplicate timestamps
                if (tPrev > 0.0 && tCurr > 0.0 && tDiff > 0.001 && d > 0.0) {
                    double raw = (d / tDiff) * 3.6;
                    // Physics filter: discard Mach 1 speeds (GPS errors)
                    if (raw < 1200.0) spd = raw;
                }
                t.speed.push_back(spd);
            }
        }

        // 2. Apply Smart Smoothing (Centered Moving Average)
        // A wider window is used to suppress GPS noise (1Hz jitter).
        if (t.speed.size() > 10) {
            std::vector<double> smooth = t.speed;
            const int kWindow = 6; // +/- 6 points (13 points total)

            for (size_t i = 0; i < t.speed.size(); ++i) {
                double sum = 0.0;
                int count = 0;

                for (int offset = -kWindow; offset <= kWindow; ++offset) {
                    int idx = (int)i + offset;
                    if (idx >= 0 && idx < (int)t.speed.size()) {
                        sum += t.speed[idx];
                        count++;
                    }
                }

                if (count > 0) {
                    smooth[i] = sum / count;
                }
            }
            t.speed = smooth;
        }

        s.totalDist += t.stats.distance;
        s.totalAsc += t.stats.ascent;
        s.totalDesc += t.stats.descent;
    }
    if (s.maxE <= s.minE) s.maxE = s.minE + 1.0;

    // Bounds for waypoints (existing code...)
    for (const auto& w : s.waypoints) {
        if (!init) { s.minLat = s.maxLat = w.lat; s.minLon = s.maxLon = w.lon; init = true; }
        else {
            s.minLat = (std::min<double>)(s.minLat, w.lat); s.maxLat = (std::max<double>)(s.maxLat, w.lat);
            s.minLon = (std::min<double>)(s.minLon, w.lon); s.maxLon = (std::max<double>)(s.maxLon, w.lon);
        }
    }
}

// Updated to use D2D1_POINT_2F for float precision with high-DPI logic
static bool IsPointInElevationProfile(const State& s, const D2D1_POINT_2F& dipPt) {
    if (!s.showElevationProfile) return false;

    RECT rc;
    GetClientRect(s.hwnd, &rc);
    float dipBottom = (float)rc.bottom / s.dpiScale;
    float mapH = dipBottom - s.profileHeight;

    // Sidebar width is physical, must be converted to DIP for logic consistency
    float dipOffset = (s.tracks.size() > 1) ? ((float)s.sidebarWidth / s.dpiScale) : 0.0f;
    bool in_vertical = ((float)dipPt.y >= mapH);
    bool in_horizontal = ((float)dipPt.x >= dipOffset);

    return in_vertical && in_horizontal;
}

// Implementation of FitToWindow corrected for selection filtering and high-DPI logic
static void FitToWindow(State& s) {
    RECT rc; GetClientRect(s.hwnd, &rc);

    // Work in DIPs to ensure consistent layout across DPI settings
    float dipW_all = (float)rc.right / s.dpiScale;
    float dipH_all = (float)rc.bottom / s.dpiScale;

    // FIX: Convert physical sidebar width to DIPs before subtracting.
    // Previously, subtracting raw pixels from DIPs reduced available space excessively, causing zoom underestimation.
    float dipSidebarW = (s.tracks.size() > 1) ? ((float)s.sidebarWidth / s.dpiScale) : 0.0f;

    float mapW = dipW_all - dipSidebarW;
    float mapH = dipH_all - (s.showElevationProfile ? s.profileHeight : 0);

    if (mapW <= 0 || mapH <= 0) return;

    // Determine boundaries based on current selection
    double minLat = s.minLat, maxLat = s.maxLat, minLon = s.minLon, maxLon = s.maxLon;

    // If a specific track is selected, use only its points for the calculation
    if (s.selectedTrack != -1 && (size_t)s.selectedTrack < s.tracks.size()) {
        const auto& t = s.tracks[s.selectedTrack];
        if (!t.pts.empty()) {
            minLat = maxLat = t.pts[0].lat;
            minLon = maxLon = t.pts[0].lon;
            for (const auto& p : t.pts) {
                minLat = (std::min<double>)(minLat, p.lat);
                maxLat = (std::max<double>)(maxLat, p.lat);
                minLon = (std::min<double>)(minLon, p.lon);
                maxLon = (std::max<double>)(maxLon, p.lon);
            }
        }
    }

    // Pick zoom by binary search so bbox fits
    int lo = 3, hi = 19, best = 13;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        double x1 = lon2x(minLon, mid), y1 = lat2y(maxLat, mid);
        double x2 = lon2x(maxLon, mid), y2 = lat2y(minLat, mid);
        double w = std::abs(x2 - x1), h = std::abs(y2 - y1);

        // FIX: Increased filling factor from 0.9 to 0.96 for a tighter fit.
        // This avoids the "too zoomed out" look when the track almost fits a higher zoom level.
        if (w <= mapW * 0.96 && h <= mapH * 0.96) {
            best = mid;
            lo = mid + 1;
        }
        else {
            hi = mid - 1;
        }
    }
    s.zoom = best;

    // FIX: Calculate center based on projected PIXELS, not geographic average.
    // Mercator projection distorts latitude, so the visual center is not the lat/lon average.
    double x1 = lon2x(minLon, s.zoom);
    double x2 = lon2x(maxLon, s.zoom);
    double y1 = lat2y(maxLat, s.zoom);
    double y2 = lat2y(minLat, s.zoom);

    s.cx = (x1 + x2) / 2.0;
    s.cy = (y1 + y2) / 2.0;
}

static void EnsureRT(State& s) {
    if (s.rt || !s.cache) return;
    RECT rc; GetClientRect(s.hwnd, &rc);
    s.d2d->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(s.hwnd, D2D1::SizeU(rc.right, rc.bottom)), &s.rt);

    // IMPORTANT: Keep a stable coordinate system. 
    // We treat 96 DPI as 1:1 and apply our own s.dpiScale via SetTransform.
    // This prevents D2D from double-scaling if the OS thinks we are System Aware.
    s.rt->SetDpi(96.0f, 96.0f);

    s.rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &s.brush);
    s.rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Dashed stroke for grid
    FLOAT dashes[2] = { 4.0f, 4.0f };
    D2D1_STROKE_STYLE_PROPERTIES sp{};
    sp.dashStyle = D2D1_DASH_STYLE_CUSTOM;
    sp.lineJoin = D2D1_LINE_JOIN_MITER;
    if (!s.gridDash)
        s.d2d->CreateStrokeStyle(sp, dashes, 2, &s.gridDash);
    if (!s.trackStrokeStyle) {
        D2D1_STROKE_STYLE_PROPERTIES tsp = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND,
            D2D1_CAP_STYLE_ROUND,
            D2D1_CAP_STYLE_ROUND,
            D2D1_LINE_JOIN_ROUND,
            1.0f,
            D2D1_DASH_STYLE_SOLID,
            0.0f);
        s.d2d->CreateStrokeStyle(tsp, nullptr, 0, &s.trackStrokeStyle);
    }
    if (s.cache) {
        s.cache->SetFactories(s.wic, s.rt);
    }
}

// draws the openstreetmap attribution text
static void DrawAttribution(const State& s) {
    if (!s.rt || !s.tf) return;

    std::wstring text = s.cache->Attribution();
    RECT rc; GetClientRect(s.hwnd, &rc);
    float dipBottom = (float)rc.bottom / s.dpiScale;
    float dipRight = (float)rc.right / s.dpiScale;
    float offset = (s.tracks.size() > 1) ? (float)s.sidebarWidth : 0.0f;
    D2D1_RECT_F r = D2D1::RectF(offset + 8.f, dipBottom - 24.f, dipRight - 8.f, dipBottom - 6.f);

    D2D1_POINT_2F dipMouse = { (float)s.mousePos.x / s.dpiScale, (float)s.mousePos.y / s.dpiScale };
    if (s.isSatelliteMode && !IsPointInElevationProfile(s, dipMouse))
        s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
    else
        s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::Black, 0.6f));

    s.rt->DrawTextW(text.c_str(), (UINT32)text.size(), s.tf, r, s.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

static void DrawScale(const State& s) {
    if (!s.showScale) return;

    RECT rc; GetClientRect(s.hwnd, &rc);
    float dipBottom = (float)rc.bottom / s.dpiScale;
    float dipRight = (float)rc.right / s.dpiScale;
    float dipOffset = (s.tracks.size() > 1) ? ((float)s.sidebarWidth / s.dpiScale) : 0.0f;
    float mapW = dipRight - dipOffset;

    double px = 100; // 100 px scale
    double lon1 = x2lon(s.cx - mapW / 2 + 20, s.zoom);
    double lon2 = x2lon(s.cx - mapW / 2 + 20 + px, s.zoom);
    double lat = y2lat(s.cy, s.zoom);
    double dx = (lon2 - lon1) * (PI / 180.0) * 6371000.0 * std::cos(lat * PI / 180.0); // metres
    std::wstring label = (dx >= 1000) ? std::to_wstring((int)(dx / 1000)) + L" km" : std::to_wstring((int)dx) + L" m";

    // Draw line
    s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::Black));
    float y = dipBottom - 40.f;
    s.rt->DrawLine(D2D1::Point2F(dipOffset + 20.f, y), D2D1::Point2F(dipOffset + 20.f + (float)px, y), s.brush, 2.f);
    D2D1_RECT_F r = D2D1::RectF(dipOffset + 24.f, y + 4.f, dipOffset + 220.f, y + 24.f);
    s.rt->DrawTextW(label.c_str(), (UINT32)label.size(), s.tf, r, s.brush);
}

// DrawCoords updated to display the selected track name
static void DrawCoords(const State& s, POINT pt) {
    if (!s.showCoords) return;

    RECT rc; GetClientRect(s.hwnd, &rc);
    float dipBottom = (float)rc.bottom / s.dpiScale;
    float dipRight = (float)rc.right / s.dpiScale;
    float dipOffset = (s.tracks.size() > 1) ? ((float)s.sidebarWidth / s.dpiScale) : 0.0f;
    float dipMouseX = (float)pt.x / s.dpiScale;
    if (dipMouseX < dipOffset) return;

    float mapW = dipRight - dipOffset;
    // Vertical center must account for the elevation profile height
    float mapH = dipBottom - (s.showElevationProfile ? s.profileHeight : 0);
    float centerY = mapH / 2.0f;

    double x = s.cx - mapW / 2 + (dipMouseX - dipOffset);
    double y = s.cy - centerY + pt.y;
    double lon = x2lon(x, s.zoom), lat = y2lat(y, s.zoom);

    /*
    // --- DEBUG CODE (Visualizzabile con DebugView o in VS Output) ---
    wchar_t dbg[256];
    swprintf(dbg, 256, L"Mouse Y: %d | isSatelliteMode: %d | InProfile: %s\n",
        pt.y, s.isSatelliteMode,
        IsPointInElevationProfile(s, s.mousePos) ? L"YES" : L"NO");
    OutputDebugStringW(dbg);*/

    wchar_t buf[256];
    if (s.selectedTrack != -1 && (size_t)s.selectedTrack < s.tracks.size()) {
        const auto& t = s.tracks[s.selectedTrack];
        std::wstring name = t.name.empty() ? (L"Track " + std::to_wstring(s.selectedTrack + 1)) : t.name;
        swprintf(buf, 256, L"%s | lat %.6f lon %.6f", name.c_str(), lat, lon);
    }
    else {
        swprintf(buf, 256, L"All Tracks | lat %.6f lon %.6f", lat, lon);
    }

    // Convert physical mouse pos to DIP for the check
    D2D1_POINT_2F dipMouse = { (float)s.mousePos.x / s.dpiScale, (float)s.mousePos.y / s.dpiScale };
    if (s.isSatelliteMode && !IsPointInElevationProfile(s, dipMouse))
        s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
    else
        s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::Black));

    D2D1_RECT_F r = D2D1::RectF(dipOffset + 8.f, 8.f, dipOffset + 600.f, 28.f);
    s.rt->DrawTextW(buf, (UINT32)wcslen(buf), s.tf, r, s.brush);
}

static void DrawGrid(const State& s) {
    // Medium gray dashed grid
    s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::Gray, 0.85f));
    RECT rc; GetClientRect(s.hwnd, &rc);
    float dipBottom = (float)rc.bottom / s.dpiScale;
    float dipRight = (float)rc.right / s.dpiScale;

    float dipOffset = (s.tracks.size() > 1) ? ((float)s.sidebarWidth / s.dpiScale) : 0.0f;
    int step = 128; // every ~128 px to be less dense
    for (float x = dipOffset; x < dipRight; x += (float)step)
        s.rt->DrawLine(D2D1::Point2F(x, 0), D2D1::Point2F(x, dipBottom), s.brush, 2.0f, s.gridDash);
    for (int y = 0; y < dipBottom; y += step)
        s.rt->DrawLine(D2D1::Point2F(dipOffset, (float)y), D2D1::Point2F(dipRight, (float)y), s.brush, 2.0f, s.gridDash);
}

static void DrawTiles(const State& s) {
    if (!s.tiles) { if (s.showGridWhenNoTiles) DrawGrid(s); return; }
    if (!s.cache) { if (s.showGridWhenNoTiles) DrawGrid(s); return; }

    RECT rc; GetClientRect(s.hwnd, &rc);
    float dipBottom = (float)rc.bottom / s.dpiScale;
    float dipRight = (float)rc.right / s.dpiScale;
    float dipOffset = (s.tracks.size() > 1) ? ((float)s.sidebarWidth / s.dpiScale) : 0.0f;
    float mapW = dipRight - dipOffset;
    const int z = s.zoom;

    // Calculate map height and vertical centre to match DrawTrack logic
    // This prevents misalignment when the elevation profile is visible
    float mapH = dipBottom - (s.showElevationProfile ? s.profileHeight : 0);
    float centerY = mapH / 2.0f;

    // Determine visible tile range
    double left = s.cx - mapW / 2, top = s.cy - centerY;
    int x0 = (int)std::floor(left / 256.0);
    int y0 = (int)std::floor(top / 256.0);
    int xtiles = (int)mapW / 256 + 3;
    int ytiles = (int)dipBottom / 256 + 3;
    bool drewAny = false;
    for (int dy = 0; dy < ytiles; ++dy) {
        for (int dx = 0; dx < xtiles; ++dx) {
            int tx = x0 + dx;
            int ty = y0 + dy;
            int n = 1 << z;
            int wx = (tx % n + n) % n; // wrap x
            if (ty < 0 || ty >= n) continue;
            TileKey k{ z, wx, ty };
            ID2D1Bitmap* bmp = nullptr;
            if (s.cache->TryGetBitmap(k, &bmp)) {
                drewAny = true;
                float x = (float)(tx * 256 - left) + dipOffset;
                float y = (float)(ty * 256 - top);
                s.rt->DrawBitmap(bmp, D2D1::RectF(x, y, x + 256.f, y + 256.f));
                bmp->Release();
            }
            else {
                // Priority: Manhattan distance from centre tile
                int pr = abs(tx - (int)floor((s.cx / 256.0))) + abs(ty - (int)floor((s.cy / 256.0)));
                s.cache->EnqueuePriority(k, pr);
            }
        }
    }
    if (!drewAny && s.showGridWhenNoTiles) DrawGrid(s);

    // PREFETCH RINGS: enqueue an extra ring of tiles around the visible area
    int rings = (std::max<int>)(0, s.opt.prefetchRings);
    if (s.tiles && rings > 0) {
        int n = 1 << z;
        int leftTile = x0, topTile = y0;
        int rightTile = x0 + xtiles - 1;
        int bottomTile = y0 + ytiles - 1;
        int cxTile = (int)std::floor(s.cx / 256.0);
        int cyTile = (int)std::floor(s.cy / 256.0);
        for (int r = 1; r <= rings; r++) {
            // Top and bottom rows of the ring
            for (int tx = leftTile - r; tx <= rightTile + r; ++tx) {
                int ttx = (tx % n + n) % n;
                int tyTop = topTile - r;
                int tyBot = bottomTile + r;
                if (tyTop >= 0 && tyTop < n) {
                    int pr = abs(tx - cxTile) + abs(tyTop - cyTile) + r * 100;
                    s.cache->EnqueuePriority(TileKey{ z, ttx, tyTop }, pr);
                }
                if (tyBot >= 0 && tyBot < n) {
                    int pr = abs(tx - cxTile) + abs(tyBot - cyTile) + r * 100;
                    s.cache->EnqueuePriority(TileKey{ z, ttx, tyBot }, pr);
                }
            }
            // Left and right columns of the ring
            for (int ty = topTile; ty <= bottomTile; ++ty) {
                if (ty < 0 || ty >= n) continue;
                int txL = leftTile - r;
                int txR = rightTile + r;
                int ttxL = (txL % n + n) % n;
                int ttxR = (txR % n + n) % n;
                int prL = abs(txL - cxTile) + abs(ty - cyTile) + r * 100;
                int prR = abs(txR - cxTile) + abs(ty - cyTile) + r * 100;
                s.cache->EnqueuePriority(TileKey{ z, ttxL, ty }, prL);
                s.cache->EnqueuePriority(TileKey{ z, ttxR, ty }, prR);
            }
        }
    }
}

static void DrawTrack(const State& s) {
    RECT rc; GetClientRect(s.hwnd, &rc);

    float dipBottom = (float)rc.bottom / s.dpiScale;
    float dipRight = (float)rc.right / s.dpiScale;
    float dipOffset = (s.tracks.size() > 1) ? ((float)s.sidebarWidth / s.dpiScale) : 0.0f;
    float mapWidth = dipRight - dipOffset;
    float mapH = dipBottom - (s.showElevationProfile ? s.profileHeight : 0);
    float centerY = mapH / 2.0f;

    for (size_t tIdx = 0; tIdx < s.tracks.size(); ++tIdx) {
        // Selection filter
        if (s.selectedTrack != -1 && s.selectedTrack != (int)tIdx) continue;

        const auto& t = s.tracks[tIdx];
        if (t.pts.size() < 2) continue;
        // Set colour based on track index
        const D2D1_COLOR_F baseCol = GetTrackColor(tIdx);
        s.brush->SetColor(baseCol);

        double slopeBucketStartDist = 0.0;
        double slopeBucketStartEle = 0.0;
        double slopeBucketRunningDist = 0.0;
        double slopeBucketLen = gpxlister_slope::AdaptiveSampleMetresFromGrade(0.0);
        double slopeBucketNextDist = 0.0;
        D2D1_COLOR_F slopeBucketColour = baseCol;

        if (s.showSlopeColouringOnTrack) {
            if (!t.cumDist.empty()) {
                slopeBucketStartDist = t.cumDist[0];
                slopeBucketRunningDist = t.cumDist[0];
            }
            slopeBucketStartEle = t.pts[0].ele;
            slopeBucketLen = gpxlister_slope::AdaptiveSampleMetresFromGrade(0.0);
            slopeBucketNextDist = slopeBucketStartDist + slopeBucketLen;

            if (t.pts.size() >= 2) {
                double initSegDist = 0.0;
                if (1 < t.cumDist.size()) {
                    initSegDist = t.cumDist[1] - t.cumDist[0];
                }
                if (initSegDist <= 0.0) {
                    initSegDist = GetDistance(t.pts[0], t.pts[1]);
                }

                const double initDeltaEle = t.pts[1].ele - t.pts[0].ele;
                const double initGrade = gpxlister_slope::SafeGradePercent(initDeltaEle, initSegDist);
                slopeBucketColour = gpxlister_slope::BlendTrackColour(baseCol, initGrade);
            }
        }

        for (size_t i = 1; i < t.pts.size(); ++i) {
            double x1 = lon2x(t.pts[i - 1].lon, s.zoom) - (s.cx - mapWidth / 2.0) + dipOffset;
            double y1 = lat2y(t.pts[i - 1].lat, s.zoom) - (s.cy - centerY);
            double x2 = lon2x(t.pts[i].lon, s.zoom) - (s.cx - mapWidth / 2.0) + dipOffset;
            double y2 = lat2y(t.pts[i].lat, s.zoom) - (s.cy - centerY);
            if (s.showSlopeColouringOnTrack) {
                double segmentDist = 0.0;
                if (i < t.cumDist.size()) {
                    segmentDist = t.cumDist[i] - t.cumDist[i - 1];
                    slopeBucketRunningDist = t.cumDist[i];
                }
                if (segmentDist <= 0.0) {
                    segmentDist = GetDistance(t.pts[i - 1], t.pts[i]);
                    slopeBucketRunningDist += segmentDist;
                }

                if (slopeBucketRunningDist >= slopeBucketNextDist) {
                    const double spanDist = slopeBucketRunningDist - slopeBucketStartDist;
                    const double spanEle = t.pts[i].ele - slopeBucketStartEle;
                    const double spanGrade = gpxlister_slope::SafeGradePercent(spanEle, spanDist);
                    slopeBucketColour = gpxlister_slope::BlendTrackColour(baseCol, spanGrade);

                    slopeBucketStartDist = slopeBucketRunningDist;
                    slopeBucketStartEle = t.pts[i].ele;
                    slopeBucketLen = gpxlister_slope::AdaptiveSampleMetresFromGrade(spanGrade);
                    slopeBucketNextDist = slopeBucketStartDist + slopeBucketLen;
                }

                s.brush->SetColor(slopeBucketColour);
            }
            s.rt->DrawLine(D2D1::Point2F((float)x1, (float)y1), D2D1::Point2F((float)x2, (float)y2), s.brush, s.trackLineWidth, s.trackStrokeStyle);
        }
    }
}

// Elevation Rendering Function. Complete segmented multi-colour Elevation Profile with Gap Prevention.
static void DrawElevationProfile(State& s) {
    // Safety check: ensure we have a valid render target and data
    if (!s.showElevationProfile || !s.rt || !s.tf || s.tracks.empty()) return;

    RECT rc; GetClientRect(s.hwnd, &rc);
    float dipBottom = (float)rc.bottom / s.dpiScale;
    float dipRight = (float)rc.right / s.dpiScale;
    float dipOffset = (s.tracks.size() > 1) ? ((float)s.sidebarWidth / s.dpiScale) : 0.0f;
    float width = dipRight - dipOffset;
    float height = s.profileHeight;

    D2D1_RECT_F panelRect = D2D1::RectF(dipOffset, dipBottom - height, dipRight, dipBottom);

    // 1. Draw Panel Background and Border
    s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::White, 0.9f));
    s.rt->FillRectangle(panelRect, s.brush);
    s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::DarkSlateGray));
    s.rt->DrawRectangle(panelRect, s.brush, 0.5f);

    // 2. Determine Local Elevation Scale and Distances based on selection
    double minE = 1e6, maxE = -1e6, plotTotalDist = 0, plotTotalAsc = 0, plotTotalDesc = 0;
    for (size_t i = 0; i < s.tracks.size(); ++i) {
        if (s.selectedTrack != -1 && s.selectedTrack != (int)i) continue;
        const auto& t = s.tracks[i];
        plotTotalDist += t.stats.distance;
        plotTotalAsc += t.stats.ascent;
        plotTotalDesc += t.stats.descent;
        for (const auto& p : t.pts) {
            minE = (std::min<double>)(minE, p.ele);
            maxE = (std::max<double>)(maxE, p.ele);
        }
    }

    // Preserve the true track extrema for the statistics overlay.
    // The rendering scale may be adjusted for perfectly flat tracks, but the displayed max/min should remain accurate.
    double displayMinE = minE;
    double displayMaxE = maxE;

    // Include waypoint elevations in displayed extrema when present.
    // Waypoints are part of the GPX snapshot and may contain a lower/higher elevation than sampled track points.
    // Heuristic: ignore zero-valued waypoint elevations, as they are commonly used as the "not provided" default.
    for (const auto& w : s.waypoints) {
        if (w.ele != 0.0 && std::isfinite(w.ele)) {
            if (w.ele < displayMinE) displayMinE = w.ele;
            if (w.ele > displayMaxE) displayMaxE = w.ele;
        }
    }

    // Prevent division by zero if the track is perfectly flat
    if (maxE <= minE) maxE = minE + 1.0;
    if (plotTotalDist <= 0) return;

    // Layout constants
    float leftPadding = 50.0f; // Space for Y-axis labels
    float margin = 10.0f;      // General padding
    float plotW = width - (leftPadding + margin); // Active graph width
    float plotH = height - (margin * 3.0f);

    // 3. Draw Y-Axis Ticks and Horizontal Grid Lines
    for (int i = 0; i < 4; ++i) {
        double eleVal = minE + (maxE - minE) * (double)i / 3.0;
        float yp = panelRect.bottom - margin - (float)((eleVal - minE) / (maxE - minE) * plotH);

        // Draw light gray grid line
        s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::Black, 0.1f));
        s.rt->DrawLine(D2D1::Point2F(dipOffset + leftPadding, yp), D2D1::Point2F(dipOffset + leftPadding + plotW, yp), s.brush, 0.5f);

        // Render elevation text (e.g., "450m")
        wchar_t valBuf[32]; swprintf(valBuf, 32, L"%.0fm", eleVal);
        s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::DimGray));
        D2D1_RECT_F textRect = D2D1::RectF(dipOffset + 5.0f, yp - 7.0f, dipOffset + leftPadding - 5.0f, yp + 7.0f);
        s.rt->DrawTextW(valBuf, (UINT32)wcslen(valBuf), s.tf, textRect, s.brush);
    }

    // 3b. Draw Right-Axis (Speed) Ticks
    double maxSpeed = 0.0;

    if (s.showSpeedProfile) {
        // Collect valid speeds to find the 98th percentile
        std::vector<double> samples;
        samples.reserve(4096);

        for (size_t i = 0; i < s.tracks.size(); ++i) {
            if (s.selectedTrack != -1 && s.selectedTrack != (int)i) continue;
            for (double v : s.tracks[i].speed) {
                if (v > 0.5) samples.push_back(v); // Ignore stops
            }
        }

        if (!samples.empty()) {
            std::sort(samples.begin(), samples.end());
            size_t idx = (size_t)(samples.size() * 0.98); // 98th percentile
            if (idx >= samples.size()) idx = samples.size() - 1;
            maxSpeed = samples[idx] * 1.1; // Add 10% headroom
        }

        // Floor: Walking is ~5km/h. Don't zoom in closer than 0-5.
        if (maxSpeed < 5.0) maxSpeed = 5.0;

        // Draw Axis ticks (Right Side)
        float rightAxisX = dipOffset + leftPadding + plotW;
        for (int i = 0; i <= 4; ++i) {
            double spdVal = maxSpeed * (double)i / 4.0;
            float yp = panelRect.bottom - margin - (float)(spdVal / maxSpeed * plotH);

            s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::Magenta, 0.8f));
            s.rt->DrawLine(D2D1::Point2F(rightAxisX, yp), D2D1::Point2F(rightAxisX + 5.0f, yp), s.brush, 1.0f);
            wchar_t buf[32]; swprintf(buf, 32, L"%.0f", spdVal);

            D2D1_RECT_F tr = D2D1::RectF(rightAxisX + 8.0f, yp - 7.0f, rightAxisX + 40.0f, yp + 7.0f);
            s.rt->DrawTextW(buf, (UINT32)wcslen(buf), s.tf, tr, s.brush);
        }
        // Label
        D2D1_RECT_F labelR = D2D1::RectF(rightAxisX, panelRect.top + 5.0f, rightAxisX + 50.0f, panelRect.top + 25.0f);
        s.rt->DrawTextW(L"km/h", 4, s.tf, labelR, s.brush);
    }

    // 4. Render Track Segments with Slope-Based Colouring
    double distAcc = 0;
    for (size_t tIdx = 0; tIdx < s.tracks.size(); ++tIdx) {
        if (s.selectedTrack != -1 && s.selectedTrack != (int)tIdx) continue;
        const auto& t = s.tracks[tIdx];
        for (size_t i = 1; i < t.pts.size(); ++i) {
            // Formula: Offset + LeftPadding + (RelativeDist / TotalDist) * PlotWidth
            float x1 = dipOffset + leftPadding + (float)((distAcc + t.cumDist[i - 1]) / plotTotalDist * plotW);
            float x2 = dipOffset + leftPadding + (float)((distAcc + t.cumDist[i]) / plotTotalDist * plotW);
            float y1 = panelRect.bottom - margin - (float)((t.pts[i - 1].ele - minE) / (maxE - minE) * plotH);
            float y2 = panelRect.bottom - margin - (float)((t.pts[i].ele - minE) / (maxE - minE) * plotH);

            // Slope analysis logic
            double segmentDist = t.cumDist[i] - t.cumDist[i - 1];
            double grade = (segmentDist > 1e-3) ? (t.pts[i].ele - t.pts[i - 1].ele) / segmentDist * 100.0 : 0.0;

            if (grade > 8.0) s.brush->SetColor(D2D1::ColorF(0.8f, 0.0f, 0.0f));
            else if (grade > 2.0) s.brush->SetColor(D2D1::ColorF(0.9f, 0.6f, 0.0f));
            else if (grade < -2.0) s.brush->SetColor(D2D1::ColorF(0.0f, 0.4f, 0.8f));
            else s.brush->SetColor(D2D1::ColorF(0.0f, 0.7f, 0.0f));

            s.rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), s.brush, 2.0f);
        }
        distAcc += t.stats.distance;
    }

    // 4b. Draw Speed Polyline (with Clipping)
    if (s.showSpeedProfile && maxSpeed > 0) {
        s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::Magenta, 0.6f));
        // Clip spikes that exceed maxSpeed
        s.rt->PushAxisAlignedClip(
            D2D1::RectF(dipOffset, panelRect.top + margin, dipRight, panelRect.bottom - margin),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE
        );

        double distAcc = 0;
        for (size_t tIdx = 0; tIdx < s.tracks.size(); ++tIdx) {
            if (s.selectedTrack != -1 && s.selectedTrack != (int)tIdx) continue;
            const auto& t = s.tracks[tIdx];

            if (t.pts.size() < 2) { distAcc += t.stats.distance; continue; }

            ID2D1PathGeometry* geo = nullptr;
            s.d2d->CreatePathGeometry(&geo);
            ID2D1GeometrySink* sink = nullptr;
            geo->Open(&sink);

            bool started = false;
            for (size_t i = 0; i < t.pts.size(); ++i) {
                float x = dipOffset + leftPadding + (float)((distAcc + t.cumDist[i]) / plotTotalDist * plotW);
                // Clamp Y to maxSpeed so line doesn't go off canvas
                double val = (std::min<double>)(t.speed[i], maxSpeed);
                float y = panelRect.bottom - margin - (float)(val / maxSpeed * plotH);

                if (!started) {
                    sink->BeginFigure(D2D1::Point2F(x, y), D2D1_FIGURE_BEGIN_HOLLOW);
                    started = true;
                }
                else {
                    sink->AddLine(D2D1::Point2F(x, y));
                }
            }
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            s.rt->DrawGeometry(geo, s.brush, 1.5f);
            sink->Release();
            geo->Release();
            distAcc += t.stats.distance;
        }
        s.rt->PopAxisAlignedClip();
    }

    // 5. Render Statistics Overlay
    wchar_t statsBuf[512];
    swprintf(statsBuf, 512, L"Dist: %.2f km | Ascent: +%.0f m | Descent: -%.0f m | Maximum: %.0f m | Minimum: %.0f m",
        plotTotalDist / 1000.0, plotTotalAsc, plotTotalDesc, displayMaxE, displayMinE);
    s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::Black));
    D2D1_RECT_F textR = D2D1::RectF(panelRect.left + leftPadding, panelRect.top + 4.0f, panelRect.right, panelRect.top + 24.0f);
    s.rt->DrawTextW(statsBuf, (UINT32)wcslen(statsBuf), s.tf, textR, s.brush);
}

//Renders waypoints as markers on the map
static void DrawWaypoints(const State& s) {
    if (s.waypoints.empty()) return;

    RECT rc; GetClientRect(s.hwnd, &rc);
    float dipBottom = (float)rc.bottom / s.dpiScale;
    float dipRight = (float)rc.right / s.dpiScale;
    float dipOffset = (s.tracks.size() > 1) ? ((float)s.sidebarWidth / s.dpiScale) : 0.0f;
    float mapWidth = dipRight - dipOffset;
    float mapH = dipBottom - (s.showElevationProfile ? s.profileHeight : 0);
    float centerY = mapH / 2.0f;

    for (const auto& w : s.waypoints) {
        double x = lon2x(w.lon, s.zoom) - (s.cx - mapWidth / 2.0) + dipOffset;
        double y = lat2y(w.lat, s.zoom) - (s.cy - centerY);

        // Simple visibility check (loose bounds)
        if (x < dipOffset - 20 || x > dipRight + 20 || y < -20 || y > mapH + 20) continue;

        // Draw a marker (Circle with border)
        // Note: Without an asset system, we map "sym" to basic colours or use a default.
        D2D1_COLOR_F fillCol = D2D1::ColorF(D2D1::ColorF::Yellow);

        // Simple heuristic for common GPX symbols
        if (w.sym.find(L"Flag") != std::wstring::npos) fillCol = D2D1::ColorF(D2D1::ColorF::Red);
        else if (w.sym.find(L"Residence") != std::wstring::npos) fillCol = D2D1::ColorF(D2D1::ColorF::Blue);
        else if (w.sym.find(L"Parking") != std::wstring::npos) fillCol = D2D1::ColorF(D2D1::ColorF::Gray);

        s.brush->SetColor(fillCol);
        s.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F((float)x, (float)y), 6.0f, 6.0f), s.brush);

        s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::Black));
        s.rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F((float)x, (float)y), 6.0f, 6.0f), s.brush, 1.5f);
    }
}

static void OnPaint(State& s) {
    EnsureRT(s);
    if (!s.rt) return;
    s.rt->BeginDraw();

    // Apply global scaling to the render target to match Windows scaling
    // This allows us to use logical DIPs in all Draw calls.
    s.rt->SetTransform(D2D1::Matrix3x2F::Scale(s.dpiScale, s.dpiScale));

    RECT rc; GetClientRect(s.hwnd, &rc);
    float dipBottom = (float)rc.bottom / s.dpiScale;
    float dipRight = (float)rc.right / s.dpiScale;
    float dipOffset = (s.tracks.size() > 1) ? ((float)s.sidebarWidth / s.dpiScale) : 0.0f;

    D2D1_RECT_F mapArea = D2D1::RectF(dipOffset, 0, dipRight, dipBottom);
    s.rt->PushAxisAlignedClip(mapArea, D2D1_ANTIALIAS_MODE_ALIASED);

    // Convert physical mouse position to DIPs for hit-testing
    D2D1_POINT_2F dipMouse = { (float)s.mousePos.x / s.dpiScale, (float)s.mousePos.y / s.dpiScale };

    s.rt->Clear(D2D1::ColorF(1, 1, 1, 1));
    DrawTiles(s); // Uses dipOffset and dipRight internally
    DrawTrack(s); // Uses dipOffset and dipRight internally

    //Draw Waypoints on top of tracks
    DrawWaypoints(s);

    //Draw Grid Overlay (if enabled)
    DrawScale(s);

    POINT pt; GetCursorPos(&pt); ScreenToClient(s.hwnd, &pt); // physical for DrawCoords
    DrawCoords(s, pt);
    DrawAttribution(s);
    DrawElevationProfile(s);

    //Direct Point Access Logic (Tooltips)
    const GpxPoint* hp = nullptr;
    const GpxWaypoint* hw = nullptr; // Pointer for hovered waypoint

    float mapW = dipRight - dipOffset;
    float mapH = dipBottom - (s.showElevationProfile ? s.profileHeight : 0);
    float centerY = mapH / 2.0f;

    // 1. Check if the mouse is hovering over a Waypoint (Priority over tracks)
    // We re-calculate this here to avoid adding complex state indices.
    float minSafeDist = 15.0f;
    for (const auto& w : s.waypoints) {
        double px = lon2x(w.lon, s.zoom) - (s.cx - mapW / 2.0) + dipOffset;
        double py = lat2y(w.lat, s.zoom) - (s.cy - centerY);
        float d = std::hypot((float)(dipMouse.x - px), (float)(dipMouse.y - py));
        if (d < minSafeDist) {
            hw = &w;
            break; // Found the closest waypoint
        }
    }

    // 2. If no waypoint is hovered, check if a Track Point is hovered
    if (!hw && s.hoverTrackIdx >= 0 && s.hoverTrackIdx < (int)s.tracks.size()) {
        const auto& t = s.tracks[s.hoverTrackIdx];
        if (s.hoverPointIdx >= 0 && s.hoverPointIdx < (int)t.pts.size()) {
            // Only show hover if the track is currently visible
            if (s.selectedTrack == -1 || s.selectedTrack == s.hoverTrackIdx) {
                hp = &t.pts[s.hoverPointIdx];
            }
        }
    }

    // 3. Render the Tooltip
    if (hw) {
        // --- WAYPOINT LOGIC ---
        //Elevation Profile Vertical Line (Snap-to-Track)
        if (IsPointInElevationProfile(s, dipMouse)) {
            float leftPadding = 50.0f, margin = 10.0f;
            float plotW = mapW - (leftPadding + margin);

            double bestGeoDist = 1e9;
            double snappedProfileDist = -1.0;
            double currentOffset = 0;
            double totalPlotDist = 0;

            // Helper struct for distance calc
            GpxPoint wpPt = { hw->lat, hw->lon, hw->ele };

            for (size_t i = 0; i < s.tracks.size(); ++i) {
                if (s.selectedTrack != -1 && s.selectedTrack != (int)i) continue;

                const auto& t = s.tracks[i];
                totalPlotDist += t.stats.distance;

                // Search for closest point on this track to the waypoint
                for (size_t j = 0; j < t.pts.size(); ++j) {
                    double d = GetDistance(t.pts[j], wpPt);
                    if (d < bestGeoDist) {
                        bestGeoDist = d;
                        snappedProfileDist = currentOffset + t.cumDist[j];
                    }
                }
                currentOffset += t.stats.distance;
            }

            // Draw line if we found a match within 250m and have valid plot dimensions
            if (totalPlotDist > 0 && snappedProfileDist >= 0 && bestGeoDist < 250.0) {
                float hx = dipOffset + leftPadding + (float)(snappedProfileDist / totalPlotDist * plotW);
                s.brush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.5f));
                s.rt->DrawLine(D2D1::Point2F(hx, dipBottom - s.profileHeight),
                    D2D1::Point2F(hx, dipBottom), s.brush, 1.0f);
            }
        }

        //Draw Waypoint Tooltip
        float mx = (float)(lon2x(hw->lon, s.zoom) - (s.cx - mapW / 2.0) + dipOffset);
        float my = (float)(lat2y(hw->lat, s.zoom) - (s.cy - centerY));

        // Draw highlight ring
        s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::OrangeRed));
        s.rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(mx, my), 9.0f, 9.0f), s.brush, 2.0f);

        // Prepare text
        std::wstring tip = hw->name;
        if (tip.empty()) tip = L"Waypoint";
        if (!hw->sym.empty()) tip += L" (" + hw->sym + L")";

        //Append Elevation to tooltip
        wchar_t eleBuf[32];
        swprintf(eleBuf, 32, L" | %.1fm", hw->ele);
        tip += eleBuf;

        // Append localized time if available
        std::wstring timeStr = FormatTimestampLocal(hw->time);
        if (!timeStr.empty()) {
            tip += L"\n" + timeStr;
        }

        // Adjust tooltip height for the extra line
        float rectHeight = timeStr.empty() ? 25.0f : 45.0f;
        D2D1_RECT_F tipR = D2D1::RectF((float)dipMouse.x + 15.0f, (float)dipMouse.y - rectHeight, (float)dipMouse.x + 250.0f, (float)dipMouse.y);

        // Draw white background for text for readability
        s.brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.8f));
        s.rt->FillRectangle(tipR, s.brush);

        // Adaptive tooltip text colour based on map mode
        if (s.isSatelliteMode && !IsPointInElevationProfile(s, dipMouse)) {
            s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
        }
        else {
            s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::Black));
        }
        s.rt->DrawTextW(tip.c_str(), (UINT32)tip.size(), s.tf, tipR, s.brush);
    }
    else if (hp) {
        // Existing Track Point Tooltip Logic
        float mx = (float)(lon2x(hp->lon, s.zoom) - (s.cx - mapW / 2.0) + dipOffset);
        float my = (float)(lat2y(hp->lat, s.zoom) - (s.cy - centerY));

        s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::Red));
        s.rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(mx, my), 6.0f, 6.0f), s.brush, 2.0f);

        // Elevation profile vertical line should be shown when the mouse is over the track on the map.
        // This is opt-in via the existing elevation toggle. Default behaviour remains unchanged when the
        // elevation profile is disabled.
        if (s.showElevationProfile && !IsPointInElevationProfile(s, dipMouse)) {
            float leftPadding = 50.0f, margin = 10.0f;
            float plotW = mapW - (leftPadding + margin);
            
            // Calculate exact distance along the plotted segments
            double hoverDist = 0, plotTotalDist = 0;
            for (size_t i = 0; i < s.tracks.size(); ++i) {
                if (s.selectedTrack == -1 || s.selectedTrack == (int)i) {
                    if ((int)i == s.hoverTrackIdx) {
                        hoverDist = plotTotalDist + s.tracks[i].cumDist[s.hoverPointIdx];
                    }
                    plotTotalDist += s.tracks[i].stats.distance;
                }
            }
            if (plotTotalDist > 0) {
                float hx = dipOffset + leftPadding + (float)(hoverDist / plotTotalDist * plotW);
                s.brush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.5f));
                s.rt->DrawLine(D2D1::Point2F(hx, dipBottom - s.profileHeight), D2D1::Point2F(hx, dipBottom), s.brush, 1.0f);
            }
        }



        if (IsPointInElevationProfile(s, dipMouse)) {
            float leftPadding = 50.0f, margin = 10.0f;
            float plotW = mapW - (leftPadding + margin);

            // Calculate exact distance along the plotted segments
            double hoverDist = 0, plotTotalDist = 0;
            for (size_t i = 0; i < s.tracks.size(); ++i) {
                if (s.selectedTrack == -1 || s.selectedTrack == (int)i) {
                    if ((int)i == s.hoverTrackIdx) {
                        hoverDist = plotTotalDist + s.tracks[i].cumDist[s.hoverPointIdx];
                    }
                    plotTotalDist += s.tracks[i].stats.distance;
                }
            }

            if (plotTotalDist > 0) {
                float hx = dipOffset + leftPadding + (float)(hoverDist / plotTotalDist * plotW);
                s.brush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.5f));
                s.rt->DrawLine(D2D1::Point2F(hx, dipBottom - s.profileHeight), D2D1::Point2F(hx, dipBottom), s.brush, 1.0f);
            }
        }

        wchar_t hoverBuf[128];
        std::wstring timeStr = FormatTimestampLocal(hp->time);

        double currentSpeed = 0.0;

        if (s.hoverTrackIdx >= 0 && s.hoverTrackIdx < (int)s.tracks.size()) {
            const auto& t = s.tracks[s.hoverTrackIdx];
            // Ensure index safety
            if (s.hoverPointIdx >= 0 && s.hoverPointIdx < (int)t.speed.size()) {
                currentSpeed = t.speed[s.hoverPointIdx];
            }
        }

        // Display elevation and speed, adding the localized date/time on the third line if available.
        if (!timeStr.empty()) {
            swprintf(hoverBuf, 128, L"%.1f m\n%.1f km/h\n%s", hp->ele, currentSpeed, timeStr.c_str());
        }
        else {
            swprintf(hoverBuf, 128, L"%.1f m\n%.1f km/h", hp->ele, currentSpeed);
        }

        // Adjust tooltip rectangle height based on the presence of the time string.
        float rectHeight = timeStr.empty() ? 25.0f : 45.0f;
        float rectWidth = timeStr.empty() ? 100.0f : 180.0f;

        D2D1_RECT_F tipR = D2D1::RectF((float)dipMouse.x + 15.0f, (float)dipMouse.y - rectHeight, (float)dipMouse.x + rectWidth, (float)dipMouse.y);
        if (s.isSatelliteMode && !IsPointInElevationProfile(s, dipMouse)) {
            s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
        }
        else {
            s.brush->SetColor(D2D1::ColorF(D2D1::ColorF::Black));
        }

        s.rt->DrawTextW(hoverBuf, (UINT32)wcslen(hoverBuf), s.tf, tipR, s.brush);
    }

    s.rt->PopAxisAlignedClip();
    s.rt->EndDraw();
}

static void OnSize(State& s, int w, int h) {
    // Layout logic for the Sidebar
    if (s.tracks.size() > 1) {
        // Ensure sidebarWidth remains within sensible bounds during resizing
        s.sidebarWidth = (std::max<int>)(50, (std::min<int>)(s.sidebarWidth, w / 2));

        if (!s.hwndList) {
            s.hwndList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
                0, 0, s.sidebarWidth, h, s.hwnd, (HMENU)101, g_hInst, NULL);
            UpdateSidebarContent(s);
        }
        MoveWindow(s.hwndList, 0, 0, s.sidebarWidth, h, TRUE);
        if (s.rt) s.rt->Resize(D2D1::SizeU(w, h));
    }
    else {
        if (s.rt) s.rt->Resize(D2D1::SizeU(w, h));
    }
}

static void OnMouse(State& s, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Single GetClientRect at the top level of the function for efficiency and consistency
    RECT rc; GetClientRect(s.hwnd, &rc);
    float dipRight = (float)rc.right / s.dpiScale;
    float dipBottom = (float)rc.bottom / s.dpiScale;
    float dipOffset = (s.tracks.size() > 1) ? ((float)s.sidebarWidth / s.dpiScale) : 0.0f;
    float dipMapW = dipRight - dipOffset;

    POINT mouse = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    D2D1_POINT_2F dipMouse = { (float)mouse.x / s.dpiScale, (float)mouse.y / s.dpiScale };

    // STORE PHYSICAL for OnPaint
    s.mousePos = mouse;

    switch (msg) {
    case WM_LBUTTONDOWN: {
        if (s.tracks.size() > 1 && std::abs(dipMouse.x - (float)s.sidebarWidth / s.dpiScale) < 7) {
            SetFocus(s.hwnd); // Ensure we have focus
            s.resizingSidebar = true;
            SetCapture(s.hwnd);
            break;
        }

        if (dipMouse.x < (float)s.sidebarWidth / s.dpiScale) break;

        // 2. Map Panning Initiation
        SetFocus(s.hwnd);
        s.panning = true;
        s.panStart = { (LONG)dipMouse.x, (LONG)dipMouse.y }; // Store DIP for logic
        s.panCx = s.cx;
        s.panCy = s.cy;
        SetCapture(s.hwnd);
        break;
    }
    case WM_MOUSEMOVE: {
        s.hoverTrackIdx = -1;
        s.hoverPointIdx = -1;

        if (s.resizingSidebar) {
            s.sidebarWidth = (std::max<int>)(50, (std::min<int>)((int)dipMouse.x, (int)(rc.right / 2)));
            OnSize(s, rc.right, rc.bottom);
            InvalidateRect(s.hwnd, NULL, FALSE);
            break;
        }

        if (s.panning) {
            // Delta is DIP
            s.cx = s.panCx - (double)(dipMouse.x - (float)s.panStart.x);
            s.cy = s.panCy - (double)(dipMouse.y - (float)s.panStart.y);
            InvalidateRect(s.hwnd, NULL, FALSE);
            break;
        }
        if (IsPointInElevationProfile(s, dipMouse)) {
            float leftPadding = 50.0f, margin = 10.0f;
            float plotW = dipMapW - (leftPadding + margin);
            double plotTotalDist = 0;
            for (size_t i = 0; i < s.tracks.size(); ++i) {
                if (s.selectedTrack == -1 || s.selectedTrack == (int)i) plotTotalDist += s.tracks[i].stats.distance;
            }

            double targetDist = ((double)dipMouse.x - (double)dipOffset - (double)leftPadding) / (double)plotW * plotTotalDist;

            if (targetDist >= 0 && targetDist <= plotTotalDist) {
                double curAccumulated = 0;
                for (size_t i = 0; i < s.tracks.size(); ++i) {
                    if (s.selectedTrack == -1 || s.selectedTrack == (int)i) {
                        const auto& t = s.tracks[i];
                        if (targetDist <= curAccumulated + t.stats.distance) {
                            double localDist = targetDist - curAccumulated;
                            for (size_t j = 0; j < t.pts.size(); ++j) {
                                if (t.cumDist[j] >= localDist) {
                                    s.hoverTrackIdx = (int)i;
                                    s.hoverPointIdx = (int)j;
                                    break;
                                }
                            }
                            break;
                        }
                        curAccumulated += t.stats.distance;
                    }
                }
            }
        }
        else if (dipMouse.x >= (float)s.sidebarWidth / s.dpiScale) {
            float minSafeDist = 15.0f; // Threshold in pixels
            float mapH = dipBottom - (s.showElevationProfile ? s.profileHeight : 0);
            float centerY = mapH / 2.0f;
            float halfMapW = dipMapW / 2.0f;

            s.hoverTrackIdx = -1;
            s.hoverPointIdx = -1;

            //Check for Waypoint Hover (Highest Priority)
            int hoverWptIdx = -1;
            for (size_t i = 0; i < s.waypoints.size(); ++i) {
                const auto& w = s.waypoints[i];
                double px = lon2x(w.lon, s.zoom) - (s.cx - halfMapW) + dipOffset;
                double py = lat2y(w.lat, s.zoom) - (s.cy - centerY);
                float d = std::hypot((float)(dipMouse.x - px), (float)(dipMouse.y - py));
                if (d < minSafeDist) {
                    minSafeDist = d;
                    hoverWptIdx = (int)i;
                }
            }

            // Iterate in reverse to detect the 'top-most' rendered tracks first
            for (int tIdx = (int)s.tracks.size() - 1; tIdx >= 0; --tIdx) {
                if (s.selectedTrack == -1 || s.selectedTrack == tIdx) {
                    const auto& pts = s.tracks[tIdx].pts;
                    for (size_t i = 0; i < pts.size(); ++i) {
                        // Project exactly as in DrawTrack to ensure pixel alignment
                        double px = lon2x(pts[i].lon, s.zoom) - (s.cx - halfMapW) + dipOffset;
                        double py = lat2y(pts[i].lat, s.zoom) - (s.cy - centerY);

                        float d = std::hypot((float)(dipMouse.x - px), (float)(dipMouse.y - py));
                        if (d < minSafeDist) {
                            minSafeDist = d;
                            s.hoverTrackIdx = tIdx;
                            s.hoverPointIdx = (int)i;
                        }
                    }
                }
            }
        }
        InvalidateRect(s.hwnd, nullptr, FALSE);
        break;
    }
    case WM_LBUTTONUP: {
        if (s.resizingSidebar || s.panning) {
            s.resizingSidebar = false;
            s.panning = false;
            if (GetCapture() == s.hwnd) {
                ReleaseCapture();
            }
            InvalidateRect(s.hwnd, nullptr, FALSE);
        }
        s.resizingSidebar = false;
        break;
    }
    case WM_MOUSEWHEEL: {
        auto _mouse = POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (!ScreenToClient(s.hwnd, &_mouse)) break;
        if ((float)_mouse.x / s.dpiScale < dipOffset) break;

        // Smoother/Slower zooming logic
        // Accumulate delta to handle high-precision touchpads/mice and slow down zoom
        s.wheelAccumulator += GET_WHEEL_DELTA_WPARAM(wParam);

        // Only trigger zoom if we have accumulated enough delta (standard 120 units)
        const int threshold = WHEEL_DELTA;
        if (std::abs(s.wheelAccumulator) < threshold) break;

        // Determine how many steps to zoom (usually 1 or -1)
        const int steps = s.wheelAccumulator / threshold;
        // Keep remainder for next event
        s.wheelAccumulator %= threshold;

        const int nz = std::clamp(s.zoom + steps, 3, 19);
        if (nz == s.zoom) break;

        // Fixed projection for Zoom-at-Mouse-Point using DIPs
        float mapH = dipBottom - (s.showElevationProfile ? s.profileHeight : 0);
        float centerY = mapH / 2.0f;

        const double beforeX = s.cx - (double)dipMapW * 0.5 + (double)(dipMouse.x - dipOffset);
        const double beforeY = s.cy - (double)centerY + (double)dipMouse.y;

        const double lon = x2lon(beforeX, s.zoom);
        const double lat = y2lat(beforeY, s.zoom);
        s.zoom = nz;

        const double afterX = lon2x(lon, s.zoom);
        const double afterY = lat2y(lat, s.zoom);
        s.cx += (afterX - beforeX);
        s.cy += (afterY - beforeY);

        s.hoverTrackIdx = -1; // Invalidate hover on zoom
        s.hoverPointIdx = -1;
        InvalidateRect(s.hwnd, nullptr, FALSE);
        break;
    }
    }
}

static void OnKey(State& s, WPARAM vk) {
    if (vk == 'I' || vk == 'i') { // Track summary dialog
        ShowTrackInfoDialog(s);
        return;
    }

    if (vk == 'T' || vk == 't') { // Toggle Tile Server
        s.isSatelliteMode = !s.isSatelliteMode;

        const wchar_t* targetUrl = s.isSatelliteMode ? s.opt.satelliteTileEndpoint : s.opt.tileEndpoint;

        if (s.cache) {
            s.cache->UpdateEndpoint(targetUrl);
            s.cache->Clear(); // Flush all current bitmaps and memory
        }
        InvalidateRect(s.hwnd, NULL, FALSE);
    }
    if (vk == 'E') { // Toggle Elevation Profile
        s.showElevationProfile = !s.showElevationProfile;
        InvalidateRect(s.hwnd, NULL, FALSE);
    }
    if (vk == 'V' || vk == 'v') { // Toggle Speed Profile
        s.showSpeedProfile = !s.showSpeedProfile;
        InvalidateRect(s.hwnd, NULL, FALSE);
    }

    if (vk == 'S' || vk == 's') { // Toggle slope colouring
        s.showSlopeColouringOnTrack = !s.showSlopeColouringOnTrack;
        InvalidateRect(s.hwnd, NULL, FALSE);
    }
    if (vk == 'M') {
        s.tiles = !s.tiles;
        InvalidateRect(s.hwnd, NULL, FALSE);
    }
    else if (vk == 'G') {
        s.showGridWhenNoTiles = !s.showGridWhenNoTiles;
        InvalidateRect(s.hwnd, NULL, FALSE);
    }
    else if (vk == VK_OEM_PLUS || vk == VK_ADD) { SendMessageW(s.hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0); }
    else if (vk == VK_OEM_MINUS || vk == VK_SUBTRACT) { SendMessageW(s.hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0); }
    else if (vk == VK_LEFT) { s.cx -= 50; InvalidateRect(s.hwnd, NULL, FALSE); }
    else if (vk == VK_RIGHT) { s.cx += 50; InvalidateRect(s.hwnd, NULL, FALSE); }
    else if (vk == VK_UP) { s.cy -= 50; InvalidateRect(s.hwnd, NULL, FALSE); }
    else if (vk == VK_DOWN) { s.cy += 50; InvalidateRect(s.hwnd, NULL, FALSE); }
}

// UpdateSidebarContent updated to use real track names from GPX
static void UpdateSidebarContent(State& s) {
    if (!s.hwndList) return;
    SendMessage(s.hwndList, LB_RESETCONTENT, 0, 0);
    SendMessage(s.hwndList, LB_ADDSTRING, 0, (LPARAM)L"--- All Tracks ---");
    for (size_t i = 0; i < s.tracks.size(); ++i) {
        wchar_t buf[64];
        if (!s.tracks[i].name.empty()) {
            (void)lstrcpynW(buf, s.tracks[i].name.c_str(), 64);
        }
        else {
            swprintf(buf, 64, L"Track %zu", i + 1);
        }
        SendMessage(s.hwndList, LB_ADDSTRING, 0, (LPARAM)buf);
    }
    SendMessage(s.hwndList, LB_SETCURSEL, 0, 0); // Default on "All"
}

// Handle sidebar selection changes
static void OnSidebarSelChange(State& s) {
    int sel = (int)SendMessage(s.hwndList, LB_GETCURSEL, 0, 0);
    s.selectedTrack = sel - 1;
    s.hoverTrackIdx = -1; // Clear hover state on selection change
    s.hoverPointIdx = -1;
    InvalidateRect(s.hwnd, nullptr, FALSE);
    SetFocus(s.hwnd);
}

// Map view helpers for context menu hit-testing.
static bool GetMapViewClientRect(const State& s, RECT& outRc) {
    if (!s.hwnd) {
        return false;
    }

    RECT rcClient{};
    if (!GetClientRect(s.hwnd, &rcClient)) {
        return false;
    }
    int sidebarOffset = 0;
    if (s.tracks.size() > 1) {
        sidebarOffset = s.sidebarWidth;
    }

    outRc.left = sidebarOffset;
    outRc.top = 0;
    outRc.right = rcClient.right;
    outRc.bottom = rcClient.bottom;

    if (s.showElevationProfile) {
        outRc.bottom -= (LONG)s.profileHeight;
    }

    if (outRc.left < 0) {
        outRc.left = 0;
    }

    if (outRc.bottom < outRc.top) {
        outRc.bottom = outRc.top;
    }

    return true;
}

// Returns true only when the point is inside the map drawing area.
static bool IsClientPointInMapView(const State& s, const POINT& clientPt) {
    RECT mapRc{};
    if (!GetMapViewClientRect(s, mapRc)) {
        return false;
    }
    if (PtInRect(&mapRc, clientPt) == 0) {
        return false;
    }
    return true;
}

// Show a context menu for map-specific toggles, mirroring existing keyboard shortcuts.
static void ShowMapContextMenu(State& s, const POINT& screenPt) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) {
        return;
    }
    UINT tilesFlags = MF_STRING;
    if (s.tiles) {
        tilesFlags |= MF_CHECKED;
    }
    AppendMenuW(hMenu, tilesFlags, ID_CTX_TOGGLE_TILES, L"Toggle tiles (M)");
    UINT serverFlags = MF_STRING;
    if (s.isSatelliteMode) {
        serverFlags |= MF_CHECKED;
    }
    AppendMenuW(hMenu, serverFlags, ID_CTX_TOGGLE_SERVER, L"Toggle tile server (T)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_CTX_FIT_TO_WINDOW, L"Fit to window (X)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    UINT gridFlags = MF_STRING;
    if (s.showGridWhenNoTiles) {
        gridFlags |= MF_CHECKED;
    }
    AppendMenuW(hMenu, gridFlags, ID_CTX_TOGGLE_GRID, L"Toggle grid when tiles are off (G)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_CTX_SHOW_INFO, L"Track summary (I)");
    
    UINT elevationFlags = MF_STRING;
    if (s.showElevationProfile) {
        elevationFlags |= MF_CHECKED;
    }
    AppendMenuW(hMenu, elevationFlags, ID_CTX_TOGGLE_ELEVATION, L"Toggle elevation profile (E)");

    UINT slopeFlags = MF_STRING;
    if (s.showSlopeColouringOnTrack) {
        slopeFlags |= MF_CHECKED;
    }

    UINT speedFlags = MF_STRING;
    if (s.showSpeedProfile) {
        speedFlags |= MF_CHECKED;
    }
    AppendMenuW(hMenu, speedFlags, ID_CTX_TOGGLE_SPEED, L"Toggle speed profile (V)");

    AppendMenuW(hMenu, slopeFlags, ID_CTX_TOGGLE_SLOPE_COLOUR, L"Toggle slope colouring on track (S)");

    const UINT trackFlags = TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY;
    const int cmd = (int)TrackPopupMenuEx(hMenu, trackFlags, screenPt.x, screenPt.y, s.hwnd, nullptr);

    DestroyMenu(hMenu);

    if (cmd == 0) {
        return;
    }
    SetFocus(s.hwnd);
    if (cmd == ID_CTX_TOGGLE_TILES) {
        OnKey(s, 'M');
        return;
    }
    if (cmd == ID_CTX_TOGGLE_SERVER) {
        OnKey(s, 'T');
        return;
    }
    if (cmd == ID_CTX_FIT_TO_WINDOW) {
        FitToWindow(s);
        InvalidateRect(s.hwnd, nullptr, FALSE);
        return;
    }
    if (cmd == ID_CTX_TOGGLE_GRID) {
        OnKey(s, 'G');
        return;
    }
    if (cmd == ID_CTX_TOGGLE_ELEVATION) {
        OnKey(s, 'E');
        return;
    }
    if (cmd == ID_CTX_TOGGLE_SPEED) {
        OnKey(s, 'V');
        return;
    }
    if (cmd == ID_CTX_TOGGLE_SLOPE_COLOUR) {
        OnKey(s, 'S');
        return;
    }
    if (cmd == ID_CTX_SHOW_INFO) {
        OnKey(s, 'I');
        return;
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto s = reinterpret_cast<State*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (m) {
    case WM_COMMAND:
        if (LOWORD(w) == 101 && HIWORD(w) == LBN_SELCHANGE) {
            if (s) OnSidebarSelChange(*s);
        } break;
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
        // Handle resize cursor
    case WM_SETCURSOR:
        if (s && s->tracks.size() > 1) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(h, &pt);
            if (s->resizingSidebar || abs(pt.x - s->sidebarWidth) < 5) {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                return TRUE;
            }
        } break;
    case WM_CONTEXTMENU:
        if (s) {
            POINT screenPt{};
            screenPt.x = GET_X_LPARAM(l);
            screenPt.y = GET_Y_LPARAM(l);

            if (screenPt.x == -1 && screenPt.y == -1) {
                GetCursorPos(&screenPt);
            }

            POINT clientPt = screenPt;
            if (!ScreenToClient(h, &clientPt)) {
                break;
            }

            if (!IsClientPointInMapView(*s, clientPt)) {
                break;
            }

            ShowMapContextMenu(*s, screenPt);
            return 0;
        } break;
    case WM_NCCREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)l;
        s = (State*)cs->lpCreateParams;
        s->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)s);
        // Create factories
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &s->d2d)) || !s->d2d) return FALSE;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&s->dwrite)) || !s->dwrite) return FALSE;
        if (FAILED(s->dwrite->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 14.f, L"", &s->tf)) || !s->tf) return FALSE;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s->wic))) || !s->wic) return FALSE;

        // Initialise DPI scale.
        // Try to force Awareness first (to prevent virtualization)
        ConfigureDpiAwarenessOnce();

        // Get safe DPI (prioritizing Monitor DPI to fix "short window" if virtualized)
        s->dpiScale = (float)GetWindowDpiSafe(h) / 96.0f;

        return TRUE;
    } break;
    case WM_SIZE:
        if (s) {
            OnSize(*s, LOWORD(l), HIWORD(l));
        } break;
    case TILE_READY_MSG:
        if (s) {
            InvalidateRect(h, NULL, FALSE);
            if (s->cache->Pending() > 0)
                SetTimer(h, TIMER_DOWNLOAD, 150, NULL);
            else
                KillTimer(h, TIMER_DOWNLOAD);
        } break;
    case WM_DPICHANGED:
        if (s) {
            UINT newDpiX = (UINT)LOWORD(w);
            if (newDpiX < 48 || newDpiX > 480) newDpiX = GetWindowDpiSafe(h);

            s->dpiScale = (float)newDpiX / 96.0f;
            if (s->rt) {
                s->rt->Release();
                s->rt = nullptr;
            } // Force RT recreation with new DPI
            InvalidateRect(h, NULL, TRUE);
        } break;
    case WM_TIMER:
        if (s && w == TIMER_DOWNLOAD) {
            InvalidateRect(h, NULL, FALSE);
            if (s->cache->Pending() == 0)
                KillTimer(h, TIMER_DOWNLOAD);
        } break;
    case WM_PAINT:
        if (s) {
            PAINTSTRUCT ps;
            BeginPaint(h, &ps);
            OnPaint(*s);
            EndPaint(h, &ps);
        } break;
        // Zoom on Double Click logic replacing FitToWindow
    case WM_LBUTTONDBLCLK: {
        if (!s) break;
        POINT pt; GetCursorPos(&pt); ScreenToClient(h, &pt);
        D2D1_POINT_2F dipPt = { (float)pt.x / s->dpiScale, (float)pt.y / s->dpiScale };
        RECT rc; GetClientRect(h, &rc);
        int offset = (s->tracks.size() > 1) ? s->sidebarWidth : 0;

        // Double-click handling for Elevation Profile, zooms to clicked point
        if (IsPointInElevationProfile(*s, dipPt)) {
            if (pt.x < offset) break;

            float leftPadding = 50.0f, margin = 10.0f;
            float plotW = (float)rc.right - offset - (leftPadding + margin);
            double plotTotalDist = 0;

            for (size_t i = 0; i < s->tracks.size(); ++i) {
                if (s->selectedTrack == -1 || s->selectedTrack == (int)i)
                    plotTotalDist += s->tracks[i].stats.distance;
            }

            if (plotTotalDist > 0) {
                // Map click X to distance
                double targetDist = ((double)pt.x - (double)offset - (double)leftPadding) / (double)plotW *
                    plotTotalDist;

                // Find the corresponding point on the track
                if (targetDist >= 0 && targetDist <= plotTotalDist) {
                    double curAccumulated = 0;
                    const GpxPoint* targetPoint = nullptr;

                    for (size_t i = 0; i < s->tracks.size(); ++i) {
                        if (s->selectedTrack == -1 || s->selectedTrack == (int)i) {
                            const auto& t = s->tracks[i];
                            if (targetDist <= curAccumulated + t.stats.distance) {
                                double localDist = targetDist - curAccumulated;
                                for (size_t j = 0; j < t.pts.size(); ++j) {
                                    if (t.cumDist[j] >= localDist) {
                                        targetPoint = &t.pts[j];
                                        break; // Found point
                                    }
                                }
                                break; // Found track
                            }
                            curAccumulated += t.stats.distance;
                        }
                    }

                    if (targetPoint) {
                        // Increase zoom slightly for detail if zoomed out
                        if (s->zoom < 16) s->zoom = 16;
                        else if (s->zoom < 19) s->zoom++;

                        s->cx = lon2x(targetPoint->lon, s->zoom);
                        s->cy = lat2y(targetPoint->lat, s->zoom);
                        InvalidateRect(h, NULL, FALSE);
                    }
                }
            }
            break; // Handled profile click
        }

        // zoom if double click is on the map area
        if (pt.x >= offset) {
            if (s->zoom < 19) {
                float mapW = (float)rc.right - offset;
                float mapH = (float)rc.bottom - (s->showElevationProfile ? s->profileHeight : 0);
                float centerY = mapH / 2.0f;

                double targetX = s->cx - mapW / 2.0 + (pt.x - offset);
                double targetY = s->cy - centerY + pt.y;

                // Get geographic location of click
                double lon = x2lon(targetX, s->zoom);
                double lat = y2lat(targetY, s->zoom);

                s->zoom++; // Zoom in

                // Set new center to the clicked location
                s->cx = lon2x(lon, s->zoom);
                s->cy = lat2y(lat, s->zoom);

                s->hoverTrackIdx = -1;
                s->hoverPointIdx = -1;
                InvalidateRect(h, NULL, FALSE);
            }
        }
    } break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL: if (s) { OnMouse(*s, m, w, l); } break;
    case WM_KEYDOWN: if (s) { OnKey(*s, w); } break;
    case WM_CHAR: if (s) {
        wchar_t ch = (wchar_t)w;
        if (ch == L'x' || ch == L'X') { FitToWindow(*s); InvalidateRect(h, NULL, FALSE); }
    } break;
    case WM_CAPTURECHANGED:
        if (s && (HWND)l != h) {
            s->resizingSidebar = false;
            s->panning = false;
        }
        break;
    case WM_DESTROY:
        if (s) {
            if (s->cache) { s->cache->Stop(); delete s->cache; s->cache = nullptr; }
            if (s->gridDash) { s->gridDash->Release(); s->gridDash = nullptr; }
            if (s->trackStrokeStyle) { s->trackStrokeStyle->Release(); s->trackStrokeStyle = nullptr; }
            if (s->brush) { s->brush->Release(); s->brush = nullptr; }
            if (s->rt) { s->rt->Release(); s->rt = nullptr; }
            if (s->wic) { s->wic->Release(); s->wic = nullptr; }
            if (s->d2d) { s->d2d->Release(); s->d2d = nullptr; }
            if (s->tf) { s->tf->Release(); s->tf = nullptr; }
            if (s->dwrite) { s->dwrite->Release(); s->dwrite = nullptr; }
        }
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

// Create child rendering window
static HWND CreateViewer(HWND parent, State* s) {
    WNDCLASSW wc{};
    wc.style = CS_DBLCLKS;
    wc.lpszClassName = L"GPXListerD2DWndClass";
    wc.hInstance = g_hInst;
    wc.lpfnWndProc = WndProc;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);
    RECT prc; GetClientRect(parent, &prc);
    return CreateWindowExW(0, wc.lpszClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, prc.right - prc.left, prc.bottom - prc.top, parent, NULL, g_hInst, s);
}

static bool ParseGpxFile(const wchar_t* path, std::vector<Track>& tracks) {
    std::vector<GpxTrack> gt;
    if (!ParseGpxFile(path, gt)) return false; // Calls the logic in GPXParser.cpp
    tracks.clear(); // Prevent stale data
    tracks.reserve(gt.size());
    for (const auto& t : gt) {
        Track tt;
        tt.name = t.name; // Copy track name from parser output
        tt.pts.reserve(t.pts.size());
        for (const auto& p : t.pts) {
            // Ensure all four fields are copied: lat, lon, ele, and time
            tt.pts.push_back({ p.lat, p.lon, p.ele, p.time });
        }
        tracks.push_back(std::move(tt));
    }
    return !tracks.empty();
}

static HWND DoLoad(HWND ParentWin, const wchar_t* path, int ShowFlags) {
    HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hrCo) && hrCo != RPC_E_CHANGED_MODE) return NULL;

    // Configure DPI awareness early so Win32 does not DPI-virtualise input and sizing.
    ConfigureDpiAwarenessOnce();

    State* s = new State();
    s->parent = ParentWin;
    LoadOptions(s->opt);

    s->tracks.clear(); // Explicitly clear any stale data before parsing
    if (ParseGpxFile(path, s->tracks)) ComputeBounds(*s); // Compute bounds only if we have tracks

    //Parse waypoints additionally
    s->waypoints.clear();
    if (ParseGpxWaypoints(path, s->waypoints)) {
        ComputeBounds(*s);
    }

    s->showElevationProfile = s->opt.showElevationProfile; // Initialise the elevation profile flag
    s->showSlopeColouringOnTrack = s->opt.showSlopeColouringOnTrack; // Initialise slope colouring flag
    s->trackLineWidth = s->opt.trackLineWidth; // Initialise track stroke width

    HWND hwnd = CreateViewer(ParentWin, s);
    if (hwnd) {
        SetFocus(hwnd);
        RECT prc; GetClientRect(ParentWin, &prc);
        MoveWindow(hwnd, 0, 0, prc.right - prc.left, prc.bottom - prc.top, TRUE);
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        s->zoom = std::clamp(s->opt.initialZoom, 3, 19);
        FitToWindow(*s);

        s->tiles = s->opt.useTiles;
        s->showGridWhenNoTiles = s->opt.showGridWhenNoTiles;
        s->showScale = s->opt.showScale;
        s->showCoords = s->opt.showCoords;

        s->cache = new TileCache();
        s->cache->Configure(s->opt.tileEndpoint, s->opt.userAgent, (size_t)std::max<int>(256, s->opt.maxBitmaps), s->opt.requestDelayMs, s->opt.backoffStartMs, s->opt.backoffMaxMs);
        s->cache->SetNotify(s->hwnd, TILE_READY_MSG);
        s->cache->Start((int)std::max<int>(1, s->opt.workers));
        InvalidateRect(hwnd, NULL, TRUE);
    }
    else {
        delete s;
        CoUninitialize();
        return NULL;
    }
    return hwnd;
}

HWND WINAPI ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags) {
    int wchars = MultiByteToWideChar(CP_ACP, 0, FileToLoad, -1, NULL, 0);
    if (wchars <= 0) return NULL;
    std::wstring wpath(wchars, L'\0'); MultiByteToWideChar(CP_ACP, 0, FileToLoad, -1, &wpath[0], wchars);
    return DoLoad(ParentWin, wpath.c_str(), ShowFlags);
}

HWND WINAPI ListLoadW(HWND ParentWin, WCHAR* FileToLoad, int ShowFlags) {
    return DoLoad(ParentWin, FileToLoad, ShowFlags);
}

int WINAPI ListLoadNext(HWND ParentWin, HWND ListWin, char* FileToLoad, int ShowFlags) {
    int wchars = MultiByteToWideChar(CP_ACP, 0, FileToLoad, -1, NULL, 0);
    if (wchars <= 0) return NULL;
    std::wstring wpath(wchars, L'\0'); MultiByteToWideChar(CP_ACP, 0, FileToLoad, -1, &wpath[0], wchars);
    return ListLoadNextW(ParentWin, ListWin, (WCHAR*)wpath.c_str(), ShowFlags);
}

int WINAPI ListLoadNextW(HWND ParentWin, HWND ListWin, WCHAR* FileToLoad, int ShowFlags) {
    auto s = reinterpret_cast<State*>(GetWindowLongPtrW(ListWin, GWLP_USERDATA));
    if (!s) return LISTPLUGIN_ERROR;

    //Reset hover state for new file to prevent stale index access
    s->hoverTrackIdx = -1;
    s->hoverPointIdx = -1;
    s->tracks.clear();
    s->selectedTrack = -1;

    if (ParseGpxFile(FileToLoad, s->tracks))
        ComputeBounds(*s);

    //Parse waypoints additionally
    s->waypoints.clear();
    if (ParseGpxWaypoints(FileToLoad, s->waypoints)) {
        ComputeBounds(*s);
    }

    //Refresh Sidebar
    if (s->tracks.size() > 1) {
        UpdateSidebarContent(*s);
        if (s->hwndList) ShowWindow(s->hwndList, SW_SHOW);
    }
    else {
        if (s->hwndList) ShowWindow(s->hwndList, SW_HIDE);
    }

    SetFocus(ListWin);
    UpdateWindow(ListWin);
    s->zoom = std::clamp(s->opt.initialZoom, 3, 19);
    FitToWindow(*s);
    InvalidateRect(ListWin, NULL, TRUE);
    return LISTPLUGIN_OK;
}

void WINAPI ListCloseWindow(HWND ListWin) {
    if (!ListWin) return;
    auto s = reinterpret_cast<State*>(GetWindowLongPtrW(ListWin, GWLP_USERDATA));
    DestroyWindow(ListWin);
    if (s) { CoUninitialize(); delete s; }
}

void WINAPI ListGetDetectString(char* DetectString, int maxlen) {
    // EXT="GPX" -> standard association
    // (force & [0]="<gpx") -> if 'Force' is used (F3 on any file), 
    // (find("<gpx") & force) -> Scans the first 8192 bytes for the tag 
    const char* ds = "(EXT=\"GPX\") | (FORCE & FIND(\"<gpx\"))";
    if (DetectString && maxlen > 0) {
        (void)lstrcpynA(DetectString, ds, maxlen);
    }
}

int WINAPI ListSendCommand(HWND, int, int) { return LISTPLUGIN_OK; }

void WINAPI ListSetDefaultParams(ListDefaultParamStruct*) {}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { g_hInst = (HINSTANCE)hModule; }
    return TRUE;
}