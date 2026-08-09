#include "Ini.h"
#include <algorithm>
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

static void ReadClampedFloat(const wchar_t* path, const wchar_t* key, float def,
                             float minimum, float maximum, float& out) {
    wchar_t buf[32]{};
    GetPrivateProfileStringW(L"GPXLister", key, L"", buf, 32, path);
    if (buf[0] == L'\0') {
        out = def;
        return;
    }
    wchar_t* endPtr = nullptr;
    const double value = wcstod(buf, &endPtr);
    if (endPtr == buf || *endPtr != L'\0') {
        out = def;
        return;
    }
    out = (float)(std::max<double>)(minimum, (std::min<double>)(maximum, value));
}

static bool ReadStrOptional(const wchar_t* path, const wchar_t* key, wchar_t* out, size_t outc) {
    if (!out || outc == 0) return false;
    const wchar_t* sentinel = L"\x1F";
    GetPrivateProfileStringW(L"GPXLister", key, sentinel, out, (DWORD)outc, path);
    if (wcscmp(out, sentinel) == 0) {
        out[0] = L'\0';
        return false;
    }
    return true;
}

static void GetIniPath(wchar_t* iniPath, size_t iniPathCount) {
    if (!iniPath || iniPathCount == 0) return;
    iniPath[0] = L'\0';

    wchar_t dllPath[MAX_PATH]; GetModuleFileNameW((HMODULE)&__ImageBase, dllPath, MAX_PATH);
    PathRemoveFileSpecW(dllPath);
    lstrcpynW(iniPath, dllPath, (int)iniPathCount);
    PathAppendW(iniPath, L"GPXLister.ini");
    if (GetFileAttributesW(iniPath) == INVALID_FILE_ATTRIBUTES) {
        wchar_t cwdIni[MAX_PATH]{};
        if (GetCurrentDirectoryW(MAX_PATH, cwdIni) > 0) {
            PathAppendW(cwdIni, L"GPXLister.ini");
            if (GetFileAttributesW(cwdIni) != INVALID_FILE_ATTRIBUTES) {
                lstrcpynW(iniPath, cwdIni, (int)iniPathCount);
            }
        }
    }
}

void LoadOptions(Options& o){
    // discover INI next to DLL (GPXLister.ini)
    wchar_t iniPath[MAX_PATH];
    GetIniPath(iniPath, MAX_PATH);

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
    lstrcpynW(o.standardTileEndpoint, o.tileEndpoint, 256); // legacy tileEndpoint is the standard map style unless overridden.
    ReadStr(iniPath, L"standardTileEndpoint", o.standardTileEndpoint, o.standardTileEndpoint, 256);
    ReadStr(iniPath, L"satelliteTileEndpoint", o.satelliteTileEndpoint, o.satelliteTileEndpoint, 256);
    ReadStr(iniPath, L"topoTileEndpoint", o.topoTileEndpoint, o.topoTileEndpoint, 256);
    o.hasMapTypeOrder = ReadStrOptional(iniPath, L"mapTypeOrder", o.mapTypeOrder, 128);
    if (!o.hasMapTypeOrder) {
        lstrcpynW(o.mapTypeOrder, L"standard,satellite,topo", 128);
    }
    wchar_t model3dText[16]{};
    const bool has3dModel = ReadStrOptional(iniPath, L"3d_model", model3dText, 16);
    wchar_t* model3dEnd = nullptr;
    const long parsed3dModel = has3dModel ? wcstol(model3dText, &model3dEnd, 10) : 0;
    if (!has3dModel || model3dEnd == model3dText || *model3dEnd != L'\0' ||
        (parsed3dModel != 1 && parsed3dModel != 2)) {
        o.preferred3dModel = 2;
        WritePrivateProfileStringW(L"GPXLister", L"3d_model", L"2", iniPath);
    }
    else {
        o.preferred3dModel = (int)parsed3dModel;
    }
    ReadStr(iniPath, L"terrainProvider", o.terrainProvider, o.terrainProvider, 32);
    ReadStr(iniPath, L"terrariumTerrainEndpoint", o.terrariumTerrainEndpoint, o.terrariumTerrainEndpoint, 512);
    ReadStr(iniPath, L"mapTilerTerrainEndpoint", o.mapTilerTerrainEndpoint, o.mapTilerTerrainEndpoint, 512);
    ReadStr(iniPath, L"mapTilerApiKey", o.mapTilerApiKey, o.mapTilerApiKey, 256);
    ReadClampedFloat(iniPath, L"terrainExaggeration", o.terrainExaggeration, 0.1f, 5.0f, o.terrainExaggeration);
    ReadStr(iniPath, L"userAgent", o.userAgent, o.userAgent, 128);
    ReadBool(iniPath, L"showElevationProfile", o.showElevationProfile, o.showElevationProfile);
    ReadBool(iniPath, L"showSpeedProfile", o.showSpeedProfile, o.showSpeedProfile);
    ReadBool(iniPath, L"showSlopeColouringOnTrack", o.showSlopeColouringOnTrack, o.showSlopeColouringOnTrack);
    ReadFloat(iniPath, L"trackLineWidth", o.trackLineWidth, o.trackLineWidth);
    ReadStr(iniPath, L"speedProfileColor", o.speedProfileColor, o.speedProfileColor, 16);
    ReadStr(iniPath, L"fitConverter", o.fitConverter, o.fitConverter, MAX_PATH);
    ReadStr(iniPath, L"fitArgs", o.fitArgs, o.fitArgs, 512);
    ReadInt(iniPath, L"fitTimeoutSec", o.fitTimeoutSec, o.fitTimeoutSec);
    if (o.fitTimeoutSec < 1) o.fitTimeoutSec = 1;
    if (o.fitTimeoutSec > 3600) o.fitTimeoutSec = 3600;
    ReadStr(iniPath, L"kmlConverter", o.kmlConverter, o.kmlConverter, MAX_PATH);
    ReadStr(iniPath, L"kmlArgs", o.kmlArgs, o.kmlArgs, 512);
    ReadInt(iniPath, L"kmlTimeoutSec", o.kmlTimeoutSec, o.kmlTimeoutSec);
    if (o.kmlTimeoutSec < 1) o.kmlTimeoutSec = 1;
    if (o.kmlTimeoutSec > 3600) o.kmlTimeoutSec = 3600;
}

void SaveOptionBool(const wchar_t* key, bool value) {
    wchar_t iniPath[MAX_PATH];
    GetIniPath(iniPath, MAX_PATH);
    WritePrivateProfileStringW(L"GPXLister", key, value ? L"1" : L"0", iniPath);
}

void SaveOptionString(const wchar_t* key, const wchar_t* value) {
    wchar_t iniPath[MAX_PATH];
    GetIniPath(iniPath, MAX_PATH);
    WritePrivateProfileStringW(L"GPXLister", key, value ? value : L"", iniPath);
}
