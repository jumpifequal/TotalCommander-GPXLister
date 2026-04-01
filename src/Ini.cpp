#include "Ini.h"
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

extern "C" IMAGE_DOS_HEADER __ImageBase;

static void ReadBool(const wchar_t* path, const wchar_t* key, int def, bool& out){
    wchar_t buf[16]; GetPrivateProfileStringW(L"GPXLister", key, def?L"1":L"0", buf, 16, path);
    out = (buf[0]==L'1' || _wcsicmp(buf,L"true")==0 || _wcsicmp(buf,L"yes")==0);
}

static void ReadInt(const wchar_t* path, const wchar_t* key, int def, int& out){
    out = GetPrivateProfileIntW(L"GPXLister", key, def, path);
}

//needs some extra checks to avoid extreme values
static void ReadFloat(const wchar_t* path, const wchar_t* key, float def, float& out) {
    wchar_t buf[32];
    GetPrivateProfileStringW(L"GPXLister", key, L"", buf, 32, path);
    if (buf[0] == L'\0') {
        out = def;
        return;
    }
    wchar_t* endPtr = nullptr;
    const double v = wcstod(buf, &endPtr);
    if (endPtr == buf) {
        out = def;
        return;
    }
    // Clamp to a safe range to avoid extreme stroke widths that degrade rendering.
    const float kMinTrackLineWidth = 0.5f; // Minimum allowed track stroke width.
    const float kMaxTrackLineWidth = 12.0f; // Maximum allowed track stroke width.
    float vf = (float)v;
    if (vf < kMinTrackLineWidth) vf = kMinTrackLineWidth;
    if (vf > kMaxTrackLineWidth) vf = kMaxTrackLineWidth;
    out = vf;
}

static void ReadStr(const wchar_t* path, const wchar_t* key, const wchar_t* def, wchar_t* out, size_t outc){
    GetPrivateProfileStringW(L"GPXLister", key, def, out, (DWORD)outc, path);
}

void LoadOptions(Options& o){
    // discover INI next to DLL (GPXLister.ini)
    wchar_t dllPath[MAX_PATH]; GetModuleFileNameW((HMODULE)&__ImageBase, dllPath, MAX_PATH);
    PathRemoveFileSpecW(dllPath);
    wchar_t iniPath[MAX_PATH]; lstrcpyW(iniPath, dllPath); PathAppendW(iniPath, L"GPXLister.ini");
    // read values (keep defaults if file missing)
    ReadBool(iniPath, L"useTiles", o.useTiles, o.useTiles);
    ReadBool(iniPath, L"showGridWhenNoTiles", o.showGridWhenNoTiles, o.showGridWhenNoTiles);
    ReadBool(iniPath, L"showScale", o.showScale, o.showScale);
    ReadBool(iniPath, L"showCoords", o.showCoords, o.showCoords);
    ReadInt(iniPath, L"initialZoom", o.initialZoom, o.initialZoom);
    ReadInt(iniPath, L"workers", o.workers, o.workers);
    ReadInt(iniPath, L"requestDelayMs", o.requestDelayMs, o.requestDelayMs);
    ReadInt(iniPath, L"backoffStartMs", o.backoffStartMs, o.backoffStartMs);
    ReadInt(iniPath, L"backoffMaxMs", o.backoffMaxMs, o.backoffMaxMs);
    ReadInt(iniPath, L"maxBitmaps", o.maxBitmaps, o.maxBitmaps);
    // Safety clamp: avoid OOM via config
    if (o.maxBitmaps < 64) o.maxBitmaps = 64;
    if (o.maxBitmaps > 4096) o.maxBitmaps = 4096;
    ReadInt(iniPath, L"prefetchRings", o.prefetchRings, o.prefetchRings);
    ReadStr(iniPath, L"tileEndpoint", o.tileEndpoint, o.tileEndpoint, 256);
    ReadStr(iniPath, L"satelliteTileEndpoint", o.satelliteTileEndpoint, o.satelliteTileEndpoint, 256);
    ReadStr(iniPath, L"userAgent", o.userAgent, o.userAgent, 128);
    ReadBool(iniPath, L"showElevationProfile", o.showElevationProfile, o.showElevationProfile);
	ReadBool(iniPath, L"showSlopeColouringOnTrack", o.showSlopeColouringOnTrack, o.showSlopeColouringOnTrack);
    ReadFloat(iniPath, L"trackLineWidth", o.trackLineWidth, o.trackLineWidth);
}