#define private public
#include "ProgressTracker.h"
#undef private

#include <iostream>
#include <cassert>
#include <cstdio>
#include <thread>
#include <chrono>
#include <string>

using namespace std;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: ProgressTracker (Pos + Neg)    " << endl;
    cout << "==========================================================" << endl;

    // =========================================================================
    // 1. INITIALIZATION & ZERO-BYTE GUARDS (Positive & Negative)
    // =========================================================================
    cout << "\n[1/5] Testing Initialization & Zero-Byte Guards..." << endl;

    // 1.1 Positive: Normal file size (100 MB)
    constexpr uint64_t TOTAL_SIZE = 100 * 1024 * 1024;
    ProgressTracker tracker(TOTAL_SIZE);

    assert(tracker.get_progress_percentage() == 0.0);
    assert(tracker.get_speed_bytes_per_sec() == 0.0);
    assert(tracker.get_formatted_eta() == "--:--");

    // 1.2 Negative / Edge: 0-byte total file size must not divide by zero
    ProgressTracker zero_tracker(0);
    assert(zero_tracker.get_progress_percentage() == 0.0);
    assert(zero_tracker.get_formatted_eta() == "--:--");
    cout << "  -> Initialization & 0-byte division-by-zero guards: PASSED" << endl;

    // =========================================================================
    // 2. UPDATE THROTTLE & SMOOTHING (Positive & Negative)
    // =========================================================================
    cout << "\n[2/5] Testing update() Throttle & Rollback Guards..." << endl;

    // 2.1 Negative: Immediate update (< 200ms) is throttled and ignored
    tracker.update(5 * 1024 * 1024); // Instantaneous call
    assert(tracker.get_progress_percentage() == 0.0); // Not updated yet!

    // 2.2 Positive: Update after waiting >= 200ms
    this_thread::sleep_for(chrono::milliseconds(220));
    tracker.update(10 * 1024 * 1024); // 10 MB downloaded out of 100 MB
    assert(tracker.get_progress_percentage() == 10.0);
    assert(tracker.get_speed_bytes_per_sec() > 0.0);
    assert(tracker.get_formatted_eta() != "--:--");

    // 2.3 Negative: Rollback / Corrupted smaller byte count does not underflow
    this_thread::sleep_for(chrono::milliseconds(220));
    tracker.update(5 * 1024 * 1024); // 5 MB < 10 MB (rollback simulation)
    // Percentage remains non-negative and valid
    assert(tracker.get_progress_percentage() == 5.0);
    cout << "  -> Throttle window & rollback underflow guards: PASSED" << endl;

    // =========================================================================
    // 3. SPEED FORMATTER UNIT TESTS (Positive B/s, KB/s, MB/s)
    // =========================================================================
    cout << "\n[3/5] Testing Speed Formatter Across All Units..." << endl;

    ProgressTracker speed_test(1000);

    // 3.1 Positive: Bytes/sec (< 1024)
    speed_test.m_current_speed_bps = 500.0;
    assert(speed_test.get_formatted_speed() == "500B/s");

    // 3.2 Positive: KB/sec (1024 to 1MB)
    speed_test.m_current_speed_bps = 1536.0; // 1.5 KB/s
    assert(speed_test.get_formatted_speed() == "1.50 KB/s");

    // 3.3 Positive: MB/sec (>= 1MB)
    speed_test.m_current_speed_bps = 5.0 * 1024.0 * 1024.0; // 5 MB/s
    assert(speed_test.get_formatted_speed() == "5.00 MB/s");
    cout << "  -> Speed formatting for B/s, KB/s, MB/s: PASSED" << endl;

    // =========================================================================
    // 4. ETA FORMATTER UNIT TESTS (Positive & Negative)
    // =========================================================================
    cout << "\n[4/5] Testing ETA Formatter Across Time Thresholds..." << endl;

    ProgressTracker eta_test(10000);

    // 4.1 Negative: Speed 0.0 returns "--:--"
    eta_test.m_current_speed_bps = 0.0;
    eta_test.m_last_bytes = 1000;
    assert(eta_test.get_formatted_eta() == "--:--");

    // 4.2 Negative: Completed download (last_bytes >= total_size) returns "--:--"
    eta_test.m_current_speed_bps = 1000.0;
    eta_test.m_last_bytes = 10000;
    assert(eta_test.get_formatted_eta() == "--:--");

    // 4.3 Positive: < 1 hour (MM:SS) -> 100 seconds = 1 min 40 sec
    eta_test.m_total_size = 2000;
    eta_test.m_last_bytes = 1000; // 1000 remaining
    eta_test.m_current_speed_bps = 10.0; // 1000 / 10 = 100 seconds
    assert(eta_test.get_formatted_eta() == "01:40");

    // 4.4 Positive: >= 1 hour (HH:MM:SS) -> 3665 seconds = 1 hr 1 min 5 sec
    eta_test.m_total_size = 36650;
    eta_test.m_last_bytes = 0; // 36650 remaining
    eta_test.m_current_speed_bps = 10.0; // 3665 seconds
    assert(eta_test.get_formatted_eta() == "01:01:05");
    cout << "  -> ETA formatting for 0-speed, complete, MM:SS, HH:MM:SS: PASSED" << endl;

    // =========================================================================
    // 5. PROGRESS BAR RENDERING & CLAMPING (Positive & Negative)
    // =========================================================================
    cout << "\n[5/5] Testing Progress Bar Rendering & Clamping..." << endl;

    // 5.1 Positive: Standard bars (width = 30)
    string bar_0 = ProgressTracker::render_bar(0.0, 30);
    assert(bar_0.front() == '[' && bar_0.back() == ']');
    assert(bar_0.length() == 32); // 30 chars + 2 brackets

    string bar_50 = ProgressTracker::render_bar(50.0, 30);
    assert(bar_50.length() == 32);

    string bar_100 = ProgressTracker::render_bar(100.0, 30);
    assert(bar_100 == "[==============================]");

    // 5.2 Negative / Clamping: Percentage > 100% must be clamped to 100%
    string bar_overflow = ProgressTracker::render_bar(150.0, 30);
    assert(bar_overflow == "[==============================]");

    // 5.3 Negative / Clamping: Percentage < 0% must be clamped to 0%
    string bar_underflow = ProgressTracker::render_bar(-25.0, 30);
    assert(bar_underflow == "[>                             ]");

    // 5.4 Positive: Custom width (10 chars)
    string bar_custom = ProgressTracker::render_bar(100.0, 10);
    assert(bar_custom == "[==========]");
    cout << "  -> Progress bar visual rendering & boundary clamping: PASSED" << endl;

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF PROGRESSTRACKER FUNCTIONS VERIFIED!    " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
