#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include "RemoteDashboard.h"
#include "../ipc/LocalServer.h"
#include "../queue/DownloadQueue.h"
#include "../core/DownloadTask.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>
#include <random>

using namespace std;
using json = nlohmann::json;

namespace idm {

string RemoteDashboard::s_mock_ip = "";

RemoteDashboard::RemoteDashboard() {
    regenerate_pin();
}

// =========================================================================
// Local LAN IP & Pairing PIN Security
// =========================================================================

string RemoteDashboard::get_local_ip_address() {
    if (!s_mock_ip.empty()) return s_mock_ip;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return "127.0.0.1";
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return "127.0.0.1";
    }
#else
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return "127.0.0.1";
#endif

    // Connect to known public DNS (8.8.8.8:53) to determine primary outbound interface IP
    sockaddr_in loopback;
    memset(&loopback, 0, sizeof(loopback));
    loopback.sin_family = AF_INET;
    loopback.sin_addr.s_addr = inet_addr("8.8.8.8");
    loopback.sin_port = htons(53);

    string local_ip = "127.0.0.1";
    if (connect(sock, reinterpret_cast<sockaddr*>(&loopback), sizeof(loopback)) == 0) {
        sockaddr_in name;
        socklen_t namelen = sizeof(name);
        if (getsockname(sock, reinterpret_cast<sockaddr*>(&name), &namelen) == 0) {
            char buf[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &name.sin_addr, buf, sizeof(buf))) {
                local_ip = buf;
            }
        }
    }

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    return local_ip;
}

void RemoteDashboard::set_mock_local_ip(const string& ip) {
    s_mock_ip = ip;
}

string RemoteDashboard::get_dashboard_url(int port) const {
    return "http://" + get_local_ip_address() + ":" + to_string(port) + "/dashboard";
}

void RemoteDashboard::set_pin_required(bool required) {
    lock_guard<mutex> lock(m_mutex);
    m_pin_required = required;
}

bool RemoteDashboard::is_pin_required() const {
    lock_guard<mutex> lock(m_mutex);
    return m_pin_required;
}

string RemoteDashboard::get_pin() const {
    lock_guard<mutex> lock(m_mutex);
    return m_pin;
}

string RemoteDashboard::regenerate_pin() {
    lock_guard<mutex> lock(m_mutex);
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(1000, 9999);
    m_pin = to_string(dis(gen));
    return m_pin;
}

bool RemoteDashboard::verify_pin(const string& candidate_pin) const {
    lock_guard<mutex> lock(m_mutex);
    if (!m_pin_required) return true;
    return (candidate_pin == m_pin);
}

// =========================================================================
// Server Integration & Route Dispatch
// =========================================================================

void RemoteDashboard::attach_to_server(LocalServer& server, DownloadQueue* queue) {
    m_queue = queue;

    // 1. Embedded Web Dashboard HTML
    server.register_route("GET", "/dashboard", [](const HttpRequest& /*req*/) -> HttpResponse {
        HttpResponse res;
        res.status_code = 200;
        res.status_message = "OK";
        res.headers["Content-Type"] = "text/html; charset=utf-8";
        res.headers["Access-Control-Allow-Origin"] = "*";
        res.body = get_embedded_dashboard_html();
        return res;
    });

    // 2. Telemetry Status JSON
    server.register_route("GET", "/api/remote/status", [this](const HttpRequest& req) -> HttpResponse {
        string pin = req.query_params.count("pin") ? req.query_params.at("pin") : "";
        if (!verify_pin(pin)) {
            return HttpResponse::error_response(401, "Invalid or missing pairing PIN");
        }

        json j;
        j["online"] = true;
        j["local_ip"] = get_local_ip_address();
        j["pin_required"] = is_pin_required();

        uint64_t total_speed = 0;
        json tasks_arr = json::array();

        if (m_queue) {
            auto tasks = m_queue->get_tasks();
            for (const auto& t : tasks) {
                if (!t) continue;
                json item;
                string id = t->get_destination_filepath();
                replace(id.begin(), id.end(), '\\', '/');
                item["id"] = id;
                item["url"] = t->get_url();
                item["filename"] = DownloadTask::extract_filename_from_url(t->get_url());
                item["status"] = static_cast<int>(t->get_status());

                double speed = t->get_speed_bps();
                if (speed > 0) total_speed += static_cast<uint64_t>(speed);

                item["speed_bps"] = speed;
                item["progress_pct"] = t->get_progress_pct();
                item["downloaded_bytes"] = t->get_downloaded_size();
                item["total_bytes"] = t->get_total_size();
                item["eta"] = t->get_eta();

                tasks_arr.push_back(item);
            }
        }

        j["total_speed_bps"] = total_speed;
        j["tasks"] = tasks_arr;
        j["task_count"] = tasks_arr.size();

        return HttpResponse::json_response(200, j);
    });

    // 3. Remote Task Submission
    server.register_route("POST", "/api/remote/add", [this](const HttpRequest& req) -> HttpResponse {
        string pin = req.json_body.value("pin", "");
        if (!verify_pin(pin)) {
            return HttpResponse::error_response(401, "Invalid or missing pairing PIN");
        }

        string url = req.json_body.value("url", "");
        if (url.empty() || !is_valid_drop_url(url)) {
            return HttpResponse::error_response(400, "Invalid or empty URL provided");
        }

        string filename = req.json_body.value("filename", "");
        if (filename.empty()) filename = DownloadTask::extract_filename_from_url(url);

        string dest = filename;
        if (m_queue) {
            auto task = make_shared<DownloadTask>(url, dest);
            m_queue->add_task(task);
        }

        return HttpResponse::json_response(201, {
            {"status", "created"},
            {"url", url},
            {"filename", filename}
        });
    });

    // 4. Remote Queue Action (Pause, Resume, Cancel)
    server.register_route("POST", "/api/remote/action", [this](const HttpRequest& req) -> HttpResponse {
        string pin = req.json_body.value("pin", "");
        if (!verify_pin(pin)) {
            return HttpResponse::error_response(401, "Invalid or missing pairing PIN");
        }

        string action = req.json_body.value("action", "");
        string target_id = req.json_body.value("id", "");

        if (!m_queue) {
            return HttpResponse::error_response(500, "Download queue unavailable");
        }

        if (action == "pause_all") {
            m_queue->pause();
            return HttpResponse::json_response(200, {{"status", "paused_all"}});
        } else if (action == "resume_all") {
            m_queue->start();
            return HttpResponse::json_response(200, {{"status", "resumed_all"}});
        } else if (action == "cancel" && !target_id.empty()) {
            string norm_target = target_id;
            replace(norm_target.begin(), norm_target.end(), '\\', '/');
            auto tasks = m_queue->get_tasks();
            bool found = false;
            for (const auto& t : tasks) {
                if (!t) continue;
                string t_path = t->get_destination_filepath();
                string t_norm = t_path;
                replace(t_norm.begin(), t_norm.end(), '\\', '/');
                if (t_norm == norm_target || t_path == target_id || t->get_url() == target_id ||
                    DownloadTask::extract_filename_from_url(t->get_url()) == target_id) {
                    m_queue->remove_task(t_path);
                    found = true;
                    break;
                }
            }
            if (!found) {
                m_queue->remove_task(target_id);
            }
            return HttpResponse::json_response(200, {{"status", "cancelled"}, {"id", target_id}});
        }

        return HttpResponse::error_response(400, "Unknown action: " + action);
    });
}

// =========================================================================
// Floating Desktop Mini-Dock Engine
// =========================================================================

DockConfig RemoteDashboard::get_dock_config() const {
    lock_guard<mutex> lock(m_mutex);
    return m_dock_config;
}

void RemoteDashboard::update_dock_config(const DockConfig& config) {
    lock_guard<mutex> lock(m_mutex);
    m_dock_config = config;
    // Clamp opacity
    if (m_dock_config.opacity < 0.1) m_dock_config.opacity = 0.1;
    if (m_dock_config.opacity > 1.0) m_dock_config.opacity = 1.0;
}

void RemoteDashboard::set_dock_position(int x, int y) {
    lock_guard<mutex> lock(m_mutex);
    m_dock_config.x = x;
    m_dock_config.y = y;
}

void RemoteDashboard::set_dock_state(DockState state) {
    lock_guard<mutex> lock(m_mutex);
    m_dock_config.state = state;
}

void RemoteDashboard::set_dock_opacity(double opacity) {
    lock_guard<mutex> lock(m_mutex);
    m_dock_config.opacity = max(0.1, min(1.0, opacity));
}

bool RemoteDashboard::is_valid_drop_url(const string& text) {
    if (text.empty() || text.length() > 4096) return false;

    // Reject whitespace-only or strings with inner spaces
    if (text.find(' ') != string::npos || text.find('\t') != string::npos) {
        return false;
    }

    if (text.rfind("http://", 0) == 0 ||
        text.rfind("https://", 0) == 0 ||
        text.rfind("ftp://", 0) == 0 ||
        text.rfind("magnet:?xt=urn:", 0) == 0) {
        return true;
    }

    return false;
}

vector<string> RemoteDashboard::parse_dropped_links(const string& raw_drop_text) {
    vector<string> links;
    istringstream iss(raw_drop_text);
    string line;
    while (getline(iss, line)) {
        // Strip carriage returns and leading/trailing whitespace
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        size_t start = line.find_first_not_of(" \t");
        if (start != string::npos) {
            line = line.substr(start);
        }
        if (is_valid_drop_url(line)) {
            links.push_back(line);
        }
    }
    return links;
}

bool RemoteDashboard::handle_dropped_url(const string& url, DownloadQueue* queue) {
    if (!is_valid_drop_url(url)) return false;

    if (queue) {
        string filename = DownloadTask::extract_filename_from_url(url);
        auto task = make_shared<DownloadTask>(url, filename);
        queue->add_task(task);
        return true;
    }

    return true;
}

// =========================================================================
// Embedded Mobile Web Dashboard UI
// =========================================================================

string RemoteDashboard::get_embedded_dashboard_html() {
    return R"html(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>IDM Remote Dashboard</title>
  <style>
    :root {
      --bg: #0d1117;
      --card-bg: #161b22;
      --border: #30363d;
      --accent: #2ea043;
      --primary: #58a6ff;
      --text: #c9d1d9;
      --text-dim: #8b949e;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
    body { background: var(--bg); color: var(--text); padding: 16px; min-height: 100vh; }
    .header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--border); padding-bottom: 12px; margin-bottom: 16px; }
    .title { font-size: 18px; font-weight: 700; color: var(--primary); }
    .badge { background: var(--accent); color: #fff; padding: 4px 10px; border-radius: 20px; font-size: 11px; font-weight: 600; }
    .card { background: var(--card-bg); border: 1px solid var(--border); border-radius: 8px; padding: 14px; margin-bottom: 12px; }
    .task-title { font-size: 14px; font-weight: 600; color: #fff; margin-bottom: 6px; word-break: break-all; }
    .task-url { font-size: 11px; color: var(--text-dim); margin-bottom: 8px; word-break: break-all; }
    .progress-bar-bg { width: 100%; height: 6px; background: #21262d; border-radius: 3px; overflow: hidden; margin-bottom: 6px; }
    .progress-bar-fill { height: 100%; width: 0%; background: var(--accent); transition: width 0.3s; }
    .add-box { display: flex; gap: 8px; margin-top: 16px; }
    .input { flex: 1; padding: 10px; background: #010409; border: 1px solid var(--border); border-radius: 6px; color: #fff; font-size: 13px; }
    .btn { padding: 10px 16px; background: var(--accent); border: none; border-radius: 6px; color: #fff; font-weight: 600; cursor: pointer; }
    .btn-action { padding: 6px 12px; background: #21262d; border: 1px solid var(--border); border-radius: 4px; color: #fff; font-size: 11px; cursor: pointer; }
  </style>
</head>
<body>
  <div class="header">
    <div class="title">⚡ IDM Remote</div>
    <div style="display: flex; gap: 8px; align-items: center;">
      <span id="speedBadge" style="font-size: 12px; color: var(--primary); font-weight: 600;">0 B/s</span>
      <div id="statusBadge" class="badge">Connected</div>
    </div>
  </div>

  <div id="taskList">
    <div style="text-align: center; color: var(--text-dim); padding: 30px;">Loading downloads...</div>
  </div>

  <div class="add-box">
    <input type="text" id="addUrl" class="input" placeholder="Paste link to download remotely...">
    <button id="btnAdd" class="btn">Download</button>
  </div>

  <script>
    function formatSpeed(bps) {
      if (!bps || bps <= 0) return '0 B/s';
      if (bps >= 1048576) return (bps / 1048576).toFixed(1) + ' MB/s';
      if (bps >= 1024) return (bps / 1024).toFixed(1) + ' KB/s';
      return Math.round(bps) + ' B/s';
    }

    function formatBytes(bytes) {
      if (!bytes || bytes <= 0) return '0 B';
      if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(2) + ' GB';
      if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + ' MB';
      if (bytes >= 1024) return (bytes / 1024).toFixed(1) + ' KB';
      return bytes + ' B';
    }

    async function updateDashboard() {
      try {
        const res = await fetch('/api/remote/status');
        if (!res.ok) throw new Error();
        const data = await res.json();
        const list = document.getElementById('taskList');
        document.getElementById('speedBadge').textContent = formatSpeed(data.total_speed_bps || 0);

        if (data.tasks.length === 0) {
          list.innerHTML = '<div style="text-align: center; color: var(--text-dim); padding: 30px;">No active downloads</div>';
          return;
        }
        list.innerHTML = data.tasks.map(t => {
          const pct = Math.min(100, Math.max(0, t.progress_pct || 0)).toFixed(1);
          const spd = formatSpeed(t.speed_bps || 0);
          const dl = formatBytes(t.downloaded_bytes || 0);
          const tot = formatBytes(t.total_bytes || 0);
          return `
          <div class="card">
            <div class="task-title">${t.filename || 'Download'}</div>
            <div class="task-url">${t.url}</div>
            <div class="progress-bar-bg">
              <div class="progress-bar-fill" style="width: ${pct}%"></div>
            </div>
            <div style="display: flex; justify-content: space-between; font-size: 11px; color: var(--text-dim); margin-top: 4px;">
              <span>${pct}% (${dl} / ${tot})</span>
              <span>${spd}</span>
            </div>
            <div style="display: flex; justify-content: flex-end; gap: 6px; margin-top: 8px;">
              <button class="btn-action" onclick="remoteAction('cancel', '${t.id}')">Cancel</button>
            </div>
          </div>
        `;}).join('');
      } catch (e) {
        document.getElementById('statusBadge').textContent = 'Disconnected';
        document.getElementById('statusBadge').style.background = '#da3633';
      }
    }

    async function remoteAction(action, id) {
      await fetch('/api/remote/action', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action, id })
      });
      updateDashboard();
    }

    document.getElementById('btnAdd').addEventListener('click', async () => {
      const input = document.getElementById('addUrl');
      const url = input.value.trim();
      if (!url) return;
      await fetch('/api/remote/add', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ url })
      });
      input.value = '';
      updateDashboard();
    });

    setInterval(updateDashboard, 1500);
    updateDashboard();
  </script>
</body>
</html>)html";
}

} // namespace idm
