#include "DownloadTask.h"
#include <iostream>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iomanip>

using namespace std;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    cout<<"[Test] Starting DownloadTask verification test..."<<endl;

    string test_url = "https://httpbin.org/bytes/2048";
    string dest_file = "downloaded_test.bin";

    DownloadTask task(test_url, dest_file, 4);

    auto callback = [](const ProgressTracker &tracker, const string &status_msg){
        cout<<"\r[Status: " <<status_msg<<"]" << ProgressTracker::render_bar(tracker.get_progress_percentage(), 25)<<" "<<fixed <<setprecision(1)<<tracker.get_progress_percentage()<<"% | "<<tracker.get_formatted_speed()<<" | ETA: "<<tracker.get_formatted_eta()<<"         "<<flush;
    };

    cout<<"[Test] Launching multi-threaded download task..."<<endl;
    bool success = task.start(callback);

    cout<<endl;
    if(!success){
        cerr<<"[Test Failed] Download failed: "<<task.get_last_error()<<endl;
        return 1;
    }

    assert(task.get_status() == DownloadStatus::Completed);
    cout<<"[Test] Download completed cleanly!"<<endl;

    ifstream file(dest_file, ios::binary | ios::ate);
    assert(file.is_open());
    streampos file_size = file.tellg();
    assert(file_size >0);
    file.close();

    cout << "[Test] Verified destination file size on disk: " << file_size << " bytes." << endl;

    remove(dest_file.c_str());
    cout << "[PASSED] DownloadTask Orchestrator test completed successfully!" << endl;
    return 0;
}