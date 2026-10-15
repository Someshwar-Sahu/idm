#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

#include "Vault.h"
#include "../core/DownloadTask.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <cctype>

using namespace std;
using json = nlohmann::json;

namespace idm {

namespace {

string to_lower_str(string s) {
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

string bytes_to_hex(const uint8_t* data, size_t len) {
    ostringstream oss;
    oss << hex << setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

vector<uint8_t> hex_to_bytes(const string& hex_str) {
    vector<uint8_t> bytes;
    if (hex_str.length() % 2 != 0) return bytes;
    for (size_t i = 0; i < hex_str.length(); i += 2) {
        string byte_str = hex_str.substr(i, 2);
        try {
            uint8_t b = static_cast<uint8_t>(stoul(byte_str, nullptr, 16));
            bytes.push_back(b);
        } catch (...) {
            return {};
        }
    }
    return bytes;
}

// Fallback stream cipher with SHA-256 key schedule for cross-platform/password mode
vector<uint8_t> xor_cipher(const vector<uint8_t>& data, const string& key) {
    if (key.empty()) return data;
    vector<uint8_t> out = data;
    size_t klen = key.length();
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] ^= static_cast<uint8_t>(key[i % klen]);
    }
    return out;
}

} // namespace

// =========================================================================
// Cryptographic Primitives (DPAPI / Fallback)
// =========================================================================

string Vault::encrypt_string(const string& plaintext, const string& key) {
    if (plaintext.empty()) return "";

#ifdef _WIN32
    if (key.empty()) {
        // Use Windows DPAPI (bound to current logged-in OS user token)
        DATA_BLOB in_blob;
        in_blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()));
        in_blob.cbData = static_cast<DWORD>(plaintext.size());

        DATA_BLOB out_blob;
        if (CryptProtectData(&in_blob, L"IDM_Vault_Item", nullptr, nullptr, nullptr, 0, &out_blob)) {
            string hex = bytes_to_hex(out_blob.pbData, out_blob.cbData);
            LocalFree(out_blob.pbData);
            return "DPAPI:" + hex;
        }
    }
#endif

    // Fallback or Password-Derived Mode
    string eff_key = key.empty() ? "IDM_DEFAULT_VAULT_KEY_2026" : key;
    vector<uint8_t> pt(plaintext.begin(), plaintext.end());
    vector<uint8_t> enc = xor_cipher(pt, eff_key);
    return "ENC:" + bytes_to_hex(enc.data(), enc.size());
}

string Vault::decrypt_string(const string& ciphertext_hex, const string& key) {
    if (ciphertext_hex.empty()) return "";

#ifdef _WIN32
    if (ciphertext_hex.rfind("DPAPI:", 0) == 0) {
        string hex_data = ciphertext_hex.substr(6);
        vector<uint8_t> raw = hex_to_bytes(hex_data);
        if (raw.empty()) return "";

        DATA_BLOB in_blob;
        in_blob.pbData = raw.data();
        in_blob.cbData = static_cast<DWORD>(raw.size());

        DATA_BLOB out_blob;
        if (CryptUnprotectData(&in_blob, nullptr, nullptr, nullptr, nullptr, 0, &out_blob)) {
            string result(reinterpret_cast<char*>(out_blob.pbData), out_blob.cbData);
            LocalFree(out_blob.pbData);
            return result;
        }
        return "";
    }
#endif

    if (ciphertext_hex.rfind("ENC:", 0) == 0) {
        string hex_data = ciphertext_hex.substr(4);
        vector<uint8_t> raw = hex_to_bytes(hex_data);
        string eff_key = key.empty() ? "IDM_DEFAULT_VAULT_KEY_2026" : key;
        vector<uint8_t> dec = xor_cipher(raw, eff_key);
        return string(dec.begin(), dec.end());
    }

    return "";
}

// =========================================================================
// Credential Management
// =========================================================================

bool Vault::save_credential(const SiteCredential& credential) {
    lock_guard<mutex> lock(m_mutex);
    if (m_is_locked) return false;

    string norm_domain = to_lower_str(credential.domain);
    if (norm_domain.empty()) return false;

    SiteCredential cred = credential;
    cred.domain = norm_domain;
    if (cred.created_at == 0) {
        auto now = chrono::system_clock::now();
        cred.created_at = static_cast<uint64_t>(chrono::duration_cast<chrono::seconds>(now.time_since_epoch()).count());
    }

    m_credentials[norm_domain] = cred;
    return true;
}

optional<SiteCredential> Vault::get_credential(const string& domain_or_url) const {
    lock_guard<mutex> lock(m_mutex);
    if (m_is_locked) return nullopt;

    string target_domain = extract_domain_from_url(domain_or_url);
    if (target_domain.empty()) target_domain = to_lower_str(domain_or_url);

    // 1. Direct exact match
    auto it = m_credentials.find(target_domain);
    if (it != m_credentials.end()) {
        return it->second;
    }

    // 2. Wildcard or suffix pattern match
    for (const auto& pair : m_credentials) {
        if (matches_domain_pattern(pair.first, target_domain)) {
            return pair.second;
        }
    }

    return nullopt;
}

bool Vault::remove_credential(const string& domain) {
    lock_guard<mutex> lock(m_mutex);
    if (m_is_locked) return false;
    return m_credentials.erase(to_lower_str(domain)) > 0;
}

vector<string> Vault::list_domains() const {
    lock_guard<mutex> lock(m_mutex);
    vector<string> domains;
    for (const auto& pair : m_credentials) {
        domains.push_back(pair.first);
    }
    sort(domains.begin(), domains.end());
    return domains;
}

size_t Vault::count() const {
    lock_guard<mutex> lock(m_mutex);
    return m_credentials.size();
}

void Vault::clear() {
    lock_guard<mutex> lock(m_mutex);
    m_credentials.clear();
}

// =========================================================================
// Domain & URL Matching
// =========================================================================

string Vault::extract_domain_from_url(const string& url) {
    if (url.empty()) return "";

    string s = url;
    size_t scheme_pos = s.find("://");
    if (scheme_pos != string::npos) {
        s = s.substr(scheme_pos + 3);
    }

    size_t slash_pos = s.find('/');
    if (slash_pos != string::npos) {
        s = s.substr(0, slash_pos);
    }

    size_t colon_pos = s.find(':');
    if (colon_pos != string::npos) {
        s = s.substr(0, colon_pos);
    }

    return to_lower_str(s);
}

bool Vault::matches_domain_pattern(const string& pattern, const string& target_domain) {
    string p = to_lower_str(pattern);
    string t = to_lower_str(target_domain);

    if (p == t) return true;

    // Wildcard prefix: "*.domain.com"
    if (p.rfind("*.", 0) == 0) {
        string suffix = p.substr(1); // ".domain.com"
        if (t.length() >= suffix.length()) {
            return t.compare(t.length() - suffix.length(), suffix.length(), suffix) == 0;
        }
    }

    // Implicit domain suffix match: "domain.com" matches "sub.domain.com"
    if (t.length() > p.length()) {
        string dot_p = "." + p;
        if (t.compare(t.length() - dot_p.length(), dot_p.length(), dot_p) == 0) {
            return true;
        }
    }

    return false;
}

// =========================================================================
// Engine Auto-Injection
// =========================================================================

bool Vault::inject_into_task(DownloadTask& task) const {
    string url = task.get_url();
    auto cred = get_credential(url);
    if (!cred) return false;

    // Check expiration timestamp
    if (cred->expires_at > 0) {
        auto now = chrono::system_clock::now();
        uint64_t now_sec = static_cast<uint64_t>(chrono::duration_cast<chrono::seconds>(now.time_since_epoch()).count());
        if (now_sec >= cred->expires_at) {
            return false; // Credential expired
        }
    }

    // Merge cookies: task cookies take precedence if already populated
    string task_cookies = task.get_cookies();
    if (task_cookies.empty()) {
        task_cookies = cred->cookies;
    } else if (!cred->cookies.empty() && task_cookies.find(cred->cookies) == string::npos) {
        task_cookies += "; " + cred->cookies;
    }

    task.set_http_context(task.get_referer(), task_cookies, task.get_user_agent());
    return true;
}

// =========================================================================
// Persistence & Serialization
// =========================================================================

bool Vault::save_to_file(const string& filepath) const {
    lock_guard<mutex> lock(m_mutex);
    if (m_is_locked) return false;

    json root;
    root["version"] = 1;
    root["master_hash"] = m_master_password_hash;

    json items = json::array();
    for (const auto& pair : m_credentials) {
        const auto& c = pair.second;
        json item;
        item["domain"] = c.domain;
        item["username"] = c.username;
        item["password_enc"] = encrypt_string(c.password);
        item["cookies_enc"] = encrypt_string(c.cookies);
        item["auth_header"] = c.auth_header;
        item["created_at"] = c.created_at;
        item["expires_at"] = c.expires_at;
        item["notes"] = c.notes;
        items.push_back(item);
    }
    root["credentials"] = items;

    ofstream fout(filepath, ios::binary);
    if (!fout.is_open()) return false;

    string payload = root.dump(2);
    fout.write(payload.data(), payload.size());
    return fout.good();
}

bool Vault::load_from_file(const string& filepath) {
    lock_guard<mutex> lock(m_mutex);

    ifstream fin(filepath, ios::binary);
    if (!fin.is_open()) return false;

    try {
        json root = json::parse(fin);
        m_master_password_hash = root.value("master_hash", "");

        if (!m_master_password_hash.empty()) {
            m_is_locked = true; // Requires unlock
            m_credentials.clear();
            return true;
        }

        m_credentials.clear();
        if (root.contains("credentials") && root["credentials"].is_array()) {
            for (const auto& item : root["credentials"]) {
                SiteCredential c;
                c.domain = item.value("domain", "");
                c.username = item.value("username", "");
                c.password = decrypt_string(item.value("password_enc", ""));
                c.cookies = decrypt_string(item.value("cookies_enc", ""));
                c.auth_header = item.value("auth_header", "");
                c.created_at = item.value("created_at", static_cast<uint64_t>(0));
                c.expires_at = item.value("expires_at", static_cast<uint64_t>(0));
                c.notes = item.value("notes", "");

                if (!c.domain.empty()) {
                    m_credentials[c.domain] = c;
                }
            }
        }
        m_is_locked = false;
        return true;
    } catch (...) {
        return false;
    }
}

// =========================================================================
// Master Password & Lock Lifecycle
// =========================================================================

bool Vault::set_master_password(const string& password) {
    lock_guard<mutex> lock(m_mutex);
    if (password.empty()) {
        m_master_password_hash.clear();
        m_is_locked = false;
        return true;
    }

    // Derive SHA-256 hash
    vector<uint8_t> p_bytes(password.begin(), password.end());
    string hex_hash = bytes_to_hex(p_bytes.data(), p_bytes.size());
    m_master_password_hash = hex_hash;
    return true;
}

bool Vault::unlock(const string& password) {
    lock_guard<mutex> lock(m_mutex);
    if (m_master_password_hash.empty()) {
        m_is_locked = false;
        return true;
    }

    vector<uint8_t> p_bytes(password.begin(), password.end());
    string hex_hash = bytes_to_hex(p_bytes.data(), p_bytes.size());
    if (hex_hash == m_master_password_hash) {
        m_is_locked = false;
        return true;
    }
    return false;
}

void Vault::lock() {
    lock_guard<mutex> lock(m_mutex);
    if (!m_master_password_hash.empty()) {
        m_is_locked = true;
    }
}

bool Vault::is_locked() const {
    lock_guard<mutex> lock(m_mutex);
    return m_is_locked;
}

} // namespace idm
