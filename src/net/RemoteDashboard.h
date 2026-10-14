#ifndef REMOTE_DASHBOARD_H
#define REMOTE_DASHBOARD_H

#include <string>
#include <vector>
#include <mutex>

using namespace std;

// Forward declaration in global namespace
class DownloadQueue;

namespace idm {

class LocalServer;
struct HttpRequest;
struct HttpResponse;

enum class DockState {
    Hidden,
    Collapsed,
    Expanded
};

struct DockConfig {
    int x{100};
    int y{100};
    int width{220};
    int height{64};
    double opacity{0.90};       // 0.1 to 1.0
    bool always_on_top{true};
    bool auto_hide{false};
    DockState state{DockState::Expanded};
};

class RemoteDashboard {
public:
    RemoteDashboard();
    ~RemoteDashboard() = default;

    // 1. Server Integration & Route Dispatch
    void attach_to_server(LocalServer& server, DownloadQueue* queue);

    // 2. Embedded Mobile Web Dashboard UI
    static string get_embedded_dashboard_html();

    // 3. Local LAN IP & Pairing PIN Security
    static string get_local_ip_address();
    static void set_mock_local_ip(const string& ip);
    string get_dashboard_url(int port) const;

    void set_pin_required(bool required);
    bool is_pin_required() const;
    string get_pin() const;
    string regenerate_pin();
    bool verify_pin(const string& candidate_pin) const;

    // 4. Floating Desktop Mini-Dock Engine
    DockConfig get_dock_config() const;
    void update_dock_config(const DockConfig& config);
    void set_dock_position(int x, int y);
    void set_dock_state(DockState state);
    void set_dock_opacity(double opacity);

    static bool is_valid_drop_url(const string& text);
    static vector<string> parse_dropped_links(const string& raw_drop_text);
    bool handle_dropped_url(const string& url, DownloadQueue* queue);

private:
    mutable mutex m_mutex;
    DownloadQueue* m_queue{nullptr};

    bool m_pin_required{false};
    string m_pin{"1234"};

    DockConfig m_dock_config;
    static string s_mock_ip;
};

} // namespace idm

#endif // REMOTE_DASHBOARD_H
