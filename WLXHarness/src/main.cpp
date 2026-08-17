
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <string>
#include <vector>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Psapi.lib")

static constexpr UINT_PTR AUTO3D_TIMER = 42;
static constexpr UINT GPXLISTER_QUERY_RENDER_MODE = WM_APP + 30;
static constexpr UINT GPXLISTER_QUERY_SYNC_STATE = WM_APP + 31;

typedef HWND (WINAPI *PFN_ListLoadW)(HWND, WCHAR*, int);
typedef int  (WINAPI *PFN_ListLoadNextW)(HWND, HWND, WCHAR*, int);
typedef void (WINAPI *PFN_ListGetDetectString)(char*, int);
typedef void (WINAPI *PFN_ListCloseWindow)(HWND);

enum : UINT {
    IDM_OPEN_NEXT = 1001,
    IDM_RELOAD_CURRENT,
    IDM_EXIT
};

struct HarnessState {
    HWND host = NULL;
    HWND child = NULL;
    PFN_ListLoadNextW listLoadNextW = nullptr;
    PFN_ListCloseWindow listCloseWindow = nullptr;
    std::wstring currentFile;
    bool auto3d = false;
    bool autoFailed = false;
    bool expect3dFailure = false;
    int expectedMode = 12;
    int autoCycles = 1;
    int completedCycles = 0;
    int autoPhase = 0;
    int waitTicks = 0;
    bool clearRequested = false;
    LRESULT lastRenderMode = 0;
    LRESULT lastSyncState = 0;
    SIZE_T initialWorkingSet = 0;
    SIZE_T peakWorkingSet = 0;
    SIZE_T cyclePeak = 0;
    SIZE_T baselineCyclePeak = 0;
    std::wstring autoLogPath;
};

static bool LoadNextFile(HarnessState& state, const std::wstring& path);

static LPARAM ProfilePoint(HWND child, int numerator) {
    RECT rc{};
    GetClientRect(child, &rc);
    return MAKELPARAM((rc.right * numerator) / 3, (rc.bottom * 9) / 10);
}

static SIZE_T CurrentWorkingSet() {
    std::vector<DWORD> processIds{ GetCurrentProcessId() };
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        bool added = true;
        while (added) {
            added = false;
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            if (Process32FirstW(snapshot, &entry)) {
                do {
                    if (std::find(processIds.begin(), processIds.end(), entry.th32ProcessID) == processIds.end() &&
                        std::find(processIds.begin(), processIds.end(), entry.th32ParentProcessID) != processIds.end()) {
                        processIds.push_back(entry.th32ProcessID);
                        added = true;
                    }
                } while (Process32NextW(snapshot, &entry));
            }
        }
        CloseHandle(snapshot);
    }

    SIZE_T total = 0;
    for (DWORD processId : processIds) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, processId);
        if (!process) continue;
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
            total += counters.PrivateUsage;
        }
        CloseHandle(process);
    }
    return total;
}

static void AppendAutomationLog(const HarnessState& state, const wchar_t* status) {
    if (state.autoLogPath.empty()) return;
    HANDLE file = CreateFileW(state.autoLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    SetFilePointer(file, 0, nullptr, FILE_END);
    wchar_t line[512]{};
    swprintf_s(line, L"%s model=%s cycles=%d phase=%d last_mode=%lld last_sync=%lld initial_private_mb=%.1f peak_private_mb=%.1f final_private_mb=%.1f\r\n",
               status, state.expect3dFailure ? L"failure" : std::to_wstring(state.expectedMode - 10).c_str(),
               state.completedCycles, state.autoPhase,
               (long long)state.lastRenderMode, (long long)state.lastSyncState,
               state.initialWorkingSet / 1048576.0, state.peakWorkingSet / 1048576.0,
               CurrentWorkingSet() / 1048576.0);
    DWORD bytes = 0;
    WriteFile(file, line, (DWORD)(wcslen(line) * sizeof(wchar_t)), &bytes, nullptr);
    CloseHandle(file);
}

static void FinishAutomation(HarnessState& state, bool passed) {
    KillTimer(state.host, AUTO3D_TIMER);
    state.autoFailed = !passed;
    AppendAutomationLog(state, passed ? L"PASS" : L"FAIL");
    PostMessageW(state.host, WM_CLOSE, 0, 0);
}

static void RunAutomationTick(HarnessState& state) {
    if (!state.child || !IsWindow(state.child)) {
        FinishAutomation(state, false);
        return;
    }
    const SIZE_T currentMemory = CurrentWorkingSet();
    state.peakWorkingSet = (std::max)(state.peakWorkingSet, currentMemory);
    state.cyclePeak = (std::max)(state.cyclePeak, currentMemory);
    const LRESULT mode = SendMessageW(state.child, GPXLISTER_QUERY_RENDER_MODE, 0, 0);
    state.lastRenderMode = mode;

    switch (state.autoPhase) {
        case 0:
            if (mode != 0) { FinishAutomation(state, false); return; }
            {
                const LPARAM profilePoint = ProfilePoint(state.child, 1);
                SendMessageW(state.child, WM_MOUSEMOVE, 0, profilePoint);
            }
            if (SendMessageW(state.child, GPXLISTER_QUERY_SYNC_STATE, 0, 0) != 4) {
                FinishAutomation(state, false);
                return;
            }
            SendMessageW(state.child, WM_KEYDOWN, 'D', 0);
            state.waitTicks = 0;
            state.autoPhase = 1;
            break;
        case 1:
            if (state.expect3dFailure && mode == -1) {
                state.completedCycles = 1;
                FinishAutomation(state, true);
                return;
            }
            if (mode == state.expectedMode) {
                const LRESULT syncState = SendMessageW(state.child, GPXLISTER_QUERY_SYNC_STATE, 0, 0);
                state.lastSyncState = syncState;
                if ((syncState & 3) != 3) {
                    FinishAutomation(state, false);
                    return;
                }
                if ((syncState & 8) == 0) {
                    if (++state.waitTicks > 20) FinishAutomation(state, false);
                    break;
                }
                state.waitTicks = 0;
                state.autoPhase = 2;
            } else if (++state.waitTicks > 60) {
                FinishAutomation(state, false);
            }
            break;
        case 2: {
            if (++state.waitTicks < 6) break;
            if (mode != state.expectedMode) { FinishAutomation(state, false); return; }
            if (state.waitTicks == 6) {
                const LPARAM profilePoint = ProfilePoint(state.child, 2);
                SendMessageW(state.child, WM_LBUTTONDOWN, MK_LBUTTON, profilePoint);
                SendMessageW(state.child, WM_LBUTTONUP, 0, profilePoint);
                SendMessageW(state.child, WM_LBUTTONDBLCLK, MK_LBUTTON, profilePoint);
            }
            const LRESULT syncState = SendMessageW(state.child, GPXLISTER_QUERY_SYNC_STATE, 0, 0);
            state.lastSyncState = syncState;
            if ((syncState & 3) != 3) {
                FinishAutomation(state, false);
                return;
            }
            if ((syncState & 8) == 0) {
                if (state.waitTicks > 26) FinishAutomation(state, false);
                break;
            }
            SendMessageW(state.child, WM_KEYDOWN, 'F', 0);
            SendMessageW(state.child, WM_KEYDOWN, 'S', 0);
            SendMessageW(state.child, WM_KEYDOWN, 'M', 0);
            SendMessageW(state.child, WM_KEYDOWN, 'M', 0);
            SendMessageW(state.child, WM_KEYDOWN, 'T', 0);
            {
                RECT hostRect{};
                GetWindowRect(state.host, &hostRect);
                SetWindowPos(state.host, nullptr, 0, 0,
                             (hostRect.right - hostRect.left) + 24,
                             (hostRect.bottom - hostRect.top) + 16,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            state.clearRequested = false;
            state.waitTicks = 0;
            state.autoPhase = 3;
            break;
        }
        case 3: {
            if (mode == 1) {
                if (++state.waitTicks > 60) FinishAutomation(state, false);
                break;
            }
            if (mode != state.expectedMode) { FinishAutomation(state, false); return; }
            if (!state.clearRequested) {
                SendMessageW(state.child, WM_MOUSELEAVE, 0, 0);
                state.clearRequested = true;
                state.waitTicks = 0;
                break;
            }
            const LRESULT syncState = SendMessageW(state.child, GPXLISTER_QUERY_SYNC_STATE, 0, 0);
            state.lastSyncState = syncState;
            if (syncState != 1) {
                if (++state.waitTicks > 20) FinishAutomation(state, false);
                break;
            }
            SendMessageW(state.child, WM_KEYDOWN, 'D', 0);
            state.waitTicks = 0;
            state.autoPhase = 4;
            break;
        }
        case 4: {
            if (mode != 0) {
                if (++state.waitTicks > 20) FinishAutomation(state, false);
                break;
            }
            const LRESULT syncState = SendMessageW(state.child, GPXLISTER_QUERY_SYNC_STATE, 0, 0);
            state.lastSyncState = syncState;
            if ((syncState & 11) != 1) {
                FinishAutomation(state, false);
                return;
            }
            ++state.completedCycles;
            state.waitTicks = 0;
            state.autoPhase = 5;
            break;
        }
        case 5:
            // Allow WebView2 child processes from the previous 3D view to exit.
            if (++state.waitTicks < (state.completedCycles >= state.autoCycles ? 30 : 10)) break;
            if (state.baselineCyclePeak == 0) state.baselineCyclePeak = state.cyclePeak;
            else if (state.cyclePeak > state.baselineCyclePeak + (SIZE_T)128 * 1024 * 1024) {
                FinishAutomation(state, false);
                return;
            }
            if (state.completedCycles >= state.autoCycles) {
                FinishAutomation(state, true);
                return;
            }
            if (!LoadNextFile(state, state.currentFile)) { FinishAutomation(state, false); return; }
            state.waitTicks = 0;
            state.cyclePeak = CurrentWorkingSet();
            state.autoPhase = 0;
            break;
    }
}

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

static std::wstring OpenFileDialog(HWND owner, const wchar_t* filter) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Select a GPX, FIT, KML, or KMZ file";
    if (GetOpenFileNameW(&ofn))
        return file;
    return L"";
}

static bool HasIniFile(const std::wstring& dir) {
    if (dir.empty()) return false;
    wchar_t candidate[MAX_PATH];
    wcsncpy_s(candidate, dir.c_str(), _TRUNCATE);
    PathAppendW(candidate, L"GPXLister.ini");
    return FileExists(candidate);
}

static std::wstring FindIniDirectory(std::wstring dir) {
    for (int depth = 0; depth < 6 && !dir.empty(); ++depth) {
        if (HasIniFile(dir)) return dir;
        std::wstring parent = GetDirName(dir);
        if (parent == dir) break;
        dir = parent;
    }
    return L"";
}

static void ConfigurePluginWorkingDirectory(const std::wstring& wlxPath, const std::wstring& exeDir) {
    wchar_t cwdBuf[MAX_PATH];
    if (GetCurrentDirectoryW(MAX_PATH, cwdBuf) > 0 && HasIniFile(cwdBuf)) {
        return;
    }

    std::wstring iniDir = FindIniDirectory(exeDir);
    if (!iniDir.empty()) {
        SetCurrentDirectoryW(iniDir.c_str());
        return;
    }

    std::wstring wlxDir = GetDirName(wlxPath);
    if (HasIniFile(wlxDir)) {
        SetCurrentDirectoryW(wlxDir.c_str());
    }
}

static void UpdateHostTitle(const HarnessState& state) {
    std::wstring title = L"WLX Harness Host";
    if (!state.currentFile.empty()) {
        title += L" - ";
        title += PathFindFileNameW(state.currentFile.c_str());
    }
    SetWindowTextW(state.host, title.c_str());
}

static void ClosePluginWindow(HarnessState& state) {
    HWND child = state.child;
    state.child = NULL;
    if (child && IsWindow(child) && state.listCloseWindow)
        state.listCloseWindow(child);
}

static bool LoadNextFile(HarnessState& state, const std::wstring& path) {
    if (!state.listLoadNextW || !state.child || path.empty()) return false;
    std::wstring mutablePath = path;
    int result = state.listLoadNextW(state.host, state.child, &mutablePath[0], 0);
    if (result != 0) {
        MessageBoxW(state.host, L"ListLoadNextW failed to load the selected file.",
                    L"WLXHarness", MB_ICONERROR);
        return false;
    }
    state.currentFile = path;
    UpdateHostTitle(state);
    return true;
}

LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    HarnessState* state = reinterpret_cast<HarnessState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = static_cast<HarnessState*>(create->lpCreateParams);
            state->host = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            return TRUE;
        }
        case WM_COMMAND:
            if (!state) break;
            switch (LOWORD(wParam)) {
                case IDM_OPEN_NEXT: {
                    std::wstring path = OpenFileDialog(hwnd,
                        L"GPX/FIT/KML/KMZ Files (*.gpx;*.fit;*.kml;*.kmz)\0*.gpx;*.fit;*.kml;*.kmz\0All Files (*.*)\0*.*\0\0");
                    if (!path.empty()) LoadNextFile(*state, path);
                    return 0;
                }
                case IDM_RELOAD_CURRENT:
                    if (!state->currentFile.empty()) LoadNextFile(*state, state->currentFile);
                    return 0;
                case IDM_EXIT:
                    SendMessageW(hwnd, WM_CLOSE, 0, 0);
                    return 0;
            }
            break;
        case WM_SIZE: {
            HWND child = state ? state->child : NULL;
            if (child && wParam != SIZE_MINIMIZED) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                MoveWindow(child, 0, 0, rc.right, rc.bottom, TRUE);
            }
            return 0;
        }
        case WM_TIMER:
            if (state && state->auto3d && wParam == AUTO3D_TIMER) {
                RunAutomationTick(*state);
                return 0;
            }
            break;
        case WM_CLOSE:
            if (state) ClosePluginWindow(*state);
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
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
        ofn.lpstrFilter = L"Lister Plugin (*.wlx;*.wlx64)\0*.wlx;*.wlx64\0All Files (*.*)\0*.*\0\0";
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

    ConfigurePluginWorkingDirectory(wlxPath, exeDir);

    // Load plugin
    HMODULE mod = LoadLibraryW(wlxPath);
    if (!mod) {
        MessageBoxW(NULL, L"Failed to load WLX", L"WLXHarness", MB_ICONERROR);
        if (argv) LocalFree(argv);
        return 0;
    }

    auto pListLoadW = (PFN_ListLoadW)GetProcAddress(mod, "ListLoadW");
    auto pListLoadNextW = (PFN_ListLoadNextW)GetProcAddress(mod, "ListLoadNextW");
    auto pListGetDetectString = (PFN_ListGetDetectString)GetProcAddress(mod, "ListGetDetectString");
    auto pListCloseWindow = (PFN_ListCloseWindow)GetProcAddress(mod, "ListCloseWindow");

    if (!pListLoadW || !pListGetDetectString || !pListCloseWindow) {
        MessageBoxW(NULL, L"Missing exports in WLX", L"WLXHarness", MB_ICONERROR);
        FreeLibrary(mod);
        if (argv) LocalFree(argv);
        return 0;
    }

    // Create host parent window (like TC Lister)
    WNDCLASSW wc{};
    wc.lpfnWndProc = HostWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"WLXHarnessHost";
    RegisterClassW(&wc);

    HMENU fileMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, IDM_OPEN_NEXT, L"&Open next...\tCtrl+O");
    AppendMenuW(fileMenu, MF_STRING, IDM_RELOAD_CURRENT, L"&Reload current\tF5");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(fileMenu, MF_STRING, IDM_EXIT, L"E&xit");
    if (!pListLoadNextW) {
        EnableMenuItem(fileMenu, IDM_OPEN_NEXT, MF_BYCOMMAND | MF_GRAYED);
        EnableMenuItem(fileMenu, IDM_RELOAD_CURRENT, MF_BYCOMMAND | MF_GRAYED);
    }
    HMENU menu = CreateMenu();
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"&File");

    HarnessState state;
    state.listLoadNextW = pListLoadNextW;
    state.listCloseWindow = pListCloseWindow;
    for (int i = 3; argv && i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--auto3d") == 0) {
            state.auto3d = true;
            if (i + 1 < argc) {
                const int requestedMode = _wtoi(argv[++i]);
                state.expect3dFailure = requestedMode == 0;
                state.expectedMode = state.expect3dFailure ? -1 : 10 + (std::max)(1, (std::min)(2, requestedMode));
            }
            if (i + 1 < argc) state.autoCycles = (std::max)(1, _wtoi(argv[++i]));
            if (i + 1 < argc) state.autoLogPath = ResolveInputPath(argv[++i], exeDir);
        }
    }

    HWND host = CreateWindowExW(0, wc.lpszClassName, L"WLX Harness Host",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700,
                                NULL, menu, hInst, &state);
    if (!host) {
        DestroyMenu(menu);
        FreeLibrary(mod);
        if (argv) LocalFree(argv);
        return 0;
    }

    // Choose a GPX/FIT/KML/KMZ file
    std::wstring gpx;
    if (argc >= 3 && argv && argv[2] && argv[2][0]) {
        gpx = ResolveInputPath(argv[2], exeDir);
    } else {
        gpx = OpenFileDialog(host, L"GPX/FIT/KML/KMZ Files (*.gpx;*.fit;*.kml;*.kmz)\0*.gpx;*.fit;*.kml;*.kmz\0All Files (*.*)\0*.*\0\0");
    }
    if (gpx.empty()) {
        DestroyWindow(host);
        FreeLibrary(mod);
        if (argv) LocalFree(argv);
        return 0;
    }
    if (!FileExists(gpx)) {
        std::wstring msg = L"GPX file not found:\n" + gpx;
        MessageBoxW(NULL, msg.c_str(), L"WLXHarness", MB_ICONERROR);
        DestroyWindow(host);
        FreeLibrary(mod);
        if (argv) LocalFree(argv);
        return 0;
    }

    // Call ListLoadW
    std::wstring mutableGpx = gpx;
    HWND child = pListLoadW(host, &mutableGpx[0], 0);
    if (!child) {
        std::wstring msg = L"ListLoadW returned NULL.\n\nWLX:\n" + std::wstring(wlxPath) +
                           L"\n\nGPX:\n" + gpx;
        MessageBoxW(NULL, msg.c_str(), L"WLXHarness", MB_ICONERROR);
        DestroyWindow(host);
        FreeLibrary(mod);
        if (argv) LocalFree(argv);
        return 0;
    }

    state.child = child;
    state.currentFile = gpx;
    UpdateHostTitle(state);

    // Resize child to fill host client area
    RECT rc; GetClientRect(host, &rc);
    SetWindowPos(child, NULL, 0, 0, rc.right-rc.left, rc.bottom-rc.top, SWP_SHOWWINDOW);
    if (state.auto3d) {
        state.initialWorkingSet = CurrentWorkingSet();
        state.peakWorkingSet = state.initialWorkingSet;
        state.cyclePeak = state.initialWorkingSet;
        SetTimer(host, AUTO3D_TIMER, 500, nullptr);
    }

    ACCEL accelerators[] = {
        { FCONTROL | FVIRTKEY, 'O', IDM_OPEN_NEXT },
        { FVIRTKEY, VK_F5, IDM_RELOAD_CURRENT },
        { FVIRTKEY, VK_ESCAPE, IDM_EXIT }
    };
    HACCEL acceleratorTable = CreateAcceleratorTableW(accelerators, ARRAYSIZE(accelerators));

    // Message loop. The plugin keeps normal keyboard/mouse focus; only harness
    // accelerators are intercepted for reload workflow testing.
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (acceleratorTable && TranslateAcceleratorW(host, acceleratorTable, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (acceleratorTable) DestroyAcceleratorTable(acceleratorTable);
    ClosePluginWindow(state);
    FreeLibrary(mod);
    if (argv) LocalFree(argv);
    return state.autoFailed ? 2 : 0;
}
