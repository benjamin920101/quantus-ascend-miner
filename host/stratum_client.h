#pragma once

#include "common.h"
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

class StratumClient {
public:
    using JobCallback = std::function<void(const MiningJob&)>;
    using ShareResultCallback = std::function<void(bool accepted, const std::string& msg)>;

    StratumClient();
    ~StratumClient();

    bool connect(const std::string& url, const std::string& wallet, const std::string& worker);
    void disconnect();
    bool is_connected() const;

    void set_job_callback(JobCallback cb);
    void set_share_result_callback(ShareResultCallback cb);

    bool submit_share(const Share& share);
    MiningJob get_current_job() const;

private:
    void reader_loop();
    void handle_message(const std::string& line);
    bool send_line(const std::string& line);

    std::string host_;
    int port_ = 7049;
    std::string wallet_;
    std::string worker_;
    int sock_ = -1;

    std::atomic<bool> connected_{false};
    std::atomic<bool> stop_{false};
    std::thread reader_thread_;

    mutable std::mutex job_mutex_;
    MiningJob current_job_;

    JobCallback job_cb_;
    ShareResultCallback share_cb_;
    int next_id_ = 10;
    std::mutex send_mutex_;
};
