#ifndef PROGRESS_TRACKER_H
#define PROGRESS_TRACKER_H

#include <chrono>
#include <string>
#include <cstdint>

class ProgressTracker {
public:
    ProgressTracker(uint64_t total_size);

    void update(uint64_t current_downloaded_bytes);

    double get_progress_percentage() const;
    double get_speed_bytes_per_sec() const ;
    std::string get_formatted_speed() const;
    std::string get_formatted_eta() const;

    static std::string render_bar(double percentage, int width =30);
private: 
    uint64_t m_total_size{0};
    uint64_t m_last_bytes{0};
    double m_current_speed_bps{0.0};

    std::chrono::steady_clock::time_point m_start_time;
    std::chrono::steady_clock::time_point m_last_time;
};

#endif