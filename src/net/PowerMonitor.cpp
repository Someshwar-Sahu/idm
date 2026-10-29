#include "PowerMonitor.h"
#include <chrono>
#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

static uint64_t file_time_to_uint64(const FILETIME &ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#endif

PowerMonitor::PowerMonitor()
    : PowerMonitor(1000) {
}

PowerMonitor::PowerMonitor(uint32_t check_interval_ms)
    : m_check_interval_ms(check_interval_ms),
      m_critical_battery_threshold(15),
      m_high_cpu_threshold(85.0),
      m_power_source(PowerSource::Unknown),
      m_battery_percent(-1),
      m_is_charging(false),
      m_is_critical(false),
      m_cpu_load_percent(0.0),
      m_policy(BatteryPolicy::AggressiveEco),
      m_running(false) {
}

PowerMonitor::~PowerMonitor() {
    stop();
}

bool PowerMonitor::start() {
    if (m_running.load()) {
        return false;
    }
    m_running.store(true);
    m_worker_thread = std::thread(&PowerMonitor::monitor_loop, this);
    return true;
}

void PowerMonitor::stop() {
    if (m_running.load()) {
        m_running.store(false);
        if (m_worker_thread.joinable()) {
            m_worker_thread.join();
        }
    }
}

bool PowerMonitor::is_running() const {
    return m_running.load();
}

PowerSource PowerMonitor::get_power_source() const {
    return m_power_source.load();
}

int PowerMonitor::get_battery_percent() const {
    return m_battery_percent.load();
}

bool PowerMonitor::is_charging() const {
    return m_is_charging.load();
}

bool PowerMonitor::is_on_battery() const {
    return m_power_source.load() == PowerSource::Battery;
}

double PowerMonitor::get_cpu_load_percent() const {
    return m_cpu_load_percent.load();
}

PowerStatus PowerMonitor::get_current_status() const {
    PowerStatus status;
    status.power_source = m_power_source.load();
    status.battery_percent = m_battery_percent.load();
    status.is_charging = m_is_charging.load();
    status.is_critical = m_is_critical.load();
    status.cpu_load_percent = m_cpu_load_percent.load();
    return status;
}

void PowerMonitor::set_battery_policy(BatteryPolicy policy) {
    m_policy.store(policy);
}

BatteryPolicy PowerMonitor::get_battery_policy() const {
    return m_policy.load();
}

void PowerMonitor::set_critical_battery_threshold(int percent) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_critical_battery_threshold = percent;
}

int PowerMonitor::get_critical_battery_threshold() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_critical_battery_threshold;
}

void PowerMonitor::set_high_cpu_threshold(double percent) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_high_cpu_threshold = percent;
}

double PowerMonitor::get_high_cpu_threshold() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_high_cpu_threshold;
}

void PowerMonitor::set_check_interval(uint32_t interval_ms) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_check_interval_ms = interval_ms;
}

uint32_t PowerMonitor::get_check_interval() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_check_interval_ms;
}

uint32_t PowerMonitor::get_recommended_worker_threads(uint32_t base_threads) const {
    if (base_threads == 0) return 0;

    // 1. Critical Battery Protection (e.g. <= 15%): emergency clamp to 2 threads
    if (m_is_critical.load() && m_power_source.load() == PowerSource::Battery) {
        return std::min(base_threads, 2u);
    }

    // 2. High CPU / Thermal Throttling Guard (e.g. >= 85%): limit to 4 threads
    double high_cpu_limit = 85.0;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        high_cpu_limit = m_high_cpu_threshold;
    }
    if (m_cpu_load_percent.load() >= high_cpu_limit) {
        return std::min(base_threads, 4u);
    }

    // 3. Battery Policy Enforcement
    if (m_power_source.load() == PowerSource::Battery) {
        BatteryPolicy policy = m_policy.load();
        switch (policy) {
            case BatteryPolicy::AggressiveEco:
                return std::min(base_threads, 4u); // Scale down 32 -> 4
            case BatteryPolicy::Moderate:
                return std::min(base_threads, 8u); // Scale down 32 -> 8
            case BatteryPolicy::Disabled:
                return base_threads;               // Full speed override
        }
    }

    // AC power or unthrottled desktop
    return base_threads;
}

void PowerMonitor::evaluate_power_sample(const PowerStatus &sample) {
    PowerSource prev_source = m_power_source.load();
    bool prev_critical = m_is_critical.load();

    int crit_thresh = 15;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        crit_thresh = m_critical_battery_threshold;
    }

    bool is_crit = sample.is_critical ||
                   (sample.battery_percent >= 0 && sample.battery_percent <= crit_thresh);

    m_power_source.store(sample.power_source);
    m_battery_percent.store(sample.battery_percent);
    m_is_charging.store(sample.is_charging);
    m_is_critical.store(is_crit);
    m_cpu_load_percent.store(sample.cpu_load_percent);

    uint32_t recommended = get_recommended_worker_threads(32);

    PowerStateCallback cb = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        cb = m_callback;
    }

    if (cb && (prev_source != sample.power_source || prev_critical != is_crit)) {
        cb(sample, recommended);
    }
}

void PowerMonitor::set_on_power_state_changed_callback(PowerStateCallback callback) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_callback = callback;
}

void PowerMonitor::set_mock_power_provider(SystemPowerProvider provider) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_mock_provider = provider;
}

double PowerMonitor::query_native_cpu_load() {
#ifdef _WIN32
    FILETIME idleTime, kernelTime, userTime;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        uint64_t idle = file_time_to_uint64(idleTime);
        uint64_t kernel = file_time_to_uint64(kernelTime);
        uint64_t user = file_time_to_uint64(userTime);
        uint64_t total = kernel + user;

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_prev_total_time > 0 && total > m_prev_total_time) {
            uint64_t total_delta = total - m_prev_total_time;
            uint64_t idle_delta = (idle > m_prev_idle_time) ? (idle - m_prev_idle_time) : 0;
            if (idle_delta > total_delta) idle_delta = total_delta;

            double load = 100.0 * (1.0 - static_cast<double>(idle_delta) / static_cast<double>(total_delta));
            m_prev_idle_time = idle;
            m_prev_total_time = total;
            if (load < 0.0) load = 0.0;
            if (load > 100.0) load = 100.0;
            return load;
        }
        m_prev_idle_time = idle;
        m_prev_total_time = total;
    }
#elif defined(__APPLE__)
    host_cpu_load_info_data_t cpu_info;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&cpu_info, &count) == KERN_SUCCESS) {
        uint64_t user = cpu_info.cpu_ticks[CPU_STATE_USER];
        uint64_t system = cpu_info.cpu_ticks[CPU_STATE_SYSTEM];
        uint64_t idle = cpu_info.cpu_ticks[CPU_STATE_IDLE];
        uint64_t total = user + system + idle;

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_prev_total_time > 0 && total > m_prev_total_time) {
            uint64_t total_delta = total - m_prev_total_time;
            uint64_t idle_delta = (idle > m_prev_idle_time) ? (idle - m_prev_idle_time) : 0;
            double load = 100.0 * (1.0 - static_cast<double>(idle_delta) / static_cast<double>(total_delta));
            m_prev_idle_time = idle;
            m_prev_total_time = total;
            return load;
        }
        m_prev_idle_time = idle;
        m_prev_total_time = total;
    }
#endif
    return 0.0;
}

PowerStatus PowerMonitor::query_native_power_status() {
    PowerStatus status;
    status.cpu_load_percent = query_native_cpu_load();

#ifdef _WIN32
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
        if (sps.ACLineStatus == 1) {
            status.power_source = PowerSource::AC;
        } else if (sps.ACLineStatus == 0) {
            status.power_source = PowerSource::Battery;
        } else {
            status.power_source = PowerSource::Unknown;
        }

        if (sps.BatteryLifePercent != 255) {
            status.battery_percent = static_cast<int>(sps.BatteryLifePercent);
        } else {
            status.battery_percent = -1;
        }

        status.is_charging = (sps.BatteryFlag & 8) != 0;

        int crit_thresh = 15;
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            crit_thresh = m_critical_battery_threshold;
        }
        status.is_critical = (status.battery_percent >= 0 && status.battery_percent <= crit_thresh);
        return status;
    }
#elif defined(__APPLE__)
    CFTypeRef power_info = IOPSCopyPowerSourcesInfo();
    if (power_info) {
        CFArrayRef power_sources = IOPSCopyPowerSourcesList(power_info);
        if (power_sources) {
            CFIndex count = CFArrayGetCount(power_sources);
            for (CFIndex i = 0; i < count; ++i) {
                CFDictionaryRef p_source = IOPSGetPowerSourceDescription(power_info, CFArrayGetValueAtIndex(power_sources, i));
                if (p_source) {
                    CFStringRef state = (CFStringRef)CFDictionaryGetValue(p_source, CFSTR(kIOPSPowerSourceStateKey));
                    if (state) {
                        if (CFStringCompare(state, CFSTR(kIOPSACPowerValue), 0) == kCFCompareEqualTo) {
                            status.power_source = PowerSource::AC;
                        } else if (CFStringCompare(state, CFSTR(kIOPSBatteryPowerValue), 0) == kCFCompareEqualTo) {
                            status.power_source = PowerSource::Battery;
                        }
                    }
                    CFNumberRef capacity = (CFNumberRef)CFDictionaryGetValue(p_source, CFSTR(kIOPSCurrentCapacityKey));
                    if (capacity) {
                        int val = 0;
                        CFNumberGetValue(capacity, kCFNumberIntType, &val);
                        status.battery_percent = val;
                    }
                    CFBooleanRef charging = (CFBooleanRef)CFDictionaryGetValue(p_source, CFSTR(kIOPSIsChargingKey));
                    if (charging) {
                        status.is_charging = CFBooleanGetValue(charging);
                    }
                    break;
                }
            }
            CFRelease(power_sources);
        }
        CFRelease(power_info);
    }
#endif

    return status;
}

void PowerMonitor::monitor_loop() {
    while (m_running.load()) {
        PowerStatus status;
        SystemPowerProvider provider = nullptr;
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            provider = m_mock_provider;
        }

        if (provider) {
            status = provider();
        } else {
            status = query_native_power_status();
        }

        evaluate_power_sample(status);

        uint32_t interval = 1000;
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            interval = m_check_interval_ms;
        }

        uint32_t elapsed = 0;
        while (m_running.load() && elapsed < interval) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            elapsed += 20;
        }
    }
}
