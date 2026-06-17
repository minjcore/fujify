// fujify_ui.h — StudioUI: editor state, controls, preview, snapshots (shared GL+VK).
#pragma once
#include "imgui.h"
#include "fujify_jobs.h"

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
    int   target_kb = 500;       // save-to-target-size
    char  acct_email[128] = "", acct_pw[128] = "";   // login form
    std::string cloud_token, cloud_email;            // session (persisted)
    std::string ex_status, status;
    // texture (dims tracked here; pixels live in the backend via ops)
    int   tex_w = 0, tex_h = 0; bool has_tex = false;
    float crop_x0 = 0.f, crop_y0 = 0.f, crop_x1 = 1.f, crop_y1 = 1.f; bool crop_mode = false;
    Histogram hist;   // RGB histogram of the current preview
    std::vector<std::string> recents;   // recently opened images (persisted)
    std::vector<EditState> snaps;       // saved looks (in-memory, this session)
    std::vector<std::string> lib_names, lib_keys;   // cloud library listing
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
        load_session();
        if (!cloud_token.empty()) start_lib_list();   // already signed in → show library
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
        if (!crop_mode) {   // while selecting, show full image; otherwise apply the crop
            t.crop[0] = crop_x0; t.crop[1] = crop_y0; t.crop[2] = crop_x1; t.crop[3] = crop_y1;
        }
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
    void start_save_target() {
        Task t = make_task(Task::SaveTarget);
        t.target_kb = target_kb;
        js->submit(t);
    }

    void start_upload() {
        Task t = make_task(Task::Upload);
        t.token = cloud_token;
        js->submit(t);
    }
    void start_lib_list() { Task t; t.kind = Task::LibList; t.token = cloud_token; js->submit(t); }
    void start_lib_get(const std::string& key) { Task t; t.kind = Task::LibGet; t.lib_key = key; js->submit(t); }

    static std::vector<std::string> split_pipe(const std::string& s) {
        std::vector<std::string> v; size_t a = 0;
        if (s.empty()) return v;
        for (size_t b; (b = s.find('|', a)) != std::string::npos; a = b + 1) v.push_back(s.substr(a, b - a));
        v.push_back(s.substr(a));
        return v;
    }
    void start_auth(bool signup) {
        Task t; t.kind = Task::Auth; t.signup = signup;
        t.email = acct_email; t.password = acct_pw;
        js->submit(t);
    }

    // --- session (persist email+token to ~/.fujify_session) ---
    std::string session_path() {
        const char* h = getenv("HOME");
        return std::string(h ? h : "/tmp") + "/.fujify_session";
    }
    void load_session() {
        FILE* f = std::fopen(session_path().c_str(), "r");
        if (!f) return;
        char e[256] = {0}, t[1200] = {0};
        auto strip = [](char* s) { for (char* p = s; *p; ++p) if (*p == '\n' || *p == '\r') { *p = 0; break; } };
        if (std::fgets(e, sizeof(e), f)) { strip(e); cloud_email = e; }
        if (std::fgets(t, sizeof(t), f)) { strip(t); cloud_token = t; }
        std::fclose(f);
    }
    void save_session() {
        FILE* f = std::fopen(session_path().c_str(), "w");
        if (!f) return;
        std::fprintf(f, "%s\n%s\n", cloud_email.c_str(), cloud_token.c_str());
        std::fclose(f);
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
        if (ImGui::Button("Load / Apply", ImVec2(-90, 36))) { start_process(); dirty = false; }
        ImGui::SameLine();
        if (ImGui::Button("Reset", ImVec2(-1, 36))) {
            use_temp = false; temp = 5200.f; tint = 0.f; wb_auto = false;
            brightness = 0.f; contrast = 0.f; shadows = 0.f; highlights = 0.f; preset_idx = 0;
            pu = use_temp; pw = wb_auto; pp = preset_idx;
            pt = temp; pti = tint; pb = brightness; pc = contrast; ps = shadows; ph = highlights;
            start_process(); dirty = false;
        }

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

        ImGui::SeparatorText(u8"Save → target dung lượng");
        ImGui::SetNextItemWidth(110); ImGui::InputInt("KB", &target_kb);
        if (target_kb < 10) target_kb = 10;
        ImGui::SameLine();
        ImGui::BeginDisabled(!has_tex || is_video(input_path));
        if (ImGui::Button("Save ~KB", ImVec2(-1, 0))) start_save_target();
        ImGui::EndDisabled();

        if (is_video(input_path)) {
            ImGui::SeparatorText(u8"Export video 動画");
            ImGui::BeginDisabled(!has_tex);
            if (ImGui::Button("Export video (.mp4)", ImVec2(-1, 30))) start_export(false);
            ImGui::EndDisabled();
        } else {
            ImGui::SeparatorText(u8"Export creative (IG / Threads)");
            ImGui::SetNextItemWidth(150); ImGui::Combo("format", &ex_fmt_idx, kFormats, IM_ARRAYSIZE(kFormats));
            ImGui::SameLine(); ImGui::SetNextItemWidth(90); ImGui::Combo("tier", &ex_tier_idx, kTiers, IM_ARRAYSIZE(kTiers));
            ImGui::Checkbox("Watermark Fuji-Fy", &ex_brand);
            ImGui::BeginDisabled(!has_tex);
            if (ImGui::Button("Export (format dang chon)", ImVec2(-1, 28))) start_export(false);
            if (ImGui::Button("Export ALL 3 formats", ImVec2(-1, 30))) start_export(true);
            ImGui::EndDisabled();
        }
        if (!ex_status.empty()) ImGui::TextWrapped("%s", ex_status.c_str());

        ImGui::SeparatorText(u8"Cloud ☁");
        if (cloud_token.empty()) {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##email", "email", acct_email, sizeof(acct_email));
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##pw", "password", acct_pw, sizeof(acct_pw),
                                     ImGuiInputTextFlags_Password);
            if (ImGui::Button("Login", ImVec2(-90, 0))) start_auth(false);
            ImGui::SameLine();
            if (ImGui::Button("Sign up", ImVec2(-1, 0))) start_auth(true);
        } else {
            ImGui::Text("☁ %s", cloud_email.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Logout")) { cloud_token.clear(); cloud_email.clear(); save_session(); }
            ImGui::BeginDisabled(!has_tex || is_video(input_path));
            if (ImGui::Button("Upload to cloud", ImVec2(-90, 0))) start_upload();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Library", ImVec2(-1, 0))) start_lib_list();
            for (size_t i = 0; i < lib_names.size(); i++) {
                ImGui::PushID((int)(1000 + i));
                if (ImGui::Selectable(lib_names[i].c_str())) start_lib_get(lib_keys[i]);
                ImGui::PopID();
            }
        }

        if (live && dirty && (ImGui::GetTime() - last_change) > 0.35) { start_process(); dirty = false; }
        if (js->busy()) {
            ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(-1, 0), "...");
            ImGui::Text("queue: %d job", js->pending());
        }
        ImGui::Separator();
        ImGui::TextWrapped("%s", status.c_str());
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
            ImGui::SameLine(); ImGui::SetNextItemWidth(130);
            ImGui::SliderFloat("##zoom", &zoom, 0.1f, 8.0f, "zoom %.2fx");
            ImGui::SameLine();
            if (ImGui::Checkbox("Crop", &crop_mode)) { zoom = 1.f; pan = ImVec2(0, 0); start_process(); }
            if (crop_mode) {
                ImGui::SameLine(); if (ImGui::SmallButton("Apply")) { crop_mode = false; start_process(); }
                ImGui::SameLine(); if (ImGui::SmallButton("Reset##crop")) { crop_x0 = crop_y0 = 0.f; crop_x1 = crop_y1 = 1.f; start_process(); }
                ImGui::SameLine(); ImGui::TextDisabled("kéo chọn vùng");
            }

            ImVec2 av = ImGui::GetContentRegionAvail();
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            float sc = fit * zoom;
            ImVec2 sz(tex_w * sc, tex_h * sc);
            ImVec2 c(p0.x + av.x * 0.5f + pan.x, p0.y + av.y * 0.5f + pan.y);
            ImVec2 ia(c.x - sz.x * 0.5f, c.y - sz.y * 0.5f), ib(ia.x + sz.x, ia.y + sz.y);

            ImGui::InvisibleButton("canvas", av, ImGuiButtonFlags_MouseButtonLeft);
            ImGuiIO& io2 = ImGui::GetIO();
            if (crop_mode) {                              // drag = select crop rect
                if (ImGui::IsItemActive() && (ib.x > ia.x) && (ib.y > ia.y)) {
                    ImVec2 m0 = io2.MouseClickedPos[0], m1 = io2.MousePos;
                    auto nx = [&](float x){ float v=(x-ia.x)/(ib.x-ia.x); return v<0?0:(v>1?1:v); };
                    auto ny = [&](float y){ float v=(y-ia.y)/(ib.y-ia.y); return v<0?0:(v>1?1:v); };
                    crop_x0 = nx(std::min(m0.x,m1.x)); crop_x1 = nx(std::max(m0.x,m1.x));
                    crop_y0 = ny(std::min(m0.y,m1.y)); crop_y1 = ny(std::max(m0.y,m1.y));
                }
            } else {                                      // wheel = zoom, drag = pan
                if (ImGui::IsItemHovered() && io2.MouseWheel != 0.f) {
                    zoom *= (1.f + io2.MouseWheel * 0.1f); if (zoom<0.1f) zoom=0.1f; if (zoom>8.f) zoom=8.f; }
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    pan.x += io2.MouseDelta.x; pan.y += io2.MouseDelta.y; }
            }

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->PushClipRect(p0, ImVec2(p0.x + av.x, p0.y + av.y), true);
            dl->AddImage(ops.id(), ia, ib);
            if (crop_mode) {                              // dim outside + bright selection + border
                ImVec2 r0(ia.x + crop_x0*(ib.x-ia.x), ia.y + crop_y0*(ib.y-ia.y));
                ImVec2 r1(ia.x + crop_x1*(ib.x-ia.x), ia.y + crop_y1*(ib.y-ia.y));
                dl->AddRectFilled(p0, ImVec2(p0.x+av.x, p0.y+av.y), IM_COL32(0,0,0,120));
                dl->AddImage(ops.id(), r0, r1, ImVec2(crop_x0,crop_y0), ImVec2(crop_x1,crop_y1));
                dl->AddRect(r0, r1, IM_COL32(255,210,90,255), 0, 0, 2.f);
            }
            dl->PopClipRect();

            // floating bottom-right: reset view (fit)
            ImVec2 bsz(96, 26);
            ImGui::SetCursorScreenPos(ImVec2(p0.x + av.x - bsz.x - 12, p0.y + av.y - bsz.y - 12));
            if (ImGui::Button("⤢ Fit view", bsz)) { zoom = 1.f; pan = ImVec2(0, 0); }
        } else {
            ImGui::TextUnformatted("Chua co anh. Nhan 'Load / Apply'.");
        }
        ImGui::End();

        // ---- drain finished jobs (UI thread owns the graphics context) ----
        Result r; bool need_upload = false; std::string preview_log;
        while (js->poll(r)) {
            if (r.kind == Task::Auth) {
                status = r.log;
                if (r.ok) { cloud_token = r.token; cloud_email = r.email; acct_pw[0] = 0; save_session();
                            start_lib_list(); }
            } else if (r.kind == Task::LibList) {
                status = r.log;
                if (r.ok) {                                  // keep only images (hide .txt etc.)
                    auto names = split_pipe(r.names), keys = split_pipe(r.keys);
                    lib_names.clear(); lib_keys.clear();
                    for (size_t i = 0; i < names.size() && i < keys.size(); i++)
                        if (is_image(names[i])) { lib_names.push_back(names[i]); lib_keys.push_back(keys[i]); }
                }
            } else if (r.kind == Task::LibGet) {
                status = r.log;
                if (r.ok && !r.path.empty()) {
                    std::snprintf(input_path, sizeof(input_path), "%s", r.path.c_str());
                    start_process();   // open the downloaded image
                }
            } else if (r.kind == Task::Export) ex_status = r.log;
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
