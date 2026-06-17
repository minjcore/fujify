// fujify_jobs.h — producer/single-consumer job queue over the engine daemon.
#pragma once
#include "fujify_engine.h"

// ---- job queue (producer / single consumer) -----------------------------
// The UI thread is the producer (push Task); one consumer thread pops and processes
// serially — matching the daemon, which handles one request at a time. Previews are
// coalesced by sequence number so dragging sliders never backs up a stale queue.

struct Task {
    enum Kind { Preview, Export, SaveTarget, Upload, Auth, LibList, LibGet } kind = Preview;
    std::string input;
    bool  use_temp = false, wb_auto = false;
    float temp = 0, tint = 0, br = 0, co = 0, sh = 0, hi = 0;
    int   preset = 0;
    bool  all = false; int fmt_idx = 0, tier_idx = 0; bool brand = true;  // export-only
    int   target_kb = 500;                                                // save-target-only
    float crop[4] = {0.f, 0.f, 1.f, 1.f};                                 // normalized crop rect
    int   rotate = 0;                                                     // CW degrees 0/90/180/270
    std::string token;                                                    // upload/library
    std::string email, password; bool signup = false;                     // auth-only
    std::string lib_key;                                                  // library_get-only
    uint64_t seq = 0;                                                     // preview coalescing
};

struct Result {
    Task::Kind kind = Task::Preview;
    std::string token, email;   // auth-only: set on successful login/signup
    std::string names, keys;    // library_list: '|'-joined
    std::string path;           // library_get: local file downloaded
    bool ok = false, reload_texture = false;
    int  ms = 0;
    std::string log;
};

class JobSystem {
public:
    explicit JobSystem(Daemon* d) : dmn(d), consumer([this] { loop(); }) {}
    ~JobSystem() {
        { std::lock_guard<std::mutex> lk(qmtx); stop = true; }
        cv.notify_all();
        if (consumer.joinable()) consumer.join();
    }
    void submit(Task t) {
        if (t.kind == Task::Preview) t.seq = ++preview_seq;  // newest preview wins
        { std::lock_guard<std::mutex> lk(qmtx); q.push(std::move(t)); }
        cv.notify_one();
    }
    bool poll(Result& out) {                                  // UI thread drains results
        std::lock_guard<std::mutex> lk(rmtx);
        if (results.empty()) return false;
        out = std::move(results.front()); results.pop();
        return true;
    }
    int  pending() { std::lock_guard<std::mutex> lk(qmtx); return (int)q.size() + (active ? 1 : 0); }
    bool busy() { return pending() > 0; }

private:
    void loop() {
        for (;;) {
            Task t;
            {
                std::unique_lock<std::mutex> lk(qmtx);
                cv.wait(lk, [this] { return stop || !q.empty(); });
                if (stop && q.empty()) return;
                t = std::move(q.front()); q.pop(); active = true;
            }
            process(t);
            active = false;
        }
    }
    void process(const Task& t) {
        // drop a preview that a newer one has already superseded
        if (t.kind == Task::Preview && t.seq < preview_seq.load()) return;
        Result r; r.kind = t.kind;
        if (t.kind == Task::Preview) {
            std::string req = build_json("preview", t.input, kPreviewPath, kProxyMaxDim,
                t.use_temp, t.temp, t.tint, t.wb_auto, t.br, t.co, t.sh, t.hi, kPresetArg[t.preset], 0, t.crop, t.rotate);
            EngineResult er = engine_run(*dmn, req);
            r.ok = er.ok; r.ms = er.elapsed_ms; r.reload_texture = er.ok;
            r.log = er.ok ? ("OK — engine " + std::to_string(er.elapsed_ms) + " ms (proxy)")
                          : ("Loi engine:\n" + er.log);
        } else if (t.kind == Task::SaveTarget) {        // save full-res at ~target KB
            std::string out = path_dir(t.input) + "/" + path_stem(t.input) +
                              "_" + std::to_string(t.target_kb) + "kb.jpg";
            EngineResult er = engine_run(*dmn, build_json("save_target", t.input, out.c_str(), 0,
                t.use_temp, t.temp, t.tint, t.wb_auto, t.br, t.co, t.sh, t.hi, kPresetArg[t.preset], t.target_kb, t.crop, t.rotate));
            r.ok = er.ok;
            r.log = er.ok ? ("Saved → " + out + "\n" + er.log) : ("Loi save:\n" + er.log);
        } else if (t.kind == Task::Upload) {            // render full-res → PUT to cloud
            EngineResult rf = engine_run(*dmn, build_json("full", t.input, kFullPath, 0,
                t.use_temp, t.temp, t.tint, t.wb_auto, t.br, t.co, t.sh, t.hi, kPresetArg[t.preset], 0, t.crop, t.rotate));
            if (!rf.ok) { r.ok = false; r.log = "Loi render:\n" + rf.log; }
            else {
                EngineResult er = engine_run(*dmn, build_upload_json(kFullPath, path_stem(t.input) + ".jpg", t.token));
                r.ok = er.ok;
                r.log = er.ok ? ("Uploaded ☁ " + json_str_field(er.log.c_str(), "url"))
                              : ("Loi upload:\n" + er.log);
            }
        } else if (t.kind == Task::Auth) {              // signup / login → token
            EngineResult er = engine_run(*dmn,
                build_auth_json(t.signup ? "auth_signup" : "auth_login", t.email, t.password));
            r.ok = er.ok;
            if (er.ok) {
                r.token = json_str_field(er.log.c_str(), "token");
                r.email = json_str_field(er.log.c_str(), "email");
                r.log = "Logged in: " + r.email;
            } else r.log = "Auth: " + er.log;
        } else if (t.kind == Task::LibList) {           // list user's cloud library
            EngineResult er = engine_run(*dmn, build_liblist_json(t.token));
            r.ok = er.ok;
            if (er.ok) { r.names = json_str_field(er.log.c_str(), "names");
                         r.keys = json_str_field(er.log.c_str(), "keys"); r.log = "Library loaded"; }
            else r.log = "Library: " + er.log;
        } else if (t.kind == Task::LibGet) {            // download one library file
            std::string base = path_stem(t.lib_key);
            std::string out = std::string("/tmp/fuji_lib_") + base + ".jpg";
            EngineResult er = engine_run(*dmn, build_libget_json(t.lib_key, out));
            r.ok = er.ok;
            r.path = er.ok ? out : "";
            r.log = er.ok ? ("Downloaded " + base) : ("Download: " + er.log);
        } else if (is_video(t.input)) {                 // video → ffmpeg look export
            std::string out = path_dir(t.input) + "/" + path_stem(t.input) + "_fujify.mp4";
            EngineResult er = engine_run(*dmn, build_json("video_export", t.input, out.c_str(), 0,
                t.use_temp, t.temp, t.tint, t.wb_auto, t.br, t.co, t.sh, t.hi, kPresetArg[t.preset]));
            r.ok = er.ok;
            r.log = er.ok ? ("Exported video (" + std::to_string(er.elapsed_ms) + " ms)\n- " + out)
                          : ("Loi video export:\n" + er.log);
        } else {
            std::string fullreq = build_json("full", t.input, kFullPath, 0,
                t.use_temp, t.temp, t.tint, t.wb_auto, t.br, t.co, t.sh, t.hi, kPresetArg[t.preset], 0, t.crop, t.rotate);
            EngineResult rf = engine_run(*dmn, fullreq);
            std::string oks, errs; int n_ok = 0;
            if (!rf.ok) { errs = "\nfull-res: " + rf.log; }
            else {
                std::string dir = path_dir(t.input), stem = path_stem(t.input);
                int lo = t.all ? 0 : t.fmt_idx, hi = t.all ? 3 : t.fmt_idx + 1;
                for (int i = lo; i < hi; ++i) {
                    std::string out = dir + "/" + stem + "_" + kFormats[i] + "_" + kTiers[t.tier_idx] + ".jpg";
                    EngineResult er = engine_run(*dmn, build_social_json(kFullPath, out, kFormats[i], kTiers[t.tier_idx], t.brand));
                    if (er.ok) { n_ok++; oks += "\n- " + out; }
                    else errs += std::string("\n[") + kFormats[i] + "] " + er.log;
                }
            }
            char hdr[80];
            std::snprintf(hdr, sizeof(hdr), "Exported %d format (tier %s):", n_ok, kTiers[t.tier_idx]);
            r.ok = errs.empty();
            r.log = std::string(hdr) + oks + (errs.empty() ? "" : ("\nLoi:" + errs));
        }
        { std::lock_guard<std::mutex> lk(rmtx); results.push(std::move(r)); }
    }

    Daemon* dmn;
    std::queue<Task> q;
    std::queue<Result> results;
    std::mutex qmtx, rmtx;
    std::condition_variable cv;
    std::atomic<bool> stop{false};
    std::atomic<bool> active{false};
    std::atomic<uint64_t> preview_seq{0};
    std::thread consumer;   // declared last → constructed after everything above
};
