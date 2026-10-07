#include "PowerMonitor.h"
#include <iostream>
#include <cassert>
#include <chrono>
#include <thread>

using namespace std;

int main() {
    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: PowerMonitor (Task 16)         " << endl;
    cout << "==========================================================" << endl;

    PowerMonitor monitor;

    // 1. Initial State & Native Hardware Query
    cout << "\n[1/7] Testing Construction & Native OS Query..." << endl;
    PowerStatus native_status = monitor.query_native_power_status();
    cout << "  -> Native Power Source: " 
         << (native_status.power_source == PowerSource::AC ? "AC Wall Power" : 
             (native_status.power_source == PowerSource::Battery ? "Battery Power" : "Desktop / Unknown"))
         << endl;
    if (native_status.battery_percent >= 0) {
        cout << "  -> Battery Remaining: " << native_status.battery_percent << "%" << endl;
    }
    assert(monitor.is_running() == false);
    cout << "  -> Native query executed safely without crash: PASSED" << endl;

    // 2. AC Connected Behavior (Unthrottled 32 Threads)
    cout << "\n[2/7] Testing AC Plugged-In Mode (Uncapped Performance)..." << endl;
    PowerStatus ac_sample;
    ac_sample.power_source = PowerSource::AC;
    ac_sample.battery_percent = 100;
    ac_sample.is_charging = true;
    ac_sample.cpu_load_percent = 15.0;

    monitor.evaluate_power_sample(ac_sample);
    assert(monitor.get_power_source() == PowerSource::AC);
    assert(monitor.is_on_battery() == false);
    assert(monitor.get_recommended_worker_threads(32) == 32);
    cout << "  -> AC mode preserves full 32 worker threads: PASSED" << endl;

    // 3. Battery Eco Mode (AggressiveEco: 32 -> 4 Threads)
    cout << "\n[3/7] Testing Battery Mode (AggressiveEco -> 4 Threads)..." << endl;
    PowerStatus batt_sample;
    batt_sample.power_source = PowerSource::Battery;
    batt_sample.battery_percent = 75;
    batt_sample.is_charging = false;
    batt_sample.cpu_load_percent = 20.0;

    monitor.set_battery_policy(BatteryPolicy::AggressiveEco);
    monitor.evaluate_power_sample(batt_sample);
    assert(monitor.is_on_battery() == true);
    assert(monitor.get_recommended_worker_threads(32) == 4);
    assert(monitor.get_recommended_worker_threads(2) == 2); // Does not artificially increase lower thread limits
    cout << "  -> AggressiveEco throttled workers from 32 to 4: PASSED" << endl;

    // 4. Policy Toggling (Moderate vs Disabled)
    cout << "\n[4/7] Testing Battery Policy Toggling (Moderate vs Disabled)..." << endl;
    monitor.set_battery_policy(BatteryPolicy::Moderate);
    assert(monitor.get_recommended_worker_threads(32) == 8);
    cout << "  -> Moderate policy throttled workers to 8: PASSED" << endl;

    monitor.set_battery_policy(BatteryPolicy::Disabled);
    assert(monitor.get_recommended_worker_threads(32) == 32);
    cout << "  -> Disabled policy granted full 32 threads override: PASSED" << endl;

    // 5. Critical Battery Protection (<= 15% -> Emergency 2 Threads)
    cout << "\n[5/7] Testing Critical Battery Guard (<= 15% -> 2 Threads)..." << endl;
    PowerStatus crit_sample;
    crit_sample.power_source = PowerSource::Battery;
    crit_sample.battery_percent = 10; // Under 15%
    crit_sample.is_charging = false;
    crit_sample.cpu_load_percent = 10.0;

    monitor.set_battery_policy(BatteryPolicy::Moderate); // Even on moderate
    monitor.evaluate_power_sample(crit_sample);
    assert(monitor.get_current_status().is_critical == true);
    assert(monitor.get_recommended_worker_threads(32) == 2);
    cout << "  -> Critical battery clamped workers to 2 threads: PASSED" << endl;

    // 6. High CPU Thermal Throttling Guard (> 85% CPU -> 4 Threads)
    cout << "\n[6/7] Testing High CPU Thermal Throttler (> 85% Load)..." << endl;
    PowerStatus high_cpu_sample;
    high_cpu_sample.power_source = PowerSource::AC; // Even on AC power!
    high_cpu_sample.battery_percent = 100;
    high_cpu_sample.cpu_load_percent = 92.5; // High thermal load

    monitor.evaluate_power_sample(high_cpu_sample);
    assert(monitor.get_recommended_worker_threads(32) == 4);
    cout << "  -> High CPU load (92.5%) triggered thermal throttle (4 threads): PASSED" << endl;

    // Recovering from thermal load
    high_cpu_sample.cpu_load_percent = 30.0;
    monitor.evaluate_power_sample(high_cpu_sample);
    assert(monitor.get_recommended_worker_threads(32) == 32);
    cout << "  -> Thermal recovery restored 32 threads automatically: PASSED" << endl;

    // 7. Power State Change Callback & Background Thread Lifecycle
    cout << "\n[7/7] Testing Callback & Background Polling Lifecycle..." << endl;
    bool callback_invoked = false;
    PowerSource cb_power = PowerSource::Unknown;
    uint32_t cb_threads = 0;

    monitor.set_on_power_state_changed_callback([&](const PowerStatus &s, uint32_t rec_threads) {
        callback_invoked = true;
        cb_power = s.power_source;
        cb_threads = rec_threads;
    });

    // Provide mock provider for background loop
    int cycle_count = 0;
    monitor.set_mock_power_provider([&]() {
        cycle_count++;
        PowerStatus ps;
        ps.power_source = (cycle_count % 2 == 1) ? PowerSource::AC : PowerSource::Battery;
        ps.battery_percent = 80;
        ps.cpu_load_percent = 25.0;
        return ps;
    });

    monitor.set_check_interval(30);
    assert(monitor.start() == true);
    assert(monitor.is_running() == true);

    this_thread::sleep_for(chrono::milliseconds(120));
    monitor.stop();
    assert(monitor.is_running() == false);
    assert(callback_invoked == true);
    assert(cycle_count >= 2);
    cout << "  -> Background polling and power transition callbacks verified: PASSED" << endl;

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF POWERMONITOR (TASK 16) VERIFIED!       " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
