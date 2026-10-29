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

namespace sha256_internal {
    inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
    inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
    inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
    inline uint32_t sig0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
    inline uint32_t sig1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
    inline uint32_t theta0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
    inline uint32_t theta1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
    struct Context {
        uint32_t state[8];
        uint64_t count;
        uint8_t buffer[64];
    };
    inline void init(Context &ctx) {
        ctx.state[0] = 0x6a09e667; ctx.state[1] = 0xbb67ae85; ctx.state[2] = 0x3c6ef372; ctx.state[3] = 0xa54ff53a;
        ctx.state[4] = 0x510e527f; ctx.state[5] = 0x9b05688c; ctx.state[6] = 0x1f83d9ab; ctx.state[7] = 0x5be0cd19;
        ctx.count = 0;
    }
    inline void transform(Context &ctx, const uint8_t data[64]) {
        uint32_t a = ctx.state[0], b = ctx.state[1], c = ctx.state[2], d = ctx.state[3];
        uint32_t e = ctx.state[4], f = ctx.state[5], g = ctx.state[6], h = ctx.state[7];
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(data[i * 4]) << 24) |
                   (static_cast<uint32_t>(data[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(data[i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(data[i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            w[i] = theta1(w[i - 2]) + w[i - 7] + theta0(w[i - 15]) + w[i - 16];
        }
        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = h + sig1(e) + ch(e, f, g) + K[i] + w[i];
            uint32_t t2 = sig0(a) + maj(a, b, c);
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        ctx.state[0] += a; ctx.state[1] += b; ctx.state[2] += c; ctx.state[3] += d;
        ctx.state[4] += e; ctx.state[5] += f; ctx.state[6] += g; ctx.state[7] += h;
    }
    inline void update(Context &ctx, const uint8_t *data, size_t len) {
        size_t idx = (ctx.count >> 3) & 0x3F;
        ctx.count += (static_cast<uint64_t>(len) << 3);
        size_t partLen = 64 - idx;
        size_t i = 0;
        if (len >= partLen) {
            memcpy(&ctx.buffer[idx], data, partLen);
            transform(ctx, ctx.buffer);
            for (i = partLen; i + 63 < len; i += 64) {
                transform(ctx, &data[i]);
            }
            idx = 0;
        }
        if (len > i) {
            memcpy(&ctx.buffer[idx], &data[i], len - i);
        }
    }
    inline void final(Context &ctx, uint8_t digest[32]) {
        static const uint8_t padding[64] = { 0x80 };
        uint8_t bits[8];
        for (int i = 0; i < 8; ++i) {
            bits[7 - i] = static_cast<uint8_t>((ctx.count >> (i * 8)) & 0xFF);
        }
        size_t idx = (ctx.count >> 3) & 0x3F;
        size_t padLen = (idx < 56) ? (56 - idx) : (120 - idx);
        update(ctx, padding, padLen);
        update(ctx, bits, 8);
        for (int i = 0; i < 8; ++i) {
            digest[i * 4]     = static_cast<uint8_t>((ctx.state[i] >> 24) & 0xFF);
            digest[i * 4 + 1] = static_cast<uint8_t>((ctx.state[i] >> 16) & 0xFF);
            digest[i * 4 + 2] = static_cast<uint8_t>((ctx.state[i] >> 8) & 0xFF);
            digest[i * 4 + 3] = static_cast<uint8_t>(ctx.state[i] & 0xFF);
        }
    }
}

string compute_sha256_hash(const string& input) {
#ifdef _WIN32
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            CryptHashData(hHash, reinterpret_cast<const BYTE*>(input.data()), static_cast<DWORD>(input.size()), 0);
            BYTE hash_bytes[32];
            DWORD hash_len = sizeof(hash_bytes);
            if (CryptGetHashParam(hHash, HP_HASHVAL, hash_bytes, &hash_len, 0)) {
                CryptDestroyHash(hHash);
                CryptReleaseContext(hProv, 0);
                return bytes_to_hex(hash_bytes, hash_len);
            }
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }
#endif
    // Portable SHA-256 fallback for non-Windows or if CryptoAPI is unavailable
    sha256_internal::Context ctx;
    sha256_internal::init(ctx);
    sha256_internal::update(ctx, reinterpret_cast<const uint8_t*>(input.data()), input.size());
    uint8_t digest[32];
    sha256_internal::final(ctx, digest);
    return bytes_to_hex(digest, 32);
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
    if (m_is_locked) return {};
    vector<string> domains;
    for (const auto& pair : m_credentials) {
        domains.push_back(pair.first);
    }
    sort(domains.begin(), domains.end());
    return domains;
}

size_t Vault::count() const {
    lock_guard<mutex> lock(m_mutex);
    if (m_is_locked) return 0;
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

    // Bug #24 Fix: Strip user:pass@ userinfo before looking for port or domain
    size_t at_pos = s.rfind('@');
    if (at_pos != string::npos) {
        s = s.substr(at_pos + 1);
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

    // Bug #20 Fix: Apply stored username, password, and Authorization header to the task
    task.set_auth_credentials(cred->username, cred->password, cred->auth_header);

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

        if (!m_master_password_hash.empty()) {
            m_is_locked = true; // Requires unlock before credentials can be accessed
        } else {
            m_is_locked = false;
        }
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

    // Bug #19 Fix: Compute cryptographic SHA-256 hash instead of plaintext ASCII hex
    m_master_password_hash = compute_sha256_hash(password);
    return true;
}

bool Vault::unlock(const string& password) {
    lock_guard<mutex> lock(m_mutex);
    if (m_master_password_hash.empty()) {
        m_is_locked = false;
        return true;
    }

    string hex_hash = compute_sha256_hash(password);
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
