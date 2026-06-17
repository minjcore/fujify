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
                                     float br, float co, float sh, float hi, const char* preset,
                                     int target_kb = 0) {
    std::string extra;
    if (preset && preset[0]) extra += std::string("\"preset\":\"") + preset + "\",";
    if (target_kb > 0) extra += "\"target_kb\":" + std::to_string(target_kb) + ",";
    char buf[2048];
    std::snprintf(buf, sizeof(buf),
        "{\"mode\":\"%s\",\"input_path\":\"%s\",\"output_path\":\"%s\",\"max_dim\":%d,\"quality\":90,%s"
        "\"settings\":{\"temp\":%s,\"tint\":%.3f,\"wb_auto\":%s,\"brightness\":%.3f,"
        "\"contrast\":%.3f,\"shadows\":%.3f,\"highlights\":%.3f}}",
        mode, in.c_str(), out, max_dim, extra.c_str(),
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

// Cloud upload request (engine PUTs the file to the Worker → user's private R2 library/).
static inline std::string build_upload_json(const std::string& src, const std::string& name,
                                            const std::string& token) {
    char buf[3072];
    std::snprintf(buf, sizeof(buf),
        "{\"mode\":\"upload\",\"input_path\":\"%s\",\"name\":\"%s\",\"token\":\"%s\"}",
        src.c_str(), name.c_str(), token.c_str());
    return buf;
}

// Account signup/login request (engine POSTs to the Worker /auth/*).
static inline std::string build_auth_json(const char* mode, const std::string& email,
                                          const std::string& pw) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        "{\"mode\":\"%s\",\"email\":\"%s\",\"password\":\"%s\"}", mode, email.c_str(), pw.c_str());
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

// ---- presets / formats ----
static const char* kPresets[]   = {"(none)", "case01_flower_warm_fix",
                                    "case02_indoor_neon_neutral", "default_indoor",
                                    "nex5n_auto_keep_vibe"};
static const char* kPresetArg[]  = {"", "case01_flower_warm_fix",
                                    "case02_indoor_neon_neutral", "default_indoor",
                                    "nex5n_auto_keep_vibe"};
static const char* kFormats[]    = {"story", "feed", "square"};   // 9:16, 4:5, 1:1
static const char* kTiers[]      = {"hq", "ig"};                  // 2560px / 2048px

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

