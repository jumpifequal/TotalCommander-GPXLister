#include "Terrain3D.h"

#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <wrl.h>
#include <shlwapi.h>

#include <cwchar>
#include <utility>

#pragma comment(lib, "Shlwapi.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;

struct Terrain3DView::Impl {
    HWND owner = nullptr;
    RECT bounds{};
    std::wstring assetFolder;
    std::wstring pendingPayload;
    std::wstring lastError;
    Terrain3DCamera camera;

    bool desiredVisible = false;
    bool pageReady = false;
    bool failed = false;
    bool shuttingDown = false;

    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webView;
    EventRegistrationToken navigationToken{};
    EventRegistrationToken messageToken{};
    EventRegistrationToken acceleratorToken{};
};

static std::wstring BuildUserDataFolder() {
    wchar_t tempPath[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, tempPath) == 0) return L"";
    PathAppendW(tempPath, L"GPXLister-WebView2");
    CreateDirectoryW(tempPath, nullptr);
    return tempPath;
}

static bool IsForwardedApplicationKey(UINT key) {
    switch (key) {
        case 'A': case 'D': case 'E': case 'F': case 'G': case 'I':
        case 'M': case 'S': case 'T': case 'V':
            return true;
        case 'C': case VK_INSERT:
            return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        default:
            return false;
    }
}

Terrain3DView::Terrain3DView(HWND owner, const RECT& bounds, std::wstring assetFolder)
    : impl_(new Impl()) {
    impl_->owner = owner;
    impl_->bounds = bounds;
    impl_->assetFolder = std::move(assetFolder);
}

Terrain3DView::~Terrain3DView() {
    Shutdown();
}

std::shared_ptr<Terrain3DView> Terrain3DView::Create(HWND owner, const RECT& bounds,
                                                     const std::wstring& assetFolder) {
    if (!owner || assetFolder.empty()) return nullptr;
    auto view = std::shared_ptr<Terrain3DView>(new Terrain3DView(owner, bounds, assetFolder));
    if (!view->Start()) return nullptr;
    return view;
}

bool Terrain3DView::Start() {
    wchar_t viewerPath[MAX_PATH]{};
    lstrcpynW(viewerPath, impl_->assetFolder.c_str(), MAX_PATH);
    PathAppendW(viewerPath, L"terrain3d.html");
    if (GetFileAttributesW(viewerPath) == INVALID_FILE_ATTRIBUTES) {
        ReportFailure(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), L"3D viewer assets are missing");
        return false;
    }

    const std::wstring userDataFolder = BuildUserDataFolder();
    if (userDataFolder.empty()) {
        ReportFailure(HRESULT_FROM_WIN32(GetLastError()), L"Unable to create the WebView2 data folder");
        return false;
    }

    auto options = Make<CoreWebView2EnvironmentOptions>();
    options->put_AdditionalBrowserArguments(
        L"--disk-cache-size=33554432 --media-cache-size=4194304 --renderer-process-limit=1 "
        L"--disable-background-networking --disable-component-update "
        L"--disable-features=MediaRouter,OptimizationHints,Translate "
        L"--js-flags=--max-old-space-size=192");

    const std::weak_ptr<Terrain3DView> weak = weak_from_this();
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userDataFolder.c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [weak](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                auto self = weak.lock();
                if (!self || self->impl_->shuttingDown) return S_OK;
                if (FAILED(result) || !environment) {
                    self->ReportFailure(FAILED(result) ? result : E_FAIL, L"WebView2 environment creation failed");
                    return S_OK;
                }
                self->impl_->environment = environment;
                const std::weak_ptr<Terrain3DView> controllerWeak = self;
                environment->CreateCoreWebView2Controller(
                    self->impl_->owner,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [controllerWeak](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                            auto view = controllerWeak.lock();
                            if (!view || view->impl_->shuttingDown) return S_OK;
                            if (FAILED(controllerResult) || !controller) {
                                view->ReportFailure(FAILED(controllerResult) ? controllerResult : E_FAIL,
                                                    L"WebView2 controller creation failed");
                                return S_OK;
                            }

                            view->impl_->controller = controller;
                            controller->put_Bounds(view->impl_->bounds);
                            controller->put_IsVisible(view->impl_->desiredVisible ? TRUE : FALSE);
                            controller->get_CoreWebView2(&view->impl_->webView);
                            if (!view->impl_->webView) {
                                view->ReportFailure(E_NOINTERFACE, L"WebView2 core interface is unavailable");
                                return S_OK;
                            }

                            ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(view->impl_->webView->get_Settings(&settings)) && settings) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_IsZoomControlEnabled(FALSE);
                                ComPtr<ICoreWebView2Settings3> settings3;
                                if (SUCCEEDED(settings.As(&settings3)) && settings3) {
                                    settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
                                }
                            }

                            ComPtr<ICoreWebView2_3> webView3;
                            if (FAILED(view->impl_->webView.As(&webView3)) || !webView3 ||
                                FAILED(webView3->SetVirtualHostNameToFolderMapping(
                                    L"gpxlister.local", view->impl_->assetFolder.c_str(),
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW))) {
                                view->ReportFailure(E_NOINTERFACE, L"WebView2 local asset mapping failed");
                                return S_OK;
                            }

                            const std::weak_ptr<Terrain3DView> eventWeak = view;
                            view->impl_->webView->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [eventWeak](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                        auto target = eventWeak.lock();
                                        if (!target || target->impl_->shuttingDown) return S_OK;
                                        BOOL success = FALSE;
                                        args->get_IsSuccess(&success);
                                        if (!success && target->impl_->desiredVisible) {
                                            COREWEBVIEW2_WEB_ERROR_STATUS status{};
                                            args->get_WebErrorStatus(&status);
                                            target->ReportFailure(HRESULT_FROM_WIN32((DWORD)status), L"3D viewer navigation failed");
                                            return S_OK;
                                        }
                                        if (target->impl_->desiredVisible) target->ExecutePayload();
                                        return S_OK;
                                    }).Get(), &view->impl_->navigationToken);

                            view->impl_->webView->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [eventWeak](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        auto target = eventWeak.lock();
                                        if (!target || target->impl_->shuttingDown) return S_OK;
                                        LPWSTR raw = nullptr;
                                        if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                                            target->HandleWebMessage(raw);
                                            CoTaskMemFree(raw);
                                        }
                                        return S_OK;
                                    }).Get(), &view->impl_->messageToken);

                            view->impl_->controller->add_AcceleratorKeyPressed(
                                Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                                    [eventWeak](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                                        auto target = eventWeak.lock();
                                        if (!target || target->impl_->shuttingDown) return S_OK;
                                        COREWEBVIEW2_KEY_EVENT_KIND kind{};
                                        UINT key = 0;
                                        args->get_KeyEventKind(&kind);
                                        args->get_VirtualKey(&key);
                                        const bool keyDown =
                                            kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN ||
                                            kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN;
                                        if (keyDown && key == VK_ESCAPE) {
                                            // WebView2 owns keyboard focus in 3D. Re-post Escape to the
                                            // WLX host so Total Commander retains its normal close action.
                                            args->put_Handled(TRUE);
                                            HWND host = GetParent(target->impl_->owner);
                                            if (host) PostMessageW(host, WM_KEYDOWN, VK_ESCAPE, 0);
                                            return S_OK;
                                        }
                                        if (!target->impl_->pageReady &&
                                            keyDown &&
                                            IsForwardedApplicationKey(key)) {
                                            LPARAM modifiers = 0;
                                            if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= TERRAIN3D_MOD_SHIFT;
                                            if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= TERRAIN3D_MOD_CONTROL;
                                            args->put_Handled(TRUE);
                                            PostMessageW(target->impl_->owner, TERRAIN3D_KEY_MSG, key, modifiers);
                                        }
                                        return S_OK;
                                    }).Get(), &view->impl_->acceleratorToken);

                            if (view->impl_->desiredVisible) view->NavigateViewer();
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    if (FAILED(hr)) {
        ReportFailure(hr, L"WebView2 startup failed");
        return false;
    }
    return true;
}

void Terrain3DView::Activate(std::wstring payloadJson) {
    if (impl_->shuttingDown || impl_->failed) return;
    impl_->pendingPayload = std::move(payloadJson);
    impl_->desiredVisible = true;
    if (impl_->controller) impl_->controller->put_IsVisible(TRUE);
    if (impl_->webView) {
        if (impl_->pageReady) ExecutePayload();
        else NavigateViewer();
    }
}

void Terrain3DView::NavigateViewer() {
    if (!impl_->webView || impl_->shuttingDown || !impl_->desiredVisible) return;
    impl_->pageReady = false;
    impl_->webView->Navigate(L"https://gpxlister.local/terrain3d.html");
}

void Terrain3DView::ExecutePayload() {
    if (!impl_->webView || impl_->pendingPayload.empty() || !impl_->desiredVisible) return;
    std::wstring script = L"window.gpxSetData(" + impl_->pendingPayload + L");";
    impl_->pendingPayload.clear();
    ExecuteScript(script);
}

void Terrain3DView::Suspend() {
    if (impl_->shuttingDown) return;
    impl_->desiredVisible = false;
    impl_->pageReady = false;
    impl_->pendingPayload.clear();
    if (impl_->webView) impl_->webView->Navigate(L"about:blank");
    if (impl_->controller) impl_->controller->put_IsVisible(FALSE);
}

void Terrain3DView::Resize(const RECT& bounds) {
    impl_->bounds = bounds;
    if (impl_->controller) impl_->controller->put_Bounds(bounds);
}

void Terrain3DView::Focus() {
    if (impl_->controller && impl_->desiredVisible)
        impl_->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

void Terrain3DView::FitTrack() {
    ExecuteScript(L"window.gpxFit && window.gpxFit();");
}

void Terrain3DView::SelectTrack(int trackIndex) {
    ExecuteScript(L"window.gpxSelectTrack && window.gpxSelectTrack(" + std::to_wstring(trackIndex) + L");");
}

void Terrain3DView::SetSlopeColouring(bool enabled) {
    ExecuteScript(std::wstring(L"window.gpxSetSlope && window.gpxSetSlope(") + (enabled ? L"true" : L"false") + L");");
}

void Terrain3DView::HighlightPoint(int trackIndex, int pointIndex, bool focusCamera) {
    if (trackIndex < 0 || pointIndex < 0) return;
    std::wstring script = L"window.gpxHighlight && window.gpxHighlight(" +
        std::to_wstring(trackIndex) + L"," + std::to_wstring(pointIndex) + L"," +
        (focusCamera ? L"true" : L"false") + L");";
    ExecuteScript(script);
}

void Terrain3DView::ExecuteScript(const std::wstring& script) {
    if (!impl_->webView || !impl_->desiredVisible || script.empty()) return;
    impl_->webView->ExecuteScript(script.c_str(), nullptr);
}

void Terrain3DView::HandleWebMessage(const std::wstring& message) {
    if (message == L"ready") {
        impl_->pageReady = true;
        PostMessageW(impl_->owner, TERRAIN3D_READY_MSG, 0, 0);
        return;
    }
    if (message == L"context") {
        PostMessageW(impl_->owner, TERRAIN3D_CONTEXT_MSG, 0, 0);
        return;
    }

    unsigned int key = 0;
    int shift = 0;
    int control = 0;
    if (swscanf_s(message.c_str(), L"key|%u|%d|%d", &key, &shift, &control) == 3) {
        LPARAM modifiers = 0;
        if (shift) modifiers |= TERRAIN3D_MOD_SHIFT;
        if (control) modifiers |= TERRAIN3D_MOD_CONTROL;
        PostMessageW(impl_->owner, TERRAIN3D_KEY_MSG, (WPARAM)key, modifiers);
        return;
    }

    int track = -1;
    int point = -1;
    if (swscanf_s(message.c_str(), L"hover|%d|%d", &track, &point) == 2) {
        PostMessageW(impl_->owner, TERRAIN3D_HOVER_MSG, (WPARAM)track, (LPARAM)point);
        return;
    }
    if (swscanf_s(message.c_str(), L"select|%d|%d", &track, &point) == 2) {
        PostMessageW(impl_->owner, TERRAIN3D_SELECT_MSG, (WPARAM)track, (LPARAM)point);
        return;
    }

    Terrain3DCamera camera;
    if (swscanf_s(message.c_str(), L"camera|%lf|%lf|%lf|%lf|%lf",
                  &camera.longitude, &camera.latitude, &camera.zoom,
                  &camera.pitch, &camera.bearing) == 5) {
        camera.valid = true;
        impl_->camera = camera;
        return;
    }

    if (message.rfind(L"error|", 0) == 0) {
        impl_->lastError = message.substr(6);
        impl_->failed = true;
        PostMessageW(impl_->owner, TERRAIN3D_FAILED_MSG, 0, 0);
    }
}

void Terrain3DView::ReportFailure(HRESULT result, const wchar_t* context) {
    wchar_t code[32]{};
    swprintf_s(code, L" (0x%08lX)", (unsigned long)result);
    impl_->lastError = context ? context : L"3D rendering failed";
    impl_->lastError += code;
    impl_->failed = true;
    if (impl_->owner) PostMessageW(impl_->owner, TERRAIN3D_FAILED_MSG, 0, 0);
}

void Terrain3DView::Shutdown() {
    if (!impl_ || impl_->shuttingDown) return;
    impl_->shuttingDown = true;
    impl_->desiredVisible = false;
    impl_->pendingPayload.clear();

    if (impl_->webView) {
        impl_->webView->remove_NavigationCompleted(impl_->navigationToken);
        impl_->webView->remove_WebMessageReceived(impl_->messageToken);
    }
    if (impl_->controller) {
        impl_->controller->remove_AcceleratorKeyPressed(impl_->acceleratorToken);
        impl_->controller->put_IsVisible(FALSE);
        impl_->controller->Close();
    }
    impl_->webView.Reset();
    impl_->controller.Reset();
    impl_->environment.Reset();
}

bool Terrain3DView::IsReady() const { return impl_->pageReady && !impl_->failed; }
bool Terrain3DView::HasFailed() const { return impl_->failed; }
const std::wstring& Terrain3DView::LastError() const { return impl_->lastError; }
Terrain3DCamera Terrain3DView::Camera() const { return impl_->camera; }
