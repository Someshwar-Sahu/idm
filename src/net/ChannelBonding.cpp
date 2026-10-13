#include "ChannelBonding.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#endif

ChannelBonding::ChannelBonding()
    : m_mode(BondingMode::RoundRobin),
      m_next_index(0),
      m_max_failures(3) {
}

std::vector<NetworkAdapter> ChannelBonding::query_native_adapters() {
    std::vector<NetworkAdapter> result;

#ifdef _WIN32
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG outBufLen = 15000;
    std::vector<BYTE> buffer(outBufLen);
    PIP_ADAPTER_ADDRESSES pAddresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());

    DWORD dwRetVal = GetAdaptersAddresses(AF_INET, flags, NULL, pAddresses, &outBufLen);
    if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(outBufLen);
        pAddresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
        dwRetVal = GetAdaptersAddresses(AF_INET, flags, NULL, pAddresses, &outBufLen);
    }

    if (dwRetVal == NO_ERROR) {
        for (PIP_ADAPTER_ADDRESSES pCurr = pAddresses; pCurr != NULL; pCurr = pCurr->Next) {
            if (pCurr->OperStatus != IfOperStatusUp) {
                continue;
            }

            char name_buf[256] = {0};
            WideCharToMultiByte(CP_UTF8, 0, pCurr->FriendlyName, -1, name_buf, sizeof(name_buf) - 1, NULL, NULL);
            std::string friendly_name(name_buf);

            char desc_buf[256] = {0};
            WideCharToMultiByte(CP_UTF8, 0, pCurr->Description, -1, desc_buf, sizeof(desc_buf) - 1, NULL, NULL);
            std::string desc(desc_buf);

            std::string ip_str = "";
            for (PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurr->FirstUnicastAddress; pUnicast != NULL; pUnicast = pUnicast->Next) {
                if (pUnicast->Address.lpSockaddr->sa_family == AF_INET) {
                    sockaddr_in *sa_in = reinterpret_cast<sockaddr_in*>(pUnicast->Address.lpSockaddr);
                    char ip_buf[INET_ADDRSTRLEN] = {0};
                    if (inet_ntop(AF_INET, &(sa_in->sin_addr), ip_buf, sizeof(ip_buf))) {
                        ip_str = ip_buf;
                        break;
                    }
                }
            }

            if (ip_str.empty() || ip_str == "127.0.0.1") {
                continue;
            }

            std::ostringstream mac_ss;
            for (ULONG i = 0; i < pCurr->PhysicalAddressLength; ++i) {
                if (i > 0) mac_ss << ":";
                mac_ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(pCurr->PhysicalAddress[i]);
            }

            NetworkAdapter adapter;
            adapter.name = friendly_name;
            adapter.description = desc;
            adapter.ip_address = ip_str;
            adapter.mac_address = mac_ss.str();
            adapter.is_up = true;
            adapter.is_enabled = true;
            adapter.speed_bps = pCurr->TransmitLinkSpeed;

            if (pCurr->IfType == IF_TYPE_ETHERNET_CSMACD) {
                adapter.type = AdapterType::Ethernet;
            } else if (pCurr->IfType == IF_TYPE_IEEE80211) {
                adapter.type = AdapterType::WiFi;
            } else if (pCurr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                adapter.type = AdapterType::Loopback;
            } else {
                adapter.type = AdapterType::Other;
            }

            result.push_back(adapter);
        }
    }
#elif defined(__APPLE__) || defined(__linux__)
    struct ifaddrs *ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == 0) {
        for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (ifa->ifa_flags & IFF_LOOPBACK) continue;
            if (!(ifa->ifa_flags & IFF_UP)) continue;

            sockaddr_in *sa_in = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
            char ip_buf[INET_ADDRSTRLEN] = {0};
            if (inet_ntop(AF_INET, &(sa_in->sin_addr), ip_buf, sizeof(ip_buf))) {
                NetworkAdapter adapter;
                adapter.name = ifa->ifa_name;
                adapter.ip_address = ip_buf;
                adapter.is_up = true;
                adapter.is_enabled = true;
                if (adapter.name.rfind("en", 0) == 0 || adapter.name.rfind("eth", 0) == 0) {
                    adapter.type = AdapterType::Ethernet;
                } else if (adapter.name.rfind("wl", 0) == 0) {
                    adapter.type = AdapterType::WiFi;
                } else {
                    adapter.type = AdapterType::Other;
                }
                result.push_back(adapter);
            }
        }
        freeifaddrs(ifaddr);
    }
#endif

    return result;
}

std::vector<NetworkAdapter> ChannelBonding::discover_adapters() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_mock_provider) {
        m_adapters = m_mock_provider();
    } else {
        m_adapters = query_native_adapters();
    }
    return m_adapters;
}

std::vector<NetworkAdapter> ChannelBonding::get_adapters() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_adapters;
}

void ChannelBonding::add_adapter(const NetworkAdapter &adapter) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_adapters.push_back(adapter);
}

void ChannelBonding::clear_adapters() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_adapters.clear();
}

bool ChannelBonding::enable_adapter(const std::string &ip_or_name, bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &ad : m_adapters) {
        if (ad.ip_address == ip_or_name || ad.name == ip_or_name) {
            ad.is_enabled = enabled;
            return true;
        }
    }
    return false;
}

bool ChannelBonding::set_adapter_weight(const std::string &ip_or_name, uint32_t weight) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &ad : m_adapters) {
        if (ad.ip_address == ip_or_name || ad.name == ip_or_name) {
            ad.weight = std::max(1u, weight);
            return true;
        }
    }
    return false;
}

void ChannelBonding::set_bonding_mode(BondingMode mode) {
    m_mode.store(mode);
}

BondingMode ChannelBonding::get_bonding_mode() const {
    return m_mode.load();
}

std::vector<NetworkAdapter> ChannelBonding::get_healthy_active_adapters() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<NetworkAdapter> healthy;
    for (const auto &ad : m_adapters) {
        if (ad.is_up && ad.is_enabled && ad.consecutive_failures < m_max_failures) {
            healthy.push_back(ad);
        }
    }
    return healthy;
}

std::string ChannelBonding::get_interface_for_chunk(uint32_t chunk_id) const {
    if (m_mode.load() == BondingMode::Disabled) {
        return "";
    }

    auto healthy = get_healthy_active_adapters();
    if (healthy.empty()) {
        return "";
    }

    BondingMode mode = m_mode.load();
    if (mode == BondingMode::RoundRobin) {
        size_t idx = chunk_id % healthy.size();
        return healthy[idx].ip_address;
    } else if (mode == BondingMode::WeightedRatio) {
        uint32_t total_weight = 0;
        for (const auto &ad : healthy) {
            total_weight += std::max(1u, ad.weight);
        }
        if (total_weight == 0) return healthy[0].ip_address;

        uint32_t mod = chunk_id % total_weight;
        uint32_t accum = 0;
        for (const auto &ad : healthy) {
            accum += std::max(1u, ad.weight);
            if (mod < accum) {
                return ad.ip_address;
            }
        }
        return healthy[0].ip_address;
    } else if (mode == BondingMode::FailoverPrimary) {
        return healthy[0].ip_address;
    }

    return "";
}

std::string ChannelBonding::get_next_interface() {
    uint32_t idx = m_next_index.fetch_add(1);
    return get_interface_for_chunk(idx);
}

bool ChannelBonding::apply_to_curl(CURL *curl, uint32_t chunk_id) {
    if (!curl) return false;
    std::string iface = get_interface_for_chunk(chunk_id);
    if (!iface.empty()) {
        CURLcode res = curl_easy_setopt(curl, CURLOPT_INTERFACE, iface.c_str());
        return (res == CURLE_OK);
    }
    return true;
}

void ChannelBonding::report_adapter_failure(const std::string &ip_or_name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &ad : m_adapters) {
        if (ad.ip_address == ip_or_name || ad.name == ip_or_name) {
            ad.consecutive_failures++;
            break;
        }
    }
}

void ChannelBonding::report_adapter_success(const std::string &ip_or_name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &ad : m_adapters) {
        if (ad.ip_address == ip_or_name || ad.name == ip_or_name) {
            ad.consecutive_failures = 0;
            break;
        }
    }
}

void ChannelBonding::record_transfer(const std::string &ip_or_name, uint64_t bytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &ad : m_adapters) {
        if (ad.ip_address == ip_or_name || ad.name == ip_or_name) {
            ad.bytes_transferred += bytes;
            break;
        }
    }
}

uint64_t ChannelBonding::get_adapter_transfer_bytes(const std::string &ip_or_name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto &ad : m_adapters) {
        if (ad.ip_address == ip_or_name || ad.name == ip_or_name) {
            return ad.bytes_transferred;
        }
    }
    return 0;
}

void ChannelBonding::set_max_failure_threshold(uint32_t max_fails) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_max_failures = std::max(1u, max_fails);
}

uint32_t ChannelBonding::get_max_failure_threshold() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_max_failures;
}

void ChannelBonding::set_mock_adapter_provider(AdapterListProvider provider) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mock_provider = provider;
}
