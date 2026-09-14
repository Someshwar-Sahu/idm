#include "SettingsManager.h"
#include <iostream>
#include <cassert>
#include <cstdio>
#include <fstream>

using namespace std;

int main() {
    cout<<"[Test] Starting SettingsManager unit test..."<<endl;

    string test_config = "test_settings.json";

    remove(test_config.c_str());

    SettingsManager manager(test_config);
    AppSettings default_settings = manager.get_settings();

    cout<<"[Test] Default download directory: "<<default_settings.default_download_directory<<endl;
    assert(!default_settings.default_download_directory.empty());

    AppSettings custom_settings = default_settings;
    custom_settings.max_concurrent_download = 5;
    custom_settings.speed_mode = SpeedLimiterMode::ManualCap;
    custom_settings.manual_speed_limit_bps = 5242880;
    custom_settings.category_folders["Videos"] = "MyVideos";

    manager.update_settings(custom_settings);
    bool saved = manager.save(test_config);
    assert(saved);
    cout<<"[Test] Settings saved to "<<test_config<<" successfully."<<endl;

    SettingsManager loaded_manager(test_config);
    AppSettings loaded_settings = loaded_manager.get_settings();

    assert(loaded_settings.max_concurrent_download == 5);
    assert(loaded_settings.speed_mode == SpeedLimiterMode::ManualCap);
    assert(loaded_settings.manual_speed_limit_bps == 5242880);
    assert(loaded_settings.category_folders["Videos"] == "MyVideos");

    cout<<"[Test] Loaded max concurrent download: "<<loaded_settings.max_concurrent_download<<endl;
    cout<<"[Test] Loaded speed mode: "<<AppSettings::speed_mode_to_string(loaded_settings.speed_mode)<<endl;

    string video_path = loaded_manager.get_category_path("Videos");
    cout<<"[Test] Videos category path: "<<video_path<<endl;
    remove(test_config.c_str());
    cout<<"[Test] SettingsManager unit test completed successfully!"<<endl;
    return 0;
}