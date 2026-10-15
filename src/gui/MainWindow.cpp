#include "MainWindow.h"
#include "../ipc/LocalServer.h"
#include <chrono>
#include <handleapi.h>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <minwinbase.h>
#include <minwindef.h>
#include <mutex>
#include <processthreadsapi.h>
#include <vector>
#include <winnt.h>
#include <winscard.h>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
    #include <shellapi.h>
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
      m_process_handler(nullptr){
}

MainWindow::~MainWindow() {
    close();
}

bool MainWindow::init(const std::string& title, int width, int height, const std::string& ui_index_path){
    m_title = title;
    m_width = width;
    m_height = height;
    m_ui_path = ui_index_path;

    std::ifstream file_check(ui_index_path);
    if(!file_check.good()){
        std::cerr<<"[MainWindow] Warning: UI HTML path not found or unreadable: "<<ui_index_path<<std::endl;
        return false;
    }
    file_check.close();

    m_is_running = true;
    std::cout<<"[MainWindow] GUI Window initialized successfully ("<<m_width<<"x"<<m_height<<") -> Loading UI: "<<m_ui_path<<std::endl;
    return true;
}

void MainWindow::run_event_loop(bool headless) {
    if(!m_is_running.load()) return;

    if(headless){
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]() { return !m_is_running.load(); });
        std::cout<<"[MainWindow] Headless keep-alive event loop unblocked and terminated cleanly."<<std::endl;
        return;
    }
    std::cout << "[MainWindow] Launching Desktop Window for " << m_ui_path << "..." << std::endl;

    std::error_code ec;
    std::string abs_path = std::filesystem::absolute(m_ui_path, ec).string();

#ifdef _WIN32
    // Search for Microsoft Edge or Google Chrome executable
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

    BOOL created = FALSE;
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    if (!browser_exe.empty()) {
        char temp_path[MAX_PATH];
        DWORD tp_len = GetTempPathA(MAX_PATH, temp_path);
        std::string profile_dir = (tp_len > 0) ? (std::string(temp_path) + "idm_app_profile") : "idm_app_profile";

        std::string cmd = "\"" + browser_exe + "\" --app=\"file:///" + abs_path + "\" --user-data-dir=\"" + profile_dir + "\" --window-size=" + std::to_string(m_width) + "," + std::to_string(m_height);

        std::vector<char> cmd_buf(cmd.begin(), cmd.end());
        cmd_buf.push_back('\0');

        created = CreateProcessA(NULL, cmd_buf.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    }

    if(created){
        m_process_handler = pi.hProcess;
        CloseHandle(pi.hThread);
        std::cout<< "[MainWindow] Native GUI Window process launched (PID: "<<pi.dwProcessId<< ")."<<std::endl;

        while(m_is_running.load()){
            std::unique_lock<std::mutex> lock(m_mutex);
            if(m_cv.wait_for(lock, std::chrono::milliseconds(100), [this]() { return !m_is_running.load(); })) {
                break;
            }

            DWORD exit_code = 0;
            if(GetExitCodeProcess(pi.hProcess, &exit_code) && exit_code != STILL_ACTIVE){
                std::cout<< "[MainWindow] Window was closed by user (Exit code: "<<exit_code<<")."<<std::endl;
                close();
                break;
            }
        }
        CloseHandle(pi.hProcess);
        m_process_handler = nullptr;
    } else {
        std::cout << "[MainWindow] Launching UI in default browser via ShellExecute..." << std::endl;
        ShellExecuteA(NULL, "open", abs_path.c_str(), NULL, NULL, SW_SHOWNORMAL);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]() { return !m_is_running.load(); });
    }
#elif defined(__APPLE__)
    std::string cmd = "open -a \"Google Chrome\" --args --app=\"file:///" + abs_path + "\" &";
    std::system(cmd.c_str());
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this]() { return !m_is_running.load(); });
#else
    std::string cmd = "google-chrome --app=\"file:///" + abs_path + "\"";
    std::system(cmd.c_str());
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this]() { return !m_is_running.load(); });
#endif

    std::cout << "[MainWindow] Native GUI Window loop finished." << std::endl;
}


void MainWindow::close() {
    bool was_running = m_is_running.exchange(false);
    if(was_running){
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_cv.notify_all();
        }

#ifdef _WIN32
        if(m_process_handler != nullptr){
            TerminateProcess(static_cast<HANDLE>(m_process_handler), 0);
        }
#endif
        if(m_server != nullptr){
            m_server -> stop();
        }

        if(m_close_callback){
            m_close_callback();
        }

        std::cout<<"[MainWindow] Window closed and resources freed."<<std::endl;
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

void MainWindow::set_ipc_handler(IPCHandler handler){
    m_ipc_handler = handler;
}

void MainWindow::set_close_callback(WindowCloseCallback callback){
    m_close_callback = callback;
}

void MainWindow::attach_server(LocalServer *server){
    m_server = server;
}

std::string MainWindow::dispatch_ipc_message(const std::string& raw_json_message){
    json response;
    try{
        json request = json::parse(raw_json_message);
        
        if(m_ipc_handler){
            response = m_ipc_handler(request);
        }else {
            std::string action = request.value("action", "unknown");
            if(action == "PING"){
                response["status"] = "OK";
                response["message"] = "PONG from IDM C++ MainWindow Backend";
            } else{
                response["status"] = "ERROR";
                response["message"] = "No custom IPC handler registered for action: "+ action;
            }
        }
    } catch (const std:: exception& e){
        response["status"] = "ERROR";
        response["message"] = std::string("JSON Parse Failure: ") +e.what();       
    }
    return response.dump();
}

}