// fujify_engine.h — backend-agnostic core shared by the OpenGL and Vulkan builds.
// Engine daemon (cached RAW decode + proxy), JSON protocol, worker thread, fonts.
// No GL/Vulkan/stb dependencies here — only ImGui (fonts), stdio and POSIX.
#pragma once

#include "imgui.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <memory>
#include <utility>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <mach-o/dyld.h>

#ifndef FUJIFY_ROOT
#define FUJIFY_ROOT "."   // overridden by Makefile (-DFUJIFY_ROOT=...)
#endif

static const char* kPreviewPath = "/tmp/fuji_studio_preview.jpg";  // proxy (display)
static const char* kFullPath    = "/tmp/fuji_studio_full.jpg";     // full-res (export source)
static const char* kPayloadPath = "/tmp/fuji_studio_payload.json";
static const int   kProxyMaxDim = 1600;

// ---- engine call --------------------------------------------------------

struct EngineResult {
    bool ok = false;
    std::string log;      // status line / captured output from the engine
    std::string code;     // defined error code on failure (BAD_REQUEST, DECODE_FAILED, ...)
    int elapsed_ms = 0;
};

// Extract the value of a "key":"value" string field from a one-line JSON response.
static inline std::string json_str_field(const char* line, const char* key) {
    std::string pat = std::string("\"") + key + "\":";
    const char* p = std::strstr(line, pat.c_str());
    if (!p) return "";
    p += pat.size();
    while (*p == ' ') ++p;
    if (*p != '"') return "";
    ++p;
    const char* e = std::strchr(p, '"');
    return e ? std::string(p, e - p) : "";
}

// Persistent Python engine process: decode RAW once, cache a downscaled proxy in RAM,
// and re-apply WB/tone per request without re-spawning the interpreter. One request
// (JSON) per line on stdin, one status line on stdout. Only the worker thread talks to it.
struct Daemon {
    pid_t pid = -1;
    FILE* in_f = nullptr;    // -> child stdin
    FILE* out_f = nullptr;   // <- child stdout
    bool ok() const { return pid > 0 && in_f && out_f; }
};

static inline Daemon start_daemon() {
    Daemon d;
    int inp[2], outp[2];                 // inp: parent->child stdin ; outp: child stdout->parent
    if (pipe(inp) != 0) return d;
    if (pipe(outp) != 0) { close(inp[0]); close(inp[1]); return d; }
    pid_t pid = fork();
    if (pid < 0) { close(inp[0]); close(inp[1]); close(outp[0]); close(outp[1]); return d; }
    if (pid == 0) {                      // child
        dup2(inp[0], STDIN_FILENO);
        dup2(outp[1], STDOUT_FILENO);
        close(inp[0]); close(inp[1]); close(outp[0]); close(outp[1]);
        const char* eng = getenv("FUJIFY_ENGINE");   // bundled standalone engine (PyInstaller)
        if (eng && eng[0]) {
            execl(eng, eng, (char*)nullptr);         // serve loop on stdin/stdout
        } else {                                     // dev: source tree + system python3
            if (chdir(FUJIFY_ROOT) == 0)
                execlp("python3", "python3", "-u", "imgui-poc/engine/preview_server.py", (char*)nullptr);
        }
        _exit(127);                      // exec failed
    }
    close(inp[0]); close(outp[1]);       // parent keeps write-end of inp, read-end of outp
    d.pid = pid;
    d.in_f = fdopen(inp[1], "w");
    d.out_f = fdopen(outp[0], "r");
    return d;
}

static inline void stop_daemon(Daemon& d) {
    if (d.in_f) { std::fclose(d.in_f); d.in_f = nullptr; }  // EOF on child stdin -> it exits
    if (d.out_f) { std::fclose(d.out_f); d.out_f = nullptr; }
    if (d.pid > 0) { int st; waitpid(d.pid, &st, 0); d.pid = -1; }
}

// Build a one-line JSON request. `core.api` ignores the extra mode/max_dim keys, so the
// same payload works for the fallback path (which always processes full-res).
static inline std::string build_json(const char* mode, const std::string& in, const char* out, int max_dim,
                                     bool use_temp, float temp, float tint, bool wb_auto,
                                     float br, float co, float sh, float hi, const char* preset) {
    std::string presetField;
    if (preset && preset[0]) presetField = std::string("\"preset\":\"") + preset + "\",";
    char buf[2048];
    std::snprintf(buf, sizeof(buf),
        "{\"mode\":\"%s\",\"input_path\":\"%s\",\"output_path\":\"%s\",\"max_dim\":%d,\"quality\":90,%s"
        "\"settings\":{\"temp\":%s,\"tint\":%.3f,\"wb_auto\":%s,\"brightness\":%.3f,"
        "\"contrast\":%.3f,\"shadows\":%.3f,\"highlights\":%.3f}}",
        mode, in.c_str(), out, max_dim, presetField.c_str(),
        use_temp ? std::to_string((int)temp).c_str() : "null",
        tint, wb_auto ? "true" : "false", br, co, sh, hi);
    return buf;
}

// Send a request to the daemon and read its one-line JSON response.
static inline EngineResult daemon_request(Daemon& d, const std::string& json_line) {
    EngineResult res;
    if (!d.ok()) { res.log = "daemon down"; return res; }
    std::fputs(json_line.c_str(), d.in_f);
    std::fputc('\n', d.in_f);
    std::fflush(d.in_f);
    char line[4096];
    if (!std::fgets(line, sizeof(line), d.out_f)) { res.log = "daemon: no response"; return res; }
    res.ok = std::strstr(line, "\"ok\": true") || std::strstr(line, "\"ok\":true");
    if (const char* m = std::strstr(line, "\"ms\":")) res.elapsed_ms = std::atoi(m + 5);
    if (res.ok) {
        res.log = line;
    } else {
        res.code = json_str_field(line, "code");
        std::string msg = json_str_field(line, "error");
        res.log = res.code.empty() ? msg : ("[" + res.code + "] " + msg);
    }
    return res;
}

// Run via daemon; on failure fall back to a one-shot `python3 -m core.api` (full-res).
static inline EngineResult engine_run(Daemon& d, const std::string& json_line) {
    EngineResult r = daemon_request(d, json_line);
    if (r.ok) return r;
    if (FILE* pf = std::fopen(kPayloadPath, "w")) {
        std::fputs(json_line.c_str(), pf); std::fclose(pf);
    }
    std::string cmd = std::string("cd '") + FUJIFY_ROOT +
        "' && python3 -m core.api '" + kPayloadPath + "' 2>&1";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) { return r; }
    std::string log; char line[1024];
    while (std::fgets(line, sizeof(line), p)) {
        log += line;
        if (const char* e = std::strstr(line, "\"elapsed_ms\":")) r.elapsed_ms = std::atoi(e + 13);
    }
    r.ok = (pclose(p) == 0);
    if (!r.ok) r.log = "engine fallback failed:\n" + log;
    return r;
}

// Social export request (rendered by the engine's "social" mode — no extra process).
static inline std::string build_social_json(const std::string& src, const std::string& out,
                                            const char* fmt, const char* tier, bool brand) {
    char buf[2048];
    std::snprintf(buf, sizeof(buf),
        "{\"mode\":\"social\",\"input_path\":\"%s\",\"output_path\":\"%s\","
        "\"social_format\":\"%s\",\"tier\":\"%s\",\"brand\":%s}",
        src.c_str(), out.c_str(), fmt, tier, brand ? "true" : "false");
    return buf;
}

// Native macOS open dialog via osascript (no extra framework). Returns "" on cancel.
static inline std::string pick_file() {
    const char* cmd =
        "osascript -e 'try' "
        "-e 'POSIX path of (choose file with prompt \"Chon anh RAW / JPEG\")' "
        "-e 'end try' 2>/dev/null";
    FILE* p = popen(cmd, "r");
    if (!p) return "";
    char buf[4096]; std::string out;
    if (std::fgets(buf, sizeof(buf), p)) out = buf;
    pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

// Path helpers — export lands next to the source image (writable on any machine).
static inline std::string path_dir(const std::string& p) {
    auto s = p.find_last_of('/');
    return s == std::string::npos ? "." : p.substr(0, s);
}
static inline std::string path_stem(const std::string& p) {
    auto s = p.find_last_of('/');
    std::string b = (s == std::string::npos) ? p : p.substr(s + 1);
    auto d = b.find_last_of('.');
    return d == std::string::npos ? b : b.substr(0, d);
}
static inline bool is_video(const std::string& p) {
    auto d = p.find_last_of('.');
    if (d == std::string::npos) return false;
    std::string e = p.substr(d);
    for (char& c : e) c = (char)tolower((unsigned char)c);
    return e == ".mp4" || e == ".mov" || e == ".m4v" || e == ".avi" || e == ".mkv" || e == ".webm";
}

// ---- fonts --------------------------------------------------------------

// UI font: Inter (IntelliJ's UI family) for Latin+Vietnamese, with Japanese merged from
// Hiragino. The default ImGui font is ASCII-only — CJK would render as blank boxes.
static inline void setup_fonts() {
    ImGuiIO& io = ImGui::GetIO();
    const float kSize = 17.0f;

    static ImVector<ImWchar> latin;           // Default + Vietnamese (must outlive atlas build)
    ImFontGlyphRangesBuilder bb;
    bb.AddRanges(io.Fonts->GetGlyphRangesDefault());
    bb.AddRanges(io.Fonts->GetGlyphRangesVietnamese());
    bb.BuildRanges(&latin);

    const char* inter[] = {
        FUJIFY_ROOT "/imgui-poc/assets/fonts/Inter-Regular.otf",
        "/Applications/IntelliJ IDEA.app/Contents/jbr/Contents/Home/lib/fonts/Inter-Regular.otf",
        "/Applications/Android Studio.app/Contents/jbr/Contents/Home/lib/fonts/Inter-Regular.otf",
    };
    ImFont* base = nullptr;
    for (const char* p : inter) {
        if (FILE* f = std::fopen(p, "rb")) {
            std::fclose(f);
            base = io.Fonts->AddFontFromFileTTF(p, kSize, nullptr, latin.Data);
            if (base) break;
        }
    }
    if (base) {
        const char* jp = "/System/Library/Fonts/Hiragino Sans GB.ttc";
        if (FILE* f = std::fopen(jp, "rb")) {
            std::fclose(f);
            ImFontConfig cfg; cfg.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(jp, kSize, &cfg, io.Fonts->GetGlyphRangesJapanese());
        }
        return;
    }
    static ImVector<ImWchar> all;
    ImFontGlyphRangesBuilder ab;
    ab.AddRanges(io.Fonts->GetGlyphRangesDefault());
    ab.AddRanges(io.Fonts->GetGlyphRangesVietnamese());
    ab.AddRanges(io.Fonts->GetGlyphRangesJapanese());
    ab.BuildRanges(&all);
    const char* au = "/System/Library/Fonts/Supplemental/Arial Unicode.ttf";
    if (FILE* f = std::fopen(au, "rb")) {
        std::fclose(f);
        if (io.Fonts->AddFontFromFileTTF(au, kSize, nullptr, all.Data)) return;
    }
    io.Fonts->AddFontDefault();
}

// ---- shared UI ----------------------------------------------------------
// All the editor state + controls + preview + worker orchestration live here so the
// OpenGL and Vulkan builds share one source of truth. The only backend-specific bits
// are injected via TextureOps (how to upload a JPEG into a GPU texture + its id).

static const char* kPresets[]   = {"(none)", "case01_flower_warm_fix",
                                    "case02_indoor_neon_neutral", "default_indoor",
                                    "nex5n_auto_keep_vibe"};
static const char* kPresetArg[]  = {"", "case01_flower_warm_fix",
                                    "case02_indoor_neon_neutral", "default_indoor",
                                    "nex5n_auto_keep_vibe"};
static const char* kFormats[]    = {"story", "feed", "square"};   // 9:16, 4:5, 1:1
static const char* kTiers[]      = {"hq", "ig"};                  // 2560px / 2048px

// ---- job queue (producer / single consumer) -----------------------------
// The UI thread is the producer (push Task); one consumer thread pops and processes
// serially — matching the daemon, which handles one request at a time. Previews are
// coalesced by sequence number so dragging sliders never backs up a stale queue.

struct Task {
    enum Kind { Preview, Export } kind = Preview;
    std::string input;
    bool  use_temp = false, wb_auto = false;
    float temp = 0, tint = 0, br = 0, co = 0, sh = 0, hi = 0;
    int   preset = 0;
    bool  all = false; int fmt_idx = 0, tier_idx = 0; bool brand = true;  // export-only
    uint64_t seq = 0;                                                     // preview coalescing
};

struct Result {
    Task::Kind kind = Task::Preview;
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
                t.use_temp, t.temp, t.tint, t.wb_auto, t.br, t.co, t.sh, t.hi, kPresetArg[t.preset]);
            EngineResult er = engine_run(*dmn, req);
            r.ok = er.ok; r.ms = er.elapsed_ms; r.reload_texture = er.ok;
            r.log = er.ok ? ("OK — engine " + std::to_string(er.elapsed_ms) + " ms (proxy)")
                          : ("Loi engine:\n" + er.log);
        } else if (is_video(t.input)) {                 // video → ffmpeg look export
            std::string out = path_dir(t.input) + "/" + path_stem(t.input) + "_fujify.mp4";
            EngineResult er = engine_run(*dmn, build_json("video_export", t.input, out.c_str(), 0,
                t.use_temp, t.temp, t.tint, t.wb_auto, t.br, t.co, t.sh, t.hi, kPresetArg[t.preset]));
            r.ok = er.ok;
            r.log = er.ok ? ("Exported video (" + std::to_string(er.elapsed_ms) + " ms)\n- " + out)
                          : ("Loi video export:\n" + er.log);
        } else {
            std::string fullreq = build_json("full", t.input, kFullPath, 0,
                t.use_temp, t.temp, t.tint, t.wb_auto, t.br, t.co, t.sh, t.hi, kPresetArg[t.preset]);
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

// When launched from a .app bundle, point the engine + default sample at bundled
// Resources (Contents/MacOS/<exe> → ../Resources/…). No-op in dev: env stays unset, so
// start_daemon() falls back to system python3 + the source tree.
static inline void fujify_bundle_autoconfig() {
    char exe[4096]; uint32_t sz = sizeof(exe);
    if (_NSGetExecutablePath(exe, &sz) != 0) return;
    std::string dir(exe);
    auto s = dir.find_last_of('/');
    if (s == std::string::npos) return;
    std::string res = dir.substr(0, s) + "/../Resources";
    auto exists = [](const std::string& p) {
        if (FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return true; }
        return false;
    };
    std::string eng = res + "/engine/fujify-engine";
    if (!getenv("FUJIFY_ENGINE") && exists(eng)) setenv("FUJIFY_ENGINE", eng.c_str(), 1);
    std::string smp = res + "/sample.ARW";
    if (!getenv("FUJIFY_SAMPLE") && exists(smp)) setenv("FUJIFY_SAMPLE", smp.c_str(), 1);
}

// RGB histogram of the displayed image (filled by the backend from decoded pixels).
static const int kHistBins = 128;
struct Histogram {
    float r[kHistBins], g[kHistBins], b[kHistBins];
    float maxv = 1.0f; bool valid = false;
};
static inline void compute_histogram(const unsigned char* rgba, int w, int h, Histogram* o) {
    for (int i = 0; i < kHistBins; i++) o->r[i] = o->g[i] = o->b[i] = 0.f;
    size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; i++) {
        const unsigned char* px = rgba + i * 4;
        o->r[px[0] >> 1]++; o->g[px[1] >> 1]++; o->b[px[2] >> 1]++;   // 256 → 128 bins
    }
    float m = 1.f;
    for (int i = 0; i < kHistBins; i++) {
        m = std::max(m, o->r[i]); m = std::max(m, o->g[i]); m = std::max(m, o->b[i]);
    }
    o->maxv = m; o->valid = true;
}

// A saved edit "state" (look) — restore to flip between looks / A-B compare.
struct EditState {
    bool use_temp = false, wb_auto = false;
    float temp = 5200.f, tint = 0.f, br = 0.f, co = 0.f, sh = 0.f, hi = 0.f;
    int preset = 0;
};

struct TextureOps {
    // Upload kPreviewPath into a GPU texture; write pixel size to *w,*h and fill *hist
    // from the decoded pixels. Runs on the UI thread (owns the GL/Vulkan context).
    std::function<bool(const char* path, int* w, int* h, Histogram* hist)> upload;
    // Current texture handle as an ImTextureID (0 / VK_NULL_HANDLE if none yet).
    std::function<ImTextureID()> id;
};

struct StudioUI {
    char  input_path[512];
    bool  use_temp = false; float temp = 5200.f, tint = 0.f; bool wb_auto = false;
    float brightness = 0.f, contrast = 0.f, shadows = 0.f, highlights = 0.f;  // neutral: load = original
    int   preset_idx = 0;
    // live-preview debounce
    bool  live = true, dirty = false; double last_change = 0.0;
    bool  pu = false, pw = false; int pp = 0;
    float pt = 5200.f, pti = 0.f, pb = 0.f, pc = 0.f, ps = 0.f, ph = 0.f;
    // export
    int   ex_fmt_idx = 0, ex_tier_idx = 0; bool ex_brand = true;
    bool  show_export = false;   // tạm ẩn panel Export
    std::string ex_status, status;
    // texture (dims tracked here; pixels live in the backend via ops)
    int   tex_w = 0, tex_h = 0; bool has_tex = false;
    Histogram hist;   // RGB histogram of the current preview
    std::vector<std::string> recents;   // recently opened images (persisted)
    std::vector<EditState> snaps;       // saved looks (in-memory, this session)
    // engine
    Daemon daemon;
    std::unique_ptr<JobSystem> js;
    TextureOps ops;
    const char* backend;   // "OpenGL" / "Vulkan", shown in the header

    StudioUI(const char* backend_name, TextureOps texture_ops)
        : ops(std::move(texture_ops)), backend(backend_name) {
        fujify_bundle_autoconfig();   // sets FUJIFY_ENGINE / FUJIFY_SAMPLE if running from .app
        const char* sample = getenv("FUJIFY_SAMPLE");   // bundled sample path
        if (sample && sample[0]) std::snprintf(input_path, sizeof(input_path), "%s", sample);
        else std::snprintf(input_path, sizeof(input_path), "%s/imgui-poc/assets/sample.ARW", FUJIFY_ROOT);
        status = std::string("Dang tu dong load sample.ARW... (") + backend + ")";
        daemon = start_daemon();
        js.reset(new JobSystem(&daemon));   // starts the single consumer thread
        load_recents();
        start_process();                    // auto-load on launch
    }

    ~StudioUI() {
        js.reset();          // stop + join the consumer
        stop_daemon(daemon);
    }

    // snapshot current edit params into a Task
    Task make_task(Task::Kind k) {
        Task t; t.kind = k; t.input = input_path;
        t.use_temp = use_temp; t.wb_auto = wb_auto; t.temp = temp; t.tint = tint;
        t.br = brightness; t.co = contrast; t.sh = shadows; t.hi = highlights; t.preset = preset_idx;
        return t;
    }
    void start_process() {
        if (recents.empty() || recents.front() != input_path) add_recent(input_path);
        js->submit(make_task(Task::Preview));
    }

    // --- snapshots: save / restore the current look (WB+tone+preset) ---
    void save_snap() {
        EditState e;
        e.use_temp = use_temp; e.wb_auto = wb_auto; e.temp = temp; e.tint = tint;
        e.br = brightness; e.co = contrast; e.sh = shadows; e.hi = highlights; e.preset = preset_idx;
        snaps.push_back(e);
    }
    void apply_snap(const EditState& e) {
        use_temp = e.use_temp; wb_auto = e.wb_auto; temp = e.temp; tint = e.tint;
        brightness = e.br; contrast = e.co; shadows = e.sh; highlights = e.hi; preset_idx = e.preset;
        pu = use_temp; pw = wb_auto; pp = preset_idx;             // sync snapshot (no double-fire)
        pt = temp; pti = tint; pb = brightness; pc = contrast; ps = shadows; ph = highlights;
        start_process(); dirty = false;
    }
    void start_export(bool all) {
        Task t = make_task(Task::Export);
        t.all = all; t.fmt_idx = ex_fmt_idx; t.tier_idx = ex_tier_idx; t.brand = ex_brand;
        js->submit(t);
    }

    // ---- recents (persisted to ~/.fujify_studio_recents) ----
    std::string recents_path() {
        const char* h = getenv("HOME");
        return std::string(h ? h : "/tmp") + "/.fujify_studio_recents";
    }
    void load_recents() {
        recents.clear();
        FILE* f = std::fopen(recents_path().c_str(), "r");
        if (!f) return;
        char line[1024];
        while (recents.size() < 8 && std::fgets(line, sizeof(line), f)) {
            std::string s(line);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            if (!s.empty()) recents.push_back(s);
        }
        std::fclose(f);
    }
    void save_recents() {
        FILE* f = std::fopen(recents_path().c_str(), "w");
        if (!f) return;
        for (auto& s : recents) { std::fputs(s.c_str(), f); std::fputc('\n', f); }
        std::fclose(f);
    }
    void add_recent(const std::string& path) {
        recents.erase(std::remove(recents.begin(), recents.end(), path), recents.end());
        recents.insert(recents.begin(), path);
        if (recents.size() > 8) recents.resize(8);
        save_recents();
    }
    void draw_recents() {
        if (recents.empty()) return;
        char hdr[32]; std::snprintf(hdr, sizeof(hdr), "Recents (%d)", (int)recents.size());
        if (!ImGui::CollapsingHeader(hdr)) return;
        for (size_t i = 0; i < recents.size(); i++) {
            const std::string& full = recents[i];
            auto s = full.find_last_of('/');
            std::string name = (s == std::string::npos) ? full : full.substr(s + 1);
            ImGui::PushID((int)i);
            if (ImGui::Selectable(name.c_str())) {
                std::snprintf(input_path, sizeof(input_path), "%s", full.c_str());
                start_process();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", full.c_str());
            ImGui::PopID();
        }
    }

    // RGB histogram (3 channels overlaid, additive translucent bars).
    void draw_histogram() {
        ImGui::SeparatorText("Histogram");
        if (!hist.valid) { ImGui::TextDisabled("(chua co anh)"); return; }
        ImVec2 sz(ImGui::GetContentRegionAvail().x, 100.f);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 e(p.x + sz.x, p.y + sz.y);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, e, IM_COL32(10, 10, 12, 255));
        for (int k = 1; k < 4; k++) {                          // quarter gridlines
            float x = p.x + sz.x * k / 4.f;
            dl->AddLine(ImVec2(x, p.y), ImVec2(x, e.y), IM_COL32(38, 38, 42, 255));
        }
        // sqrt scale so the distribution reads like a real photo histogram (tall spikes
        // don't flatten everything else).
        auto plot = [&](const float* hbin, ImU32 col) {
            for (int i = 0; i < kHistBins; i++) {
                float v = std::sqrt(hbin[i] / hist.maxv);
                float x0 = p.x + sz.x * i / kHistBins;
                float x1 = p.x + sz.x * (i + 1) / kHistBins;
                dl->AddRectFilled(ImVec2(x0, p.y + sz.y * (1.f - v)), ImVec2(x1, e.y), col);
            }
        };
        plot(hist.r, IM_COL32(255, 70, 70, 115));
        plot(hist.g, IM_COL32(70, 225, 110, 115));
        plot(hist.b, IM_COL32(90, 140, 255, 115));
        dl->AddRect(p, e, IM_COL32(70, 70, 74, 255));
        ImGui::Dummy(sz);
    }

    // Draw the whole UI for one frame. Call between ImGui::NewFrame() and ImGui::Render().
    void draw() {
        // ---- Controls ----
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360, 800), ImGuiCond_FirstUseEver);
        ImGui::Begin("Fuji-fy Studio");
        ImGui::Text("Backend: %s", backend);
        ImGui::TextWrapped(u8"Trình sửa ảnh (RAW/JPEG). Storage + Ads là revenue features; "
                           u8"mua Premium để tắt ads.");
        ImGui::TextDisabled(u8"写真編集（RAW/JPEG）。ストレージと広告は収益機能、プレミアムで広告オフ。");
        ImGui::Separator();

        ImGui::TextUnformatted("Input");
        ImGui::SetNextItemWidth(-90);
        ImGui::InputText("##path", input_path, IM_ARRAYSIZE(input_path));
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(-1, 0))) {
            std::string f = pick_file();
            if (!f.empty()) {
                std::snprintf(input_path, sizeof(input_path), "%s", f.c_str());
                start_process();
            }
        }
        draw_recents();

        ImGui::SeparatorText("White balance");
        ImGui::Checkbox("Set temp (K)", &use_temp);
        if (use_temp) { ImGui::SameLine(); ImGui::SetNextItemWidth(140);
            ImGui::SliderFloat("##temp", &temp, 2000.f, 10000.f, "%.0fK"); }
        ImGui::SliderFloat("tint", &tint, -50.f, 50.f, "%.0f");
        ImGui::Checkbox("Auto WB (gray world)", &wb_auto);

        ImGui::SeparatorText("Tone");
        ImGui::SliderFloat("brightness", &brightness, -1.f, 1.f, "%.2f");
        ImGui::SliderFloat("contrast",   &contrast,   -1.f, 1.f, "%.2f");
        ImGui::SliderFloat("shadows",    &shadows,    -1.f, 1.f, "%.2f");
        ImGui::SliderFloat("highlights", &highlights, -1.f, 1.f, "%.2f");

        draw_histogram();

        ImGui::SeparatorText("Preset"); ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##preset", &preset_idx, kPresets, IM_ARRAYSIZE(kPresets));

        if (use_temp != pu || wb_auto != pw || preset_idx != pp || temp != pt || tint != pti ||
            brightness != pb || contrast != pc || shadows != ps || highlights != ph) {
            dirty = true; last_change = ImGui::GetTime();
            pu = use_temp; pw = wb_auto; pp = preset_idx;
            pt = temp; pti = tint; pb = brightness; pc = contrast; ps = shadows; ph = highlights;
        }

        ImGui::Separator();
        ImGui::Checkbox("Live preview", &live);
        ImGui::SameLine(); ImGui::TextDisabled(live ? "(tu apply sau ~0.35s)" : "(bam Load/Apply)");
        // No disable on busy: jobs are queued. Previews coalesce; exports run FIFO.
        if (ImGui::Button("Load / Apply", ImVec2(-90, 36))) { start_process(); dirty = false; }
        ImGui::SameLine();
        if (ImGui::Button("Reset", ImVec2(-1, 36))) {
            use_temp = false; temp = 5200.f; tint = 0.f; wb_auto = false;
            brightness = 0.f; contrast = 0.f; shadows = 0.f; highlights = 0.f; preset_idx = 0;
            pu = use_temp; pw = wb_auto; pp = preset_idx;             // sync snapshot (no double-fire)
            pt = temp; pti = tint; pb = brightness; pc = contrast; ps = shadows; ph = highlights;
            start_process(); dirty = false;
        }
        if (live && dirty && (ImGui::GetTime() - last_change) > 0.35) { start_process(); dirty = false; }

        // ---- snapshots: save the current look, click S# to restore ----
        ImGui::SeparatorText("Snapshots");
        if (ImGui::SmallButton("+ Save state")) save_snap();
        for (size_t i = 0; i < snaps.size(); i++) {
            ImGui::SameLine();
            ImGui::PushID((int)i);
            char lbl[8]; std::snprintf(lbl, sizeof(lbl), "S%d", (int)i + 1);
            if (ImGui::SmallButton(lbl)) apply_snap(snaps[i]);
            if (ImGui::IsItemHovered()) {
                const EditState& e = snaps[i];
                ImGui::SetTooltip("temp %s  tint %.0f\nbright %.2f  contrast %.2f\nshadow %.2f  high %.2f  preset %s",
                    e.use_temp ? std::to_string((int)e.temp).c_str() : "off", e.tint,
                    e.br, e.co, e.sh, e.hi, kPresets[e.preset]);
            }
            ImGui::PopID();
        }
        if (!snaps.empty()) { ImGui::SameLine(); if (ImGui::SmallButton("x")) snaps.clear(); }

        if (js->busy()) {
            ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(-1, 0), "...");
            ImGui::Text("queue: %d job", js->pending());
        }
        ImGui::Separator();
        ImGui::TextWrapped("%s", status.c_str());

        if (show_export) {   // tạm ẩn nút export (đặt show_export=true để bật lại)
            bool can_export = has_tex;   // queue handles concurrency; can enqueue while busy
            if (is_video(input_path)) {
                ImGui::SeparatorText(u8"Export video 動画");
                ImGui::TextDisabled("Ap look (temp/brightness/contrast) qua ffmpeg → .mp4");
                ImGui::BeginDisabled(!can_export);
                if (ImGui::Button("Export video (.mp4)", ImVec2(-1, 30))) start_export(false);
                ImGui::EndDisabled();
            } else {
                ImGui::SeparatorText(u8"Export creative 書き出し (IG / Threads)");
                ImGui::SetNextItemWidth(150); ImGui::Combo("format", &ex_fmt_idx, kFormats, IM_ARRAYSIZE(kFormats));
                ImGui::SameLine(); ImGui::SetNextItemWidth(90); ImGui::Combo("tier", &ex_tier_idx, kTiers, IM_ARRAYSIZE(kTiers));
                ImGui::Checkbox("Watermark Fuji-Fy", &ex_brand);
                ImGui::TextDisabled("story 9:16 - feed 4:5 - square 1:1 | hq=2560px ig=2048px");
                ImGui::BeginDisabled(!can_export);
                if (ImGui::Button("Export (format dang chon)", ImVec2(-1, 28))) start_export(false);
                if (ImGui::Button("Export ALL 3 formats", ImVec2(-1, 30))) start_export(true);
                ImGui::EndDisabled();
            }
            if (!has_tex) ImGui::TextDisabled("(Load anh/video truoc khi export)");
            if (!ex_status.empty()) ImGui::TextWrapped("%s", ex_status.c_str());
        }
        ImGui::End();

        // ---- Preview (zoom/pan) ----
        ImGui::SetNextWindowPos(ImVec2(360, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(920, 800), ImGuiCond_FirstUseEver);
        ImGui::Begin("Preview");
        if (has_tex) {
            static float zoom = 1.0f; static ImVec2 pan(0, 0);
            ImVec2 a0 = ImGui::GetContentRegionAvail();
            float fit = (tex_w && tex_h) ? std::min(a0.x / tex_w, a0.y / tex_h) : 1.f;
            if (fit > 1.f) fit = 1.f;
            ImGui::Text("%d x %d px", tex_w, tex_h);
            ImGui::SameLine(); if (ImGui::SmallButton("Fit")) { zoom = 1.f; pan = ImVec2(0, 0); }
            ImGui::SameLine(); if (ImGui::SmallButton("100%")) { zoom = (fit > 0 ? 1.f / fit : 1.f); pan = ImVec2(0, 0); }
            ImGui::SameLine(); ImGui::SetNextItemWidth(160);
            ImGui::SliderFloat("##zoom", &zoom, 0.1f, 8.0f, "zoom %.2fx");
            ImGui::SameLine(); ImGui::TextDisabled("(cuon = zoom, keo = pan)");

            ImVec2 av = ImGui::GetContentRegionAvail();
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("canvas", av, ImGuiButtonFlags_MouseButtonLeft);
            if (ImGui::IsItemHovered()) { float wl = ImGui::GetIO().MouseWheel;
                if (wl != 0.f) { zoom *= (1.f + wl * 0.1f); if (zoom < 0.1f) zoom = 0.1f; if (zoom > 8.f) zoom = 8.f; } }
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 d = ImGui::GetIO().MouseDelta; pan.x += d.x; pan.y += d.y; }
            float sc = fit * zoom;
            ImVec2 sz(tex_w * sc, tex_h * sc);
            ImVec2 c(p0.x + av.x * 0.5f + pan.x, p0.y + av.y * 0.5f + pan.y);
            ImVec2 ia(c.x - sz.x * 0.5f, c.y - sz.y * 0.5f), ib(ia.x + sz.x, ia.y + sz.y);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->PushClipRect(p0, ImVec2(p0.x + av.x, p0.y + av.y), true);
            dl->AddImage(ops.id(), ia, ib);
            dl->PopClipRect();
        } else {
            ImGui::TextUnformatted("Chua co anh. Nhan 'Load / Apply'.");
        }
        ImGui::End();

        // ---- drain finished jobs (UI thread owns the graphics context) ----
        Result r; bool need_upload = false; std::string preview_log;
        while (js->poll(r)) {
            if (r.kind == Task::Export) ex_status = r.log;
            else { status = r.log; if (r.reload_texture && r.ok) { need_upload = true; preview_log = r.log; } }
        }
        if (need_upload) {
            int w = 0, h = 0;
            if (ops.upload(kPreviewPath, &w, &h, &hist)) {
                tex_w = w; tex_h = h; has_tex = true;
                char dim[48]; std::snprintf(dim, sizeof(dim), " [%dx%d]", w, h);
                status = preview_log + dim;
            } else {
                status = "Decode OK nhung khong upload duoc texture.";
            }
        }
    }
};
