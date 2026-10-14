#ifndef NOTIFICATION_MANAGER_H
#define NOTIFICATION_MANAGER_H

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <cstdint>

enum class ToastAction {
    Dismiss,
    OpenFile,
    OpenFolder
};

struct NotificationPayload {
    std::string id;            // Unique identifier for the toast/task
    std::string title;         // Title (e.g. "IDM: Download Complete")
    std::string message;       // Message (e.g. filename)
    std::string filepath;      // Local path to downloaded file
    bool play_sound{true};     // Play notification chime
    uint64_t file_size_bytes{0};
};

using ToastActionHandler = std::function<void(const std::string &id, ToastAction action, const std::string &filepath)>;
using NativeToastDispatcher = std::function<bool(const NotificationPayload &payload)>;

class NotificationManager {
public:
    NotificationManager();
    ~NotificationManager() = default;

    // Toast Dispatch
    bool show_download_complete(const NotificationPayload &payload);
    bool show_download_complete(const std::string &filepath, const std::string &filename = "", uint64_t file_size = 0);

    // Interactive Action Triggers (Open File / Open Folder)
    bool trigger_action(const std::string &id, ToastAction action, bool dry_run = false);
    static bool open_file(const std::string &filepath, bool dry_run = false);
    static bool open_folder_and_select_file(const std::string &filepath, bool dry_run = false);

    // History & Queries
    std::vector<NotificationPayload> get_history() const;
    void clear_history();
    size_t get_history_count() const;

    // Callbacks & Mocks for Testing
    void set_action_handler(ToastActionHandler handler);
    void set_mock_dispatcher(NativeToastDispatcher dispatcher);

    // Native Platform Toasts
    static bool dispatch_native_toast(const NotificationPayload &payload, bool dry_run = false);
    static std::string build_windows_toast_xml(const NotificationPayload &payload);

private:
    mutable std::mutex m_mutex;
    std::vector<NotificationPayload> m_history;
    ToastActionHandler m_action_handler{nullptr};
    NativeToastDispatcher m_mock_dispatcher{nullptr};
};

#endif // NOTIFICATION_MANAGER_H
