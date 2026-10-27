// ps5cc - The console app's data over HTTP (127.0.0.1:8086), on a thread of its own.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ps5cc/data.hpp"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>

namespace hui::ps5cc
{

class HttpSource final : public Source
{
  public:
    // Starts the worker thread (1 MiB stack). Call once.
    void start();

    bool fetch(Snapshot &out, std::uint64_t &seen) override;
    bool poll_notice(Notice &out) override;
    void set_target(int celsius) override;
    void launch(const std::string &id) override;
    void close_game(const std::string &id) override;
    void cancel_job(JobKind kind) override;
    void open_web_ui() override;
    void reload() override;
    void retry() override;
    void watch_jobs(bool watching) override;
    void quit_app() override;

    // The worker; public only for the thread entry.
    void run();

  private:
    struct Request
    {
        enum class Kind
        {
            target,
            launch,
            close,
            cancel,
            web,
            reload,
        } kind;
        int value = 0;
        std::string id;
    };

    void push(Request request);
    void notice(Notice::Kind kind, std::string title, std::string body = {});
    void set_link(Link link, std::string detail);
    bool connect_phase();
    void handle(const Request &request);
    bool poll_status();
    void poll_library();
    void poll_jobs();
    void poll_history();
    std::string game_name(const std::string &id);

    std::mutex mutex_;
    Snapshot data_;
    std::deque<Notice> notices_;
    std::deque<Request> requests_;
    std::atomic<bool> watching_{false};
    std::atomic<bool> retry_{false};
    bool online_ = false;
    bool payload_sent_ = false;
    std::int64_t starting_since_us_ = 0;
    int status_failures_ = 0;
    bool reported_silence_ = false;
};

} // namespace hui::ps5cc
