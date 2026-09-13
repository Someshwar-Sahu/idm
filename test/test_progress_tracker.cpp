#include "ProgressTracker.h"
#include <iostream>
#include <cassert>
#include <cstdio>
#include<thread>
#include<chrono>

using namespace std;

int main() {
    setvbuf(stdout, NULL, _IONBF,0);

    cout<<"[Test] Starting ProgressTracker verification test..."<<endl;

    uint64_t total_size = 100 * 1024 * 1024;
    ProgressTracker tracker(total_size);

    assert(tracker.get_progress_percentage() == 0.0);
    assert(tracker.get_speed_bytes_per_sec() == 0.0);
    assert(tracker.get_formatted_eta() == "--:--");

    this_thread::sleep_for(chrono::milliseconds(250));
    tracker.update(10 * 1024 * 1024);

    double pct = tracker.get_progress_percentage();
    assert (pct == 10.0);
    cout<<"[Test] Progress Percentage: "<<pct<<"%"<<endl;

    double speed = tracker.get_speed_bytes_per_sec();
    assert(speed > 0.0);
    cout<<"[Test] Download Speed: "<<tracker.get_formatted_speed() << endl;

    string eta = tracker.get_formatted_eta();
    assert(eta != "--:--");
    cout<< "[Test] Estimated Time Remaining (ETA): "<<eta<<endl;

    string bar = ProgressTracker::render_bar(pct, 30);
    cout<<"[Test] Rendered Progress Bar: "<<bar<<endl;
    assert(bar.front() == '[' && bar.back() == ']');

    this_thread::sleep_for(chrono::milliseconds(250));
    tracker.update(total_size);
    assert(tracker.get_progress_percentage() == 100.0);
    string full_bar = ProgressTracker::render_bar(100.0, 30);
    cout<<"[Test] 100% Bar: "<<full_bar<<endl;
    
    cout<<"[PASSED] ProgressTracker verification test completed successfully!"<<endl;
    return 0;
}