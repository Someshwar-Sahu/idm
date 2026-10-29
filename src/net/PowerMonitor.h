#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

#include <cstdint>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>

enum class PowerSource {
    AC,         // Plugged into wall AC power
    Battery,    // Discharging on battery
    Unknown     // Desktop PC or undetected
};

enum class BatteryPolicy {
    AggressiveEco, // Limit worker threads to 4 when on battery
    Moderate,      // Limit worker threads to 8 when on battery
    Disabled       // Full speed (no battery throttling)
};

struct PowerStatus {
    PowerSource power_source{PowerSource::Unknown};
    int battery_percent{-1};              // 0 - 100, or -1 if unknown / desktop
    bool is_charging{false};
    bool is_critical{false};              // Battery <= critical threshold
    double cpu_load_percent{0.0};         // 0.0 - 100.0%
};

using PowerStateCallback = std::function<void(const PowerStatus &status, uint32_t recommended_threads)>;
using SystemPowerProvider = std::function<PowerStatus()>;

class PowerMonitor {
public:
    PowerMonitor();
    explicit PowerMonitor(uint32_t check_interval_ms);
    ~PowerMonitor();

    // Lifecycle
    bool start();
    void stop();
    bool is_running() const;

    // Power & Battery Queries
    PowerSource get_power_source() const;
    int get_battery_percent() const;
    bool is_charging() const;
    bool is_on_battery() const;
    double get_cpu_load_percent() const;
    PowerStatus get_current_status() const;

    // Policies & Thresholds
    void set_battery_policy(BatteryPolicy policy);
    BatteryPolicy get_battery_policy() const;

    void set_critical_battery_threshold(int percent); // Default: 15%
    int get_critical_battery_threshold() const;

    void set_high_cpu_threshold(double percent);      // Default: 85.0%
    double get_high_cpu_threshold() const;

    void set_check_interval(uint32_t interval_ms);
    uint32_t get_check_interval() const;

    // Concurrency Calculation (Task 16 Core Engine)
    uint32_t get_recommended_worker_threads(uint32_t base_threads = 32) const;

    // Manual Evaluation (Deterministic testing)
    void evaluate_power_sample(const PowerStatus &sample);

    // Callbacks & Mocks
    void set_on_power_state_changed_callback(PowerStateCallback callback);
    void set_mock_power_provider(SystemPowerProvider provider);

    // Native Platform Probing
    PowerStatus query_native_power_status();
    double query_native_cpu_load();

private:
    void monitor_loop();

    uint32_t m_check_interval_ms{1000};
    int m_critical_battery_threshold{15};
    double m_high_cpu_threshold{85.0};

    std::atomic<PowerSource> m_power_source{PowerSource::Unknown};
    std::atomic<int> m_battery_percent{-1};
    std::atomic<bool> m_is_charging{false};
    std::atomic<bool> m_is_critical{false};
    std::atomic<double> m_cpu_load_percent{0.0};
    std::atomic<BatteryPolicy> m_policy{BatteryPolicy::AggressiveEco};

    std::atomic<bool> m_running{false};
    std::thread m_worker_thread;
    mutable std::recursive_mutex m_mutex;

    PowerStateCallback m_callback{nullptr};
    SystemPowerProvider m_mock_provider{nullptr};

    // Platform-specific CPU load state
    uint64_t m_prev_idle_time{0};
    uint64_t m_prev_total_time{0};
};

#endif // POWER_MONITOR_H
