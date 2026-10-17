#include "ProgressTracker.h"
#include<iomanip>
#include<sstream>
#include<cmath>

using namespace std;

ProgressTracker::ProgressTracker(uint64_t total_size)
    : m_total_size(total_size),
      m_last_bytes(0),
      m_current_speed_bps(0.0) {
    m_start_time = chrono::steady_clock::now();
    m_last_time = m_start_time;
}

void ProgressTracker::update(uint64_t current_downloaded_bytes){
    std::lock_guard<std::mutex> lock(m_mutex);
    auto now = chrono::steady_clock::now();
    chrono::duration<double> elapsed = now - m_last_time;

    if(elapsed.count() >= 0.2){
        // Bug 10 Fix: Prevent extreme speed spikes when resuming from sleep or after thread stalls
        if (elapsed.count() > 5.0) {
            m_last_bytes = current_downloaded_bytes;
            m_last_time = now;
            return;
        }

        double effective_elapsed = std::min(elapsed.count(), 2.0);
        uint64_t bytes_delta = (current_downloaded_bytes >= m_last_bytes) ? (current_downloaded_bytes - m_last_bytes) : 0;

        double instant_speed = static_cast<double>(bytes_delta) / effective_elapsed;

        if(m_current_speed_bps == 0.0) m_current_speed_bps = instant_speed;
        else m_current_speed_bps = 0.7 * m_current_speed_bps + 0.3 * instant_speed;

        m_last_bytes = current_downloaded_bytes;
        m_last_time = now;
    }
}

double ProgressTracker::get_progress_percentage() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if(m_total_size == 0) return 0.0;
    return (static_cast<double> (m_last_bytes) / static_cast<double>(m_total_size)) * 100.0;
}

double ProgressTracker::get_speed_bytes_per_sec() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_current_speed_bps;
}

string ProgressTracker::get_formatted_speed() const{
    std::lock_guard<std::mutex> lock(m_mutex);
    ostringstream ss;
    ss<<fixed<<setprecision(2);

    if(m_current_speed_bps >= 1024.0*1024.0) {
        ss<<(m_current_speed_bps / (1024.0*1024.0))<< " MB/s";
    }
    else if(m_current_speed_bps >= 1024.0) {
        ss<<(m_current_speed_bps / 1024.0)<<" KB/s";
    }
    else{
        ss<<static_cast<uint64_t> (m_current_speed_bps)<< "B/s";   
    }
    return ss.str();
}

string ProgressTracker::get_formatted_eta() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if(m_current_speed_bps <=0.0 || m_last_bytes>=m_total_size) {
        return "--:--";
    }

    uint64_t remaining_bytes = m_total_size - m_last_bytes;
    uint64_t seconds_left = static_cast<uint64_t>(ceil(remaining_bytes / m_current_speed_bps));

    uint64_t mins = seconds_left / 60;
    uint64_t secs = seconds_left % 60;
    uint64_t hrs = mins /60;
    mins = mins%60;

    ostringstream ss;
    if(hrs > 0){
        ss<<setfill('0')<<setw(2)<<hrs<< ":"<<setw(2)<<mins<<":"<<setw(2)<<secs;
    }
    else {
        ss<<setfill('0')<<setw(2)<<mins<<":"<<setw(2)<<secs;
    }
    return ss.str();
}
string ProgressTracker::render_bar(double percentage, int width){
    int filled = static_cast<int>(round((percentage / 100.0) * width));
    if(filled > width) filled = width;
    if(filled < 0) filled = 0;

    string bar = "[";
    for(int i=0; i<width;i++){
        if(i<filled) bar += "=";
        else if (i == filled) bar += ">";
        else bar += " ";
    }
    bar += "]";
    return bar;
}