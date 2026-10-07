#ifndef NETWORK_MONITOR_H
#define NETWORK_MONITOR_H

#include <string>
#include <vector>
#include <unordered_set>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <cstdint>

enum class NetworkQoSState {
    Normal,        // Latency is within normal baseline range
    Elevated,      // Latency moderately higher than normal
    Critical,      // Severe bufferbloat detected, throttle required
    GamingActive   // Active game or voice client detected, extreme ping protection active
};

struct NetworkStats {
    uint32_t current_rtt_ms{0};
    uint32_t baseline_rtt_ms{0};
    uint32_t spike_threshold_ms{50};
    double suggested_throttle_ratio{1.0}; // 1.0 = 100% (uncapped), 0.1 = 10% (90% throttle)
    NetworkQoSState qos_state{NetworkQoSState::Normal};
    bool gaming_active{false};
    std::string active_game_name;
};

using LatencySpikeCallback = std::function<void(uint32_t current_rtt, uint32_t baseline_rtt, double throttle_ratio)>;
using GamingStateCallback = std::function<void(bool game_active, const std::string &game_name, double throttle_ratio)>;
using RttProbeFunction = std::function<uint32_t()>;
using ProcessListProvider = std::function<std::vector<std::string>()>;

class NetworkMonitor {
public:
    NetworkMonitor();
    explicit NetworkMonitor(const std::string &probe_target_url, uint32_t check_interval_ms = 1000);
    ~NetworkMonitor();

    // Lifecycle
    bool start();
    void stop();
    bool is_running() const;

    // Measurement & Query (Task 13)
    uint32_t measure_live_rtt();
    NetworkStats get_stats() const;
    uint32_t get_current_rtt() const;
    uint32_t get_baseline_rtt() const;
    double get_suggested_throttle_ratio() const;
    bool is_bufferbloat_detected() const;

    // Configuration (Task 13)
    void set_baseline_rtt(uint32_t baseline_ms);
    void set_latency_spike_threshold(uint32_t threshold_ms);
    uint32_t get_latency_spike_threshold() const;
    void set_check_interval(uint32_t interval_ms);
    void set_probe_target(const std::string &url);

    // Callbacks & Mocking (Task 13)
    void set_on_latency_spike_callback(LatencySpikeCallback callback);
    void set_mock_rtt_provider(RttProbeFunction mock_provider);

    // QoS Evaluation (Task 13)
    void evaluate_qos_sample(uint32_t rtt_ms);

    // --- Task 14: Zero-Lag Gaming QoS Mode ---
    void set_gaming_mode_enabled(bool enabled);
    bool is_gaming_mode_enabled() const;
    bool is_game_active() const;
    std::string get_active_game_detected() const;
    void set_gaming_throttle_ratio(double ratio);
    double get_gaming_throttle_ratio() const;

    void add_monitored_game(const std::string &process_name);
    void remove_monitored_game(const std::string &process_name);
    std::vector<std::string> get_monitored_games() const;
    void reset_default_monitored_games();

    std::string scan_for_active_games();
    void set_on_gaming_state_changed_callback(GamingStateCallback callback);
    void set_mock_process_provider(ProcessListProvider provider);

private:
    void monitor_loop();
    std::vector<std::string> get_running_process_names();

    std::string m_probe_target{"http://www.google.com/generate_204"};
    uint32_t m_check_interval_ms{1000};
    uint32_t m_spike_threshold_ms{50};

    std::atomic<uint32_t> m_current_rtt_ms{0};
    std::atomic<uint32_t> m_baseline_rtt_ms{0};
    std::atomic<double> m_suggested_throttle_ratio{1.0};
    std::atomic<NetworkQoSState> m_qos_state{NetworkQoSState::Normal};
    std::atomic<bool> m_bufferbloat_detected{false};

    // Task 14 State
    std::atomic<bool> m_gaming_mode_enabled{true};
    std::atomic<bool> m_gaming_active{false};
    std::atomic<double> m_gaming_throttle_ratio{0.10}; // 90% throttle during gaming
    std::string m_active_game_name;
    std::unordered_set<std::string> m_monitored_games;

    std::atomic<bool> m_running{false};
    std::thread m_worker_thread;
    mutable std::mutex m_mutex;

    LatencySpikeCallback m_spike_callback{nullptr};
    GamingStateCallback m_gaming_callback{nullptr};
    RttProbeFunction m_mock_provider{nullptr};
    ProcessListProvider m_mock_process_provider{nullptr};
};

#endif
