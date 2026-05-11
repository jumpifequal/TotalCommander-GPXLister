
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <string>

#pragma comment(lib, "Shlwapi.lib")

typedef HWND (WINAPI *PFN_ListLoadW)(HWND, const WCHAR*, int);
typedef int  (WINAPI *PFN_ListGetDetectString)(char*, int);
typedef int  (WINAPI *PFN_ListCloseWindow)(HWND);

static bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring GetDirName(const std::wstring& path) {
    wchar_t buf[MAX_PATH];
    wcsncpy_s(buf, path.c_str(), _TRUNCATE);
    PathRemoveFileSpecW(buf);
    return buf;
}

static std::wstring MakeAbsoluteFromBase(const std::wstring& baseDir, const std::wstring& input) {
    wchar_t combined[MAX_PATH];
    wchar_t absolute[MAX_PATH];
    if (PathIsRelativeW(input.c_str())) {
        PathCombineW(combined, baseDir.c_str(), input.c_str());
        if (GetFullPathNameW(combined, MAX_PATH, absolute, nullptr) > 0)
            return absolute;
        return combined;
    }
    if (GetFullPathNameW(input.c_str(), MAX_PATH, absolute, nullptr) > 0)
        return absolute;
    return input;
}

static std::wstring ResolveInputPath(const std::wstring& input, const std::wstring& exeDir) {
    wchar_t cwdBuf[MAX_PATH];
    std::wstring cwd;
    std::wstring candidate;
    std::wstring repoRoot = exeDir;

    if (GetCurrentDirectoryW(MAX_PATH, cwdBuf) > 0)
        cwd = cwdBuf;

    if (!PathIsRelativeW(input.c_str())) {
        candidate = MakeAbsoluteFromBase(L"", input);
        return candidate;
    }

    if (!cwd.empty()) {
        candidate = MakeAbsoluteFromBase(cwd, input);
        if (FileExists(candidate)) return candidate;
    }

    candidate = MakeAbsoluteFromBase(exeDir, input);
    if (FileExists(candidate)) return candidate;

    repoRoot = GetDirName(GetDirName(exeDir));
    candidate = MakeAbsoluteFromBase(repoRoot, input);
    if (FileExists(candidate)) return candidate;

    return MakeAbsoluteFromBase(!cwd.empty() ? cwd : exeDir, input);
}

static std::wstring OpenFileDialog(const wchar_t* filter) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Select a gpx file";
    if (GetOpenFileNameW(&ofn))
        return file;
    return L"";
}

LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY: PostQuitMessage(0); return 0;
        case WM_SIZE: {
            // Retrieve the first child window (the loaded WLX plugin)
            HWND child = GetWindow(hwnd, GW_CHILD);
            if (child && wParam != SIZE_MINIMIZED) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                MoveWindow(child, 0, 0, rc.right, rc.bottom, TRUE);
            }
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    wchar_t exePath[MAX_PATH] = L"";
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring exeDir = GetDirName(exePath);

    // Ask for WLX path
    wchar_t wlxPath[MAX_PATH] = L"";
    if (argc >= 2 && argv && argv[1] && argv[1][0]) {
        std::wstring resolved = ResolveInputPath(argv[1], exeDir);
        wcsncpy_s(wlxPath, MAX_PATH, resolved.c_str(), _TRUNCATE);
    } else {
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = NULL;
        ofn.lpstrFilter = L"Lister Plugin (*.wlx)\0*.wlx\0\0";
        ofn.lpstrFile = wlxPath;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        ofn.lpstrTitle = L"Select GPXLister.wlx";
        if (!GetOpenFileNameW(&ofn)) {
            if (argv) LocalFree(argv);
            return 0;
        }
    }

    if (!FileExists(wlxPath)) {
        std::wstring msg = L"WLX file not found:\n" + std::wstring(wlxPath);
        MessageBoxW(NULL, msg.c_str(), L"WLXHarness", MB_ICONERROR);
        if (argv) LocalFree(argv);
        return 0;
    }

    // Load plugin
    HMODULE mod = LoadLibraryW(wlxPath);
    if (!mod) { MessageBoxW(NULL, L"Failed to load WLX", L"WLXHarness", MB_ICONERROR); return 0; }

    auto pListLoadW = (PFN_ListLoadW)GetProcAddress(mod, "ListLoadW");
    auto pListGetDetectString = (PFN_ListGetDetectString)GetProcAddress(mod, "ListGetDetectString");
    auto pListCloseWindow = (PFN_ListCloseWindow)GetProcAddress(mod, "ListCloseWindow");

    if (!pListLoadW || !pListGetDetectString || !pListCloseWindow) {
        MessageBoxW(NULL, L"Missing exports in WLX", L"WLXHarness", MB_ICONERROR);
        return 0;
    }

    // Create host parent window (like TC Lister)
    WNDCLASSW wc{};
    wc.lpfnWndProc = HostWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"WLXHarnessHost";
    RegisterClassW(&wc);

    HWND host = CreateWindowExW(0, wc.lpszClassName, L"WLX Harness Host",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700,
                                NULL, NULL, hInst, NULL);
    if (!host) return 0;

    // Choose a gpx file
    std::wstring gpx;
    if (argc >= 3 && argv && argv[2] && argv[2][0]) {
        gpx = ResolveInputPath(argv[2], exeDir);
    } else {
        gpx = OpenFileDialog(L"gpx Files (*.gpx)\0*.gpx\0All Files (*.*)\0*.*\0\0");
    }
    if (gpx.empty()) {
        if (argv) LocalFree(argv);
        return 0;
    }
    if (!FileExists(gpx)) {
        std::wstring msg = L"GPX file not found:\n" + gpx;
        MessageBoxW(NULL, msg.c_str(), L"WLXHarness", MB_ICONERROR);
        if (argv) LocalFree(argv);
        return 0;
    }

    // Call ListLoadW
    HWND child = pListLoadW(host, gpx.c_str(), 0);
    if (!child) {
        std::wstring msg = L"ListLoadW returned NULL.\n\nWLX:\n" + std::wstring(wlxPath) +
                           L"\n\nGPX:\n" + gpx;
        MessageBoxW(NULL, msg.c_str(), L"WLXHarness", MB_ICONERROR);
        return 0;
    }

    // Resize child to fill host client area
    RECT rc; GetClientRect(host, &rc);
    SetWindowPos(child, NULL, 0, 0, rc.right-rc.left, rc.bottom-rc.top, SWP_SHOWWINDOW);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Close plugin window
    pListCloseWindow(child);
    FreeLibrary(mod);
    if (argv) LocalFree(argv);
    return 0;
}
