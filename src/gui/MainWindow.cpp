#include "MainWindow.h"
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace idm {

MainWindow::MainWindow()
    : m_title("IDM Native Engine Dashboard"),
      m_width(1100),
      m_height(720),
      m_ui_path(""),
      m_is_running(false),
      m_ipc_handler(nullptr){
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

void MainWindow::run_event_loop() {
    if(!m_is_running.load()) return;
    std::cout << "[MainWindow] Launching Desktop Window for " << m_ui_path << "..." << std::endl;

    std::error_code ec;
    std::string abs_path = std::filesystem::absolute(m_ui_path, ec).string();

#ifdef _WIN32
    std::string cmd = "start msedge --app=\"file:///" + abs_path + "\" --window-size=" + std::to_string(m_width) + "," + std::to_string(m_height);
    std::system(cmd.c_str());
#elif defined(__APPLE__)
    std::string cmd = "open -a \"Google Chrome\" --args --app=\"file:///" + abs_path + "\" &";
    std::system(cmd.c_str());
#else
    // Linux
    std::string cmd = "google-chrome --app=\"file:///" + abs_path + "\"";
    std::system(cmd.c_str());
#endif

    std::cout << "[MainWindow] Native GUI Window launched successfully." << std::endl;
}


void MainWindow::close() {
    if(m_is_running.load()){
        m_is_running = false;
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