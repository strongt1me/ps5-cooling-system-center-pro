// ps5cc - The console app's data over HTTP (127.0.0.1:8086), on a thread of its own.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ps5cc/data.hpp"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>

struct cJSON;

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
    void watch(Watch page) override;
    void save_config(const std::string &patch) override;
    void selftest(bool start) override;
    void power(const std::string &action) override;
    void start_payload(const PayloadFile &file) override;
    void kill_payload(int pid) override;
    void store_action(const std::string &action, const std::string &id) override;
    void quit_app() override;
    std::uint64_t request_plan(Tool tool, const std::string &id) override;
    void start_tool(Tool tool, const std::string &id, const std::string &body) override;

    // The workers; public only for the thread entries.
    void run();
    // Plans and starts of the game tools: a plan measures a whole game and may take a minute, so it has a thread of
    // its own and the temperatures keep coming meanwhile.
    void run_tools();

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
            config,
            selftest,
            power,
            payload_start,
            payload_kill,
            store,
        } kind;
        int value = 0;
        std::string id;
        std::string text; // a JSON body, or the store's action
    };

    struct ToolRequest
    {
        bool start = false; // false: the plan
        Tool tool = Tool::copy;
        std::string id;
        std::string body;
        std::uint64_t ticket = 0;
    };

    void push(Request request);
    void handle_tool(const ToolRequest &request);
    void notice(Notice::Kind kind, std::string title, std::string body = {});
    void set_link(Link link, std::string detail);
    bool connect_phase();
    void handle(const Request &request);
    bool poll_status();
    void poll_library();
    void poll_jobs();
    void read_storage_job(const cJSON *root, Job &job);
    void read_delete_job(const cJSON *root, Job &job);
    void poll_history();
    void poll_config();
    void poll_selftest();
    void poll_payloads();
    void poll_store();
    void poll_log();
    void poll_klog();
    void fetch_cover();
    std::string game_name(const std::string &id);

    std::mutex mutex_;
    Snapshot data_;
    std::deque<Notice> notices_;
    std::deque<Request> requests_;
    std::deque<ToolRequest> tool_requests_;
    std::uint64_t next_ticket_ = 0;
    // ShadowMountPlus keeps reporting its last job for ever: one that ended is shown only when it ran in this session.
    std::atomic<std::int64_t> storage_seen_{0};
    std::atomic<bool> jobs_wanted_{false};    // a tool started: ask for the jobs at once
    bool leaving_for_delete_ = false;         // the app ends itself for a delete (worker thread only)
    std::atomic<bool> library_wanted_{false}; // a job ended: games may have moved or gone
    std::atomic<Watch> watching_{Watch::none};
    std::atomic<bool> config_wanted_{true};
    std::atomic<bool> retry_{false};
    bool online_ = false;
    bool payload_sent_ = false;
    std::int64_t starting_since_us_ = 0;
    int status_failures_ = 0;
    bool reported_silence_ = false;
};

} // namespace hui::ps5cc
