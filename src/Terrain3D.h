#ifndef TERRAIN3D_H
#define TERRAIN3D_H

#include <windows.h>
#include <memory>
#include <string>

constexpr UINT TERRAIN3D_READY_MSG = WM_APP + 20;
constexpr UINT TERRAIN3D_FAILED_MSG = WM_APP + 21;
constexpr UINT TERRAIN3D_HOVER_MSG = WM_APP + 22;
constexpr UINT TERRAIN3D_SELECT_MSG = WM_APP + 23;
constexpr UINT TERRAIN3D_CONTEXT_MSG = WM_APP + 24;
constexpr UINT TERRAIN3D_KEY_MSG = WM_APP + 25;
constexpr UINT TERRAIN3D_HIGHLIGHTED_MSG = WM_APP + 26;
constexpr LPARAM TERRAIN3D_MOD_SHIFT = 1;
constexpr LPARAM TERRAIN3D_MOD_CONTROL = 2;

struct Terrain3DCamera {
    double longitude = 0.0;
    double latitude = 0.0;
    double zoom = 13.0;
    double pitch = 0.0;
    double bearing = 0.0;
    bool valid = false;
};

class Terrain3DView : public std::enable_shared_from_this<Terrain3DView> {
public:
    static std::shared_ptr<Terrain3DView> Create(HWND owner, const RECT& bounds,
                                                  const std::wstring& assetFolder);
    ~Terrain3DView();

    Terrain3DView(const Terrain3DView&) = delete;
    Terrain3DView& operator=(const Terrain3DView&) = delete;

    void Activate(std::wstring payloadJson);
    void Suspend();
    void Resize(const RECT& bounds);
    void Focus();
    void FitTrack();
    void SelectTrack(int trackIndex);
    void SetSlopeColouring(bool enabled);
    void HighlightPoint(int trackIndex, int pointIndex, double lon, double lat, bool focusCamera);
    void ClearHighlight();
    void Shutdown();

    bool IsReady() const;
    bool HasFailed() const;
    const std::wstring& LastError() const;
    Terrain3DCamera Camera() const;

private:
    struct Impl;

    Terrain3DView(HWND owner, const RECT& bounds, std::wstring assetFolder);
    bool Start();
    void NavigateViewer();
    void ExecutePayload();
    void ExecuteScript(const std::wstring& script);
    void ReportFailure(HRESULT result, const wchar_t* context);
    void HandleWebMessage(const std::wstring& message);

    std::unique_ptr<Impl> impl_;
};

#endif
