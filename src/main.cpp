#define VERSION "1.1"

#ifdef PLATFORM_WINDOWS
    #pragma comment(linker, "/ENTRY:mainCRTStartup")
#endif

#define IMGUI_DEFINE_MATH_OPERATORS
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <imgui_theme.h>

#include <ImGradientHDR.h>
#include <glad/glad.h>
#include <battery/embed.hpp>
#include <GLFW/glfw3.h>
#include <stb/stb_image_write.h>
#include <tinyfiledialogs/tinyfiledialogs.h>
#include <chromium/cubic_bezier.h>
#include <nlohmann/json.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <glm/gtx/string_cast.hpp>

#ifdef PLATFORM_WINDOWS
    #include <Windows.h>
    #include <dwmapi.h>
    #include <GLFW/glfw3native.h>
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <iomanip>
#include <ctime>
#include <string>
#include <filesystem>
#include <functional>
#include <algorithm>
#include <regex>

#define MV_COMPUTE  2
#define MV_POSTPROC 1
#define MV_RENDER   0

float vertices[] = {
    -1.0f, -1.0f,
     1.0f, -1.0f,
     1.0f,  1.0f,
    -1.0f, -1.0f,
     1.0f,  1.0f,
    -1.0f,  1.0f
};

void GLAPIENTRY glMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    if (type != GL_DEBUG_TYPE_ERROR) return;
    fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n", "** GL ERROR **", type, severity, message);
}

const char* vertexShaderSource = R"(
#version 460 core

layout(location = 0) in vec2 aPos;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}

)";

static gfx::CubicBezier fast_out_slow_in(0.4, 0.0, 0.2, 1.0);

static float bezier(float t) {
    return fast_out_slow_in.Solve(t);
}

namespace ImGui {
    ImFont* font;

    // credit: https://github.com/zfedoran
    bool BufferingBar(const char* label, float value, const ImVec2& size_arg, const ImU32& bg_col, const ImU32& fg_col) {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);

        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size = size_arg;
        size.x -= style.FramePadding.x * 2;

        const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
        ItemSize(bb, style.FramePadding.y);
        if (!ItemAdd(bb, id))
            return false;

        window->DrawList->AddRectFilled(bb.Min, bb.Max, bg_col);
        window->DrawList->AddRectFilled(bb.Min, ImVec2(bb.Min.x + value * size.x, bb.Max.y), fg_col);
    }

    // credit: https://github.com/hofstee
    constexpr static auto lerp(float x0, float x1) {
        return [=](float t) {
            return (1 - t) * x0 + t * x1;
        };
    }

    constexpr static float lerp(float x0, float x1, float t) {
        return lerp(x0, x1)(t);
    }

    static auto interval(float T0, float T1, std::function<float(float)> tween = lerp(0.0, 1.0)) {
        return [=](float t) {
            return t < T0 ? 0.0f : t > T1 ? 1.0f : tween((t - T0) / (T1 - T0));
        };
    }

    template <int T> float sawtooth(float t) {
        return ImFmod(((float)T) * t, 1.0f);
    }

    bool Spinner(const char* label, float radius, int thickness, const ImU32& color) {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems) return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);

        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size((radius) * 2, (radius + style.FramePadding.y) * 2);

        const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
        ItemSize(bb, style.FramePadding.y);
        if (!ItemAdd(bb, id)) return false;

        const ImVec2 center = ImVec2(pos.x + radius, pos.y + radius + thickness + style.FramePadding.y);

        const float start_angle = -IM_PI / 2.0f;         // Start at the top
        const int num_detents = 5;                       // how many rotations we want before a repeat
        const int skip_detents = 3;                      // how many steps we skip each rotation
        const float period = 5.0f;                       // in seconds
        const float t = ImFmod(g.Time, period) / period; // map period into [0, 1]

        auto stroke_head_tween = [](float t) {
            t = sawtooth<num_detents>(t);
            return interval(0.0, 0.5, bezier)(t);
        };

        auto stroke_tail_tween = [](float t) {
            t = sawtooth<num_detents>(t);
            return interval(0.5, 1.0, bezier)(t);
        };

        auto step_tween = [=](float t) {
            return floor(lerp(0.0, (float)num_detents, t));
        };

        auto rotation_tween = sawtooth<num_detents>;

        const float head_value = stroke_head_tween(t);
        const float tail_value = stroke_tail_tween(t);
        const float step_value = step_tween(t);
        const float rotation_value = rotation_tween(t);

        const float min_arc = 30.0f / 360.0f * 2.0f * IM_PI;
        const float max_arc = 270.0f / 360.0f * 2.0f * IM_PI;
        const float step_offset = skip_detents * 2.0f * IM_PI / num_detents;
        const float rotation_compensation = ImFmod(4.0 * IM_PI - step_offset - max_arc, 2 * IM_PI);

        const float a_min = start_angle + tail_value * max_arc + rotation_value * rotation_compensation - step_value * step_offset;
        const float a_max = a_min + (head_value - tail_value) * max_arc + min_arc;

        window->DrawList->PathClear();

        int num_segments = 24;
        for (int i = 0; i < num_segments; i++) {
            const float a = a_min + ((float)i / (float)num_segments) * (a_max - a_min);
            window->DrawList->PathLineTo(ImVec2(center.x + ImCos(a) * radius,
                center.y + ImSin(a) * radius));
        }

        window->DrawList->PathStroke(color, false, thickness);

        return true;
    }

    // from imgui demo app
    static void HelpMarker(const char* desc) {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
}

class Error : public std::exception {
    char* msg;
public:
    Error(const char* message) {
        msg = const_cast<char*>(message);
        this->display();
    }
    void display() const {
        std::cout << msg;
    }
};

using namespace glm;

constexpr double zoom_co = 0.85;
constexpr double doubleClick_interval = 0.2;
ivec2 monitorSize;

static void HelpMarker(const char* desc) { // code from imgui demo
    ImGui::TextDisabled("[?]");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

struct Slider {
    std::string name;
    float value = 0.f;
    float min = 0.f, max = 0.f;
    float step = 1.f;
};

struct Fractal {
    std::string name = "Mandelbrot";
    std::string equation = "cpow(z, degree) + c";
    std::string condition = "distance(z, c) > 10";
    std::string initialz = "c";
    float degree = 2.f;
    bool continuous_compatible = true;

    std::vector<Slider> sliders;
};

std::vector<Fractal> fractals = {
    Fractal({.name = "Custom"}),
    Fractal({.name = "Mandelbrot",   .equation = "cpow(z, degree) + c", .condition = "xsq + ysq > 100", .initialz = "c", .degree = 2.f, .continuous_compatible = true}),
    Fractal({.name = "Julia Set",    .equation = "cpow(z, degree) + dvec2(Re, Im)", .condition = "xsq + ysq > 100", .initialz = "c", .degree = 2.f, .continuous_compatible = true,
        .sliders = { Slider({.name = "Re", .value = 0.f}), Slider({.name = "Im", .value = 0.f})}}),
    Fractal({.name = "Nova",         .equation = "z - cdivide(cpow(z, degree) - dvec2(1, 0), degree * cpow(z, degree - 1)) + c", .condition = "distance(z, prevz) < 10e-5", .initialz = "dvec2(1, 0)", .degree = 3.f, .continuous_compatible = false}),
    Fractal({.name = "Burning ship", .equation = "cpow(dvec2(abs(z.x), abs(z.y)), degree) + c", .condition = "xsq + ysq > 100", .initialz = "c", .degree = 2.f, .continuous_compatible = true}),
    Fractal({.name = "Magnet 1",     .equation = "cpow(cdivide(cpow(z, degree) + c - dvec2(1, 0), degree * z + c - dvec2(degree, 0)), degree)", .condition = "length(z) >= 100 || length(z - dvec2(1,0)) <= 1e-5", .initialz="dvec2(0)", .degree = 2.f, .continuous_compatible = false}),
    Fractal({.name = "Magnet 2",     .equation = "cpow(cdivide(cpow(z, degree + 1) + 3 * cmultiply(c - dvec2(1, 0), z) + cmultiply(c - dvec2(1, 0), c - dvec2(2, 0)), "
        "3 * cpow(z, degree) + 3 * cmultiply(c - dvec2(2, 0), z) + cmultiply(c - dvec2(1, 0), c - dvec2(degree, 0)) + dvec2(1, 0)), degree)", .condition = "length(z) >= 100 || length(z - dvec2(1,0)) <= 1e-5", .initialz = "dvec2(0)", .degree = 2.f, .continuous_compatible = false}),
    Fractal({.name = "Lambda",       .equation = "cmultiply(c, cmultiply(z, cpow(dvec2(1, 0) - z, degree - 1)))", .condition = "xsq + ysq > 100", .initialz = "dvec2(0.5f, 0.f)", .degree = 2.f, .continuous_compatible=true}),
    Fractal({.name = "Tricorn",      .equation = "cpow(cconj(z), degree) + c", .condition = "distance(z, c) > 10", .initialz = "c", .degree = 2.f, .continuous_compatible = true}),
};

struct Config {
    dvec2  offset = { -0.4, 0.0 };
    ivec2  screenSize = { 1000, 600 };
    double zoom = 5.0;
    float  spectrum_offset = 860.f;
    float  iter_multiplier = 12.f;
    bool   auto_adjust_iter = true;
    int    max_iters = 100;
    float  iter_co = 1.045f;
    bool   continuous_coloring = true;
    bool   normal_map_effect = false;
    fvec3 set_color = { 0.f, 0.f, 0.f };
    int    ssaa = 2;
    int    transfer_function = 0;
    // experimental
    float  degree = 2.f;
    // normal mapping
    float  angle = 180.f;
    float  height = 1.5f;
};

struct ZoomSequenceConfig {
    Config tcfg;
    int fps = 30;
    int duration = 30;
    int direction = 0;
    char path[256]{};

    bool ease_inout = true;
};

class MV2 {
    GLFWwindow* window = nullptr;
    ImFont* font_title = nullptr;

    std::vector<vec4> paletteData = {
        {0.0000f, 0.0274f, 0.3921f, 0.0000f},
        {0.1254f, 0.4196f, 0.7960f, 0.1600f},
        {0.9294f, 1.0000f, 1.0000f, 0.4200f},
        {1.0000f, 0.6666f, 0.0000f, 0.6425f},
        {0.0000f, 0.0078f, 0.0000f, 0.8575f},
        {0.0000f, 0.0274f, 0.3921f, 1.0000f}
    };
    int span = 1000;
    const int max_colors = 8;

    ivec2 screenPos = { 0, 0 };
    bool fullscreen = false;
    float dpi_scale = 1.f;
    Config config;
    int fractal = 1;
    bool startup_anim_complete = false;
    ZoomSequenceConfig zsc;
    int julia_size = 215;
    double julia_zoom = 3;
    double fps_update_interval = 0.03;
    bool juliaset = true;
    bool orbit = true;
    bool cmplxinfo = true;

    dvec2 oldPos = { 0, 0 };
    dvec2 lastPresses = { -doubleClick_interval, 0 };
    bool dragging = false;
    bool rightClickHold = false;
    dvec2 tempOffset = config.offset;
    double tempZoom = config.zoom;

    dvec2 cmplxCoord;
    int numIterations;

    bool recording = false;
    bool paused = false;
    int progress = 0;

    GLuint shaderProgram  = NULL;
    GLuint vertexShader = NULL;

    GLuint mandelbrotFrameBuffer = NULL;
    GLuint postprocFrameBuffer = NULL;
    GLuint finalFrameBuffer = NULL;
    GLuint juliaFrameBuffer = NULL;

    GLuint mandelbrotTexBuffer = NULL;
    GLuint postprocTexBuffer = NULL;
    GLuint finalTexBuffer = NULL;
    GLuint juliaTexBuffer = NULL;

    GLuint paletteBuffer = NULL;
    GLuint orbitBuffer = NULL;
    GLuint sliderBuffer = NULL;
    GLuint kernelBuffer = NULL;

    int32_t stateID = 10;
    ImGradientHDRState state;
    ImGradientHDRTemporaryState tempState;

    int op = MV_COMPUTE;
public:
    MV2() {
        glfwInit();

        std::cout << "init\n";

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());

        glfwWindowHint(GLFW_RED_BITS, mode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
        glfwWindowHintString(GLFW_WAYLAND_APP_ID, "mv2");
        monitorSize.x = mode->width;
        monitorSize.y = mode->height;

        const char* session = std::getenv("XDG_SESSION_DESKTOP");
        const char* hyprSig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");

        window = glfwCreateWindow(config.screenSize.x, config.screenSize.y, "Mandelbrot Voyage", NULL, NULL);
        if (window == nullptr) {
            std::cout << "Failed to create OpenGL window" << std::endl;
            return;
        }

#ifdef PLATFORM_LINUX
        const char* wayland_display = std::getenv("WAYLAND_DISPLAY");
        const char* x11_display = std::getenv("DISPLAY");

        if (wayland_display) {
            if ((session && std::string(session) == "Hyprland") || (hyprSig != nullptr)) {
                auto exec_command = [](const char* cmd) {
                    std::array<char, 128> buffer;
                    std::stringstream result;
                    FILE* pipe = popen(cmd, "r");
                    if (!pipe) return std::string();
                    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
                        result << buffer.data();
                    }
                    pclose(pipe);
                    return result.str();
                };
                std::string json = exec_command("hyprctl monitors -j");
                if (!json.empty()) {
                    auto parsedjson = nlohmann::json::parse(json);
                    std::string monitor = nlohmann::json::parse(exec_command("hyprctl activeworkspace -j"))["monitor"];
                    for (const auto& m : parsedjson) {
                        if (m["name"] == monitor) {
                            dpi_scale = m["scale"].get<float>();
                            break;
                        }
                    }
                }
            }
            else {
                float xscale, yscale;
                glfwGetWindowContentScale(window, &xscale, &yscale);
                dpi_scale = xscale;
            }
        }
#endif
        glfwSetWindowUserPointer(window, this);
        glfwSwapInterval(1);
        glfwMakeContextCurrent(window);

        glfwSetFramebufferSizeCallback(window, on_windowResize);
        glfwSetCursorPosCallback(window, on_cursorMove);
        glfwSetMouseButtonCallback(window, on_mouseButton);
        glfwSetScrollCallback(window, on_mouseScroll);
        glfwSetKeyCallback(window, on_keyPress);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.Fonts->AddFontDefault();
#ifndef PLATFORM_WINDOWS
        auto font = b::embed<"assets/consola.ttf">();
        font_title = io.Fonts->AddFontFromMemoryTTF((void*)font.data(), font.size(), 11.f, nullptr);
#else
        font_title = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 11.f, nullptr);
        BOOL use_dark_mode = true;
        DwmSetWindowAttribute(glfwGetWin32Window(window), 20, &use_dark_mode, sizeof(use_dark_mode));
#endif
        IM_ASSERT(font_title != NULL);
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = NULL;
        io.LogFilename = NULL;

        ImGui::StyleColorsDark();
        ImGui::LoadTheme();

        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 460");

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cout << "Failed to create OpenGL window" << std::endl;
            return;
        }

        glGenTextures(1, &mandelbrotTexBuffer);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mandelbrotTexBuffer);
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, config.screenSize.x * config.ssaa,
            config.screenSize.y * config.ssaa, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        

        glGenTextures(1, &postprocTexBuffer);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, config.screenSize.x * config.ssaa,
            config.screenSize.y * config.ssaa, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenTextures(1, &finalTexBuffer);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, finalTexBuffer);
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1920, 1080, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenTextures(1, &juliaTexBuffer);
        glBindTexture(GL_TEXTURE_2D, juliaTexBuffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, julia_size * config.ssaa,
            julia_size * config.ssaa, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenFramebuffers(1, &mandelbrotFrameBuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, mandelbrotFrameBuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mandelbrotTexBuffer, 0);

        glGenFramebuffers(1, &postprocFrameBuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, postprocFrameBuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, postprocTexBuffer, 0);

        glGenFramebuffers(1, &finalFrameBuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, finalFrameBuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, finalTexBuffer, 0);

        glGenFramebuffers(1, &juliaFrameBuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, juliaFrameBuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, juliaTexBuffer, 0);

        vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
        glCompileShader(vertexShader);
        int success;
        char infoLog[512];
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
            std::cout << infoLog << std::endl;
            return;
        }

        b::EmbedInternal::EmbeddedFile embed;
        const char* content;
        size_t length;

        unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        embed = b::embed<"shaders/render.glsl">();
        content = embed.data();
        length = embed.length();
        
        char* modifiedSource = new char[length + 4096];
        sprintf(modifiedSource, content, fractals[fractal].equation.data(), 0, fractals[fractal].condition.data(), fractals[fractal].initialz.data(), fractals[fractal].condition.data(), fractals[fractal].initialz.data());
        glShaderSource(fragmentShader, 1, &modifiedSource, NULL);
        glCompileShader(fragmentShader);
        glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
            std::cout << infoLog << std::endl;
            return;
        }

        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        std::cout << "attach\n";
        glLinkProgram(shaderProgram);
        std::cout << "link\n";
        glDeleteShader(fragmentShader);
        delete[] modifiedSource;

        unsigned int VBO, VAO;
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 2, GL_FLOAT, false, 2 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ARRAY_BUFFER, NULL);

        glUseProgram(shaderProgram);

        glUniform1i(glGetUniformLocation(shaderProgram, "mandelbrotTex"), 0);
        glUniform1i(glGetUniformLocation(shaderProgram, "postprocTex"), 1);
        glUniform1i(glGetUniformLocation(shaderProgram, "finalTex"), 2);

        glGenBuffers(1, &orbitBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, orbitBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, orbitBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "orbit"), 0);
        glUniform1i(glGetUniformLocation(shaderProgram, "numVertices"), 0);

        glGenBuffers(1, &paletteBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, paletteBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, max_colors * sizeof(fvec4), paletteData.data(), GL_DYNAMIC_COPY);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, paletteBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "spectrum"), 1);
        glUniform1i(glGetUniformLocation(shaderProgram, "span"), span);

        glGenBuffers(1, &sliderBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, sliderBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, sliderBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "variables"), 2);

        glGenBuffers(1, &kernelBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, kernelBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, kernelBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "kernel"), 3);
        upload_kernel(config.ssaa);

        use_config(config, true, false);
        on_windowResize(window, config.screenSize.x * dpi_scale, config.screenSize.y * dpi_scale);

        for (const vec4& c : paletteData) {
            state.AddColorMarker(c.w, { c.r, c.g, c.b }, 1.0f);
        }
    }
private:
    void use_config(Config config, bool variables = true, bool textures = true) {
        if (variables) {
            glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), config.screenSize.x, config.screenSize.y);
            glUniform2d(glGetUniformLocation(shaderProgram, "offset"), config.offset.x, config.offset.y);
            glUniform1f(glGetUniformLocation(shaderProgram, "iter_multiplier"), config.iter_multiplier);
            glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), config.zoom);
            if (!config.auto_adjust_iter) {
                glUniform1i(glGetUniformLocation(shaderProgram, "max_iters"), config.max_iters);
            } else {
                glUniform1i(glGetUniformLocation(shaderProgram, "max_iters"), max_iters(config.zoom, zoom_co, config.iter_co));
            }
            glUniform1f(glGetUniformLocation(shaderProgram, "spectrum_offset"), config.spectrum_offset);
            glUniform1i(glGetUniformLocation(shaderProgram, "continuous_coloring"), config.continuous_coloring);
            glUniform1i(glGetUniformLocation(shaderProgram, "normal_map_effect"), config.normal_map_effect);
            glUniform3f(glGetUniformLocation(shaderProgram, "set_color"), config.set_color.x, config.set_color.y, config.set_color.z);
            glUniform1d(glGetUniformLocation(shaderProgram, "julia_zoom"), julia_zoom);
            glUniform1i(glGetUniformLocation(shaderProgram, "julia_maxiters"), max_iters(julia_zoom, zoom_co, config.iter_co, 3.0));
            glUniform1i(glGetUniformLocation(shaderProgram, "blur"), (config.ssaa ? config.ssaa : 1));
            glUniform1i(glGetUniformLocation(shaderProgram, "transfer_function"), config.transfer_function);

            glUniform1f(glGetUniformLocation(shaderProgram, "degree"), config.degree);

            glUniform1f(glGetUniformLocation(shaderProgram, "angle"), config.angle);
            glUniform1f(glGetUniformLocation(shaderProgram, "height"), config.height);

            glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "orbit"), 0);
            glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "spectrum"), 1);
        }
        if (textures) {
            glBindFramebuffer(GL_FRAMEBUFFER, mandelbrotFrameBuffer);
            glBindTexture(GL_TEXTURE_2D, mandelbrotTexBuffer);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, config.screenSize.x * config.ssaa, config.screenSize.y * config.ssaa, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

            glBindFramebuffer(GL_FRAMEBUFFER, postprocFrameBuffer);
            glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, config.screenSize.x * config.ssaa, config.screenSize.y * config.ssaa, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        }
    }


    void set_op(int p, bool override = false) {
        if (p > op || override) op = p;
    }

    static dvec2 pixel_to_complex(dvec2 pixelCoord, ivec2 screenSize, double zoom, dvec2 offset) {
        return ((dvec2(pixelCoord.x / screenSize.x, (screenSize.y - pixelCoord.y) / screenSize.y)) - dvec2(0.5, 0.5)) *
            dvec2(zoom, (screenSize.y * zoom) / screenSize.x) + offset;
    }
    static dvec2 pixel_to_complex(MV2* app, dvec2 pixelCoord) {
        ivec2 ss = (app->fullscreen ? monitorSize : app->config.screenSize);
        return ((dvec2(pixelCoord.x / ss.x, pixelCoord.y / ss.y)) - dvec2(0.5, 0.5)) *
            dvec2(app->config.zoom, (ss.y * app->config.zoom) / ss.x) + app->config.offset;
    }
    static dvec2 complex_to_pixel(dvec2 complexCoord, ivec2 screenSize, double zoom, dvec2 offset) {
        dvec2 normalizedCoord = (complexCoord - offset);
        normalizedCoord /= dvec2(zoom, (screenSize.y * zoom) / screenSize.x);
        dvec2 pixelCoordNormalized = normalizedCoord + dvec2(0.5, 0.5);
        return dvec2(pixelCoordNormalized.x * screenSize.x, screenSize.y - pixelCoordNormalized.y * screenSize.y);
    }
    static int max_iters(double zoom, double zoom_co, double iter_co, double initial_zoom = 5.0) {
        return 100 * std::max(1.0, pow(iter_co, log2(zoom / initial_zoom) / log2(zoom_co)));
    }

    // https://stackoverflow.com/a/8204886/15514474
    static std::vector<float> generate_kernel(int radius) {  
        auto gaussian = [](float x, float mu, float sigma) -> float {
            const float a = (x - mu) / sigma;
            return std::exp(-0.5 * a * a);
        };
        const float sigma = radius / 2.f;
        int rowLength = 2 * radius + 1;
        std::vector<float> kernel(rowLength * rowLength);
        float sum = 0;
        for (uint64_t row = 0; row < rowLength; row++) {
            for (uint64_t col = 0; col < rowLength; col++) {
                float x = gaussian(row, radius, sigma) * gaussian(col, radius, sigma);
                kernel[row * rowLength + col] = x;
                sum += x;
            }
        }
        for (uint64_t row = 0; row < rowLength; row++) {
            for (uint64_t col = 0; col < rowLength; col++) {
                kernel[row * rowLength + col] /= sum;
            }
        }
        return kernel;
    }
    void upload_kernel(int radius) {
        std::vector<float> kernel = generate_kernel(radius);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, kernelBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, kernel.size() * sizeof(float), kernel.data(), GL_STATIC_DRAW);
        glUniform1i(glGetUniformLocation(shaderProgram, "radius"), radius);
    }

    static void on_windowResize(GLFWwindow* window, int width, int height) {
        MV2* app = static_cast<MV2*>(glfwGetWindowUserPointer(window));
        if (app->config.screenSize.x == width && app->config.screenSize.y == height && app->fullscreen) {
            app->set_op(MV_RENDER);
            return;
        }
        glViewport(0, 0, width, height);

        if (!app->fullscreen) app->config.screenSize = { width, height };
        if (app->shaderProgram)
            glUniform2i(glGetUniformLocation(app->shaderProgram, "screenSize"), width, height);
        app->set_op(MV_COMPUTE);

        glBindFramebuffer(GL_FRAMEBUFFER, app->mandelbrotFrameBuffer);
        glBindTexture(GL_TEXTURE_2D, app->mandelbrotTexBuffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width * app->config.ssaa, height * app->config.ssaa, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        glBindFramebuffer(GL_FRAMEBUFFER, app->postprocFrameBuffer);
        glBindTexture(GL_TEXTURE_2D, app->postprocTexBuffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width * app->config.ssaa, height * app->config.ssaa, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }

    static void on_mouseButton(GLFWwindow* window, int button, int action, int mod) {
        MV2* app = static_cast<MV2*>(glfwGetWindowUserPointer(window));
        if (!app->shaderProgram) return;
        ivec2 ss = (app->fullscreen ? monitorSize : app->config.screenSize);
        if (ImGui::GetIO().WantCaptureMouse) return;
        switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT:
            if (action == GLFW_PRESS) {
                app->dragging = true;
                app->lastPresses.x = app->lastPresses.y;
                app->lastPresses.y = glfwGetTime();
                glfwGetCursorPos(window, &app->oldPos.x, &app->oldPos.y);
                app->oldPos *= app->dpi_scale;
            }
            else if (app->lastPresses.y - app->lastPresses.x < doubleClick_interval) {
                auto switch_shader = [&]() {
                    b::EmbedInternal::EmbeddedFile embed;
                    const char* content;
                    int length;

                    GLuint shader = NULL;
                    GLint success = false;
                    char infoLog[512];
                    embed = b::embed<"shaders/render.glsl">();
                    content = embed.data();
                    length = embed.length();
                    app->compile_shader(shader, &success, infoLog, content, length);
                    app->reload_shader(shader);
                    app->update_shader();
                    app->set_op(MV_COMPUTE, true);
                };
                if (app->rightClickHold && app->fractal == 1 && app->juliaset) {
                    app->fractal = 2;
                    double x, y;
                    glfwGetCursorPos(window, &x, &y);
                    x *= app->dpi_scale;
                    y *= app->dpi_scale;
                    dvec2 cmplx = app->pixel_to_complex(app, dvec2(x, y));
                    fractals[2].sliders[0].value = cmplx.x;
                    fractals[2].sliders[1].value = cmplx.y;
                    switch_shader();
                    app->tempOffset = app->config.offset;
                    app->tempZoom = app->config.zoom;
                }
                else if (app->rightClickHold && app->fractal == 2) {
                    app->fractal = 1;
                    app->config.offset = app->tempOffset;
                    app->config.zoom = app->tempZoom;
                    switch_shader();
                }
                else {
                    glfwGetCursorPos(window, &app->oldPos.x, &app->oldPos.y);
                    app->oldPos *= app->dpi_scale;
                    dvec2 pos = pixel_to_complex(app, app->oldPos);
                    dvec2 center = pixel_to_complex(app, static_cast<dvec2>(ss) / 2.0);
                    app->config.offset += pos - center;
                    glUniform2d(glGetUniformLocation(app->shaderProgram, "offset"), app->config.offset.x, app->config.offset.y);
                }
                app->dragging = false;
                app->set_op(MV_COMPUTE);
            }
            else {
                app->dragging = false;
            }
            break;
        case GLFW_MOUSE_BUTTON_RIGHT:
            switch (action) {
            case GLFW_PRESS:
                app->rightClickHold = true;
                app->julia_zoom = 3.0;
                glUniform1d(glGetUniformLocation(app->shaderProgram, "julia_zoom"), app->julia_zoom);
                glUniform1i(glGetUniformLocation(app->shaderProgram, "julia_maxiters"),
                    max_iters(app->julia_zoom, zoom_co, app->config.iter_co, 3.0));
                app->refresh_rightclick();
                break;
            case GLFW_RELEASE:
                app->rightClickHold = false;
            }
        }
    }

    static void on_cursorMove(GLFWwindow* window, double x, double y) {
        MV2* app = static_cast<MV2*>(glfwGetWindowUserPointer(window));
        x *= app->dpi_scale;
        y *= app->dpi_scale;
        if (!app->shaderProgram) return;
        ivec2 ss = (app->fullscreen ? monitorSize : app->config.screenSize);
        glUniform2d(glGetUniformLocation(app->shaderProgram, "mousePos"), x / ss.x, y / ss.y);
        if (ImGui::GetIO().WantCaptureMouse)
            return;
        if (app->dragging) {
            app->lastPresses = { -doubleClick_interval, 0 };
            app->config.offset.x -= ((x - app->oldPos.x) * app->config.zoom) / ss.x;
            app->config.offset.y -= ((y - app->oldPos.y) * ((app->config.zoom * ss.y) / ss.x)) / ss.y;
            glUniform2d(glGetUniformLocation(app->shaderProgram, "offset"), app->config.offset.x, app->config.offset.y);
            app->oldPos = { x, y };
            app->set_op(MV_COMPUTE);
        }
        if (app->rightClickHold) {
            app->refresh_rightclick();
        }
    }

    static void on_mouseScroll(GLFWwindow* window, double x, double y) {
        MV2* app = static_cast<MV2*>(glfwGetWindowUserPointer(window));
        if (!app->shaderProgram) return;
        if (app->rightClickHold) {
            app->julia_zoom *= pow(zoom_co, y * 1.5);
            glUniform1d(glGetUniformLocation(app->shaderProgram, "julia_zoom"), app->julia_zoom);
            glUniform1i(glGetUniformLocation(app->shaderProgram, "julia_maxiters"), max_iters(app->julia_zoom, zoom_co, app->config.iter_co, 3.0));
            glBindFramebuffer(GL_FRAMEBUFFER, app->juliaFrameBuffer);
            glViewport(0, 0, app->julia_size * app->config.ssaa, app->julia_size * app->config.ssaa);
            glUniform2i(glGetUniformLocation(app->shaderProgram, "screenSize"), app->julia_size * app->config.ssaa, app->julia_size * app->config.ssaa);
            glUniform1i(glGetUniformLocation(app->shaderProgram, "op"), 4);
            if (app->juliaset) glDrawArrays(GL_TRIANGLES, 0, 6);
            app->refresh_rightclick();
        }
        else if (!ImGui::GetIO().WantCaptureMouse) {
            app->config.zoom *= pow(zoom_co, y * 1.5);
            glUniform1d(glGetUniformLocation(app->shaderProgram, "zoom"), app->config.zoom);
            if (app->config.auto_adjust_iter) {
                app->config.max_iters = max_iters(app->config.zoom, zoom_co, app->config.iter_co);
                glUniform1i(glGetUniformLocation(app->shaderProgram, "max_iters"), app->config.max_iters);
            }
            app->set_op(MV_COMPUTE);
        }
    }

    static void on_keyPress(GLFWwindow* window, int key, int scancode, int action, int mods) {
        MV2* app = static_cast<MV2*>(glfwGetWindowUserPointer(window));
        if (!app->shaderProgram) return;
        if (action != GLFW_PRESS) return;
        switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, true);
            break;
        case GLFW_KEY_F11:
            if (glfwGetWindowMonitor(window) == nullptr) {
                app->fullscreen = true;
                GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                glfwGetWindowPos(window, &app->screenPos.x, &app->screenPos.y);
                glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
            }
            else {
                app->fullscreen = false;
                glfwSetWindowMonitor(window, nullptr, app->screenPos.x, app->screenPos.y, app->config.screenSize.x, app->config.screenSize.y, 0);
            }
        }
    }

    void refresh_rightclick() {
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        x *= dpi_scale;
        y *= dpi_scale;
        ivec2 ss = (fullscreen ? monitorSize : config.screenSize);

        cmplxCoord = pixel_to_complex(this, { x, y });
        
        if (cmplxinfo) {
            float texel[4];
            glBindFramebuffer(GL_FRAMEBUFFER, mandelbrotFrameBuffer);
            glReadBuffer(GL_FRONT);
            glReadPixels(config.ssaa * x, (ss.y - y) * config.ssaa, 1, 1, GL_RGBA, GL_FLOAT, texel);
            numIterations = static_cast<int>(texel[1]);
        }
        if (juliaset) {
            glViewport(0, 0, julia_size * config.ssaa, julia_size * config.ssaa);
            glBindFramebuffer(GL_FRAMEBUFFER, juliaFrameBuffer);
            glUniform1i(glGetUniformLocation(shaderProgram, "op"), 3);
            glUniform2d(glGetUniformLocation(shaderProgram, "mouseCoord"), cmplxCoord.x, cmplxCoord.y);
            glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), julia_size * config.ssaa, julia_size * config.ssaa);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        if (orbit) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, orbitBuffer);
            glUniform2d(glGetUniformLocation(shaderProgram, "mousePos"), x, ss.y - y);
            glUniform1i(glGetUniformLocation(shaderProgram, "numVertices"), 400);
            glBufferData(GL_SHADER_STORAGE_BUFFER, 400 * sizeof(vec2), nullptr, GL_DYNAMIC_COPY);
            set_op(MV_POSTPROC);
        }
    }

    void compile_shader(GLuint& shader, GLint* success, char* infoLog, const char* fragmentSource, size_t length) {
        char* modifiedSource = new char[length + 4096];
        if (shader) glDeleteShader(shader);
        shader = glCreateShader(GL_FRAGMENT_SHADER);

        auto replace_variables = [&](std::string& str) {
            for (int i = 0; i < fractals[fractal].sliders.size(); i++) {
                std::string pattern = "\\b";
                pattern.append(fractals[fractal].sliders[i].name.c_str());
                pattern.append("\\b");
                str = std::regex_replace(str, std::regex(pattern), std::format("sliders[{}]", i));
            }
        };

        std::string eq = fractals[fractal].equation.data(), cond = fractals[fractal].condition.data(), init = fractals[fractal].initialz.data();
        replace_variables(eq);
        replace_variables(cond);
        replace_variables(init);

        sprintf(modifiedSource, fragmentSource, eq.data(), 1, cond.data(), init.data(), cond.data(), init.data());
        glShaderSource(shader, 1, &modifiedSource, NULL);
        glCompileShader(shader);
        glGetShaderiv(shader, GL_COMPILE_STATUS, success);
        if (!*success) {
            glGetShaderInfoLog(shader, 512, NULL, infoLog);
        }
        else infoLog[0] = '\0';

        delete[] modifiedSource;
    }

    void reload_shader(GLuint shader) {
        glDeleteProgram(shaderProgram);
        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, shader);
        glLinkProgram(shaderProgram);
        glDeleteShader(vertexShader);
        glDeleteShader(shader);
        glUseProgram(shaderProgram);
        config.normal_map_effect = false;
        use_config(config, true, false);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, orbitBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, orbitBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "orbit"), 0);
        glUniform1i(glGetUniformLocation(shaderProgram, "numVertices"), 0);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, paletteBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, paletteBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "spectrum"), 1);
        glUniform1i(glGetUniformLocation(shaderProgram, "span"), span);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, sliderBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, sliderBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "variables"), 2);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, kernelBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, kernelBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "kernel"), 3);
        glUniform1i(glGetUniformLocation(shaderProgram, "radius"), config.ssaa);
    }

    void update_shader() const {
        glUniform1f(glGetUniformLocation(shaderProgram, "degree"), config.degree);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, sliderBuffer);
        std::vector<float> values(fractals[fractal].sliders.size());
        for (int i = 0; i < values.size(); i++) {
            values[i] = fractals[fractal].sliders[i].value;
        }
        glBufferData(GL_SHADER_STORAGE_BUFFER, values.size() * sizeof(float), values.data(), GL_DYNAMIC_DRAW);
    }
public:
    void mainloop() {
        do {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ivec2 ss = (fullscreen ? monitorSize : config.screenSize);
            
            double currentTime = glfwGetTime();

            if (currentTime < 2.f) {
                config.degree = (currentTime < 0.5f) ? 1.f : (2.f - pow(1.f - (currentTime - 0.5f) / 1.5f, 11));
                update_shader();
                set_op(MV_COMPUTE);
            }
            else if (!startup_anim_complete) {
                config.degree = 2.f;
                startup_anim_complete = true;
                update_shader();
                set_op(MV_COMPUTE);
            }

            ImGui::PushFont(font_title);
            ImGui::SetNextWindowPos({ 5.f * dpi_scale, 5.f * dpi_scale });
            ImGui::SetNextWindowSize(ImVec2(310.f, 0.f));
            ImGui::SetNextWindowCollapsed(true, 1 << 1);
            if (ImGui::Begin("Settings", nullptr,
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoMove
            )) {
                if (ImGui::BeginPopupModal("Zoom video creator", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    if (recording) ImGui::BeginDisabled();
                    if (ImGui::Button("Browse...")) {
                        char const* lFilterPatterns[1] = { "*.avi" };
                        auto t = std::time(nullptr);
                        auto tm = *std::localtime(&t);
                        std::ostringstream oss;
                        oss << std::put_time(&tm, "MV2 %d-%m-%Y %H-%M-%S");
                        char const* buf = tinyfd_saveFileDialog("Save video", oss.str().c_str(), 1, lFilterPatterns, "Video Files (*.avi)");
                        if (glfwGetWindowMonitor(window) != nullptr) glfwRestoreWindow(window);
                        glfwShowWindow(window);
                        if (buf) strcpy(zsc.path, buf);
                    }
                    ImGui::SameLine();
                    ImGui::PushItemWidth(330);
                    ImGui::InputTextWithHint("##output", "Output", zsc.path, 256, ImGuiInputTextFlags_None, nullptr, nullptr);
                    ImGui::PushItemWidth(80);
                    ImGui::InputInt("FPS", &zsc.fps, 1, 5);
                    ImGui::SameLine();
                    ImGui::InputInt("Duration", &zsc.duration, 5, 20);
                    ImGui::SameLine();
                    const std::vector<ivec2> commonres = {
                        { 640,  480  },
                        { 1280, 720  },
                        { 1920, 1080 },
                        { 2560, 1440 },
                        { 3840, 2160 }
                    };
                    static int res = 2;
                    zsc.tcfg.screenSize = commonres[res];
                    auto vec_to_str = [commonres](int i) {
                        return std::format("{}x{}", commonres.at(i).x, commonres.at(i).y);
                        };
                    std::string preview = vec_to_str(res);

                    if (ImGui::BeginCombo("Resolution", preview.c_str())) {
                        for (int i = 0; i < commonres.size(); i++) {
                            const bool is_selected = (res == i);
                            if (ImGui::Selectable(vec_to_str(i).c_str(), is_selected)) {
                                zsc.tcfg.screenSize = commonres.at(i);
                                glBindFramebuffer(GL_FRAMEBUFFER, finalFrameBuffer);
                                glBindTexture(GL_TEXTURE_2D, finalTexBuffer);
                                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, zsc.tcfg.screenSize.x, zsc.tcfg.screenSize.y, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
                                res = i;
                            }
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    const char* dirs[] = { "Zoom in", "Zoom out" };
                    preview = dirs[zsc.direction];

                    if (ImGui::BeginCombo("Direction", preview.c_str())) {
                        for (int i = 0; i < 2; i++) {
                            const bool is_selected = (zsc.direction == i);
                            if (ImGui::Selectable(dirs[i], is_selected)) {
                                zsc.direction = i;
                            }
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();

                    const char* factors[] = { "None", "2X", "4X", "8X" };
                    int idx = static_cast<int>(log2(static_cast<float>(zsc.tcfg.ssaa)));
                    preview = factors[idx];

                    ImGui::PushItemWidth(44);
                    if (ImGui::BeginCombo("SSAA", preview.c_str())) {
                        for (int i = 0; i < 4; i++) {
                            const bool is_selected = (idx == i);
                            if (ImGui::Selectable(factors[i], is_selected)) {
                                zsc.tcfg.ssaa = pow(2, i);
                            }
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    ImGui::Checkbox("Ease in/out", &zsc.ease_inout);

                    if (strlen(zsc.path) == 0 && !recording) ImGui::BeginDisabled();
                    if (!recording && ImGui::Button("Render", ImVec2(100, 0))) {
                        recording = true;
                        progress = 0;
                        glfwSwapInterval(0);
                        zsc.tcfg.zoom = 8.0f;
                        //writer = cv::VideoWriter(zsc.path, cv::VideoWriter::fourcc('m', 'j', 'p', 'g'), zsc.fps, cv::Size(zsc.tcfg.screenSize.x, zsc.tcfg.screenSize.y));

                        //if (!writer.isOpened()) throw Error("Failed to initialize sequencer");
                        use_config(zsc.tcfg, true, true);
                        upload_kernel(zsc.tcfg.ssaa);
                        ImGui::BeginDisabled();
                    }
                    if ((strlen(zsc.path) == 0 && !recording) || recording)
                        ImGui::EndDisabled();
                    if (recording && ImGui::Button(paused ? "Resume" : "Pause", ImVec2(100, 0))) {
                        paused ^= 1;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                        if (recording && progress != 0) {
                            //writer.release();
                            std::filesystem::remove(zsc.path);
                            glfwSwapInterval(1);
                        }
                        recording = false;
                        progress = 0;
                        use_config(config, true, true);
                        upload_kernel(config.ssaa);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (recording) {
                        const ImU32 col = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
                        const ImU32 bg = ImGui::GetColorU32(ImGuiCol_Button);

                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1.f);
                        ImGui::Spinner("##spinner", 5, 2, col);
                        ImGui::SameLine();
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.f);
                        ImGui::BufferingBar("##buffer_bar", progress / static_cast<float>(zsc.fps * zsc.duration), ImVec2(182, 6), bg, col);
                    }
                    ImGui::EndPopup();
                }

                if (ImGui::BeginPopupModal("About Mandelbrot Voyage II", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
                    ImGui::Text("Version v" VERSION " (Build date: " __DATE__ " " __TIME__ ")\n\nMV2 is a fully interactive open-source GPU-based fully customizable fractal zoom\nprogram aimed at creating artistic and high quality images & videos.");
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 150, 255));
                    ImGui::Text("Copyright (c) 2018-2025 Yilmaz Alpaslan");
                    ImGui::PopStyleColor();
                    if (ImGui::Button("Open GitHub Page")) {
#ifdef PLATFORM_WINDOWS
                        ShellExecuteW(0, 0, L"https://github.com/Yilmaz4/MV2", 0, 0, SW_SHOW);
#elif defined(PLATFORM_LINUX)
                        system("xdg-open https://github.com/Yilmaz4/MV2");
#endif
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Close"))
                        ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }

                if (ImGui::Button("Take screenshot")) {
                    int w, h;
                    if (fullscreen) {
                        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                        w = mode->width, h = mode->height;
                    }
                    else w = config.screenSize.x, h = config.screenSize.y;
                    unsigned char* buffer = new unsigned char[4 * w * h];
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    glReadBuffer(GL_BACK);
                    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buffer);

                    char const* lFilterPatterns[1] = { "*.png" };
                    auto t = std::time(nullptr);
                    auto tm = *std::localtime(&t);
                    std::ostringstream oss;
                    oss << std::put_time(&tm, "MV2 %d-%m-%Y %H-%M-%S");
                    const char* path = tinyfd_saveFileDialog("Save screenshot", oss.str().c_str(), 1, lFilterPatterns, "PNG (*.png)");
                    if (glfwGetWindowMonitor(window) != nullptr) glfwRestoreWindow(window);
                    glfwShowWindow(window);
                    if (path) {
                        stbi_flip_vertically_on_write(true);
                        stbi_write_png(path, w, h, 4, buffer, 4 * w);
                    }
                    delete[] buffer;
                }
                ImGui::SameLine();

                if (ImGui::Button("Create zoom video")) {
                    zsc.tcfg = config;
                    ImGui::OpenPopup("Zoom video creator");
                }
                ImGui::SameLine();
                if (ImGui::Button("About", ImVec2(ImGui::GetContentRegionAvail().x, 0.f)))
                    ImGui::OpenPopup("About Mandelbrot Voyage II");
                ImGui::SeparatorText("Parameters");
                bool update = false;

                ImGui::Text("Re");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
                update |= ImGui::InputDouble("##re", &config.offset.x, 0.0, 0.0, "%.17g");

                ImGui::Text("Im");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
                update |= ImGui::InputDouble("##im", &config.offset.y, 0.0, 0.0, "%.17g");
                
                if (update) {
                    glUniform2d(glGetUniformLocation(shaderProgram, "offset"), config.offset.x, config.offset.y);
                    set_op(MV_COMPUTE);
                }
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.f);
                ImGui::Text("Zoom"); ImGui::SetNextItemWidth(80); ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
                if (ImGui::InputDouble("##zoom", &config.zoom, 0.0, 0.0, "%.2e")) {
                    glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), config.zoom);
                    set_op(MV_COMPUTE);
                }
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
                if (ImGui::Button("Save", ImVec2(ImGui::GetContentRegionAvail().x / 2.f - 1.f, 0))) {
                    const char* lFilterPatterns[1] = { "*.mvl" };
                    auto t = std::time(nullptr);
                    auto tm = *std::localtime(&t);
                    std::ostringstream oss;
                    oss << std::put_time(&tm, "MV2 %d-%m-%Y %H-%M-%S");
                    const char* buf = tinyfd_saveFileDialog("Save location", oss.str().c_str(),
                        1, lFilterPatterns, "MV2 Location File (*.mvl)");
                    if (glfwGetWindowMonitor(window) != nullptr) glfwRestoreWindow(window);
                    glfwShowWindow(window);
                    if (buf != nullptr) {
                        std::ofstream fout;
                        fout.open(buf, std::ios::binary | std::ios::out | std::ofstream::trunc);
                        fout.write(reinterpret_cast<const char*>(value_ptr(config.offset)), sizeof(dvec2));
                        fout.write(reinterpret_cast<const char*>(&config.zoom), sizeof(double));
                        fout.close();
                    }
                }
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
                if (ImGui::Button("Load", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                    const char* lFilterPatterns[1] = { "*.mvl" };
                    char* buf = tinyfd_openFileDialog("Open location", nullptr,
                        1, lFilterPatterns, "MV2 Location File (*.mvl)", false);
                    if (glfwGetWindowMonitor(window) != nullptr) glfwRestoreWindow(window);
                    glfwShowWindow(window);
                    if (buf != nullptr) {
                        std::ifstream fin;
                        fin.open(buf, std::ios::binary | std::ios::in | std::ios::ate);
                        int size = static_cast<int>(fin.tellg());
                        if (size % 4 != 0 || (size /= 16) > 16) {
                            throw Error("Palette file invalid");
                        }
                        paletteData.resize(size);
                        fin.seekg(0);
                        fin.read(reinterpret_cast<char*>(value_ptr(config.offset)), sizeof(dvec2));
                        fin.read(reinterpret_cast<char*>(&config.zoom), sizeof(double));
                        fin.close();
                        glUniform2d(glGetUniformLocation(shaderProgram, "offset"), config.offset.x, config.offset.y);
                        glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), config.zoom);
                        set_op(MV_COMPUTE);
                    }
                }

                ImGui::BeginGroup();
                ImGui::SeparatorText("Computation");
                if (ImGui::DragInt("Maximum iterations", &config.max_iters, abs(config.max_iters) / 20.f, 10, INT_MAX, "%d", ImGuiSliderFlags_AlwaysClamp)) {
                    glUniform1i(glGetUniformLocation(shaderProgram, "max_iters"), config.max_iters);
                    set_op(MV_COMPUTE);
                }
                if (ImGui::Checkbox("Adjust automatically", &config.auto_adjust_iter) || config.auto_adjust_iter && ImGui::SliderFloat("Iteration coeff.", &config.iter_co, 1.01, 1.1)) {
                    config.max_iters = max_iters(config.zoom, zoom_co, config.iter_co);
                    glUniform1i(glGetUniformLocation(shaderProgram, "max_iters"), config.max_iters);
                    set_op(MV_COMPUTE);
                }
                    
                ImGui::SetNextItemWidth(50);
                const char* factors[] = { "None", "2X", "4X", "8X" };
                int idx = static_cast<int>(log2(static_cast<float>(config.ssaa)));
                const char* preview = factors[idx];

                if (ImGui::BeginCombo("SSAA", preview)) {
                    for (int i = 0; i < 4; i++) {
                        const bool is_selected = (idx == i);
                        if (ImGui::Selectable(factors[i], is_selected)) {
                            config.ssaa = i != 0;
                            config.ssaa = pow(2, i);
                            
                            glBindFramebuffer(GL_FRAMEBUFFER, mandelbrotFrameBuffer);
                            glBindTexture(GL_TEXTURE_2D, mandelbrotTexBuffer);
                            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, ss.x * config.ssaa,
                                ss.y * config.ssaa, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

                            glBindFramebuffer(GL_FRAMEBUFFER, postprocFrameBuffer);
                            glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
                            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, ss.x * config.ssaa,
                                ss.y * config.ssaa, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

                            glBindFramebuffer(GL_FRAMEBUFFER, juliaFrameBuffer);
                            glBindTexture(GL_TEXTURE_2D, juliaTexBuffer);
                            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, julia_size * config.ssaa,
                                julia_size * config.ssaa, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

                            glUniform1i(glGetUniformLocation(shaderProgram, "blur"), config.ssaa);
                            upload_kernel(config.ssaa);
                            set_op(MV_COMPUTE);
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::SameLine();
                ImGui::BeginDisabled(!fractals[fractal].continuous_compatible);
                if (ImGui::Checkbox("Smooth coloring", reinterpret_cast<bool*>(&config.continuous_coloring))) {
                    glUniform1i(glGetUniformLocation(shaderProgram, "continuous_coloring"), config.continuous_coloring);
                    set_op(MV_COMPUTE);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(fractal != 1 && fractal != 2);
                if (ImGui::Checkbox("Normal map", &config.normal_map_effect)) {
                    glUniform1i(glGetUniformLocation(shaderProgram, "normal_map_effect"), config.normal_map_effect);
                    set_op(MV_COMPUTE);
                }
                ImGui::EndDisabled();
                if (config.normal_map_effect) {
                    if (ImGui::DragFloat("Angle", &config.angle, 1.f, 0.f, 0.f, "%.1f deg")) {
                        if (config.angle > 360.f) config.angle = config.angle - 360.f;
                        if (config.angle < 0.f) config.angle = 360.f + config.angle;
                        glUniform1f(glGetUniformLocation(shaderProgram, "angle"), 360.f - config.angle);
                        set_op(MV_COMPUTE);
                    }
                    if (ImGui::DragFloat("Height", &config.height, 0.1f, 0.f, FLT_MAX, "%.1f", ImGuiSliderFlags_AlwaysClamp)) {
                        glUniform1f(glGetUniformLocation(shaderProgram, "height"), config.height);
                        set_op(MV_COMPUTE);
                    }
                }

                ImGui::SeparatorText("Fractal");

                b::EmbedInternal::EmbeddedFile embed;
                const char* content;
                int length;

                static char* infoLog = new char[512]{'\0'};
                static int success;
                static GLuint shader = NULL;
                embed = b::embed<"shaders/render.glsl">();
                content = embed.data();
                length = embed.length();
                static bool reverted = false;
                bool compile = false;
                bool reload = false;

                preview = fractals[fractal].name.c_str();
                
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::BeginCombo("##presets", preview)) {
                    for (int n = 0; n < fractals.size(); n++) {
                        const bool is_selected = (fractal == n);
                        if (ImGui::Selectable(fractals[n].name.c_str(), is_selected)) {
                            if (n == 0) {
                                fractals[0].equation  = fractals[fractal].equation;
                                fractals[0].equation.resize(1024);
                                fractals[0].condition = fractals[fractal].condition;
                                fractals[0].condition.resize(1024);
                                fractals[0].initialz  = fractals[fractal].initialz;
                                fractals[0].initialz.resize(1024);
                                fractals[0].degree    = config.degree;
                                fractals[0].sliders   = fractals[fractal].sliders;
                            } else {
                                config.degree = fractals[n].degree;
                                config.continuous_coloring = static_cast<int>(config.continuous_coloring && fractals[n].continuous_compatible);
                            }
                            fractal = n;
                            update_shader();
                            reload = compile = update = true;
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (fractal == 0) {
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (ImGui::InputText("##equation", fractals[0].equation.data(), 1024) || reverted) compile = true;
                    if (ImGui::InputText("Bailout condition", fractals[0].condition.data(), 1024)) compile = true;
                    if (ImGui::InputText("Initial Z", fractals[0].initialz.data(), 1024)) compile = true;
                    
                    ImGui::InputTextMultiline("##errorlist", infoLog, 512, ImVec2(ImGui::GetContentRegionAvail().x, 40), ImGuiInputTextFlags_ReadOnly);
                    ImGui::BeginDisabled(!success);
                    if (ImGui::Button("Reload", ImVec2(ImGui::GetContentRegionAvail().x / 2.f - 1.f, 0.f)) || reverted) reload = true;
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button("Reset", ImVec2(ImGui::GetContentRegionAvail().x, 0.f))) {
                        fractal = 0;
                        reverted = true;
                    }
                }
                if (compile) {
                    compile_shader(shader, &success, infoLog, content, length);
                }
                if (reload) {
                    reload_shader(shader);
                    set_op(MV_COMPUTE, true);
                    reverted = false;
                }
                if (fractal == 0) ImGui::SeparatorText("Variables");
                int update_fractal = 0;
                std::vector<int> to_delete;
                auto slider = [&]<typename type>(const char* label, type* ptr, int index, const type def, float speed, float min, float max) {
                    ImGui::PushID(ptr);
                    if (ImGui::Button("Round##", ImVec2(80, 0))) {
                        *ptr = round(*ptr);
                        update_fractal = 1;
                    }
                    ImGui::SameLine();
                    if (index != -1 && ImGui::Button("Delete", ImVec2(80, 0))) {
                        to_delete.push_back(index);
                        update_fractal = 1;
                    }
                    else if (index == -1 && ImGui::Button("Reset", ImVec2(80, 0))) {
                        *ptr = def;
                        update_fractal = 1;
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80);
                    update_fractal |= ImGui::DragFloat(label, ptr, speed, min, max, "%.5f", ImGuiSliderFlags_NoRoundToFormat);
                    ImGui::PopID();
                };
                float mouseSpeed = cbrt(pow(ImGui::GetIO().MouseDelta.x, 2) + pow(ImGui::GetIO().MouseDelta.y, 2));
                slider("Power", &config.degree, -1, 2.f, std::max(1e-4f, abs(round(config.degree) - config.degree)) * mouseSpeed * std::min(pow(1.1, config.degree), 1e+3) / 40.f, (fractal == 0) ? 0.f : 2.f, FLT_MAX);
                int numSliders = fractals[fractal].sliders.size();
                for (int i = 0; i < numSliders; i++) {
                    Slider& s = fractals[fractal].sliders[i];
                    slider(s.name.c_str(), &s.value, i, 0.f, std::max(1e-2f, abs(s.value) * mouseSpeed / 40.f), s.min, s.max);
                }
                for (const int& i : to_delete) {
                    fractals[fractal].sliders.erase(fractals[fractal].sliders.begin() + i);
                }
                if (fractal == 0) {
                    if (ImGui::Button("New variable", ImVec2(ImGui::GetContentRegionAvail().x / 2.f - 1.f, 0))) {
                        Slider s;
                        s.name.resize(12);
                        fractals[0].sliders.push_back(s);
                        ImGui::OpenPopup("Create new variable");
                    }
                    ImGui::SameLine();
                    ImGui::BeginDisabled(fractals[0].sliders.size() > 0);
                    if (ImGui::Button("Delete all", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                        fractals[0].sliders.clear();
                    }
                    ImGui::EndDisabled();

                    if (ImGui::BeginPopupModal("Create new variable", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                        Slider& slider = fractals[0].sliders[fractals[0].sliders.size() - 1];
                        bool upper_limit, lower_limit;
                        auto charFilter = [](ImGuiInputTextCallbackData* data) -> int {
                            const char* forbiddenChars = "!'^+%&/()=?_*-<>£#$½{[]}\\|.:,;\" ";
                            if (strchr(forbiddenChars, data->EventChar)) return 1;
                            return 0;
                        };
                        ImGui::InputText("Name", slider.name.data(), 12, ImGuiInputTextFlags_CallbackCharFilter, charFilter);
                        ImGui::Checkbox("Upper limit", &upper_limit);
                        if (!upper_limit) ImGui::BeginDisabled();
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                        ImGui::DragFloat("##max", &slider.max, std::max(1e-4f, abs(slider.max) / 20.f), 0.f, 0.f, "%.9g");
                        if (!upper_limit) ImGui::EndDisabled();
                        ImGui::Checkbox("Lower limit", &lower_limit);
                        if (!lower_limit) ImGui::BeginDisabled();
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                        ImGui::DragFloat("##min", &slider.min, std::max(1e-4f, abs(slider.min) / 20.f), 0.f, 0.f, "%.9g");
                        if (!lower_limit) ImGui::EndDisabled();
                        if (strlen(slider.name.c_str()) == 0) ImGui::BeginDisabled();
                        if (ImGui::Button("Create", ImVec2(ImGui::GetContentRegionAvail().x / 2.f - 1.f, 0))) {
                            if (!lower_limit) slider.min = 0.f;
                            if (!upper_limit) slider.max = 0.f;
                            ImGui::CloseCurrentPopup();
                        }
                        if (strlen(slider.name.c_str()) == 0) ImGui::EndDisabled();
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                            fractals[0].sliders.pop_back();
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }
                }
                if (update_fractal) {
                    update_shader();
                    set_op(MV_COMPUTE);
                }

                ImGui::SeparatorText("Coloring");

                if (ImGui::BeginTabBar("MyTabBar")) {
                    if (ImGui::BeginTabItem("Outside")) {
                        std::vector<std::string> functions = { "Linear", "Square root", "Cubic root", "Logarithmic" };
                        const char* preview = functions[config.transfer_function].c_str();

                        if (ImGui::BeginCombo("Transfer function", preview)) {
                            for (int n = 0; n < functions.size(); n++) {
                                const bool is_selected = (config.transfer_function == n);
                                if (ImGui::Selectable(functions[n].c_str(), is_selected)) {
                                    config.transfer_function = n;
                                    glUniform1i(glGetUniformLocation(shaderProgram, "transfer_function"), config.transfer_function);
                                    set_op(MV_POSTPROC);
                                }
                                if (is_selected) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                        if (ImGui::SliderFloat("Multiplier", &config.iter_multiplier, 1, 256, "x%.4g", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat)) {
                            glUniform1f(glGetUniformLocation(shaderProgram, "iter_multiplier"), config.iter_multiplier);
                            set_op(MV_POSTPROC);
                        }
                        if (ImGui::SliderFloat("Offset", &config.spectrum_offset, 0, span)) {
                            glUniform1f(glGetUniformLocation(shaderProgram, "spectrum_offset"), config.spectrum_offset);
                            set_op(MV_POSTPROC);
                        }
                        ImGui::Dummy(ImVec2(0.0f, 3.0f));
                        bool isMarkerShown = true;
                        ImGui::SetCursorPosX(15.f);
                        ImGui::BeginGroup();
                        ImGradientHDR(stateID, state, tempState, isMarkerShown);

                        if (tempState.selectedMarkerType == ImGradientHDRMarkerType::Color) {
                            auto selectedColorMarker = state.GetColorMarker(tempState.selectedIndex);
                            if (selectedColorMarker != nullptr) {
                                ImGui::ColorEdit3("", selectedColorMarker->Color.data(), ImGuiColorEditFlags_Float);
                            }
                        }
                        if (tempState.selectedMarkerType != ImGradientHDRMarkerType::Unknown) {
                            ImGui::SameLine();
                            if (tempState.selectedMarkerType == ImGradientHDRMarkerType::Color
                                && (ImGui::Button("Delete") || glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS)) {
                                state.RemoveColorMarker(tempState.selectedIndex);
                                tempState = ImGradientHDRTemporaryState{};
                            }
                        }
                        ImGui::EndGroup();
                        bool resync = false;
                        paletteData.resize(state.ColorCount);
                        for (int i = 0; i < paletteData.size(); i++) {
                            auto co = vec4(make_vec3(state.Colors[i].Color.data()), state.Colors[i].Position);
                            if (update |= paletteData.at(i) != co) {
                                paletteData[i] = co;
                            }
                        }
                        if (ImGui::Button("Save##", ImVec2(ImGui::GetContentRegionAvail().x / 3.f - 2.f, 0.f))) {
                            const char* lFilterPatterns[1] = { "*.mvcp" };
                            auto t = std::time(nullptr);
                            auto tm = *std::localtime(&t);
                            std::ostringstream oss;
                            oss << std::put_time(&tm, "MV2 %d-%m-%Y %H-%M-%S");
                            const char* buf = tinyfd_saveFileDialog("Save color palette", oss.str().c_str(),
                                1, lFilterPatterns, "MV2 Color Palette File (*.mvcp)");
                            if (glfwGetWindowMonitor(window) != nullptr) glfwRestoreWindow(window);
                            glfwShowWindow(window);
                            if (buf != nullptr) {
                                std::ofstream fout;
                                fout.open(buf, std::ios::binary | std::ios::out | std::ofstream::trunc);
                                for (const vec4& c : paletteData) {
                                    fout.write(reinterpret_cast<const char*>(value_ptr(c)), sizeof(float) * 4);
                                }
                                fout.close();
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Load##", ImVec2(ImGui::GetContentRegionAvail().x / 2.f - 1.f, 0.f))) {
                            const char* lFilterPatterns[1] = { "*.mvcp" };
                            char* buf = tinyfd_openFileDialog("Open color palette", nullptr,
                                1, lFilterPatterns, "MV2 Color Palette File (*.mvcp)", false);
                            if (glfwGetWindowMonitor(window) != nullptr) glfwRestoreWindow(window);
                            glfwShowWindow(window);
                            if (buf != nullptr) {
                                std::ifstream fin;
                                fin.open(buf, std::ios::binary | std::ios::in | std::ios::ate);
                                int size = static_cast<int>(fin.tellg());
                                if (size % 4 != 0 || (size /= 16) > 16) {
                                    throw Error("Palette file invalid");
                                }
                                paletteData.resize(size);
                                fin.seekg(0);
                                for (int i = 0; i < size; i++) {
                                    fin.read(reinterpret_cast<char*>(value_ptr(paletteData[i])), 16);
                                }
                                fin.close();
                                resync = update = true;
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Reset", ImVec2(ImGui::GetContentRegionAvail().x, 0.f))) {
                            paletteData = {
                                { 0.0000f, 0.0274f, 0.3921f, 0.0000f },
                                { 0.1254f, 0.4196f, 0.7960f, 0.1600f },
                                { 0.9294f, 1.0000f, 1.0000f, 0.4200f },
                                { 1.0000f, 0.6666f, 0.0000f, 0.6425f },
                                { 0.0000f, 0.0078f, 0.0000f, 0.8575f },
                                { 0.0000f, 0.0274f, 0.3921f, 1.0000f }
                            };
                            resync = update = true;
                        }
                        if (resync) {
                            state.ColorCount = paletteData.size();
                            for (int i = 0; i < state.ColorCount; i++) {
                                state.Colors[i].Color = *reinterpret_cast<std::array<float, 3>*>(value_ptr(paletteData[i]));
                                state.Colors[i].Position = paletteData[i][3];
                            }
                        }
                        if (update) {
                            glBindBuffer(GL_SHADER_STORAGE_BUFFER, paletteBuffer);
                            memcpy(glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_WRITE_ONLY),
                                paletteData.data(), paletteData.size() * sizeof(vec4));
                            glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
                            set_op(MV_POSTPROC);
                        }
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Inside")) {
                        if (ImGui::ColorEdit3("Set color", glm::value_ptr(config.set_color))) {
                            glUniform3f(glGetUniformLocation(shaderProgram, "set_color"), config.set_color.r, config.set_color.g, config.set_color.b);
                            set_op(MV_POSTPROC);
                        }
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                
                ImGui::SeparatorText("Preferences");
                if (ImGui::TreeNode("Right-click")) {
                    ImGui::Checkbox("Coordinate info", &cmplxinfo);
                    ImGui::SameLine();
                    ImGui::Checkbox("Julia set", &juliaset);
                    ImGui::SameLine();
                    ImGui::Checkbox("Orbit", &orbit);
                    ImGui::SetNextItemWidth(70);
                    if (ImGui::InputInt("Julia preview size", &julia_size, 5, 20)) {
                        glBindTexture(GL_TEXTURE_2D, juliaTexBuffer);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, julia_size * config.ssaa, julia_size * config.ssaa, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
                    }
                    ImGui::TreePop();
                }
                ImGui::EndGroup();
            }
            ImGui::End();

            if (rightClickHold) {
                double x, y;
                glfwGetCursorPos(window, &x, &y);
                x *= dpi_scale;
                y *= dpi_scale;

                if (juliaset || cmplxinfo) {
                    ImGui::Begin("info", nullptr,
                        ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse |
                        ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoTitleBar);
                    ImVec2 size = ImGui::GetWindowSize();
                    ImVec2 pos = { (float)x / dpi_scale + 10.f, (float)y / dpi_scale + 20.f };
                    if (size.x > ss.x / dpi_scale - pos.x - 5.f * dpi_scale)
                        pos.x = ss.x / dpi_scale - size.x - 5.f * dpi_scale;
                    if (size.y > ss.y / dpi_scale - pos.y - 5.f * dpi_scale)
                        pos.y = ss.y / dpi_scale - size.y - 5.f * dpi_scale;
                    if (pos.x < 5.f * dpi_scale) pos.x = 5.f * dpi_scale;
                    if (pos.y < 5.f * dpi_scale) pos.y = 5.f * dpi_scale;
                    ImGui::SetWindowPos(pos);
                }
                if (cmplxinfo) {
                    if (numIterations > 0) ImGui::Text("Re: %.17g\nIm: %.17g\nIterations before bailout: %d", cmplxCoord.x, -cmplxCoord.y, numIterations);
                    else if (numIterations == -1) ImGui::Text("Re: %.17g\nIm: %.17g\nPoint is in set", cmplxCoord.x, -cmplxCoord.y);
                    else ImGui::Text("Re: %.17g\nIm: %.17g\nPoint out of bounds", cmplxCoord.x, -cmplxCoord.y);
                }
                if (juliaset) {
                    if (cmplxinfo) ImGui::SeparatorText("Julia Set");
                    ImGui::Image((void*)(intptr_t)juliaTexBuffer, ImVec2(julia_size, julia_size));
                }
                if (cmplxinfo || juliaset) ImGui::End();
            }
            if (!rightClickHold || !orbit) {
                glUniform1i(glGetUniformLocation(shaderProgram, "numVertices"), 0);
            }
            ImGui::PopFont();
            if (recording) {
                glViewport(0, 0, zsc.tcfg.screenSize.x * zsc.tcfg.ssaa, zsc.tcfg.screenSize.y * zsc.tcfg.ssaa);
                glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), zsc.tcfg.screenSize.x * zsc.tcfg.ssaa, zsc.tcfg.screenSize.y * zsc.tcfg.ssaa);
            } else {
                glViewport(0, 0, ss.x * config.ssaa, ss.y * config.ssaa);
                glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), ss.x * config.ssaa, ss.y * config.ssaa);
            }
            switch (op) {
            case MV_COMPUTE:
                glBindFramebuffer(GL_FRAMEBUFFER, mandelbrotFrameBuffer);
                glUniform1i(glGetUniformLocation(shaderProgram, "op"), MV_COMPUTE);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                [[fallthrough]];
            case MV_POSTPROC:
                glBindTexture(GL_TEXTURE_2D, mandelbrotTexBuffer);
                glBindFramebuffer(GL_FRAMEBUFFER, postprocFrameBuffer);
                glUniform1i(glGetUniformLocation(shaderProgram, "op"), MV_POSTPROC);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
                [[fallthrough]];
            case MV_RENDER:
                if (recording) {
                    glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
                    glBindFramebuffer(GL_FRAMEBUFFER, finalFrameBuffer);
                    glUniform1i(glGetUniformLocation(shaderProgram, "op"), MV_RENDER);
                    glViewport(0, 0, zsc.tcfg.screenSize.x, zsc.tcfg.screenSize.y);
                    glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), zsc.tcfg.screenSize.x, zsc.tcfg.screenSize.y);
                    glDrawArrays(GL_TRIANGLES, 0, 6);

                    glBindTexture(GL_TEXTURE_2D, finalTexBuffer);
                    glBindFramebuffer(GL_FRAMEBUFFER, NULL);
                    glViewport(0, 0, ss.x, ss.y);
                    glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), ss.x, ss.y);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    set_op(MV_RENDER, true);
                } else {
                    glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
                    glBindFramebuffer(GL_FRAMEBUFFER, NULL);
                    glUniform1i(glGetUniformLocation(shaderProgram, "op"), MV_RENDER);
                    glViewport(0, 0, ss.x, ss.y);
                    glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), ss.x, ss.y);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    set_op(MV_RENDER, true);
                }
            }
            if (recording && !paused) {
                int w = zsc.tcfg.screenSize.x, h = zsc.tcfg.screenSize.y;
                unsigned char* buffer = new unsigned char[4 * w * h];
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, buffer);

                if (progress > 0) {
                    //cv::Mat frame(cv::Size(w, h), CV_8UC3, buffer);
                    //cv::cvtColor(frame, frame, cv::COLOR_RGB2BGR);
                    //cv::flip(frame, frame, 0);
                    //writer.write(frame);
                }
                progress++;
                if (zsc.fps * zsc.duration == progress) {
                    use_config(config);
                    recording = false;
                    //writer.release();
                    glfwSwapInterval(1);
                } else {
                    int framecount = (zsc.fps * zsc.duration);
                    double x = static_cast<double>(progress) / framecount;
                    double z = 3 * pow(x, 2) - 2 * pow(x, 3);
                    if (zsc.direction == 1) z = -z + 1;
                    double coeff = pow(config.zoom / 5.0, 1.0 / framecount);
                    if (zsc.ease_inout)
                        zsc.tcfg.zoom = 8.0 * pow(coeff, z * framecount);
                    else
                        zsc.tcfg.zoom = 8.0 * pow(coeff, progress);
                    glUniform1i(glGetUniformLocation(shaderProgram, "max_iters"),
                        max_iters(zsc.tcfg.zoom, zoom_co, config.iter_co));
                    glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), zsc.tcfg.zoom);
                }
                delete[] buffer;
                set_op(MV_COMPUTE, true);
            }
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);

        } while (!glfwWindowShouldClose(window));
    }
};

int main() {
    MV2 app;
    app.mainloop();

    return 0;
}