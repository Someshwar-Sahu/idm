#ifndef CHANNEL_BONDING_H
#define CHANNEL_BONDING_H

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>
#include <cstdint>
#include <curl/curl.h>

using namespace std;

enum class AdapterType {
    Ethernet,
    WiFi,
    Cellular,
    Loopback,
    Other
};

struct NetworkAdapter {
    string name;          // e.g. "Ethernet", "Wi-Fi"
    string description;   // Hardware description
    string ip_address;    // e.g. "192.168.1.100"
    string mac_address;   // e.g. "00:1A:2B:3C:4D:5E"
    enum AdapterType type{AdapterType::Other};
    bool is_up{true};
    bool is_enabled{true};
    uint64_t speed_bps{0};     // Link speed in bits/sec
    uint32_t weight{1};        // Weight for weighted ratio distribution
    uint32_t consecutive_failures{0};
    uint64_t bytes_transferred{0};
};

enum class BondingMode {
    RoundRobin,      // Even distribution across all active adapters
    WeightedRatio,   // Distribute based on adapter weights/speeds
    FailoverPrimary, // Use primary adapter unless degraded/offline
    Disabled         // Default OS routing table (no interface binding)
};

using AdapterListProvider = function<vector<NetworkAdapter>()>;

class ChannelBonding {
public:
    ChannelBonding();
    ~ChannelBonding() = default;

    // Adapter Discovery & Management
    vector<NetworkAdapter> discover_adapters();
    vector<NetworkAdapter> get_adapters() const;
    void add_adapter(const NetworkAdapter &adapter);
    void clear_adapters();

    bool enable_adapter(const string &ip_or_name, bool enabled = true);
    bool set_adapter_weight(const string &ip_or_name, uint32_t weight);
    void set_bonding_mode(BondingMode mode);
    BondingMode get_bonding_mode() const;

    // Interface Binding for Sockets / libcurl
    string get_interface_for_chunk(uint32_t chunk_id) const;
    string get_next_interface();
    bool apply_to_curl(CURL *curl, uint32_t chunk_id = 0);

    // Health, Failover & Accounting
    void report_adapter_failure(const string &ip_or_name);
    void report_adapter_success(const string &ip_or_name);
    void record_transfer(const string &ip_or_name, uint64_t bytes);
    uint64_t get_adapter_transfer_bytes(const string &ip_or_name) const;

    void set_max_failure_threshold(uint32_t max_fails); // Default: 3
    uint32_t get_max_failure_threshold() const;

    // Testing & Extensibility
    void set_mock_adapter_provider(AdapterListProvider provider);

    // Native Platform Probing
    vector<NetworkAdapter> query_native_adapters();

private:
    vector<NetworkAdapter> get_healthy_active_adapters() const;

    mutable mutex m_mutex;
    vector<NetworkAdapter> m_adapters;
    atomic<BondingMode> m_mode{BondingMode::RoundRobin};
    atomic<uint32_t> m_next_index{0};
    uint32_t m_max_failures{3};

    AdapterListProvider m_mock_provider{nullptr};
};

#endif // CHANNEL_BONDING_H
