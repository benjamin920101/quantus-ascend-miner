#pragma once

#include <cstdint>
#include <array>
#include <cstring>

// Quantus Poseidon2 over Goldilocks
// Spec: SPONGE_WIDTH=12, RATE=8, CAPACITY=4
// S-box: x^7
// Mining: hash_squeeze_twice(header_32 || nonce_64) -> 64 bytes

namespace poseidon2 {

constexpr size_t SPONGE_WIDTH = 12;
constexpr size_t SPONGE_RATE  = 8;
constexpr size_t INTERNAL_ROUNDS = 22;
constexpr size_t EXTERNAL_ROUNDS = 8;
constexpr size_t HALF_EXTERNAL  = 4;

// Goldilocks prime: p = 2^64 - 2^32 + 1
constexpr uint64_t GOLDILOCKS_P = 0xFFFFFFFF00000001ULL;

struct Goldilocks {
    uint64_t v;

    static constexpr Goldilocks ZERO() { return {0}; }
    static Goldilocks from_u64(uint64_t x) { return {x}; }

    uint64_t as_canonical() const {
        return (v >= GOLDILOCKS_P) ? (v - GOLDILOCKS_P) : v;
    }

    Goldilocks operator+(const Goldilocks& o) const {
        uint64_t r = v + o.v;
        // reduce if overflow or >= P
        uint64_t c = (r < v) ? 1 : 0; // carry
        r += c * (0xFFFFFFFFULL); // 2^32 - 1 adjustment for Goldilocks? use simple reduce
        // Standard Goldilocks add:
        // x + y, if x+y >= 2^64 then result = x+y - P = x+y + (2^32-1) mod 2^64 effectively handled differently
        // Simplified correct reduce:
        __uint128_t sum = (__uint128_t)v + o.v;
        if (sum >= GOLDILOCKS_P) sum -= GOLDILOCKS_P;
        if (sum >= GOLDILOCKS_P) sum -= GOLDILOCKS_P;
        return {static_cast<uint64_t>(sum)};
    }

    Goldilocks operator*(const Goldilocks& o) const {
        __uint128_t prod = (__uint128_t)v * o.v;
        // Reduce mod P using Goldilocks reduction
        // P = 2^64 - 2^32 + 1
        // For x = a*2^64 + b, x mod P = b + a*(2^32 - 1)  then reduce again
        uint64_t lo = static_cast<uint64_t>(prod);
        uint64_t hi = static_cast<uint64_t>(prod >> 64);
        // hi * (2^32 - 1) + lo
        __uint128_t t = (__uint128_t)hi * 0xFFFFFFFFULL + lo;
        uint64_t t_lo = static_cast<uint64_t>(t);
        uint64_t t_hi = static_cast<uint64_t>(t >> 64);
        t = (__uint128_t)t_hi * 0xFFFFFFFFULL + t_lo;
        if (t >= GOLDILOCKS_P) t -= GOLDILOCKS_P;
        if (t >= GOLDILOCKS_P) t -= GOLDILOCKS_P;
        return {static_cast<uint64_t>(t)};
    }

    Goldilocks& operator+=(const Goldilocks& o) { *this = *this + o; return *this; }
    Goldilocks& operator*=(const Goldilocks& o) { *this = *this * o; return *this; }

    Goldilocks square() const { return *this * *this; }
    Goldilocks double_() const { return *this + *this; }

    // x^7
    Goldilocks exp7() const {
        Goldilocks x2 = square();
        Goldilocks x3 = x2 * (*this);
        Goldilocks x4 = x2.square();
        return x3 * x4;
    }
};

// Round constants from qp-poseidon-core (seed 0x3141592653589793)
extern const uint64_t INTERNAL_CONSTANTS[INTERNAL_ROUNDS];
extern const uint64_t MATRIX_DIAG[SPONGE_WIDTH];
extern const uint64_t INITIAL_EXTERNAL_CONSTANTS[HALF_EXTERNAL][SPONGE_WIDTH];
extern const uint64_t TERMINAL_EXTERNAL_CONSTANTS[HALF_EXTERNAL][SPONGE_WIDTH];

void permute(Goldilocks state[SPONGE_WIDTH]);

// Mining PoW hash: input = header(32) || nonce(64) → 64-byte digest
void hash_squeeze_twice(const uint8_t* input, size_t len, uint8_t out[64]);

} // namespace poseidon2
