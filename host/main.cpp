#include "stratum_client.h"
#include "poseidon2.h"
#include "common.h"

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <cstring>
#include <atomic>

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " --pool <url> --wallet <addr> [--worker <name>]\n"
              << "Example:\n"
              << "  " << prog << " --pool stratum+tcp://qtc.kryptex.network:7049 \\\n"
              << "      --wallet qzYourAddress --worker ascend-01\n\n"
              << "This build uses CPU Poseidon2 (hash_squeeze_twice) and will submit\n"
              << "shares that meet the pool target. Ascend NPU path is still experimental.\n";
}

int main(int argc, char** argv) {
    std::string pool = "stratum+tcp://qtc.kryptex.network:7049";
    std::string wallet, worker = "cpu-worker";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--pool" && i + 1 < argc) pool = argv[++i];
        else if (a == "--wallet" && i + 1 < argc) wallet = argv[++i];
        else if (a == "--worker" && i + 1 < argc) worker = argv[++i];
        else if (a == "--help" || a == "-h") { print_usage(argv[0]); return 0; }
    }
    if (wallet.empty()) {
        std::cerr << "--wallet required\n";
        print_usage(argv[0]);
        return 1;
    }

    std::cout << "============================================\n"
              << " Quantus QPoW Miner v0.2 (CPU Poseidon2)\n"
              << "============================================\n"
              << "Pool   : " << pool << "\n"
              << "Wallet : " << wallet << "\n"
              << "Worker : " << worker << "\n"
              << "Hash   : Poseidon2 hash_squeeze_twice (Goldilocks)\n"
              << "============================================\n";

    StratumClient client;
    MinerStats stats;

    client.set_job_callback([](const MiningJob& j) {
        std::cout << "[Job] id=" << j.job_id << " target[0..3]="
                  << to_hex(j.target.data(), 4) << "...\n";
    });
    client.set_share_result_callback([&](bool ok, const std::string& msg) {
        if (ok) {
            stats.shares_accepted++;
            std::cout << "[Share] ACCEPTED\n";
        } else {
            stats.shares_rejected++;
            std::cout << "[Share] REJECTED " << msg << "\n";
        }
    });

    if (!client.connect(pool, wallet, worker)) {
        std::cerr << "Connect failed\n";
        return 1;
    }

    std::mt19937_64 rng{std::random_device{}()};
    auto last_report = std::chrono::steady_clock::now();
    uint64_t hashes_window = 0;

    // Continuous mining loop
    while (stats.running && client.is_connected()) {
        MiningJob job = client.get_current_job();
        if (!job.valid || job.job_id.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // 512-bit nonce, random start then increment
        std::array<uint8_t, NONCE_BYTES> nonce{};
        for (size_t i = 0; i < NONCE_BYTES; ++i) nonce[i] = static_cast<uint8_t>(rng() & 0xFF);

        constexpr int BATCH = 4096;
        for (int i = 0; i < BATCH; ++i) {
            // input = header || nonce
            uint8_t input[INPUT_BYTES];
            std::memcpy(input, job.mining_hash.data(), HEADER_HASH_BYTES);
            std::memcpy(input + HEADER_HASH_BYTES, nonce.data(), NONCE_BYTES);

            uint8_t digest[DIGEST_BYTES];
            poseidon2::hash_squeeze_twice(input, INPUT_BYTES, digest);
            hashes_window++;
            stats.hashes++;

            // valid share if digest < target (big-endian compare)
            // Note: official uses U512 big-endian interpretation
            if (be_less(digest, job.target.data(), DIGEST_BYTES)) {
                Share s;
                s.job_id = job.job_id;
                s.nonce = to_hex(nonce.data(), NONCE_BYTES);
                s.extranonce2 = "";
                s.ntime = "";
                std::cout << "[Share] Found! nonce=" << s.nonce.substr(0, 16) << "...\n";
                if (client.submit_share(s)) {
                    stats.shares_submitted++;
                }
            }

            // increment nonce (little-endian style)
            for (int b = 0; b < (int)NONCE_BYTES; ++b) {
                if (++nonce[b] != 0) break;
            }
        }

        auto now = std::chrono::steady_clock::now();
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(now - last_report).count();
        if (sec >= 10) {
            double hs = hashes_window / (double)sec;
            std::cout << "[Stats] " << hs << " H/s | submitted=" << stats.shares_submitted
                      << " A=" << stats.shares_accepted << " R=" << stats.shares_rejected << "\n";
            hashes_window = 0;
            last_report = now;
        }
    }

    client.disconnect();
    return 0;
}
