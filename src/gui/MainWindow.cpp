#include "MainWindow.h"
#include "../ipc/LocalServer.h"
#include "../queue/DownloadQueue.h"
#include <chrono>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
    #include <shellapi.h>

    #define WM_TRAYICON (WM_USER + 101)
    #define IDM_TRAY_OPEN 2001
    #define IDM_TRAY_PAUSE_ALL 2002
    #define IDM_TRAY_RESUME_ALL 2003
    #define IDM_TRAY_STOP_ALL 2004
    #define IDM_TRAY_SPEED_ON 2005
    #define IDM_TRAY_SPEED_OFF 2006
    #define IDM_TRAY_EXIT 2007

    static idm::MainWindow* s_main_window_instance = nullptr;

    static LRESULT CALLBACK TrayWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == WM_TRAYICON) {
            if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP) {
                if (s_main_window_instance) {
                    s_main_window_instance->restore_or_launch_window();
                }
                return 0;
            } else if (lParam == WM_RBUTTONUP) {
                HMENU hMenu = CreatePopupMenu();
                AppendMenuA(hMenu, MF_STRING, IDM_TRAY_OPEN, "Open Internet Download Manager");
                AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuA(hMenu, MF_STRING, IDM_TRAY_PAUSE_ALL, "Pause All Downloads");
                AppendMenuA(hMenu, MF_STRING, IDM_TRAY_RESUME_ALL, "Resume All Downloads");
                AppendMenuA(hMenu, MF_STRING, IDM_TRAY_STOP_ALL, "Stop All Downloads");
                AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuA(hMenu, MF_STRING, IDM_TRAY_SPEED_ON, "Speed Limiter: Turn On");
                AppendMenuA(hMenu, MF_STRING, IDM_TRAY_SPEED_OFF, "Speed Limiter: Turn Off");
                AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuA(hMenu, MF_STRING, IDM_TRAY_EXIT, "Exit");

                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hWnd);
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
                PostMessageA(hWnd, WM_NULL, 0, 0);
                DestroyMenu(hMenu);
                return 0;
            }
        } else if (uMsg == WM_COMMAND) {
            int wmId = LOWORD(wParam);
            if (!s_main_window_instance) return 0;

            switch (wmId) {
                case IDM_TRAY_OPEN:
                    s_main_window_instance->restore_or_launch_window();
                    break;
                case IDM_TRAY_PAUSE_ALL: {
                    nlohmann::json req;
                    req["action"] = "PAUSE_ALL";
                    s_main_window_instance->dispatch_ipc_message(req.dump());
                    break;
                }
                case IDM_TRAY_RESUME_ALL: {
                    nlohmann::json req;
                    req["action"] = "RESUME_ALL";
                    s_main_window_instance->dispatch_ipc_message(req.dump());
                    break;
                }
                case IDM_TRAY_STOP_ALL: {
                    nlohmann::json req;
                    req["action"] = "STOP_ALL";
                    s_main_window_instance->dispatch_ipc_message(req.dump());
                    break;
                }
                case IDM_TRAY_SPEED_ON: {
                    nlohmann::json req;
                    req["action"] = "SET_SPEED_LIMIT";
                    req["enabled"] = true;
                    s_main_window_instance->dispatch_ipc_message(req.dump());
                    break;
                }
                case IDM_TRAY_SPEED_OFF: {
                    nlohmann::json req;
                    req["action"] = "SET_SPEED_LIMIT";
                    req["enabled"] = false;
                    s_main_window_instance->dispatch_ipc_message(req.dump());
                    break;
                }
                case IDM_TRAY_EXIT:
                    s_main_window_instance->close();
                    break;
            }
            return 0;
        }
        return DefWindowProcA(hWnd, uMsg, wParam, lParam);
    }
#endif

namespace idm {

MainWindow::MainWindow()
    : m_title("IDM Native Engine Dashboard"),
      m_width(1100),
      m_height(720),
      m_ui_path(""),
      m_is_running(false),
      m_ipc_handler(nullptr),
      m_close_callback(nullptr),
      m_server(nullptr),
      m_queue(nullptr),
      m_process_handler(nullptr) {
}

MainWindow::~MainWindow() {
    close();
}

static std::string get_executable_directory() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, buffer, MAX_PATH);
    if (len > 0) {
        std::filesystem::path p(buffer);
        return p.parent_path().string();
    }
#endif
    return std::filesystem::current_path().string();
}

static std::string resolve_existing_ui_path(const std::string& input_path) {
    if (std::filesystem::exists(input_path)) {
        return input_path;
    }

    std::filesystem::path exe_dir(get_executable_directory());
    std::filesystem::path cand1 = exe_dir / input_path;
    if (std::filesystem::exists(cand1)) {
        return cand1.string();
    }

    std::filesystem::path cand2 = exe_dir.parent_path() / input_path;
    if (std::filesystem::exists(cand2)) {
        return cand2.string();
    }

    return input_path;
}

bool MainWindow::init(const std::string& title, int width, int height, const std::string& ui_index_path) {
    m_title = title;
    m_width = width;
    m_height = height;

    std::string resolved_path = resolve_existing_ui_path(ui_index_path);

    std::ifstream file_check(resolved_path);
    if (!file_check.good()) {
        std::cerr << "[MainWindow] Warning: UI HTML path not found or unreadable: " << ui_index_path << std::endl;
        return false;
    }
    file_check.close();

    m_ui_path = resolved_path;
    m_is_running = true;
    std::cout << "[MainWindow] GUI Window initialized successfully (" << m_width << "x" << m_height << ") -> Loading UI: " << m_ui_path << std::endl;
    return true;
}

bool MainWindow::setup_system_tray() {
#ifdef _WIN32
    if (m_tray_icon_added) return true;
    s_main_window_instance = this;

    HINSTANCE hInstance = GetModuleHandleA(NULL);
    const char CLASS_NAME[] = "IDMTrayWindowClass";

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = TrayWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(
        0, CLASS_NAME, "IDM System Tray Host",
        0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, hInstance, NULL
    );

    if (!hwnd) {
        std::cerr << "[MainWindow] Failed to create tray host window." << std::endl;
        return false;
    }
    m_tray_hwnd = static_cast<void*>(hwnd);

    NOTIFYICONDATAA nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd = hwnd;
    nid.uID = 101;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    HICON hAppIcon = LoadIconA(hInstance, "IDI_APPICON");
    if (!hAppIcon) hAppIcon = LoadIconA(hInstance, MAKEINTRESOURCEA(101));
    if (!hAppIcon) hAppIcon = LoadIconA(NULL, IDI_APPLICATION);
    nid.hIcon = hAppIcon;
    strcpy_s(nid.szTip, sizeof(nid.szTip), "Internet Download Manager (IDM)");

    if (Shell_NotifyIconA(NIM_ADD, &nid)) {
        m_tray_icon_added = true;
        std::cout << "[MainWindow] System Tray icon registered successfully in Windows Notification Area." << std::endl;
        return true;
    }
#endif
    return false;
}

void MainWindow::remove_system_tray() {
#ifdef _WIN32
    if (m_tray_icon_added && m_tray_hwnd) {
        NOTIFYICONDATAA nid = {};
        nid.cbSize = sizeof(NOTIFYICONDATAA);
        nid.hWnd = static_cast<HWND>(m_tray_hwnd);
        nid.uID = 101;
        Shell_NotifyIconA(NIM_DELETE, &nid);
        m_tray_icon_added = false;
    }
    if (m_tray_hwnd) {
        DestroyWindow(static_cast<HWND>(m_tray_hwnd));
        m_tray_hwnd = nullptr;
    }
#endif
}

void MainWindow::show_tray_notification(const std::string& title, const std::string& message) {
#ifdef _WIN32
    if (!m_tray_icon_added || !m_tray_hwnd) return;
    NOTIFYICONDATAA nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd = static_cast<HWND>(m_tray_hwnd);
    nid.uID = 101;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    strcpy_s(nid.szInfoTitle, sizeof(nid.szInfoTitle), title.c_str());
    strcpy_s(nid.szInfo, sizeof(nid.szInfo), message.c_str());
    Shell_NotifyIconA(NIM_MODIFY, &nid);
#else
    std::cout << "[Notification] " << title << " - " << message << std::endl;
#endif
}

#ifdef _WIN32
static BOOL CALLBACK EnumWindowsFindIDM(HWND hwnd, LPARAM lParam) {
    HWND* out_hwnd = reinterpret_cast<HWND*>(lParam);
    if (!IsWindow(hwnd)) return TRUE;
    char title[512] = {0};
    GetWindowTextA(hwnd, title, sizeof(title));
    if (strstr(title, "Internet Download Manager") || strstr(title, "IDM Native Engine")) {
        char className[256] = {0};
        GetClassNameA(hwnd, className, sizeof(className));
        if (strstr(className, "Chrome_WidgetWin") || strstr(className, "ApplicationFrameWindow") || strstr(className, "Intermediate D3D")) {
            *out_hwnd = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

static HWND find_idm_window(const std::string& fallback_title) {
    HWND hwndTarget = FindWindowA(NULL, "Internet Download Manager 6.38");
    if (!hwndTarget && !fallback_title.empty()) {
        hwndTarget = FindWindowA(NULL, fallback_title.c_str());
    }
    if (!hwndTarget) {
        EnumWindows(EnumWindowsFindIDM, reinterpret_cast<LPARAM>(&hwndTarget));
    }
    return hwndTarget;
}
#endif

void MainWindow::restore_or_launch_window() {
#ifdef _WIN32
    static auto s_last_launch_time = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    auto now = std::chrono::steady_clock::now();
    long long elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_last_launch_time).count();

    // 1. Check if the window is already open/minimized
    HWND hwndExisting = find_idm_window(m_title);
    if (hwndExisting && IsWindow(hwndExisting)) {
        ShowWindow(hwndExisting, SW_SHOW);
        ShowWindow(hwndExisting, SW_RESTORE);
        SetForegroundWindow(hwndExisting);
        return;
    }

    // Debounce rapid double-clicks if a launch was triggered within the last 1500ms
    if (elapsed_ms < 1500) {
        return;
    }
    s_last_launch_time = now;

    // Clean up any stale process handle or zombie background process
    if (m_process_handler != nullptr) {
        DWORD exit_code = 0;
        if (GetExitCodeProcess(static_cast<HANDLE>(m_process_handler), &exit_code)) {
            if (exit_code == STILL_ACTIVE) {
                TerminateProcess(static_cast<HANDLE>(m_process_handler), 0);
            }
        }
        CloseHandle(static_cast<HANDLE>(m_process_handler));
        m_process_handler = nullptr;
    }

    std::string browser_exe = "";
    const std::vector<std::string> candidates = {
        "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
        "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
        "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
        "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe"
    };
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            browser_exe = path;
            break;
        }
    }

    int port = m_server ? m_server->get_port() : 8080;
    std::error_code ec;
    std::string abs_path = std::filesystem::absolute(m_ui_path, ec).string();
    std::string target_url = (m_server && m_server->is_running()) 
        ? ("http://127.0.0.1:" + std::to_string(port) + "/index.html")
        : ("file:///" + abs_path);

    if (!browser_exe.empty()) {
        char temp_path[MAX_PATH];
        DWORD tp_len = GetTempPathA(MAX_PATH, temp_path);
        std::string profile_dir = (tp_len > 0) ? (std::string(temp_path) + "idm_app_profile") : "idm_app_profile";

        // Remove any stale lock file from previous abnormal termination
        std::error_code ec_lock;
        std::filesystem::remove(profile_dir + "\\lockfile", ec_lock);

        std::string cmd = "\"" + browser_exe + "\" "
            "--app=\"" + target_url + "\" "
            "--user-data-dir=\"" + profile_dir + "\" "
            "--window-size=" + std::to_string(m_width) + "," + std::to_string(m_height) + " "
            "--disable-popup-blocking "
            "--no-first-run "
            "--no-default-browser-check "
            "--disable-background-mode "
            "--disable-background-networking "
            "--disable-component-update";

        std::vector<char> cmd_buf(cmd.begin(), cmd.end());
        cmd_buf.push_back('\0');

        STARTUPINFOA si{};
        PROCESS_INFORMATION pi{};
        si.cb = sizeof(si);

        if (CreateProcessA(NULL, cmd_buf.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            m_process_handler = pi.hProcess;
            CloseHandle(pi.hThread);
            std::cout << "[MainWindow] Native GUI Window process launched (PID: " << pi.dwProcessId << ")." << std::endl;
        }
    } else {
        ShellExecuteA(NULL, "open", target_url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
#endif
}

void MainWindow::run_event_loop(bool headless) {
    if (!m_is_running.load()) return;

    if (headless) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]() { return !m_is_running.load(); });
        std::cout << "[MainWindow] Headless keep-alive event loop unblocked and terminated cleanly." << std::endl;
        return;
    }

    setup_system_tray();
    restore_or_launch_window();

    while (m_is_running.load()) {
#ifdef _WIN32
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (m_process_handler != nullptr) {
            DWORD exit_code = 0;
            if (GetExitCodeProcess(static_cast<HANDLE>(m_process_handler), &exit_code) && exit_code != STILL_ACTIVE) {
                std::cout << "[MainWindow] Window was closed by user (Exit code: " << exit_code << "). Minimizing to system tray." << std::endl;
                CloseHandle(static_cast<HANDLE>(m_process_handler));
                m_process_handler = nullptr;

                show_tray_notification("Internet Download Manager", "IDM is still running in the background. Ongoing downloads will continue.");
            }
        }
        Sleep(50);
#else
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_cv.wait_for(lock, std::chrono::milliseconds(100), [this]() { return !m_is_running.load(); })) {
            break;
        }
#endif
    }

    std::cout << "[MainWindow] Native GUI Window loop finished." << std::endl;
}

void MainWindow::close() {
    bool was_running = m_is_running.exchange(false);
    if (was_running) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_cv.notify_all();
        }

#ifdef _WIN32
        remove_system_tray();
        if (m_process_handler != nullptr) {
            TerminateProcess(static_cast<HANDLE>(m_process_handler), 0);
            CloseHandle(static_cast<HANDLE>(m_process_handler));
            m_process_handler = nullptr;
        }
        HWND hwndTarget = find_idm_window(m_title);
        if (hwndTarget && IsWindow(hwndTarget)) {
            PostMessageA(hwndTarget, WM_CLOSE, 0, 0);
        }
#endif
        if (m_server != nullptr) {
            m_server->stop();
        }

        if (m_close_callback) {
            m_close_callback();
        }

        std::cout << "[MainWindow] Window closed and resources freed." << std::endl;
    }
}

bool MainWindow::is_running() const {
    return m_is_running.load();
}

std::string MainWindow::get_title() const {
    return m_title;
}

std::string MainWindow::get_ui_path() const {
    return m_ui_path;
}

int MainWindow::get_width() const {
    return m_width;
}

int MainWindow::get_height() const {
    return m_height;
}

void MainWindow::set_ipc_handler(IPCHandler handler) {
    m_ipc_handler = handler;
}

void MainWindow::set_close_callback(WindowCloseCallback callback) {
    m_close_callback = callback;
}

void MainWindow::attach_server(LocalServer* server) {
    m_server = server;
}

void MainWindow::attach_queue(DownloadQueue* queue) {
    m_queue = queue;
}

std::string MainWindow::dispatch_ipc_message(const std::string& raw_json_message) {
    json response;
    try {
        json request = json::parse(raw_json_message);
        
        if (m_ipc_handler) {
            response = m_ipc_handler(request);
        } else {
            std::string action = request.value("action", "unknown");
            if (action == "PING") {
                response["status"] = "OK";
                response["message"] = "PONG from IDM C++ MainWindow Backend";
            } else {
                response["status"] = "ERROR";
                response["message"] = "No custom IPC handler registered for action: " + action;
            }
        }
    } catch (const std::exception& e) {
        response["status"] = "ERROR";
        response["message"] = std::string("JSON Parse Failure: ") + e.what();       
    }
    return response.dump();
}

} // namespace idm