#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <atomic>
#include <mutex>
#include <cstring>

constexpr size_t HEADER_HASH_BYTES = 32;
constexpr size_t NONCE_BYTES       = 64;   // 512-bit nonce
constexpr size_t DIGEST_BYTES      = 64;   // Poseidon2 squeeze twice
constexpr size_t INPUT_BYTES       = HEADER_HASH_BYTES + NONCE_BYTES; // 96

struct MiningJob {
    std::string job_id;
    std::array<uint8_t, HEADER_HASH_BYTES> mining_hash{}; // pre-seal header hash
    std::array<uint8_t, DIGEST_BYTES> target{};           // 512-bit target (big-endian)
    uint64_t difficulty = 0;
    std::string extranonce1;   // hex
    int extranonce2_size = 4;
    bool clean_jobs = false;
    bool valid = false;
};

struct Share {
    std::string job_id;
    std::string extranonce2;   // hex
    std::string ntime;         // may be empty for Quantus
    std::string nonce;         // 128 hex chars (64 bytes)
};

struct MinerStats {
    std::atomic<uint64_t> hashes{0};
    std::atomic<uint64_t> shares_submitted{0};
    std::atomic<uint64_t> shares_accepted{0};
    std::atomic<uint64_t> shares_rejected{0};
    std::atomic<bool> running{true};
};

// helpers
inline std::string to_hex(const uint8_t* data, size_t len) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.resize(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out[i * 2]     = hex[data[i] >> 4];
        out[i * 2 + 1] = hex[data[i] & 0x0F];
    }
    return out;
}

inline bool from_hex(const std::string& hex, uint8_t* out, size_t out_len) {
    if (hex.size() != out_len * 2) return false;
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < out_len; ++i) {
        int hi = val(hex[i * 2]);
        int lo = val(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

// big-endian compare: return true if a < b (both len bytes)
inline bool be_less(const uint8_t* a, const uint8_t* b, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (a[i] < b[i]) return true;
        if (a[i] > b[i]) return false;
    }
    return false; // equal
}
