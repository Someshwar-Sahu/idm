#include "NotificationManager.h"
#include <sstream>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#ifdef PlaySound
#undef PlaySound
#endif
#endif


using namespace std;

NotificationManager::NotificationManager() = default;

bool NotificationManager::show_download_complete(const NotificationPayload &payload){
    if(payload.filepath.empty()){
        return false;
    }
    {
        lock_guard<mutex> lock(m_mutex);
        m_history.push_back(payload);
    }

    NativeToastDispatcher dispatcher = nullptr;
    {
        lock_guard<mutex> lock(m_mutex);
        dispatcher = m_mock_dispatcher;
    }
    if(dispatcher){
        return dispatcher(payload);
    }

    return dispatch_native_toast(payload, false);
}
bool NotificationManager::show_download_complete(const std::string &filepath, const std::string &filename, uint64_t file_size){
    NotificationPayload payload;
    payload.filepath = filepath;
    payload.file_size_bytes = file_size;

    string name = filename;
    if(name.empty()){
        try{
            name = filesystem::path(filepath).filename().string();
        } catch (...){
            name = filepath;
        }
    }

    payload.title = "IDM: Download Complete";
    payload.message = name;
    payload.id = name;
    return show_download_complete(payload);
}

bool NotificationManager::trigger_action(const std::string &id, ToastAction action, bool dry_run){
    NotificationPayload target_payload;
    bool found = false;

    {
        lock_guard<mutex> lock(m_mutex);
        for (const auto &p : m_history){
            if(p.id == id || p.filepath == id){
                target_payload = p;
                found = true;
                break;
            }
        }
    }

    ToastActionHandler handler = nullptr;
    {
        lock_guard<mutex> lock(m_mutex);
        handler = m_action_handler;
    }
    if(handler && found){
        handler(id, action, target_payload.filepath);
    }

    if(!found){
        return false;
    }
    if(action == ToastAction::OpenFile){
        return open_file(target_payload.filepath, dry_run);
    } else if (action == ToastAction::OpenFolder){
        return open_folder_and_select_file(target_payload.filepath, dry_run);
    }

    return true;
}

bool NotificationManager::open_file(const std::string &filepath, bool dry_run){
    if(filepath.empty()) return false;
    if(dry_run) return true;

#ifdef _WIN32
    HINSTANCE res = ShellExecuteA(NULL, "open", filepath.c_str(), NULL, NULL, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(res) > 32;
#elif defined(__APPLE__)
    string cmd = "open \"" + filepath + "\"";
    return (system(cmd.c_str()) == 0);
#elif defined(__linux__)
    string cmd = "xdg-open \"" + filepath + "\"";
    return (system(cmd.c_str()) ==0);
#endif
    return false;
}

bool NotificationManager::open_folder_and_select_file(const std::string &filepath, bool dry_run){
    if(filepath.empty()) return false;
    if(dry_run) return true;

#ifdef _WIN32
    filesystem::path p(filepath);
    p.make_preferred(); // Converts forward slashes to backslashes

    string cmd;
    if(filesystem::exists(p)){
        // File exists -> launch explorer and highlight file
        cmd = "explorer.exe /select,\"" + p.string() + "\"";
    } else if (p.has_parent_path() && filesystem::exists(p.parent_path())){
        // Parent folder exists -> open folder directly
        cmd = "explorer.exe \"" + p.parent_path().string() + "\"";
    } else {
        cmd = "explorer.exe .";
    }

    UINT res = WinExec(cmd.c_str(), SW_SHOWNORMAL);
    return res > 31;
#elif defined(__APPLE__)
    string cmd = "open -R \"" + filepath + "\"";
    return (system(cmd.c_str()) == 0);
#elif defined(__linux__)
    try {
        string parent_dir = filesystem::path(filepath).parent_path().string();
        string cmd = "xdg-open \"" + parent_dir + "\"";
        return (system(cmd.c_str()) == 0);
    } catch (...) {
        return false;
    }
#endif
    return false;
}

vector<NotificationPayload> NotificationManager::get_history() const {
    lock_guard<mutex> lock(m_mutex);
    return m_history;
}

void NotificationManager::clear_history(){
    lock_guard<mutex> lock(m_mutex);
    m_history.clear();
}

size_t NotificationManager::get_history_count() const{
    lock_guard<mutex> lock(m_mutex);
    return m_history.size();
}

void NotificationManager::set_action_handler(ToastActionHandler handler){
    lock_guard<mutex> lock(m_mutex);
    m_action_handler = handler;
}

void NotificationManager::set_mock_dispatcher(NativeToastDispatcher dispatcher){
    lock_guard<mutex> lock(m_mutex);
    m_mock_dispatcher = dispatcher;
}

string NotificationManager::build_windows_toast_xml(const NotificationPayload &payload) {
    ostringstream ss;
    ss << "<toast activationType='protocol'>\n"
       << "  <visual>\n"
       << "    <binding template='ToastGeneric'>\n"
       << "      <text>" << payload.title << "</text>\n"
       << "      <text>" << payload.message << "</text>\n"
       << "    </binding>\n"
       << "  </visual>\n"
       << "  <actions>\n"
       << "    <action content='Open File' arguments='action=open_file&amp;path=" << payload.filepath << "' activationType='foreground'/>\n"
       << "    <action content='Open Folder' arguments='action=open_folder&amp;path=" << payload.filepath << "' activationType='foreground'/>\n"
       << "  </actions>\n"
       << "</toast>";
    return ss.str();
}

bool NotificationManager::dispatch_native_toast(const NotificationPayload &payload, bool dry_run){
    if(payload.filepath.empty()) return false;
    if (dry_run) return true;

#ifdef _WIN32
    if(payload.play_sound){
        PlaySoundA("SystemNotification", NULL, SND_ALIAS | SND_ASYNC);
    }

    string safe_title = payload.title;
    string safe_msg = payload.message;
    for(char &c : safe_title) if (c == '"' || c == '\'') c = ' ';
    for(char &c : safe_msg) if (c == '"' || c == '\'') c = ' ';

    ostringstream ps_cmd;
    ps_cmd << "powershell -Command \""
           << "[Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime] > $null; "
           << "$template = [Windows.UI.Notifications.ToastNotificationManager]::GetTemplateContent([Windows.UI.Notifications.ToastTemplateType]::ToastText02); "
           << "$textNodes = $template.GetElementsByTagName('text'); "
           << "$textNodes.Item(0).AppendChild($template.CreateTextNode('" << safe_title << "')) > $null; "
           << "$textNodes.Item(1).AppendChild($template.CreateTextNode('" << safe_msg << "')) > $null; "
           << "$toast = [Windows.UI.Notifications.ToastNotification]::new($template); "
           << "[Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier('Internet Download Manager').Show($toast);\"";

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    string cmd_str = ps_cmd.str();
    if(CreateProcessA(NULL, &cmd_str[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)){
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    return false;
#elif defined(__APPLE__)
    std::string cmd = "osascript -e 'display notification \"" + payload.message + "\" with title \"" + payload.title + "\"'";
    return (system(cmd.c_str()) == 0);
#elif defined(__linux__)
    std::string cmd = "notify-send \"" + payload.title + "\" \"" + payload.message + "\"";
    return (system(cmd.c_str()) == 0);
#endif
    return true;
}