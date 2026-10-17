#ifndef VAULT_H
#define VAULT_H

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <cstdint>

class DownloadTask;

namespace idm {

struct SiteCredential {
    std::string domain;         // e.g. "rapidgator.net" or "*.1fichier.com"
    std::string username;       // User / Account ID
    std::string password;       // Plaintext in memory, DPAPI encrypted on disk
    std::string cookies;        // Cookie string "name=val; name2=val2"
    std::string auth_header;    // e.g. "Bearer ..." or "Basic ..."
    uint64_t created_at{0};     // Epoch seconds
    uint64_t expires_at{0};     // 0 = never expires
    std::string notes;
};

class Vault {
public:
    Vault() = default;
    ~Vault() = default;

    Vault(const Vault&) = delete;
    Vault& operator=(const Vault&) = delete;

    // 1. Credential Management
    bool save_credential(const SiteCredential& credential);
    std::optional<SiteCredential> get_credential(const std::string& domain_or_url) const;
    bool remove_credential(const std::string& domain);
    std::vector<std::string> list_domains() const;
    size_t count() const;
    void clear();

    // 2. Domain & URL Matching
    static std::string extract_domain_from_url(const std::string& url);
    static bool matches_domain_pattern(const std::string& pattern, const std::string& target_domain);

    // 3. Engine Auto-Injection
    bool inject_into_task(DownloadTask& task) const;

    // 4. Persistence & Encryption
    bool save_to_file(const std::string& filepath) const;
    bool load_from_file(const std::string& filepath);

    // 5. Vault Master Password & Lock Lifecycle
    bool set_master_password(const std::string& password);
    bool unlock(const std::string& password);
    void lock();
    bool is_locked() const;

    // 6. Cryptographic Primitives (Public for verification)
    static std::string encrypt_string(const std::string& plaintext, const std::string& key = "");
    static std::string decrypt_string(const std::string& ciphertext_hex, const std::string& key = "");

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, SiteCredential> m_credentials; // domain -> credential
    std::string m_master_password_hash;
    bool m_is_locked{false};
};

} // namespace idm

#endif // VAULT_H
