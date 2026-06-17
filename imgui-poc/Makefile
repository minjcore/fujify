# Dear ImGui POC — macOS (GLFW + OpenGL3 / Vulkan)
# Layout: src/ (our code) · vendor/ (stb + imgui) · engine/ (python) · assets/
EXE       = imgui_poc
IMGUI_DIR = vendor/imgui
GLFW_DIR  = $(shell brew --prefix glfw)

SOURCES  = src/main.cpp
SOURCES += $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp \
           $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp \
           $(IMGUI_DIR)/imgui_demo.cpp
SOURCES += $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp \
           $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp

OBJS = $(addsuffix .o, $(basename $(notdir $(SOURCES))))

# Project root (parent of imgui-poc/) — so the app can invoke `python3 -m core.api`.
FUJIFY_ROOT = $(realpath ..)

CXX      = clang++
CXXFLAGS = -std=c++11 -Isrc -Ivendor -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -I$(GLFW_DIR)/include
CXXFLAGS += -Wall -Wformat -pthread -DFUJIFY_ROOT='"$(FUJIFY_ROOT)"'
LIBS     = -L$(GLFW_DIR)/lib -lglfw \
           -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo

# Vulkan (via MoltenVK on macOS) — headers, loader, and the MoltenVK ICD manifest.
VK_HEADERS = $(shell brew --prefix vulkan-headers)
VK_LOADER  = $(shell brew --prefix vulkan-loader)
VK_MOLTEN  = $(shell brew --prefix molten-vk)
VK_ICD     = $(VK_MOLTEN)/etc/vulkan/icd.d/MoltenVK_icd.json
VKFLAGS    = -I$(VK_HEADERS)/include -DMOLTENVK_ICD='"$(VK_ICD)"'
VKLIBS     = -L$(VK_LOADER)/lib -lvulkan -Wl,-rpath,$(VK_LOADER)/lib

%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<
%.o: src/%.mm
	$(CXX) $(CXXFLAGS) -fobjc-arc -c -o $@ $<
%.o: $(IMGUI_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<
%.o: $(IMGUI_DIR)/backends/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Rebuild app objects when the shared header changes (pattern rule omits this prereq).
fujify_studio.o: src/fujify_studio.cpp src/fujify_engine.h src/fujify_jobs.h src/fujify_ui.h

# Core Dear ImGui objects (backend-agnostic) + GLFW platform backend
IMGUI_CORE = imgui.o imgui_draw.o imgui_tables.o imgui_widgets.o imgui_demo.o imgui_impl_glfw.o
IMGUI_OBJS = $(IMGUI_CORE) imgui_impl_opengl3.o

all: $(EXE) fujify

$(EXE): $(OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LIBS)
	@echo "Build complete -> ./$(EXE)"

# Fuji-fy Studio — OpenGL3 backend
fujify: fujify_studio.o fujify_macos.o $(IMGUI_OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LIBS)
	@echo "Build complete -> ./fujify"

# Fuji-fy Studio — Vulkan backend (needs Vulkan headers for these two TUs)
fujify_vk.o: src/fujify_vk.cpp src/fujify_engine.h src/fujify_jobs.h src/fujify_ui.h
	$(CXX) $(CXXFLAGS) $(VKFLAGS) -c -o $@ src/fujify_vk.cpp
imgui_impl_vulkan.o: $(IMGUI_DIR)/backends/imgui_impl_vulkan.cpp
	$(CXX) $(CXXFLAGS) $(VKFLAGS) -c -o $@ $<

fujify_vk: fujify_vk.o imgui_impl_vulkan.o fujify_macos.o $(IMGUI_CORE)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LIBS) $(VKLIBS)
	@echo "Build complete -> ./fujify_vk"

clean:
	rm -f $(EXE) fujify fujify_vk $(OBJS) fujify_studio.o fujify_vk.o imgui_impl_vulkan.o fujify_macos.o

.PHONY: all clean
