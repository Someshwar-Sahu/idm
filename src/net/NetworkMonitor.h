#ifndef NETWORK_MONITOR_H
#define NETWORK_MONITOR_H

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <cstdint>

enum class NetworkQoSState {
    Normal,        // Latency is within normal baseline range
    Elevated,      // Latency moderately higher than normal
    Critical       // Severe bufferbloat detected, throttle required
};

struct NetworkStats {
    uint32_t current_rtt_ms{0};
    uint32_t baseline_rtt_ms{0};
    uint32_t spike_threshold_ms{50};
    double suggested_throttle_ratio{1.0}; // 1.0 = 100% (uncapped), 0.7 = 70% (30% reduction)
    NetworkQoSState qos_state{NetworkQoSState::Normal};
};

using LatencySpikeCallback = std::function<void(uint32_t current_rtt, uint32_t baseline_rtt, double throttle_ratio)>;
using RttProbeFunction = std::function<uint32_t()>;

class NetworkMonitor {
public:
    NetworkMonitor();
    explicit NetworkMonitor(const std::string &probe_target_url, uint32_t check_interval_ms = 1000);
    ~NetworkMonitor();

    // Lifecycle
    bool start();
    void stop();
    bool is_running() const;

    // Measurement & Query
    uint32_t measure_live_rtt();
    NetworkStats get_stats() const;
    uint32_t get_current_rtt() const;
    uint32_t get_baseline_rtt() const;
    double get_suggested_throttle_ratio() const;
    bool is_bufferbloat_detected() const;

    // Configuration
    void set_baseline_rtt(uint32_t baseline_ms);
    void set_latency_spike_threshold(uint32_t threshold_ms);
    uint32_t get_latency_spike_threshold() const;
    void set_check_interval(uint32_t interval_ms);
    void set_probe_target(const std::string &url);

    // Callbacks & Mocking
    void set_on_latency_spike_callback(LatencySpikeCallback callback);
    void set_mock_rtt_provider(RttProbeFunction mock_provider);

    // QoS Evaluation (Exposed for deterministic testing)
    void evaluate_qos_sample(uint32_t rtt_ms);

private:
    void monitor_loop();

    std::string m_probe_target{"http://www.google.com/generate_204"};
    uint32_t m_check_interval_ms{1000};
    uint32_t m_spike_threshold_ms{50};

    std::atomic<uint32_t> m_current_rtt_ms{0};
    std::atomic<uint32_t> m_baseline_rtt_ms{0};
    std::atomic<double> m_suggested_throttle_ratio{1.0};
    std::atomic<NetworkQoSState> m_qos_state{NetworkQoSState::Normal};
    std::atomic<bool> m_bufferbloat_detected{false};

    std::atomic<bool> m_running{false};
    std::thread m_worker_thread;
    mutable std::mutex m_mutex;

    LatencySpikeCallback m_spike_callback{nullptr};
    RttProbeFunction m_mock_provider{nullptr};
};

#endif
