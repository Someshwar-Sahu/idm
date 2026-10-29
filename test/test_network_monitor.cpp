#include "net/NetworkMonitor.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace std;

int main() {
    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: NetworkMonitor (Task 13)       " << endl;
    cout << "==========================================================" << endl;

    NetworkMonitor monitor("http://127.0.0.1:9", 100);

    // =========================================================================
    // 1. BASELINE CALCULATION & NORMAL LATENCY (Positive)
    // =========================================================================
    cout << "\n[1/5] Testing Baseline Latency Initialization..." << endl;

    monitor.set_latency_spike_threshold(50);
    assert(monitor.get_latency_spike_threshold() == 50);

    // Initial baseline sample (e.g. 25ms ping)
    monitor.evaluate_qos_sample(25);
    assert(monitor.get_current_rtt() == 25);
    assert(monitor.get_baseline_rtt() == 25);
    assert(monitor.is_bufferbloat_detected() == false);
    assert(monitor.get_suggested_throttle_ratio() == 1.0);
    cout << "  -> Baseline initialized to 25ms: PASSED" << endl;

    // Normal jitter (30ms - difference is only 5ms < 50ms)
    monitor.evaluate_qos_sample(30);
    assert(monitor.get_current_rtt() == 30);
    assert(monitor.is_bufferbloat_detected() == false);
    assert(monitor.get_suggested_throttle_ratio() == 1.0);
    cout << "  -> Normal jitter (30ms) stays uncapped: PASSED" << endl;

    // =========================================================================
    // 2. MODERATE SPIKE DETECTION (Elevated -> 20% Throttle)
    // =========================================================================
    cout << "\n[2/5] Testing Moderate Latency Spike (+55ms -> 20% Throttle)..." << endl;

    monitor.set_baseline_rtt(25);
    // 25 + 55 = 80ms (diff is 55ms >= 50ms threshold)
    monitor.evaluate_qos_sample(80);
    assert(monitor.get_current_rtt() == 80);
    assert(monitor.is_bufferbloat_detected() == true);
    assert(monitor.get_suggested_throttle_ratio() == 0.80); // 20% throttle
    cout << "  -> Moderate spike flagged: Suggested ratio = 0.80 (20% throttle): PASSED" << endl;

    // =========================================================================
    // 3. CRITICAL BUFFERBLOAT DETECTION (Critical -> 30% Throttle)
    // =========================================================================
    cout << "\n[3/5] Testing Critical Bufferbloat (+110ms -> 30% Throttle)..." << endl;

    monitor.set_baseline_rtt(25);
    // 25 + 110 = 135ms (diff is 110ms >= 100ms [2x threshold])
    monitor.evaluate_qos_sample(135);
    assert(monitor.get_current_rtt() == 135);
    assert(monitor.is_bufferbloat_detected() == true);
    assert(monitor.get_suggested_throttle_ratio() == 0.70); // 30% throttle
    cout << "  -> Critical bufferbloat flagged: Suggested ratio = 0.70 (30% throttle): PASSED" << endl;

    // =========================================================================
    // 4. SPIKE CALLBACK NOTIFICATION & RESTORATION
    // =========================================================================
    cout << "\n[4/5] Testing Spike Callback & Auto-Recovery..." << endl;

    uint32_t notified_rtt = 0;
    double notified_ratio = 0.0;
    monitor.set_on_latency_spike_callback([&](uint32_t cur, uint32_t base, double ratio) {
        notified_rtt = cur;
        notified_ratio = ratio;
    });

    monitor.set_baseline_rtt(25);
    monitor.evaluate_qos_sample(150);
    assert(notified_rtt == 150);
    assert(notified_ratio == 0.70);
    cout << "  -> Callback received spike notification: PASSED" << endl;

    // Recovery back to normal (26ms)
    monitor.evaluate_qos_sample(26);
    assert(monitor.is_bufferbloat_detected() == false);
    assert(monitor.get_suggested_throttle_ratio() == 1.0);
    cout << "  -> Recovery back to 1.0 (uncapped): PASSED" << endl;

    // Bug 43: Freeze baseline RTT updates while bufferbloat is actively detected
    monitor.set_baseline_rtt(25);
    for (int i = 0; i < 20; ++i) {
        monitor.evaluate_qos_sample(200); // 200ms heavy bufferbloat
    }
    assert(monitor.get_baseline_rtt() == 25);
    assert(monitor.is_bufferbloat_detected() == true);
    cout << "  -> Bug #43: Baseline RTT successfully frozen at 25ms under sustained bufferbloat: PASSED" << endl;

    // =========================================================================
    // 5. BACKGROUND THREAD LIFECYCLE & MOCK PROVIDER
    // =========================================================================
    cout << "\n[5/5] Testing Thread Lifecycle with Mock Provider..." << endl;

    atomic<int> probe_count{0};
    monitor.set_mock_rtt_provider([&]() -> uint32_t {
        probe_count++;
        return 22; // 22ms stable ping
    });
    monitor.set_mock_process_provider([]() { return vector<string>{"explorer.exe"}; });

    monitor.set_check_interval(50); // Fast 50ms loop for test
    assert(monitor.is_running() == false);
    monitor.start();
    assert(monitor.is_running() == true);

    this_thread::sleep_for(chrono::milliseconds(200));
    monitor.stop();
    assert(monitor.is_running() == false);
    assert(probe_count.load() >= 2);
    assert(monitor.get_current_rtt() == 22);
    assert(monitor.is_bufferbloat_detected() == false);
    cout << "  -> Background thread executed " << probe_count.load() << " mock probes cleanly: PASSED" << endl;

        // =========================================================================
    // 6. TASK 14: GAMING PROCESS DETECTION & ZERO-LAG QOS (Pos + Neg)
    // =========================================================================
    cout << "\n[6/7] Testing Zero-Lag Gaming Process Detection (Task 14)..." << endl;

    // Normal non-gaming processes
    vector<string> regular_apps = {"explorer.exe", "notepad.exe", "chrome.exe"};
    monitor.set_mock_process_provider([&]() { return regular_apps; });

    string detected_game = monitor.scan_for_active_games();
    assert(detected_game.empty());
    assert(monitor.is_game_active() == false);
    cout << "  -> Non-gaming apps detected cleanly without false triggers: PASSED" << endl;

    // Launching game process (e.g. valorant.exe)
    vector<string> gaming_apps = {"explorer.exe", "chrome.exe", "Valorant.exe"}; // Mixed case
    monitor.set_mock_process_provider([&]() { return gaming_apps; });

    detected_game = monitor.scan_for_active_games();
    assert(!detected_game.empty());
    cout << "  -> Game process detected: " << detected_game << " (PASSED)" << endl;

    // Custom game process addition
    assert(monitor.scan_for_active_games().empty() == false);
    vector<string> custom_game_apps = {"MyCustomIndieGame.exe"};
    monitor.set_mock_process_provider([&]() { return custom_game_apps; });
    assert(monitor.scan_for_active_games().empty() == true); // Not yet monitored

    monitor.add_monitored_game("MyCustomIndieGame.exe");
    assert(monitor.scan_for_active_games() == "MyCustomIndieGame.exe");
    cout << "  -> Dynamic custom game addition verified: PASSED" << endl;

    // =========================================================================
    // 7. TASK 14: GAMING AUTO-THROTTLE & RESTORATION LIFECYCLE
    // =========================================================================
    cout << "\n[7/7] Testing Gaming QoS Auto-Throttle & Restoration..." << endl;

    bool cb_game_active = false;
    string cb_game_name = "";
    double cb_throttle = 1.0;

    monitor.set_on_gaming_state_changed_callback([&](bool active, const string &name, double ratio) {
        cb_game_active = active;
        cb_game_name = name;
        cb_throttle = ratio;
    });

    vector<string> active_processes = {"discord.exe"};
    monitor.set_mock_process_provider([&]() { return active_processes; });
    monitor.set_check_interval(30);

    monitor.start();
    this_thread::sleep_for(chrono::milliseconds(150));

    assert(monitor.is_game_active() == true);
    assert(monitor.get_suggested_throttle_ratio() == 0.10); // 90% throttle
    assert(cb_game_active == true);
    assert(cb_game_name == "discord.exe");
    cout << "  -> Active game throttled download to 0.10 (90% reduction): PASSED" << endl;

    // Game closes
    active_processes = {"notepad.exe"};
    this_thread::sleep_for(chrono::milliseconds(150));

    assert(monitor.is_game_active() == false);
    assert(monitor.get_suggested_throttle_ratio() == 1.0); // Full speed restored
    assert(cb_game_active == false);
    cout << "  -> Game exited: 100% full speed restored automatically: PASSED" << endl;

    monitor.stop();

        // =========================================================================
    // 8. TASK 15: METERED WI-FI & MOBILE HOTSPOT DETECTION (Pos + Neg)
    // =========================================================================
    cout << "\n[8/9] Testing Metered Wi-Fi & Hotspot Detection (Task 15)..." << endl;

    // Normal non-metered home fiber Wi-Fi
    monitor.set_mock_network_name_provider([&]() { return "Home_Fiber_5GHz"; });
    assert(monitor.is_connection_metered() == false);
    cout << "  -> Regular home fiber connection flagged as unmetered: PASSED" << endl;

    // Tethered to iPhone Mobile Hotspot
    monitor.set_mock_network_name_provider([&]() { return "Alex_iPhone_15_Hotspot"; });
    assert(monitor.is_connection_metered() == true);
    cout << "  -> Mobile hotspot detected via SSID pattern matching: PASSED" << endl;

    // Custom metered network keyword addition
    monitor.set_mock_network_name_provider([&]() { return "InFlight_GogoWiFi"; });
    assert(monitor.is_connection_metered() == false); // Not yet added

    monitor.add_metered_network("gogowifi");
    assert(monitor.is_connection_metered() == true);
    cout << "  -> Custom metered network registration verified: PASSED" << endl;

    // =========================================================================
    // 9. TASK 15: METERED POLICY ENFORCEMENT (AutoPause vs Throttle vs Allow)
    // =========================================================================
    cout << "\n[9/9] Testing Metered Policy Enforcement (Task 15)..." << endl;

    bool metered_cb_called = false;
    MeteredNetworkPolicy received_policy = MeteredNetworkPolicy::AutoPause;
    monitor.set_on_metered_state_changed_callback([&](bool metered, MeteredNetworkPolicy policy) {
        metered_cb_called = metered;
        received_policy = policy;
    });

    // Policy 1: AutoPause (Default - completely block heavy downloads)
    monitor.set_metered_policy(MeteredNetworkPolicy::AutoPause);
    monitor.set_mock_metered_provider([&]() { return true; }); // Simulate metered
    monitor.set_check_interval(30);

    monitor.start();
    this_thread::sleep_for(chrono::milliseconds(150));

    assert(monitor.is_download_allowed() == false);
    assert(monitor.get_suggested_throttle_ratio() == 0.0); // 0.0 signals full pause
    assert(metered_cb_called == true);
    cout << "  -> AutoPause policy paused downloads (ratio = 0.0): PASSED" << endl;

    // Policy 2: Throttle (Allow with heavy 80% throttle cap to conserve data)
    monitor.set_metered_policy(MeteredNetworkPolicy::Throttle);
    monitor.evaluate_qos_sample(25);
    assert(monitor.is_download_allowed() == true);
    assert(monitor.get_suggested_throttle_ratio() == 0.20);
    cout << "  -> Throttle policy throttled downloads to 0.20 (80% reduction): PASSED" << endl;

    // Policy 3: Allow (User override to download at full speed)
    monitor.set_metered_policy(MeteredNetworkPolicy::Allow);
    monitor.evaluate_qos_sample(25);
    assert(monitor.is_download_allowed() == true);
    assert(monitor.get_suggested_throttle_ratio() == 1.0);
    cout << "  -> Allow policy granted full speed override (ratio = 1.0): PASSED" << endl;

    // Disconnecting from metered hotspot (restores unmetered state)
    monitor.set_mock_metered_provider([&]() { return false; });
    this_thread::sleep_for(chrono::milliseconds(150));
    assert(monitor.is_download_allowed() == true);
    assert(monitor.get_suggested_throttle_ratio() == 1.0);
    cout << "  -> Unmetered restoration verified cleanly: PASSED" << endl;

    monitor.stop();

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF NETWORKMONITOR (TASK 13 & 14) VERIFIED!     " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
