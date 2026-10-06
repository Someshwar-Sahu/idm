#include "NetworkMonitor.h"
#include <curl/curl.h>
#include <chrono>
#include <algorithm>
#include <iostream>

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
      m_running(false) {
}

NetworkMonitor::~NetworkMonitor() {
    stop();
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

void NetworkMonitor::evaluate_qos_sample(uint32_t rtt_ms) {
    if (rtt_ms == 0) {
        return; // Failed probe sample
    }

    m_current_rtt_ms.store(rtt_ms);

    // Initialize or update baseline RTT using Exponential Moving Average
    uint32_t current_baseline = m_baseline_rtt_ms.load();
    if (current_baseline == 0) {
        m_baseline_rtt_ms.store(rtt_ms);
        current_baseline = rtt_ms;
    } else if (rtt_ms < current_baseline) {
        // Fast adaptation when network latency improves
        m_baseline_rtt_ms.store(rtt_ms);
        current_baseline = rtt_ms;
    } else {
        // Slow smoothing drift (EMA alpha = 0.05)
        uint32_t smoothed = static_cast<uint32_t>(current_baseline * 0.95 + rtt_ms * 0.05);
        m_baseline_rtt_ms.store(smoothed);
        current_baseline = smoothed;
    }

    uint32_t threshold = m_spike_threshold_ms;
    uint32_t diff = (rtt_ms > current_baseline) ? (rtt_ms - current_baseline) : 0;

    LatencySpikeCallback cb_to_call = nullptr;
    double throttle_to_notify = 1.0;

    if (diff >= threshold * 2) {
        // Critical bufferbloat: Latency spiked > 2x threshold (e.g. +100ms) -> Throttle by 30%
        m_qos_state.store(NetworkQoSState::Critical);
        m_bufferbloat_detected.store(true);
        m_suggested_throttle_ratio.store(0.70);
        throttle_to_notify = 0.70;

        std::lock_guard<std::mutex> lock(m_mutex);
        cb_to_call = m_spike_callback;
    } else if (diff >= threshold) {
        // Moderate spike: Latency spiked > threshold (e.g. +50ms) -> Throttle by 20%
        m_qos_state.store(NetworkQoSState::Elevated);
        m_bufferbloat_detected.store(true);
        m_suggested_throttle_ratio.store(0.80);
        throttle_to_notify = 0.80;

        std::lock_guard<std::mutex> lock(m_mutex);
        cb_to_call = m_spike_callback;
    } else {
        // Normal network conditions: Full speed
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
