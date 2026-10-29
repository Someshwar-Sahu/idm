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

namespace {
string escape_xml(const string &in) {
    string out;
    out.reserve(static_cast<size_t>(in.size() * 1.2));
    for (char c : in) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += c; break;
        }
    }
    return out;
}

string escape_shell_arg(const string &in) {
    string out;
    for (char c : in) {
        if (c == '"' || c == '`' || c == '$' || c == '\\' || c == '\'') out += '\\';
        out += c;
    }
    return out;
}

string base64_encode(const string &in) {
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(b64[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(b64[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}
} // namespace

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
    string cmd = "open \"" + escape_shell_arg(filepath) + "\"";
    return (system(cmd.c_str()) == 0);
#elif defined(__linux__)
    string cmd = "xdg-open \"" + escape_shell_arg(filepath) + "\"";
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

    string params;
    if(filesystem::exists(p)){
        // File exists -> launch explorer and highlight file
        params = "/select,\"" + p.string() + "\"";
    } else if (p.has_parent_path() && filesystem::exists(p.parent_path())){
        // Parent folder exists -> open folder directly
        params = "\"" + p.parent_path().string() + "\"";
    } else {
        params = ".";
    }

    // Bug #21 Fix: Use ShellExecuteA with explorer.exe instead of WinExec shell string
    HINSTANCE res = ShellExecuteA(NULL, "open", "explorer.exe", params.c_str(), NULL, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(res) > 32;
#elif defined(__APPLE__)
    string clean_path;
    for (char c : filepath) {
        if (c == '"' || c == '`' || c == '$' || c == '\\') clean_path += '\\';
        clean_path += c;
    }
    string cmd = "open -R \"" + clean_path + "\"";
    return (system(cmd.c_str()) == 0);
#elif defined(__linux__)
    try {
        string parent_dir = filesystem::path(filepath).parent_path().string();
        string clean_dir;
        for (char c : parent_dir) {
            if (c == '"' || c == '`' || c == '$' || c == '\\') clean_dir += '\\';
            clean_dir += c;
        }
        string cmd = "xdg-open \"" + clean_dir + "\"";
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
       << "      <text>" << escape_xml(payload.title) << "</text>\n"
       << "      <text>" << escape_xml(payload.message) << "</text>\n"
       << "    </binding>\n"
       << "  </visual>\n"
       << "  <actions>\n"
       << "    <action content='Open File' arguments='action=open_file&amp;path=" << escape_xml(payload.filepath) << "' activationType='foreground'/>\n"
       << "    <action content='Open Folder' arguments='action=open_folder&amp;path=" << escape_xml(payload.filepath) << "' activationType='foreground'/>\n"
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

    // Bug #21 Fix: Base64 encode title and message to completely eliminate command injection in PowerShell
    string b64_title = base64_encode(payload.title);
    string b64_msg = base64_encode(payload.message);

    ostringstream ps_cmd;
    ps_cmd << "powershell -Command \""
           << "[Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime] > $null; "
           << "$template = [Windows.UI.Notifications.ToastNotificationManager]::GetTemplateContent([Windows.UI.Notifications.ToastTemplateType]::ToastText02); "
           << "$textNodes = $template.GetElementsByTagName('text'); "
           << "$t = [System.Text.Encoding]::UTF8.GetString([System.Convert]::FromBase64String('" << b64_title << "')); "
           << "$m = [System.Text.Encoding]::UTF8.GetString([System.Convert]::FromBase64String('" << b64_msg << "')); "
           << "$textNodes.Item(0).AppendChild($template.CreateTextNode($t)) > $null; "
           << "$textNodes.Item(1).AppendChild($template.CreateTextNode($m)) > $null; "
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
    std::string clean_title = escape_shell_arg(payload.title);
    std::string clean_msg = escape_shell_arg(payload.message);
    std::string cmd = "osascript -e 'display notification \"" + clean_msg + "\" with title \"" + clean_title + "\"'";
    return (system(cmd.c_str()) == 0);
#elif defined(__linux__)
    std::string clean_title = escape_shell_arg(payload.title);
    std::string clean_msg = escape_shell_arg(payload.message);
    std::string cmd = "notify-send \"" + clean_title + "\" \"" + clean_msg + "\"";
    return (system(cmd.c_str()) == 0);
#endif
    return true;
}