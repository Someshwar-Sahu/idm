#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "../src/security/Vault.h"
#include "../src/core/DownloadTask.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <chrono>

using namespace std;
using namespace idm;

void test_encryption_roundtrip() {
    cout << "\n[1/8] Testing Windows DPAPI & Cipher Round-Trip..." << endl;

    string sensitive_password = "SuperSecretP@ssw0rd!#2026";
    string encrypted = Vault::encrypt_string(sensitive_password);
    assert(!encrypted.empty());
    assert(encrypted != sensitive_password);

    string decrypted = Vault::decrypt_string(encrypted);
    assert(decrypted == sensitive_password);

    // Password-keyed cipher mode
    string keyed_enc = Vault::encrypt_string("CloudToken_ABC123", "CustomMasterKey");
    assert(keyed_enc.rfind("ENC:", 0) == 0);
    assert(Vault::decrypt_string(keyed_enc, "CustomMasterKey") == "CloudToken_ABC123");
    assert(Vault::decrypt_string(keyed_enc, "WrongKey") != "CloudToken_ABC123");

    cout << "  -> DPAPI and keyed cipher encryption/decryption: PASSED" << endl;
}

void test_site_credential_crud() {
    cout << "\n[2/8] Testing Site Credential CRUD (Save, Get, List, Remove)..." << endl;

    Vault vault;
    assert(vault.count() == 0);

    SiteCredential cred;
    cred.domain = "Rapidgator.net"; // Mixed case
    cred.username = "premium_user";
    cred.password = "rg_secret";
    cred.cookies = "sessionid=abc123xyz";

    assert(vault.save_credential(cred));
    assert(vault.count() == 1);

    // Case-insensitive retrieval
    auto fetched = vault.get_credential("rapidgator.net");
    assert(fetched.has_value());
    assert(fetched->domain == "rapidgator.net");
    assert(fetched->username == "premium_user");
    assert(fetched->cookies == "sessionid=abc123xyz");

    // List domains
    auto domains = vault.list_domains();
    assert(domains.size() == 1);
    assert(domains[0] == "rapidgator.net");

    // Remove
    assert(vault.remove_credential("rapidgator.net"));
    assert(vault.count() == 0);

    cout << "  -> Credential storage, retrieval, and removal: PASSED" << endl;
}

void test_domain_and_url_wildcard_matching() {
    cout << "\n[3/8] Testing Domain Extraction & Wildcard Matching..." << endl;

    // URL domain extractor
    assert(Vault::extract_domain_from_url("https://www.google.com/drive/file/123") == "www.google.com");
    assert(Vault::extract_domain_from_url("http://rapidgator.net:8080/download") == "rapidgator.net");
    assert(Vault::extract_domain_from_url("ftp://dl.1fichier.com/test.zip") == "dl.1fichier.com");

    // Wildcard pattern matching
    assert(Vault::matches_domain_pattern("*.1fichier.com", "dl.1fichier.com"));
    assert(Vault::matches_domain_pattern("*.1fichier.com", "sub.dl.1fichier.com"));
    assert(!Vault::matches_domain_pattern("*.1fichier.com", "otherdomain.com"));

    // Suffix domain matching
    assert(Vault::matches_domain_pattern("rapidgator.net", "premium.rapidgator.net"));
    assert(Vault::matches_domain_pattern("rapidgator.net", "rapidgator.net"));
    assert(!Vault::matches_domain_pattern("rapidgator.net", "fake-rapidgator.net"));

    // Vault retrieval using direct download URLs
    Vault vault;
    SiteCredential c;
    c.domain = "1fichier.com";
    c.cookies = "LG=en; session=xyz";
    vault.save_credential(c);

    auto match = vault.get_credential("https://a-4.1fichier.com/c123456?inline");
    assert(match.has_value());
    assert(match->domain == "1fichier.com");

    cout << "  -> URL host extraction, subdomains, and wildcard queries: PASSED" << endl;
}

void test_session_cookies_and_auth_headers() {
    cout << "\n[4/8] Testing Session Cookies & Auth Header Management..." << endl;

    Vault vault;
    SiteCredential c;
    c.domain = "api.drive.google.com";
    c.cookies = "SID=123; HSID=456; SSID=789";
    c.auth_header = "Bearer ya29.a0AfH6SM...";
    vault.save_credential(c);

    auto res = vault.get_credential("api.drive.google.com");
    assert(res.has_value());
    assert(res->cookies.find("SID=123") != string::npos);
    assert(res->auth_header.find("Bearer") != string::npos);

    cout << "  -> Multi-cookie strings and OAuth2 bearer tokens: PASSED" << endl;
}

void test_credential_expiry_handling() {
    cout << "\n[5/8] Testing Credential & Cookie Expiration Timestamps..." << endl;

    Vault vault;
    SiteCredential c;
    c.domain = "expired.com";
    c.cookies = "session=expired_token";
    c.expires_at = 1000; // Epoch from year 1970 -> Already expired

    vault.save_credential(c);

    DownloadTask task("https://expired.com/file.zip", "test.bin");
    bool injected = vault.inject_into_task(task);
    assert(!injected); // Must reject expired credentials
    assert(task.get_cookies().empty());

    // Valid future expiration
    auto now = chrono::system_clock::now();
    uint64_t future = static_cast<uint64_t>(chrono::duration_cast<chrono::seconds>(now.time_since_epoch()).count()) + 3600;
    c.domain = "valid.com";
    c.expires_at = future;
    vault.save_credential(c);

    DownloadTask task2("https://valid.com/file.zip", "test2.bin");
    assert(vault.inject_into_task(task2));
    assert(task2.get_cookies() == "session=expired_token");

    cout << "  -> Expiration timestamp evaluation and rejection: PASSED" << endl;
}

void test_download_task_context_injection() {
    cout << "\n[6/8] Testing Automatic Injection into DownloadTask..." << endl;

    Vault vault;
    SiteCredential c;
    c.domain = "mega.nz";
    c.cookies = "cf_clearance=cf123; user_session=mega99";
    vault.save_credential(c);

    DownloadTask task("https://mega.nz/file/sample#key", "sample.zip");
    assert(task.get_cookies().empty());

    bool injected = vault.inject_into_task(task);
    assert(injected);
    assert(task.get_cookies() == "cf_clearance=cf123; user_session=mega99");

    cout << "  -> Seamless injection of cookies into DownloadTask: PASSED" << endl;
}

void test_encrypted_vault_disk_persistence() {
    cout << "\n[7/8] Testing Encrypted Vault Disk File Persistence..." << endl;
    const string vault_file = "test_vault_storage.json";
    filesystem::remove(vault_file);

    {
        Vault v1;
        SiteCredential c1;
        c1.domain = "gdrive.com";
        c1.username = "alice@gmail.com";
        c1.password = "top_secret_gdrive";
        c1.cookies = "gdrive_session=112233";
        v1.save_credential(c1);

        assert(v1.save_to_file(vault_file));
    }

    // Verify raw file contents on disk are encrypted (password not stored in plaintext)
    ifstream fin(vault_file);
    string raw((istreambuf_iterator<char>(fin)), istreambuf_iterator<char>());
    fin.close();

    assert(raw.find("top_secret_gdrive") == string::npos); // Encrypted!
    assert(raw.find("112233") == string::npos);           // Encrypted!

    // Reload into a fresh vault instance
    {
        Vault v2;
        assert(v2.load_from_file(vault_file));
        assert(v2.count() == 1);

        auto cred = v2.get_credential("gdrive.com");
        assert(cred.has_value());
        assert(cred->username == "alice@gmail.com");
        assert(cred->password == "top_secret_gdrive");
        assert(cred->cookies == "gdrive_session=112233");
    }

    filesystem::remove(vault_file);
    cout << "  -> Encrypted persistence, plaintext leak prevention, and deserialization: PASSED" << endl;
}

void test_vault_master_password_and_lock() {
    cout << "\n[8/8] Testing Master Password Protection & Lock Lifecycle..." << endl;

    Vault vault;
    SiteCredential c;
    c.domain = "secure.com";
    c.password = "pass123";
    vault.save_credential(c);

    vault.set_master_password("MasterKey2026");
    vault.lock();
    assert(vault.is_locked());

    // When locked: credential access and modification must be rejected
    assert(!vault.get_credential("secure.com").has_value());
    assert(!vault.save_credential(c));
    assert(!vault.remove_credential("secure.com"));

    // Incorrect unlock
    assert(!vault.unlock("WrongPassword"));
    assert(vault.is_locked());

    // Correct unlock
    assert(vault.unlock("MasterKey2026"));
    assert(!vault.is_locked());

    auto cred = vault.get_credential("secure.com");
    assert(cred.has_value());
    assert(cred->password == "pass123");

    cout << "  -> Master password locking and unauthorized access guards: PASSED" << endl;
}

int main() {
    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: Site Logins Vault (Task 27)    " << endl;
    cout << "==========================================================" << endl;

    test_encryption_roundtrip();
    test_site_credential_crud();
    test_domain_and_url_wildcard_matching();
    test_session_cookies_and_auth_headers();
    test_credential_expiry_handling();
    test_download_task_context_injection();
    test_encrypted_vault_disk_persistence();
    test_vault_master_password_and_lock();

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF SITE LOGINS VAULT & COOKIES VERIFIED!   " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
