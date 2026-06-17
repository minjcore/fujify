// Fuji-fy Studio — OpenGL3 build (Dear ImGui + GLFW/OpenGL3).
// All editor state + UI + engine orchestration live in fujify_engine.h (shared with the
// Vulkan build). This file only provides the OpenGL texture upload + render loop.

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "fujify_ui.h"   // StudioUI + jobs + engine (split headers)

// ---- OpenGL texture -----------------------------------------------------
struct GLTexture { GLuint id = 0; int w = 0, h = 0; };

static bool gl_upload(GLTexture& t, const char* path, Histogram* hist) {
    int w, h, n;
    unsigned char* data = stbi_load(path, &w, &h, &n, 4);
    if (!data) return false;
    if (t.id == 0) glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    if (hist) compute_histogram(data, w, h, hist);
    stbi_image_free(data);
    t.w = w; t.h = h;
    return true;
}

static void glfw_error_callback(int e, const char* d) { std::fprintf(stderr, "GLFW %d: %s\n", e, d); }

int main(int, char**) {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return 1;
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    GLFWwindow* window = glfwCreateWindow(1280, 800, "Fuji-fy Studio — OpenGL", nullptr, nullptr);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    setup_fonts();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    GLTexture tex;
    TextureOps ops;
    ops.upload = [&](const char* p, int* w, int* h, Histogram* hist) {
        if (!gl_upload(tex, p, hist)) return false;
        *w = tex.w; *h = tex.h; return true;
    };
    ops.id = [&]() { return (ImTextureID)(intptr_t)tex.id; };

    {
        StudioUI ui("OpenGL", ops);   // owns engine state; joins worker + stops daemon on scope exit
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ui.draw();

            ImGui::Render();
            int w, h; glfwGetFramebufferSize(window, &w, &h);
            glViewport(0, 0, w, h);
            glClearColor(0.08f, 0.09f, 0.10f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }
    }   // ui destructed here (worker joined, daemon stopped) before GL teardown

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
