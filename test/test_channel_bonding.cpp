#include "ChannelBonding.h"
#include <iostream>
#include <cassert>
#include <curl/curl.h>

using namespace std;

int main() {
    curl_global_init(CURL_GLOBAL_ALL);

    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: ChannelBonding (Task 17)       " << endl;
    cout << "==========================================================" << endl;

    ChannelBonding bonding;

    // 1. Initial State & Native OS Hardware Query
    cout << "\n[1/7] Testing Native Network Interface Discovery..." << endl;
    auto native_adapters = bonding.query_native_adapters();
    cout << "  -> Detected " << native_adapters.size() << " active native IPv4 adapters on host:" << endl;
    for (const auto &ad : native_adapters) {
        cout << "     * [" << ad.name << "] IP: " << ad.ip_address 
             << " | Type: " << (ad.type == AdapterType::Ethernet ? "Ethernet" : (ad.type == AdapterType::WiFi ? "Wi-Fi" : "Other"))
             << " | Speed: " << (ad.speed_bps / 1000000) << " Mbps" << endl;
    }
    cout << "  -> Native adapter discovery executed cleanly: PASSED" << endl;

    // 2. Dual-Adapter Round-Robin Distribution
    cout << "\n[2/7] Testing Dual-Adapter Round-Robin Chunk Distribution..." << endl;
    NetworkAdapter eth;
    eth.name = "Ethernet 1";
    eth.ip_address = "192.168.1.10";
    eth.type = AdapterType::Ethernet;
    eth.speed_bps = 1000000000; // 1 Gbps
    eth.is_up = true;
    eth.is_enabled = true;
    eth.weight = 1;

    NetworkAdapter wifi;
    wifi.name = "Wi-Fi 6";
    wifi.ip_address = "192.168.1.20";
    wifi.type = AdapterType::WiFi;
    wifi.speed_bps = 300000000; // 300 Mbps
    wifi.is_up = true;
    wifi.is_enabled = true;
    wifi.weight = 1;

    bonding.clear_adapters();
    bonding.add_adapter(eth);
    bonding.add_adapter(wifi);
    bonding.set_bonding_mode(BondingMode::RoundRobin);

    // Chunks should alternate evenly
    assert(bonding.get_interface_for_chunk(0) == "192.168.1.10"); // Eth
    assert(bonding.get_interface_for_chunk(1) == "192.168.1.20"); // Wi-Fi
    assert(bonding.get_interface_for_chunk(2) == "192.168.1.10"); // Eth
    assert(bonding.get_interface_for_chunk(3) == "192.168.1.20"); // Wi-Fi
    cout << "  -> Round-Robin evenly distributed 4 chunks across 2 adapters: PASSED" << endl;

    // 3. Weighted Ratio Chunk Distribution (3:1 Proportional Bandwidth)
    cout << "\n[3/7] Testing Weighted Ratio Chunk Distribution (3:1 Ratio)..." << endl;
    bonding.set_adapter_weight("192.168.1.10", 3); // Ethernet 3x weight
    bonding.set_adapter_weight("192.168.1.20", 1); // Wi-Fi 1x weight
    bonding.set_bonding_mode(BondingMode::WeightedRatio);

    int eth_count = 0;
    int wifi_count = 0;
    for (uint32_t i = 0; i < 8; ++i) {
        string iface = bonding.get_interface_for_chunk(i);
        if (iface == "192.168.1.10") eth_count++;
        else if (iface == "192.168.1.20") wifi_count++;
    }
    assert(eth_count == 6);  // 3 out of 4 * 8 = 6
    assert(wifi_count == 2); // 1 out of 4 * 8 = 2
    cout << "  -> Weighted ratio distributed exactly 6 chunks to Eth and 2 to Wi-Fi: PASSED" << endl;

    // 4. Failover & Degradation Resilience
    cout << "\n[4/7] Testing Adapter Failover & Degradation Resilience..." << endl;
    bonding.set_bonding_mode(BondingMode::RoundRobin);
    bonding.set_max_failure_threshold(3);

    // Simulate Ethernet experiencing 3 consecutive socket drops
    bonding.report_adapter_failure("192.168.1.10");
    bonding.report_adapter_failure("192.168.1.10");
    bonding.report_adapter_failure("192.168.1.10");

    // Ethernet degraded: all chunks should now failover to Wi-Fi
    assert(bonding.get_interface_for_chunk(0) == "192.168.1.20");
    assert(bonding.get_interface_for_chunk(1) == "192.168.1.20");
    cout << "  -> Unplugged/failing Ethernet gracefully failed over 100% to Wi-Fi: PASSED" << endl;

    // Cable plugged back in -> success reported restores dual bonding
    bonding.report_adapter_success("192.168.1.10");
    assert(bonding.get_interface_for_chunk(0) == "192.168.1.10");
    assert(bonding.get_interface_for_chunk(1) == "192.168.1.20");
    cout << "  -> Ethernet recovery restored dual bonding cleanly: PASSED" << endl;

    // 5. Disabled Mode (Default OS Routing Passthrough)
    cout << "\n[5/7] Testing Disabled Bonding Mode (Default Routing)..." << endl;
    bonding.set_bonding_mode(BondingMode::Disabled);
    assert(bonding.get_interface_for_chunk(0) == "");
    assert(bonding.get_interface_for_chunk(1) == "");
    cout << "  -> Disabled mode yields empty interface for OS default routing: PASSED" << endl;

    // 6. Per-Adapter Data Accounting
    cout << "\n[6/7] Testing Per-Adapter Transfer Byte Accounting..." << endl;
    bonding.record_transfer("192.168.1.10", 52428800); // 50 MB
    bonding.record_transfer("192.168.1.20", 26214400); // 25 MB

    assert(bonding.get_adapter_transfer_bytes("192.168.1.10") == 52428800);
    assert(bonding.get_adapter_transfer_bytes("192.168.1.20") == 26214400);
    assert(bonding.get_adapter_transfer_bytes("192.168.1.99") == 0); // Non-existent
    cout << "  -> Byte accounting tracked per physical adapter accurately: PASSED" << endl;

    // 7. libcurl Socket Interface Binding Integration
    cout << "\n[7/7] Testing libcurl CURLOPT_INTERFACE Socket Binding..." << endl;
    bonding.set_bonding_mode(BondingMode::RoundRobin);
    CURL *curl = curl_easy_init();
    assert(curl != nullptr);

    bool applied_chunk_0 = bonding.apply_to_curl(curl, 0); // Binds to Eth
    assert(applied_chunk_0 == true);

    bool applied_chunk_1 = bonding.apply_to_curl(curl, 1); // Binds to Wi-Fi
    assert(applied_chunk_1 == true);

    curl_easy_cleanup(curl);
    cout << "  -> CURLOPT_INTERFACE applied to libcurl handles successfully: PASSED" << endl;

    curl_global_cleanup();

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF CHANNELBONDING (TASK 17) VERIFIED!     " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
