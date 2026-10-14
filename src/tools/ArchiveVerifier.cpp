#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "ArchiveVerifier.h"
#include "../security/AntivirusScanner.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <cctype>
#include <iomanip>

using namespace std;

namespace idm {

ArchiveVerifier::ClipboardReader ArchiveVerifier::s_mock_clipboard = nullptr;
ArchiveVerifier::ProcessRunner ArchiveVerifier::s_mock_runner = nullptr;

namespace {

// =========================================================================
// RFC 1321 MD5 Implementation (Self-Contained)
// =========================================================================
struct MD5Context {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t buffer[64];
};

#define MD5_F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | (~z)))
#define MD5_ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define MD5_FF(a, b, c, d, x, s, ac) { \
    (a) += MD5_F((b), (c), (d)) + (x) + static_cast<uint32_t>(ac); \
    (a) = MD5_ROTL((a), (s)); \
    (a) += (b); \
}
#define MD5_GG(a, b, c, d, x, s, ac) { \
    (a) += MD5_G((b), (c), (d)) + (x) + static_cast<uint32_t>(ac); \
    (a) = MD5_ROTL((a), (s)); \
    (a) += (b); \
}
#define MD5_HH(a, b, c, d, x, s, ac) { \
    (a) += MD5_H((b), (c), (d)) + (x) + static_cast<uint32_t>(ac); \
    (a) = MD5_ROTL((a), (s)); \
    (a) += (b); \
}
#define MD5_II(a, b, c, d, x, s, ac) { \
    (a) += MD5_I((b), (c), (d)) + (x) + static_cast<uint32_t>(ac); \
    (a) = MD5_ROTL((a), (s)); \
    (a) += (b); \
}

void md5_transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], x[16];
    for (int i = 0, j = 0; i < 16; ++i, j += 4) {
        x[i] = static_cast<uint32_t>(block[j]) |
               (static_cast<uint32_t>(block[j + 1]) << 8) |
               (static_cast<uint32_t>(block[j + 2]) << 16) |
               (static_cast<uint32_t>(block[j + 3]) << 24);
    }

    // Round 1
    MD5_FF(a, b, c, d, x[ 0],  7, 0xd76aa478);
    MD5_FF(d, a, b, c, x[ 1], 12, 0xe8c7b756);
    MD5_FF(c, d, a, b, x[ 2], 17, 0x242070db);
    MD5_FF(b, c, d, a, x[ 3], 22, 0xc1bdceee);
    MD5_FF(a, b, c, d, x[ 4],  7, 0xf57c0faf);
    MD5_FF(d, a, b, c, x[ 5], 12, 0x4787c62a);
    MD5_FF(c, d, a, b, x[ 6], 17, 0xa8304613);
    MD5_FF(b, c, d, a, x[ 7], 22, 0xfd469501);
    MD5_FF(a, b, c, d, x[ 8],  7, 0x698098d8);
    MD5_FF(d, a, b, c, x[ 9], 12, 0x8b44f7af);
    MD5_FF(c, d, a, b, x[10], 17, 0xffff5bb1);
    MD5_FF(b, c, d, a, x[11], 22, 0x895cd7be);
    MD5_FF(a, b, c, d, x[12],  7, 0x6b901122);
    MD5_FF(d, a, b, c, x[13], 12, 0xfd987193);
    MD5_FF(c, d, a, b, x[14], 17, 0xa679438e);
    MD5_FF(b, c, d, a, x[15], 22, 0x49b40821);

    // Round 2
    MD5_GG(a, b, c, d, x[ 1],  5, 0xf61e2562);
    MD5_GG(d, a, b, c, x[ 6],  9, 0xc040b340);
    MD5_GG(c, d, a, b, x[11], 14, 0x265e5a51);
    MD5_GG(b, c, d, a, x[ 0], 20, 0xe9b6c7aa);
    MD5_GG(a, b, c, d, x[ 5],  5, 0xd62f105d);
    MD5_GG(d, a, b, c, x[10],  9, 0x02441453);
    MD5_GG(c, d, a, b, x[15], 14, 0xd8a1e681);
    MD5_GG(b, c, d, a, x[ 4], 20, 0xe7d3fbc8);
    MD5_GG(a, b, c, d, x[ 9],  5, 0x21e1cde6);
    MD5_GG(d, a, b, c, x[14],  9, 0xc33707d6);
    MD5_GG(c, d, a, b, x[ 3], 14, 0xf4d50d87);
    MD5_GG(b, c, d, a, x[ 8], 20, 0x455a14ed);
    MD5_GG(a, b, c, d, x[13],  5, 0xa9e3e905);
    MD5_GG(d, a, b, c, x[ 2],  9, 0xfcefa3f8);
    MD5_GG(c, d, a, b, x[ 7], 14, 0x676f02d9);
    MD5_GG(b, c, d, a, x[12], 20, 0x8d2a4c8a);

    // Round 3
    MD5_HH(a, b, c, d, x[ 5],  4, 0xfffa3942);
    MD5_HH(d, a, b, c, x[ 8], 11, 0x8771f681);
    MD5_HH(c, d, a, b, x[11], 16, 0x6d9d6122);
    MD5_HH(b, c, d, a, x[14], 23, 0xfde5380c);
    MD5_HH(a, b, c, d, x[ 1],  4, 0xa4beea44);
    MD5_HH(d, a, b, c, x[ 4], 11, 0x4bdecfa9);
    MD5_HH(c, d, a, b, x[ 7], 16, 0xf6bb4b60);
    MD5_HH(b, c, d, a, x[10], 23, 0xbebfbc70);
    MD5_HH(a, b, c, d, x[13],  4, 0x289b7ec6);
    MD5_HH(d, a, b, c, x[ 0], 11, 0xeaa127fa);
    MD5_HH(c, d, a, b, x[ 3], 16, 0xd4ef3085);
    MD5_HH(b, c, d, a, x[ 6], 23, 0x04881d05);
    MD5_HH(a, b, c, d, x[ 9],  4, 0xd9d4d039);
    MD5_HH(d, a, b, c, x[12], 11, 0xe6db99e5);
    MD5_HH(c, d, a, b, x[15], 16, 0x1fa27cf8);
    MD5_HH(b, c, d, a, x[ 2], 23, 0xc4ac5665);

    // Round 4
    MD5_II(a, b, c, d, x[ 0],  6, 0xf4292244);
    MD5_II(d, a, b, c, x[ 7], 10, 0x432aff97);
    MD5_II(c, d, a, b, x[14], 15, 0xab9423a7);
    MD5_II(b, c, d, a, x[ 5], 21, 0xfc93a039);
    MD5_II(a, b, c, d, x[12],  6, 0x655b59c3);
    MD5_II(d, a, b, c, x[ 3], 10, 0x8f0ccc92);
    MD5_II(c, d, a, b, x[10], 15, 0xffeff47d);
    MD5_II(b, c, d, a, x[ 1], 21, 0x85845dd1);
    MD5_II(a, b, c, d, x[ 8],  6, 0x6fa87e4f);
    MD5_II(d, a, b, c, x[15], 10, 0xfe2ce6e0);
    MD5_II(c, d, a, b, x[ 6], 15, 0xa3014314);
    MD5_II(b, c, d, a, x[13], 21, 0x4e0811a1);
    MD5_II(a, b, c, d, x[ 4],  6, 0xf7537e82);
    MD5_II(d, a, b, c, x[11], 10, 0xbd3af235);
    MD5_II(c, d, a, b, x[ 2], 15, 0x2ad7d2bb);
    MD5_II(b, c, d, a, x[ 9], 21, 0xeb86d391);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

void md5_init(MD5Context& ctx) {
    ctx.count[0] = ctx.count[1] = 0;
    ctx.state[0] = 0x67452301;
    ctx.state[1] = 0xefcdab89;
    ctx.state[2] = 0x98badcfe;
    ctx.state[3] = 0x10325476;
}

void md5_update(MD5Context& ctx, const uint8_t* input, size_t inputLen) {
    size_t index = (ctx.count[0] >> 3) & 0x3F;
    if ((ctx.count[0] += static_cast<uint32_t>(inputLen << 3)) < static_cast<uint32_t>(inputLen << 3)) {
        ctx.count[1]++;
    }
    ctx.count[1] += static_cast<uint32_t>(inputLen >> 29);

    size_t partLen = 64 - index;
    size_t i = 0;

    if (inputLen >= partLen) {
        memcpy(&ctx.buffer[index], input, partLen);
        md5_transform(ctx.state, ctx.buffer);
        for (i = partLen; i + 63 < inputLen; i += 64) {
            md5_transform(ctx.state, &input[i]);
        }
        index = 0;
    }
    memcpy(&ctx.buffer[index], &input[i], inputLen - i);
}

void md5_final(MD5Context& ctx, uint8_t digest[16]) {
    static const uint8_t PADDING[64] = { 0x80 };
    uint8_t bits[8];
    for (int i = 0; i < 4; ++i) {
        bits[i] = static_cast<uint8_t>((ctx.count[0] >> (i * 8)) & 0xFF);
        bits[i + 4] = static_cast<uint8_t>((ctx.count[1] >> (i * 8)) & 0xFF);
    }
    size_t index = (ctx.count[0] >> 3) & 0x3F;
    size_t padLen = (index < 56) ? (56 - index) : (120 - index);
    md5_update(ctx, PADDING, padLen);
    md5_update(ctx, bits, 8);

    for (int i = 0; i < 4; ++i) {
        digest[i]      = static_cast<uint8_t>((ctx.state[0] >> (i * 8)) & 0xFF);
        digest[i + 4]  = static_cast<uint8_t>((ctx.state[1] >> (i * 8)) & 0xFF);
        digest[i + 8]  = static_cast<uint8_t>((ctx.state[2] >> (i * 8)) & 0xFF);
        digest[i + 12] = static_cast<uint8_t>((ctx.state[3] >> (i * 8)) & 0xFF);
    }
}

// =========================================================================
// RFC 3174 SHA-1 Implementation (Self-Contained)
// =========================================================================
struct SHA1Context {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
};

#define SHA1_ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

void sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    uint32_t w[80];

    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(buffer[i * 4]) << 24) |
               (static_cast<uint32_t>(buffer[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(buffer[i * 4 + 2]) << 8) |
               static_cast<uint32_t>(buffer[i * 4 + 3]);
    }
    for (int i = 16; i < 80; ++i) {
        w[i] = SHA1_ROTL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t temp = SHA1_ROTL(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = SHA1_ROTL(b, 30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void sha1_init(SHA1Context& ctx) {
    ctx.state[0] = 0x67452301;
    ctx.state[1] = 0xEFCDAB89;
    ctx.state[2] = 0x98BADCFE;
    ctx.state[3] = 0x10325476;
    ctx.state[4] = 0xC3D2E1F0;
    ctx.count[0] = ctx.count[1] = 0;
}

void sha1_update(SHA1Context& ctx, const uint8_t* data, size_t len) {
    size_t i = 0;
    size_t j = (ctx.count[0] >> 3) & 63;
    if ((ctx.count[0] += static_cast<uint32_t>(len << 3)) < static_cast<uint32_t>(len << 3)) {
        ctx.count[1]++;
    }
    ctx.count[1] += static_cast<uint32_t>(len >> 29);

    if ((j + len) > 63) {
        memcpy(&ctx.buffer[j], data, (i = 64 - j));
        sha1_transform(ctx.state, ctx.buffer);
        for (; i + 63 < len; i += 64) {
            sha1_transform(ctx.state, &data[i]);
        }
        j = 0;
    }
    memcpy(&ctx.buffer[j], &data[i], len - i);
}

void sha1_final(SHA1Context& ctx, uint8_t digest[20]) {
    uint8_t finalcount[8];
    for (int i = 0; i < 8; ++i) {
        finalcount[i] = static_cast<uint8_t>((ctx.count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255);
    }
    uint8_t c = 0x80;
    sha1_update(ctx, &c, 1);
    while ((ctx.count[0] & 504) != 448) {
        c = 0x00;
        sha1_update(ctx, &c, 1);
    }
    sha1_update(ctx, finalcount, 8);
    for (int i = 0; i < 20; ++i) {
        digest[i] = static_cast<uint8_t>((ctx.state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    }
}

string to_hex_string(const uint8_t* data, size_t len) {
    ostringstream oss;
    oss << hex << setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

string to_lower_str(string s) {
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

// =========================================================================
// Archive Signature & Type Detection
// =========================================================================

ArchiveType ArchiveVerifier::detect_archive_type_from_buffer(const uint8_t* data, size_t size) {
    if (!data || size < 4) return ArchiveType::Unknown;

    // 1. ZIP: "PK\x03\x04", "PK\x05\x06", or "PK\x07\x08"
    if (data[0] == 'P' && data[1] == 'K') {
        if ((data[2] == 0x03 && data[3] == 0x04) ||
            (data[2] == 0x05 && data[3] == 0x06) ||
            (data[2] == 0x07 && data[3] == 0x08)) {
            return ArchiveType::Zip;
        }
    }

    // 2. 7-Zip: "7z\xBC\xAF\x27\x1C" (6 bytes)
    if (size >= 6) {
        static const uint8_t seven_zip_sig[6] = { 0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C };
        if (memcmp(data, seven_zip_sig, 6) == 0) {
            return ArchiveType::SevenZip;
        }
    }

    // 3. RAR: "Rar!\x1A\x07\x00" (RAR4) or "Rar!\x1A\x07\x01\x00" (RAR5)
    if (size >= 7 && memcmp(data, "Rar!\x1A\x07\x00", 7) == 0) {
        return ArchiveType::Rar;
    }
    if (size >= 8 && memcmp(data, "Rar!\x1A\x07\x01\x00", 8) == 0) {
        return ArchiveType::Rar;
    }

    // 4. GZip: "\x1F\x8B"
    if (size >= 2 && data[0] == 0x1F && data[1] == 0x8B) {
        return ArchiveType::Gz;
    }

    // 5. BZip2: "BZh"
    if (size >= 3 && data[0] == 'B' && data[1] == 'Z' && data[2] == 'h') {
        return ArchiveType::Bz2;
    }

    // 6. TAR: "ustar" magic at byte 257 (512-byte header block)
    if (size >= 262 && memcmp(data + 257, "ustar", 5) == 0) {
        return ArchiveType::Tar;
    }

    return ArchiveType::Unknown;
}

ArchiveType ArchiveVerifier::detect_archive_type(const string& filepath) {
    if (filepath.empty()) return ArchiveType::Unknown;

    // Check header magic first
    ifstream file(filepath, ios::binary);
    if (file.is_open()) {
        vector<uint8_t> buffer(512, 0);
        file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        streamsize bytes_read = file.gcount();
        file.close();

        if (bytes_read > 0) {
            ArchiveType t = detect_archive_type_from_buffer(buffer.data(), static_cast<size_t>(bytes_read));
            if (t != ArchiveType::Unknown) return t;
        }
    }

    // Fallback to file extension
    string ext;
    size_t dot_pos = filepath.rfind('.');
    if (dot_pos != string::npos) {
        ext = to_lower_str(filepath.substr(dot_pos + 1));
    }

    if (ext == "zip" || ext == "jar" || ext == "apk") return ArchiveType::Zip;
    if (ext == "rar") return ArchiveType::Rar;
    if (ext == "7z") return ArchiveType::SevenZip;
    if (ext == "tar") return ArchiveType::Tar;
    if (ext == "gz" || ext == "tgz") return ArchiveType::Gz;
    if (ext == "bz2" || ext == "tbz2") return ArchiveType::Bz2;

    return ArchiveType::Unknown;
}

bool ArchiveVerifier::is_supported_archive(const string& filepath) {
    return detect_archive_type(filepath) != ArchiveType::Unknown;
}

string ArchiveVerifier::archive_type_to_string(ArchiveType type) {
    switch (type) {
        case ArchiveType::Zip: return "ZIP";
        case ArchiveType::Rar: return "RAR";
        case ArchiveType::SevenZip: return "7-Zip";
        case ArchiveType::Tar: return "TAR";
        case ArchiveType::Gz: return "GZip";
        case ArchiveType::Bz2: return "BZip2";
        case ArchiveType::Unknown: default: return "Unknown";
    }
}

// =========================================================================
// Cryptographic Checksum Engine
// =========================================================================

string ArchiveVerifier::calculate_buffer_md5(const uint8_t* data, size_t size) {
    MD5Context ctx;
    md5_init(ctx);
    if (data && size > 0) {
        md5_update(ctx, data, size);
    }
    uint8_t digest[16];
    md5_final(ctx, digest);
    return to_hex_string(digest, 16);
}

string ArchiveVerifier::calculate_file_md5(const string& filepath) {
    ifstream file(filepath, ios::binary);
    if (!file.is_open()) return "";

    MD5Context ctx;
    md5_init(ctx);

    vector<uint8_t> buffer(64 * 1024); // 64 KB streaming buffer
    while (file.read(reinterpret_cast<char*>(buffer.data()), buffer.size()) || file.gcount() > 0) {
        md5_update(ctx, buffer.data(), static_cast<size_t>(file.gcount()));
    }

    uint8_t digest[16];
    md5_final(ctx, digest);
    return to_hex_string(digest, 16);
}

string ArchiveVerifier::calculate_buffer_sha1(const uint8_t* data, size_t size) {
    SHA1Context ctx;
    sha1_init(ctx);
    if (data && size > 0) {
        sha1_update(ctx, data, size);
    }
    uint8_t digest[20];
    sha1_final(ctx, digest);
    return to_hex_string(digest, 20);
}

string ArchiveVerifier::calculate_file_sha1(const string& filepath) {
    ifstream file(filepath, ios::binary);
    if (!file.is_open()) return "";

    SHA1Context ctx;
    sha1_init(ctx);

    vector<uint8_t> buffer(64 * 1024);
    while (file.read(reinterpret_cast<char*>(buffer.data()), buffer.size()) || file.gcount() > 0) {
        sha1_update(ctx, buffer.data(), static_cast<size_t>(file.gcount()));
    }

    uint8_t digest[20];
    sha1_final(ctx, digest);
    return to_hex_string(digest, 20);
}

string ArchiveVerifier::calculate_file_sha256(const string& filepath) {
    return AntivirusScanner::calculate_file_sha256(filepath);
}

// =========================================================================
// Hash Extraction & Verification
// =========================================================================

HashAlgorithm ArchiveVerifier::detect_hash_algorithm(const string& hash_str) {
    string clean;
    for (char c : hash_str) {
        if (isxdigit(static_cast<unsigned char>(c))) clean += static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    if (clean.length() == 32) return HashAlgorithm::MD5;
    if (clean.length() == 40) return HashAlgorithm::SHA1;
    if (clean.length() == 64) return HashAlgorithm::SHA256;
    return HashAlgorithm::Unknown;
}

optional<pair<HashAlgorithm, string>> ArchiveVerifier::extract_hash_from_text(const string& text) {
    if (text.empty()) return nullopt;

    // Split text into tokens by whitespace, commas, colons, equals
    string token;
    vector<string> candidates;

    for (size_t i = 0; i <= text.length(); ++i) {
        char ch = (i < text.length()) ? text[i] : ' ';
        if (isxdigit(static_cast<unsigned char>(ch))) {
            token += static_cast<char>(tolower(static_cast<unsigned char>(ch)));
        } else {
            if (!token.empty()) {
                candidates.push_back(token);
                token.clear();
            }
        }
    }

    // Prioritize stronger hashes (SHA-256 > SHA-1 > MD5)
    for (const auto& c : candidates) {
        if (c.length() == 64) return make_pair(HashAlgorithm::SHA256, c);
    }
    for (const auto& c : candidates) {
        if (c.length() == 40) return make_pair(HashAlgorithm::SHA1, c);
    }
    for (const auto& c : candidates) {
        if (c.length() == 32) return make_pair(HashAlgorithm::MD5, c);
    }

    return nullopt;
}

ChecksumResult ArchiveVerifier::verify_file_checksum(const string& filepath, const string& expected_hash) {
    ChecksumResult result;
    result.verified = false;

    auto extracted = extract_hash_from_text(expected_hash);
    if (!extracted) {
        result.details = "No valid hex checksum (MD5/SHA1/SHA256) detected in input string";
        return result;
    }

    result.algorithm = extracted->first;
    result.expected_hash = extracted->second;

    if (!filesystem::exists(filepath)) {
        result.details = "Target file does not exist on disk";
        return result;
    }

    switch (result.algorithm) {
        case HashAlgorithm::MD5:
            result.actual_hash = calculate_file_md5(filepath);
            break;
        case HashAlgorithm::SHA1:
            result.actual_hash = calculate_file_sha1(filepath);
            break;
        case HashAlgorithm::SHA256:
            result.actual_hash = calculate_file_sha256(filepath);
            break;
        default:
            result.details = "Unsupported hash algorithm";
            return result;
    }

    result.verified = (to_lower_str(result.actual_hash) == to_lower_str(result.expected_hash));
    if (result.verified) {
        string algo_name = (result.algorithm == HashAlgorithm::SHA256 ? "SHA-256" :
                            result.algorithm == HashAlgorithm::SHA1 ? "SHA-1" : "MD5");
        result.details = "Checksum matched successfully (" + algo_name + ")";
    } else {
        result.details = "Checksum mismatch: expected " + result.expected_hash + ", got " + result.actual_hash;
    }

    return result;
}

ChecksumResult ArchiveVerifier::verify_against_clipboard(const string& filepath, ClipboardReader custom_reader) {
    string clip_text;
    if (custom_reader) {
        clip_text = custom_reader();
    } else if (s_mock_clipboard) {
        clip_text = s_mock_clipboard();
    } else {
        clip_text = read_system_clipboard();
    }

    return verify_file_checksum(filepath, clip_text);
}

// =========================================================================
// Extraction Engine & Security
// =========================================================================

string ArchiveVerifier::generate_destination_folder(const string& archive_path,
                                                   const string& output_base_dir,
                                                   bool create_subfolder) {
    filesystem::path p(archive_path);
    filesystem::path base = output_base_dir.empty() ? p.parent_path() : filesystem::path(output_base_dir);

    if (!create_subfolder) {
        return base.string();
    }

    string stem = p.stem().string();
    // Strip secondary extension if e.g. .tar.gz
    size_t second_dot = stem.rfind('.');
    if (second_dot != string::npos && stem.substr(second_dot) == ".tar") {
        stem = stem.substr(0, second_dot);
    }

    return (base / stem).string();
}

bool ArchiveVerifier::is_safe_extract_path(const string& base_dest_dir, const string& relative_entry_path) {
    if (relative_entry_path.empty()) return false;

    // Reject absolute paths
    if (relative_entry_path[0] == '/' || relative_entry_path[0] == '\\' ||
        (relative_entry_path.length() >= 2 && relative_entry_path[1] == ':')) {
        return false;
    }

    // Reject Zip-Slip traversal sequences
    string norm = relative_entry_path;
    replace(norm.begin(), norm.end(), '\\', '/');
    if (norm.find("../") != string::npos || norm.find("/..") != string::npos || norm == "..") {
        return false;
    }

    error_code ec;
    filesystem::path base = filesystem::weakly_canonical(base_dest_dir, ec);
    filesystem::path target = filesystem::weakly_canonical(filesystem::path(base_dest_dir) / relative_entry_path, ec);

    string base_str = base.string();
    string target_str = target.string();
    return target_str.rfind(base_str, 0) == 0;
}

string ArchiveVerifier::build_extraction_command(ArchiveType type,
                                                const string& archive_path,
                                                const string& dest_dir,
                                                const ExtractionOptions& options) {
    string tool = options.custom_tool_path;

#ifdef _WIN32
    // Windows 10/11 includes native bsdtar at C:\Windows\System32\tar.exe
    // handles .zip, .tar, .gz, .bz2 with zero third-party dependencies!
    if (tool.empty()) {
        if (type == ArchiveType::Zip || type == ArchiveType::Tar ||
            type == ArchiveType::Gz || type == ArchiveType::Bz2) {
            tool = "tar.exe";
        } else if (type == ArchiveType::SevenZip || type == ArchiveType::Rar) {
            tool = "7z.exe";
        }
    }

    if (tool == "tar.exe" || tool.find("tar") != string::npos) {
        return "tar.exe -xf \"" + archive_path + "\" -C \"" + dest_dir + "\"";
    }

    if (tool == "7z.exe" || tool.find("7z") != string::npos) {
        string ov = options.overwrite_existing ? " -y " : " ";
        return "7z.exe x" + ov + "-o\"" + dest_dir + "\" \"" + archive_path + "\"";
    }

    // PowerShell Expand-Archive fallback for ZIP
    if (type == ArchiveType::Zip) {
        string ov = options.overwrite_existing ? " -Force" : "";
        return "powershell.exe -NoProfile -NonInteractive -Command \"Expand-Archive -Path '" +
               archive_path + "' -DestinationPath '" + dest_dir + "'" + ov + "\"";
    }

    return "tar.exe -xf \"" + archive_path + "\" -C \"" + dest_dir + "\"";
#else
    if (tool.empty()) {
        if (type == ArchiveType::Zip) tool = "unzip";
        else if (type == ArchiveType::SevenZip) tool = "7z";
        else if (type == ArchiveType::Rar) tool = "unrar";
        else tool = "tar";
    }

    if (tool == "unzip") {
        string ov = options.overwrite_existing ? "-o " : "";
        return "unzip " + ov + "\"" + archive_path + "\" -d \"" + dest_dir + "\"";
    }
    if (tool == "7z") {
        string ov = options.overwrite_existing ? "-y " : "";
        return "7z x " + ov + "-o\"" + dest_dir + "\" \"" + archive_path + "\"";
    }
    return "tar -xf \"" + archive_path + "\" -C \"" + dest_dir + "\"";
#endif
}

bool ArchiveVerifier::extract_archive(const string& archive_path,
                                     const string& output_dir,
                                     const ExtractionOptions& options,
                                     ProcessRunner custom_runner,
                                     string* out_log) {
    if (!filesystem::exists(archive_path)) {
        if (out_log) *out_log = "Archive file not found: " + archive_path;
        return false;
    }

    ArchiveType type = detect_archive_type(archive_path);
    if (type == ArchiveType::Unknown) {
        if (out_log) *out_log = "Unrecognized or unsupported archive format: " + archive_path;
        return false;
    }

    string dest = generate_destination_folder(archive_path, output_dir, options.create_subfolder);
    error_code ec;
    filesystem::create_directories(dest, ec);
    if (ec) {
        if (out_log) *out_log = "Failed to create target directory: " + dest + " (" + ec.message() + ")";
        return false;
    }

    string cmd = build_extraction_command(type, archive_path, dest, options);

    ProcessRunner runner = custom_runner ? custom_runner : (s_mock_runner ? s_mock_runner : run_system_command);
    string cmd_output;
    bool success = runner(cmd, cmd_output);

    if (out_log) *out_log = cmd_output;

    if (success && options.delete_archive_on_success) {
        filesystem::remove(archive_path, ec);
    }

    return success;
}

// =========================================================================
// System Utilities (Clipboard & Subprocess)
// =========================================================================

string ArchiveVerifier::read_system_clipboard() {
    if (s_mock_clipboard) return s_mock_clipboard();

#ifdef _WIN32
    if (!OpenClipboard(nullptr)) return "";

    HANDLE hData = GetClipboardData(CF_TEXT);
    if (!hData) {
        CloseClipboard();
        return "";
    }

    char* pszText = static_cast<char*>(GlobalLock(hData));
    string text = pszText ? string(pszText) : "";
    GlobalUnlock(hData);
    CloseClipboard();
    return text;
#else
    return "";
#endif
}

void ArchiveVerifier::set_mock_clipboard_reader(ClipboardReader reader) {
    s_mock_clipboard = reader;
}

bool ArchiveVerifier::run_system_command(const string& cmd, string& output) {
    if (s_mock_runner) return s_mock_runner(cmd, output);

#ifdef _WIN32
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    HANDLE hStdOutRead = nullptr;
    HANDLE hStdOutWrite = nullptr;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    if (CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0)) {
        SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = hStdOutWrite;
        si.hStdError = hStdOutWrite;
    }

    vector<char> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back('\0');

    BOOL created = CreateProcessA(
        nullptr,
        cmd_buf.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (hStdOutWrite) CloseHandle(hStdOutWrite);

    if (!created) {
        if (hStdOutRead) CloseHandle(hStdOutRead);
        output = "Failed to execute command: " + cmd;
        return false;
    }

    char buf[1024];
    DWORD bytesRead = 0;
    output.clear();
    while (ReadFile(hStdOutRead, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        buf[bytesRead] = '\0';
        output += buf;
    }

    WaitForSingleObject(pi.hProcess, 30000); // 30 sec timeout
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (hStdOutRead) CloseHandle(hStdOutRead);

    return (exit_code == 0);
#else
    array<char, 128> buffer;
    output.clear();
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return false;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }
    int status = pclose(pipe);
    return (status == 0);
#endif
}

void ArchiveVerifier::set_mock_process_runner(ProcessRunner runner) {
    s_mock_runner = runner;
}

} // namespace idm
