#include "NetworkMonitor.h"
#include <curl/curl.h>
#include <chrono>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#elif defined(__APPLE__)
#include <libproc.h>
#include <sys/proc_info.h>
#endif

static std::string to_lower_copy(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

NetworkMonitor::NetworkMonitor()
    : NetworkMonitor("http://www.google.com/generate_204", 1000) {
}

NetworkMonitor::NetworkMonitor(const std::string &probe_target_url, uint32_t check_interval_ms)
    : m_probe_target(probe_target_url),
      m_check_interval_ms(check_interval_ms),
      m_spike_threshold_ms(50),
      m_current_rtt_ms(0),
      m_baseline_rtt_ms(0),
      m_suggested_throttle_ratio(1.0),
      m_qos_state(NetworkQoSState::Normal),
      m_bufferbloat_detected(false),
      m_gaming_mode_enabled(true),
      m_gaming_active(false),
      m_gaming_throttle_ratio(0.10),
      m_active_game_name(""),
      m_running(false) {
    reset_default_monitored_games();
}

NetworkMonitor::~NetworkMonitor() {
    stop();
}

void NetworkMonitor::reset_default_monitored_games() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_monitored_games = {
        "steam.exe", "steam",
        "epicgameslauncher.exe", "epicgameslauncher",
        "riotclientservices.exe", "riotclientservices",
        "valorant.exe", "valorant",
        "cs2.exe", "cs2", "csgo.exe",
        "leagueclient.exe", "leagueclient", "league of legends",
        "dota2.exe", "dota2",
        "fortniteclient-win64-shipping.exe", "fortnite",
        "overwatch.exe", "overwatch",
        "discord.exe", "discord",
        "teams.exe", "teams"
    };
}

bool NetworkMonitor::start() {
    if (m_running.load()) {
        return true;
    }
    m_running.store(true);
    m_worker_thread = std::thread(&NetworkMonitor::monitor_loop, this);
    return true;
}

void NetworkMonitor::stop() {
    if (!m_running.load()) {
        return;
    }
    m_running.store(false);
    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }
}

bool NetworkMonitor::is_running() const {
    return m_running.load();
}

uint32_t NetworkMonitor::measure_live_rtt() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_mock_provider) {
            return m_mock_provider();
        }
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        return 0;
    }

    curl_easy_setopt(curl, CURLOPT_URL, m_probe_target.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // HTTP HEAD
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2000L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    auto t_start = std::chrono::steady_clock::now();
    CURLcode res = curl_easy_perform(curl);
    auto t_end = std::chrono::steady_clock::now();

    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return 0;
    }

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
    return static_cast<uint32_t>(duration);
}

std::vector<std::string> NetworkMonitor::get_running_process_names() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_mock_process_provider) {
            return m_mock_process_provider();
        }
    }

    std::vector<std::string> names;

#ifdef _WIN32
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(hSnapshot, &pe)) {
            do {
                names.push_back(pe.szExeFile);
            } while (Process32Next(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);
    }
#elif defined(__APPLE__)
    pid_t pids[2048];
    int bytes = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
    if (bytes > 0) {
        int count = bytes / sizeof(pid_t);
        for (int i = 0; i < count; ++i) {
            if (pids[i] == 0) continue;
            char name[256];
            if (proc_name(pids[i], name, sizeof(name)) > 0) {
                names.push_back(name);
            }
        }
    }
#endif

    return names;
}

std::string NetworkMonitor::scan_for_active_games() {
    if (!m_gaming_mode_enabled.load()) {
        return "";
    }

    std::vector<std::string> current_processes = get_running_process_names();
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto &proc : current_processes) {
        std::string lower_proc = to_lower_copy(proc);
        if (m_monitored_games.find(lower_proc) != m_monitored_games.end()) {
            return proc;
        }
    }
    return "";
}

void NetworkMonitor::evaluate_qos_sample(uint32_t rtt_ms) {
    if (rtt_ms == 0) {
        return;
    }

    m_current_rtt_ms.store(rtt_ms);

    uint32_t current_baseline = m_baseline_rtt_ms.load();
    if (current_baseline == 0) {
        m_baseline_rtt_ms.store(rtt_ms);
        current_baseline = rtt_ms;
    } else if (rtt_ms < current_baseline) {
        m_baseline_rtt_ms.store(rtt_ms);
        current_baseline = rtt_ms;
    } else {
        uint32_t smoothed = static_cast<uint32_t>(current_baseline * 0.95 + rtt_ms * 0.05);
        m_baseline_rtt_ms.store(smoothed);
        current_baseline = smoothed;
    }

    // If gaming mode is active, gaming throttle takes precedence
    if (m_gaming_active.load()) {
        m_qos_state.store(NetworkQoSState::GamingActive);
        m_suggested_throttle_ratio.store(m_gaming_throttle_ratio.load());
        return;
    }

    uint32_t threshold = m_spike_threshold_ms;
    uint32_t diff = (rtt_ms > current_baseline) ? (rtt_ms - current_baseline) : 0;

    LatencySpikeCallback cb_to_call = nullptr;
    double throttle_to_notify = 1.0;

    if (diff >= threshold * 2) {
        m_qos_state.store(NetworkQoSState::Critical);
        m_bufferbloat_detected.store(true);
        m_suggested_throttle_ratio.store(0.70);
        throttle_to_notify = 0.70;

        std::lock_guard<std::mutex> lock(m_mutex);
        cb_to_call = m_spike_callback;
    } else if (diff >= threshold) {
        m_qos_state.store(NetworkQoSState::Elevated);
        m_bufferbloat_detected.store(true);
        m_suggested_throttle_ratio.store(0.80);
        throttle_to_notify = 0.80;

        std::lock_guard<std::mutex> lock(m_mutex);
        cb_to_call = m_spike_callback;
    } else {
        m_qos_state.store(NetworkQoSState::Normal);
        m_bufferbloat_detected.store(false);
        m_suggested_throttle_ratio.store(1.0);
    }

    if (cb_to_call) {
        cb_to_call(rtt_ms, current_baseline, throttle_to_notify);
    }
}

void NetworkMonitor::monitor_loop() {
    while (m_running.load()) {
        // 1. Task 14: Gaming process scan
        std::string active_game = scan_for_active_games();
        bool was_gaming = m_gaming_active.load();
        bool is_gaming = !active_game.empty();

        if (is_gaming != was_gaming) {
            m_gaming_active.store(is_gaming);
            GamingStateCallback cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_active_game_name = active_game;
                cb = m_gaming_callback;
            }

            if (is_gaming) {
                m_qos_state.store(NetworkQoSState::GamingActive);
                m_suggested_throttle_ratio.store(m_gaming_throttle_ratio.load());
            } else {
                m_qos_state.store(NetworkQoSState::Normal);
                m_suggested_throttle_ratio.store(1.0);
            }

            if (cb) {
                cb(is_gaming, active_game, m_suggested_throttle_ratio.load());
            }
        }

        // 2. Task 13: RTT Latency probe
        uint32_t sample = measure_live_rtt();
        evaluate_qos_sample(sample);

        uint32_t interval = m_check_interval_ms;
        uint32_t elapsed = 0;
        while (elapsed < interval && m_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            elapsed += 50;
        }
    }
}

NetworkStats NetworkMonitor::get_stats() const {
    NetworkStats stats;
    stats.current_rtt_ms = m_current_rtt_ms.load();
    stats.baseline_rtt_ms = m_baseline_rtt_ms.load();
    stats.spike_threshold_ms = m_spike_threshold_ms;
    stats.suggested_throttle_ratio = m_suggested_throttle_ratio.load();
    stats.qos_state = m_qos_state.load();
    stats.gaming_active = m_gaming_active.load();

    std::lock_guard<std::mutex> lock(m_mutex);
    stats.active_game_name = m_active_game_name;
    return stats;
}

uint32_t NetworkMonitor::get_current_rtt() const {
    return m_current_rtt_ms.load();
}

uint32_t NetworkMonitor::get_baseline_rtt() const {
    return m_baseline_rtt_ms.load();
}

double NetworkMonitor::get_suggested_throttle_ratio() const {
    return m_suggested_throttle_ratio.load();
}

bool NetworkMonitor::is_bufferbloat_detected() const {
    return m_bufferbloat_detected.load();
}

void NetworkMonitor::set_baseline_rtt(uint32_t baseline_ms) {
    m_baseline_rtt_ms.store(baseline_ms);
}

void NetworkMonitor::set_latency_spike_threshold(uint32_t threshold_ms) {
    m_spike_threshold_ms = threshold_ms;
}

uint32_t NetworkMonitor::get_latency_spike_threshold() const {
    return m_spike_threshold_ms;
}

void NetworkMonitor::set_check_interval(uint32_t interval_ms) {
    m_check_interval_ms = interval_ms;
}

void NetworkMonitor::set_probe_target(const std::string &url) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_probe_target = url;
}

void NetworkMonitor::set_on_latency_spike_callback(LatencySpikeCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_spike_callback = callback;
}

void NetworkMonitor::set_mock_rtt_provider(RttProbeFunction mock_provider) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mock_provider = mock_provider;
}

// --- Task 14 Implementation ---
void NetworkMonitor::set_gaming_mode_enabled(bool enabled) {
    m_gaming_mode_enabled.store(enabled);
}

bool NetworkMonitor::is_gaming_mode_enabled() const {
    return m_gaming_mode_enabled.load();
}

bool NetworkMonitor::is_game_active() const {
    return m_gaming_active.load();
}

std::string NetworkMonitor::get_active_game_detected() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_active_game_name;
}

void NetworkMonitor::set_gaming_throttle_ratio(double ratio) {
    m_gaming_throttle_ratio.store(ratio);
}

double NetworkMonitor::get_gaming_throttle_ratio() const {
    return m_gaming_throttle_ratio.load();
}

void NetworkMonitor::add_monitored_game(const std::string &process_name) {
    if (process_name.empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_monitored_games.insert(to_lower_copy(process_name));
}

void NetworkMonitor::remove_monitored_game(const std::string &process_name) {
    if (process_name.empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_monitored_games.erase(to_lower_copy(process_name));
}

std::vector<std::string> NetworkMonitor::get_monitored_games() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::vector<std::string>(m_monitored_games.begin(), m_monitored_games.end());
}

void NetworkMonitor::set_on_gaming_state_changed_callback(GamingStateCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_gaming_callback = callback;
}

void NetworkMonitor::set_mock_process_provider(ProcessListProvider provider) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mock_process_provider = provider;
}
