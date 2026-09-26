#include "stratum_client.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <regex>
#include <vector>

static std::string extract_string(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"\\s*:\\s*\"([^\"]*)\"";
    std::regex re(pattern);
    std::smatch m;
    if (std::regex_search(json, m, re)) return m[1].str();
    return "";
}

static std::string extract_string_flexible(const std::string& json, const std::vector<std::string>& keys) {
    for (const auto& k : keys) {
        auto v = extract_string(json, k);
        if (!v.empty()) return v;
    }
    return "";
}

StratumClient::StratumClient() = default;
StratumClient::~StratumClient() { disconnect(); }

bool StratumClient::connect(const std::string& url, const std::string& wallet, const std::string& worker) {
    std::string host = url;
    int port = 7049;
    if (host.find("stratum+tcp://") == 0) host = host.substr(14);
    else if (host.find("stratum+ssl://") == 0) host = host.substr(14);

    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        port = std::stoi(host.substr(colon + 1));
        host = host.substr(0, colon);
    }
    host_ = host; port_ = port; wallet_ = wallet; worker_ = worker;

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host_.c_str(), std::to_string(port_).c_str(), &hints, &res) != 0) {
        std::cerr << "[Stratum] DNS failed: " << host_ << std::endl;
        return false;
    }
    sock_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock_ < 0) { freeaddrinfo(res); return false; }
    if (::connect(sock_, res->ai_addr, res->ai_addrlen) < 0) {
        std::cerr << "[Stratum] connect failed " << host_ << ":" << port_ << std::endl;
        close(sock_); sock_ = -1; freeaddrinfo(res); return false;
    }
    freeaddrinfo(res);

    connected_ = true;
    stop_ = false;
    reader_thread_ = std::thread(&StratumClient::reader_loop, this);

    // subscribe
    send_line(R"({"id":1,"method":"mining.subscribe","params":["quantus-ascend/0.2.0"]})");
    // authorize
    std::ostringstream auth;
    auth << R"({"id":2,"method":"mining.authorize","params":[")"
         << wallet_ << "." << worker_ << R"(", "x"]})";
    send_line(auth.str());

    std::cout << "[Stratum] Connected " << host_ << ":" << port_
              << " as " << wallet_ << "." << worker_ << std::endl;
    return true;
}

void StratumClient::disconnect() {
    stop_ = true;
    connected_ = false;
    if (sock_ >= 0) { shutdown(sock_, SHUT_RDWR); close(sock_); sock_ = -1; }
    if (reader_thread_.joinable()) reader_thread_.join();
}

bool StratumClient::is_connected() const { return connected_; }

void StratumClient::set_job_callback(JobCallback cb) { job_cb_ = std::move(cb); }
void StratumClient::set_share_result_callback(ShareResultCallback cb) { share_cb_ = std::move(cb); }

bool StratumClient::submit_share(const Share& share) {
    if (!connected_) return false;
    // Quantus / LuckyPool-style submit: [user, job_id, nonce]
    // Also try classic stratum: [user, job_id, extranonce2, ntime, nonce]
    std::ostringstream oss;
    oss << R"({"id":)" << (next_id_++)
        << R"(,"method":"mining.submit","params":[")"
        << wallet_ << "." << worker_ << R"(",")"
        << share.job_id << R"(",")"
        << share.nonce << R"("]})";
    return send_line(oss.str());
}

MiningJob StratumClient::get_current_job() const {
    std::lock_guard<std::mutex> lock(job_mutex_);
    return current_job_;
}

bool StratumClient::send_line(const std::string& line) {
    std::lock_guard<std::mutex> lock(send_mutex_);
    if (sock_ < 0) return false;
    std::string msg = line + "\n";
    ssize_t n = send(sock_, msg.c_str(), msg.size(), 0);
    return n == static_cast<ssize_t>(msg.size());
}

void StratumClient::reader_loop() {
    char buf[8192];
    std::string residual;
    while (!stop_ && connected_) {
        ssize_t n = recv(sock_, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            connected_ = false;
            std::cerr << "[Stratum] disconnected\n";
            break;
        }
        buf[n] = 0;
        residual += buf;
        size_t pos;
        while ((pos = residual.find('\n')) != std::string::npos) {
            std::string line = residual.substr(0, pos);
            residual.erase(0, pos + 1);
            if (!line.empty()) handle_message(line);
        }
    }
}

void StratumClient::handle_message(const std::string& line) {
    // New job (mining.notify or method "job")
    if (line.find("mining.notify") != std::string::npos ||
        line.find("\"method\":\"job\"") != std::string::npos ||
        line.find("\"method\": \"job\"") != std::string::npos) {

        MiningJob job;
        job.job_id = extract_string_flexible(line, {"job_id", "jobId"});
        std::string mh = extract_string_flexible(line, {"mining_hash", "miningHash", "header", "prevhash", "prev_hash"});
        std::string tgt = extract_string_flexible(line, {"target", "Target"});
        std::string diff_s = extract_string_flexible(line, {"difficulty", "diff"});

        if (!mh.empty() && mh.size() == 64) {
            from_hex(mh, job.mining_hash.data(), HEADER_HASH_BYTES);
        }
        if (!tgt.empty() && tgt.size() == 128) {
            from_hex(tgt, job.target.data(), DIGEST_BYTES);
            job.valid = true;
        } else if (!diff_s.empty()) {
            // fallback: compute target = MAX / difficulty (simplified, only low difficulty useful)
            try {
                uint64_t d = std::stoull(diff_s);
                job.difficulty = d;
                // For demo we set a weak target so shares can be found for testing
                // Real pool will send proper 64-byte target
                std::memset(job.target.data(), 0xFF, DIGEST_BYTES);
                if (d > 0) {
                    // rough: put difficulty into high bytes
                    job.target[0] = 0x00;
                    job.target[1] = 0x00;
                }
                job.valid = true;
            } catch (...) {}
        }

        // try parse params array style (classic stratum)
        // params: [job_id, ..., clean]
        if (job.job_id.empty()) {
            std::regex re("\"params\"\\s*:\\s*\\[\\s*\"([^\"]+)\"");
            std::smatch m;
            if (std::regex_search(line, m, re)) job.job_id = m[1].str();
        }

        if (!job.job_id.empty()) {
            {
                std::lock_guard<std::mutex> lock(job_mutex_);
                current_job_ = job;
            }
            std::cout << "[Stratum] New job " << job.job_id
                      << " valid=" << job.valid << std::endl;
            if (job_cb_) job_cb_(job);
        }
    }
    // set_difficulty
    else if (line.find("mining.set_difficulty") != std::string::npos) {
        // optional
    }
    // submit result
    else if (line.find("\"result\":true") != std::string::npos ||
             line.find("\"result\": true") != std::string::npos ||
             line.find("\"status\":\"OK\"") != std::string::npos) {
        if (share_cb_) share_cb_(true, "accepted");
    }
    else if (line.find("\"error\"") != std::string::npos && line.find("null") == std::string::npos) {
        if (share_cb_) share_cb_(false, line);
    }
}
