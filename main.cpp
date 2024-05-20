#ifndef _DEBUG
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

#define IMGUI_DEFINE_MATH_OPERATORS
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define NOMINMAX

#include <Windows.h>
#include "resource.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <imgui_theme.h>

#include <ImGradientHDR.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb/stb_image_write.h>
#include <tinyfiledialogs/tinyfiledialogs.h>
#include <opencv.hpp>
#include <chromium/cubic_bezier.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <iomanip>
#include <ctime>
#include <string>
#include <filesystem>
#include <functional>

#define MV_COMPUTE  3
#define MV_POSTPROC 2
#define MV_RENDER   1
#define MV_DISPLAY  0

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
#version 430 core

layout(location = 0) in vec2 aPos;

void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
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

constexpr double zoom_co = 0.85;
constexpr double doubleClick_interval = 0.2;
glm::ivec2 monitorSize;

static void HelpMarker(const char* desc) { // code from imgui demo
    ImGui::TextDisabled("[?]");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

struct Fractal {
    std::string name = "Mandelbrot";
    std::string equation = "cpow(z, degree) + c";
    std::string condition = "distance(z, c) > 10";
    std::string initialz = "c";
    float degree = 2.f;
    bool continuous_compatible = true;
};

std::vector<Fractal> fractals = {
    Fractal({.name = "Custom"}),
    Fractal({.name = "Mandelbrot", .equation = "cpow(z, degree) + c", .condition = "distance(z, c) > 10", .initialz = "c", .degree = 2.f, .continuous_compatible = true}),
    Fractal({.name = "Nova", .equation = "z - cdivide(cpow(z, degree) - dvec2(1, 0), degree * cpow(z, degree - 1)) + c", .condition = "distance(z, prevz) < 10e-5", .initialz = "dvec2(1, 0)", .degree = 3.f, .continuous_compatible = false}),
    Fractal({.name = "Burning ship", .equation = "cpow(dvec2(abs(z.x), abs(z.y)), degree) + c", .condition = "distance(z, c) > 10", .initialz = "c", .degree = 2.f, .continuous_compatible = true}),
    Fractal({.name = "Magnet 1", .equation = "cpow(cdivide(cpow(z, degree) + c - dvec2(1,0), degree * z + c - dvec2(2,0)), degree)", .condition = "distance(z, c) > 30", .initialz="dvec2(0)", .degree = 2.f, .continuous_compatible = true}),
    Fractal({.name = "Magnet 2", .equation = "cpow(cdivide(cpow(z, degree + 1) + 3 * cmultiply(c - dvec2(1, 0), z) + cmultiply(c - dvec2(1, 0), c - dvec2(2, 0)),\
        3 * cpow(z, degree) + 3 * cmultiply(c - dvec2(2, 0), z) + cmultiply(c - dvec2(1, 0), c - dvec2(2, 0)) + dvec2(1,0)), degree)", .condition = "distance(z, c) > 30", .initialz = "dvec2(0)", .degree = 2.f, .continuous_compatible = true}),
    Fractal({.name = "Tricorn", .equation = "cpow(cconj(z), degree) + c", .condition = "distance(z, c) > 10", .initialz = "c", .degree = 2.f, .continuous_compatible = true}),
};

struct Config {
    glm::dvec2 offset = { -0.4, 0 };
    glm::ivec2 screenSize = { 840, 590 };
    double zoom = 5.0;
    float  spectrum_offset = 0.f;
    float  iter_multiplier = 18.f;
    float  iter_co = 1.045f;
    int    continuous_coloring = 1;
    int    normal_map_effect = 0;
    glm::fvec3 set_color = { 0.f, 0.f, 0.f };
    bool   ssaa = true;
    int    ssaa_factor = 2;
    // experimental
    float  degree = 2.f;
    // normal mapping
    float  angle = 0.f;
    float  height = 1.5f;
};

struct ZoomSequenceConfig {
    Config tcfg;
    int fps = 30;
    int duration = 30;
    char path[256]{};
};

class MV2 {
    GLFWwindow* window = nullptr;
    ImFont* font_title = nullptr;

    std::vector<glm::vec4> paletteData = {
        {0.0000f, 0.0274f, 0.3921f, 0.0000f},
        {0.1254f, 0.4196f, 0.7960f, 0.1600f},
        {0.9294f, 1.0000f, 1.0000f, 0.4200f},
        {1.0000f, 0.6666f, 0.0000f, 0.6425f},
        {0.0000f, 0.0078f, 0.0000f, 0.8575f},
        {0.0000f, 0.0274f, 0.3921f, 1.0000f}
    };
    int span = 1000;
    const int max_colors = 8;

    glm::ivec2 screenPos = { 0, 0 };
    bool fullscreen = false;
    Config config;
    int fractal = 1;
    ZoomSequenceConfig zsc;
    int julia_size = 180;
    double julia_zoom = 3;
    double fps_update_interval = 0.03;

    glm::dvec2 oldPos = { 0, 0 };
    glm::dvec2 lastPresses = { -doubleClick_interval, 0 };
    bool dragging = false;
    bool rightClickHold = false;

    bool recording = false;
    bool paused = false;
    int progress = 0;
    cv::VideoWriter writer;

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

    int32_t stateID = 10;
    ImGradientHDRState state;
    ImGradientHDRTemporaryState tempState;

    int op = MV_COMPUTE;
public:
    MV2() {
        glfwInit();

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());

        glfwWindowHint(GLFW_RED_BITS, mode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
        monitorSize.x = mode->width;
        monitorSize.y = mode->height;

        window = glfwCreateWindow(config.screenSize.x, config.screenSize.y, "Mandelbrot Voyage II", NULL, NULL);
        if (window == nullptr) {
            std::cout << "Failed to create OpenGL window" << std::endl;
            return;
        }
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
        font_title = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 11.f);
        IM_ASSERT(font_title != NULL);
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = NULL;
        io.LogFilename = NULL;

        ImGui::StyleColorsDark();
        ImGui::LoadTheme();

        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 430");

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cout << "Failed to create OpenGL window" << std::endl;
            return;
        }

        int factor = (config.ssaa ? config.ssaa_factor : 1);

        glGenTextures(1, &mandelbrotTexBuffer);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mandelbrotTexBuffer);
        glUniform1i(glGetUniformLocation(shaderProgram, "mandelbrotTex"), 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, config.screenSize.x * factor,
            config.screenSize.y * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glGenTextures(1, &postprocTexBuffer);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
        glUniform1i(glGetUniformLocation(shaderProgram, "postprocTex"), 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, config.screenSize.x * factor,
            config.screenSize.y * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenTextures(1, &finalTexBuffer);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, finalTexBuffer);
        glUniform1i(glGetUniformLocation(shaderProgram, "finalTex"), 2);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1920, 1080, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenTextures(1, &juliaTexBuffer);
        glBindTexture(GL_TEXTURE_2D, juliaTexBuffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, julia_size * factor,
            julia_size * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
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

        unsigned int fragmentShader;
        fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        char* fragmentSource = read_resource(IDR_RNDR);
        char* modifiedSource = new char[strlen(fragmentSource) + 4096];
        sprintf(modifiedSource, fragmentSource, fractals[fractal].equation.data(), 0, fractals[fractal].condition.data(), fractals[fractal].initialz.data(), fractals[fractal].condition.data());
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
        glLinkProgram(shaderProgram);
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

        glGenBuffers(1, &paletteBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, paletteBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, max_colors * sizeof(glm::fvec4), paletteData.data(), GL_DYNAMIC_COPY);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, paletteBuffer);
        GLuint block_index = glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "spectrum");
        GLuint ssbo_binding_point_index = 2;
        glShaderStorageBlockBinding(shaderProgram, block_index, ssbo_binding_point_index);
        glUniform1i(glGetUniformLocation(shaderProgram, "span"), span);

        glGenBuffers(1, &orbitBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, orbitBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, orbitBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "orbit"), 0);

        use_config(config, true, false);
        on_windowResize(window, config.screenSize.x, config.screenSize.y);

        for (const glm::vec4& c : paletteData) {
            state.AddColorMarker(c.w, { c.r, c.g, c.b }, 1.0f);
        }
    }

    static inline char* read_resource(int name) {
        HMODULE handle = GetModuleHandleW(NULL);
        HRSRC rc = FindResourceW(handle, MAKEINTRESOURCE(name), MAKEINTRESOURCE(TEXTFILE));
        if (rc == NULL) return nullptr;
        HGLOBAL rcData = LoadResource(handle, rc);
        if (rcData == NULL) return nullptr;
        DWORD size = SizeofResource(handle, rc);
        char* res = new char[size + 1];
        memcpy(res, static_cast<const char*>(LockResource(rcData)), size);
        res[size] = '\0';
        return res;
    }

    void use_config(Config config, bool variables = true, bool textures = true) {
        if (variables) {
            glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), config.screenSize.x, config.screenSize.y);
            glUniform2d(glGetUniformLocation(shaderProgram, "offset"), config.offset.x, config.offset.y);
            glUniform1f(glGetUniformLocation(shaderProgram, "iter_multiplier"), config.iter_multiplier);
            glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), config.zoom);
            glUniform1i(glGetUniformLocation(shaderProgram, "max_iters"),
                max_iters(config.zoom, zoom_co, config.iter_co));
            glUniform1f(glGetUniformLocation(shaderProgram, "spectrum_offset"), config.spectrum_offset);
            glUniform1i(glGetUniformLocation(shaderProgram, "continuous_coloring"), config.continuous_coloring);
            glUniform1i(glGetUniformLocation(shaderProgram, "normal_map_effect"), config.normal_map_effect);
            glUniform3f(glGetUniformLocation(shaderProgram, "set_color"), config.set_color.x, config.set_color.y, config.set_color.z);
            glUniform1d(glGetUniformLocation(shaderProgram, "julia_zoom"), julia_zoom);
            glUniform1i(glGetUniformLocation(shaderProgram, "julia_maxiters"),
                max_iters(julia_zoom, zoom_co, config.iter_co, 3.0));
            glUniform1i(glGetUniformLocation(shaderProgram, "blur"), (config.ssaa ? config.ssaa_factor : 1));

            glUniform1f(glGetUniformLocation(shaderProgram, "degree"), config.degree);

            glUniform1f(glGetUniformLocation(shaderProgram, "angle"), config.angle);
            glUniform1f(glGetUniformLocation(shaderProgram, "height"), config.height);
        }
        if (textures) {
            int factor = (config.ssaa ? config.ssaa_factor : 1);
            glBindFramebuffer(GL_FRAMEBUFFER, mandelbrotFrameBuffer);
            glBindTexture(GL_TEXTURE_2D, mandelbrotTexBuffer);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, config.screenSize.x * factor, config.screenSize.y * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

            glBindFramebuffer(GL_FRAMEBUFFER, postprocFrameBuffer);
            glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, config.screenSize.x * factor, config.screenSize.y * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        }
    }


    void set_op(int p, bool override = false) {
        if (p > op || override) op = p;
    }

    static glm::dvec2 pixel_to_complex(glm::dvec2 pixelCoord, glm::ivec2 screenSize, double zoom, glm::dvec2 offset) {
        return ((glm::dvec2(pixelCoord.x / screenSize.x, (screenSize.y - pixelCoord.y) / screenSize.y)) - glm::dvec2(0.5, 0.5)) *
            glm::dvec2(zoom, (screenSize.y * zoom) / screenSize.x) + offset;
    }
    static glm::dvec2 pixel_to_complex(MV2* app, glm::dvec2 pixelCoord) {
        glm::ivec2 ss = (app->fullscreen ? monitorSize : app->config.screenSize);
        return ((glm::dvec2(pixelCoord.x / ss.x, pixelCoord.y / ss.y)) - glm::dvec2(0.5, 0.5)) *
            glm::dvec2(app->config.zoom, (ss.y * app->config.zoom) / ss.x) + app->config.offset;
    }
    static glm::dvec2 complex_to_pixel(glm::dvec2 complexCoord, glm::ivec2 screenSize, double zoom, glm::dvec2 offset) {
        glm::dvec2 normalizedCoord = (complexCoord - offset);
        normalizedCoord /= glm::dvec2(zoom, (screenSize.y * zoom) / screenSize.x);
        glm::dvec2 pixelCoordNormalized = normalizedCoord + glm::dvec2(0.5, 0.5);
        return glm::dvec2(pixelCoordNormalized.x * screenSize.x, screenSize.y - pixelCoordNormalized.y * screenSize.y);
    }
    static int max_iters(double zoom, double zoom_co, double iter_co, double initial_zoom = 5.0) {
        return 100 * pow(iter_co, log2(zoom / initial_zoom) / log2(zoom_co));
    }

    static void on_windowResize(GLFWwindow* window, int width, int height) {
        MV2* app = static_cast<MV2*>(glfwGetWindowUserPointer(window));
        glViewport(0, 0, width, height);

        if (!app->fullscreen) app->config.screenSize = { width, height };
        if (app->shaderProgram)
            glUniform2i(glGetUniformLocation(app->shaderProgram, "screenSize"), width, height);
        app->set_op(MV_COMPUTE);
        int factor = (app->config.ssaa ? app->config.ssaa_factor : 1);

        glBindFramebuffer(GL_FRAMEBUFFER, app->mandelbrotFrameBuffer);
        glBindTexture(GL_TEXTURE_2D, app->mandelbrotTexBuffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width * factor, height * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        glBindFramebuffer(GL_FRAMEBUFFER, app->postprocFrameBuffer);
        glBindTexture(GL_TEXTURE_2D, app->postprocTexBuffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width * factor, height * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }

    static void on_mouseButton(GLFWwindow* window, int button, int action, int mod) {
        MV2* app = static_cast<MV2*>(glfwGetWindowUserPointer(window));
        glm::ivec2 ss = (app->fullscreen ? monitorSize : app->config.screenSize);
        if (ImGui::GetIO().WantCaptureMouse) return;
        switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT:
            if (action == GLFW_PRESS) {
                app->dragging = true;
                app->lastPresses.x = app->lastPresses.y;
                app->lastPresses.y = glfwGetTime();
                glfwGetCursorPos(window, &app->oldPos.x, &app->oldPos.y);
            }
            else if (app->lastPresses.y - app->lastPresses.x < doubleClick_interval) {
                glfwGetCursorPos(window, &app->oldPos.x, &app->oldPos.y);
                glm::dvec2 pos = pixel_to_complex(app, app->oldPos);
                glm::dvec2 center = pixel_to_complex(app, static_cast<glm::dvec2>(ss) / 2.0);
                app->config.offset += pos - center;
                glUniform2d(glGetUniformLocation(app->shaderProgram, "offset"), app->config.offset.x, app->config.offset.y);
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
                break;
            case GLFW_RELEASE:
                app->rightClickHold = false;
            }
        }
    }

    static void on_cursorMove(GLFWwindow* window, double x, double y) {
        MV2* app = static_cast<MV2*>(glfwGetWindowUserPointer(window));
        glm::ivec2 ss = (app->fullscreen ? monitorSize : app->config.screenSize);
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
    }

    static void on_mouseScroll(GLFWwindow* window, double x, double y) {
        MV2* app = static_cast<MV2*>(glfwGetWindowUserPointer(window));
        if (app->rightClickHold) {
            app->julia_zoom *= pow(zoom_co, y * 1.5);
            glUniform1d(glGetUniformLocation(app->shaderProgram, "julia_zoom"), app->julia_zoom);
            glUniform1i(glGetUniformLocation(app->shaderProgram, "julia_maxiters"),
                max_iters(app->julia_zoom, zoom_co, app->config.iter_co, 3.0));
        }
        else if (!ImGui::GetIO().WantCaptureMouse) {
            app->config.zoom *= pow(zoom_co, y * 1.5);
            glUniform1d(glGetUniformLocation(app->shaderProgram, "zoom"), app->config.zoom);
            glUniform1i(glGetUniformLocation(app->shaderProgram, "max_iters"),
                max_iters(app->config.zoom, zoom_co, app->config.iter_co));
            app->set_op(MV_COMPUTE);
        }
    }

    static void on_keyPress(GLFWwindow* window, int key, int scancode, int action, int mods) {
        MV2* app = static_cast<MV2*>(glfwGetWindowUserPointer(window));
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
                glfwSetWindowMonitor(window, nullptr, app->screenPos.x,
                    app->screenPos.y, app->config.screenSize.x, app->config.screenSize.y, 0);
                app->fullscreen = false;
            }
        }
    }

    void mainloop() {
        do {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            glm::ivec2 ss = (fullscreen ? monitorSize : config.screenSize);

            ImGui::PushFont(font_title);
            ImGui::SetNextWindowPos({ 10, 10 });
            ImGui::SetNextWindowCollapsed(true, 1 << 1);
            if (ImGui::Begin("Settings", nullptr,
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoMove
            )) {
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
                if (ImGui::BeginPopupModal("Zoom sequence creator", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    zsc.tcfg = config;
                    if (recording) ImGui::BeginDisabled();
                    if (ImGui::Button("Browse...")) {
                        char const* lFilterPatterns[1] = { "*.avi" };
                        auto t = std::time(nullptr);
                        auto tm = *std::localtime(&t);
                        std::ostringstream oss;
                        oss << std::put_time(&tm, "MV2 %d-%m-%Y %H-%M-%S");
                        char const* buf = tinyfd_saveFileDialog("Save sequence", oss.str().c_str(), 1, lFilterPatterns, "Video Files (*.avi)");
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
                    const std::vector<glm::ivec2> commonres = {
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
                    ImGui::PopItemWidth();

                    if (strlen(zsc.path) == 0 && !recording) ImGui::BeginDisabled();
                    if (!recording && ImGui::Button("Render", ImVec2(100, 0))) {
                        recording = true;
                        progress = 0;
                        glfwSwapInterval(0);
                        zsc.tcfg.zoom = 8.0f;
                        int factor = (zsc.tcfg.ssaa ? zsc.tcfg.ssaa_factor : 1);
                        writer = cv::VideoWriter(zsc.path, cv::VideoWriter::fourcc('m', 'j', 'p', 'g'), zsc.fps, cv::Size(zsc.tcfg.screenSize.x, zsc.tcfg.screenSize.y));

                        if (!writer.isOpened()) throw Error("Failed to initialize sequencer");
                        use_config(zsc.tcfg, true, true);
                        ImGui::BeginDisabled();
                    }
                    else if (recording && ImGui::Button(paused ? "Resume" : "Pause", ImVec2(100, 0))) {
                        paused ^= 1;
                    }
                    if ((strlen(zsc.path) == 0 && !recording) || recording) ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                        if (recording && progress != 0) {
                            writer.release();
                            std::filesystem::remove(zsc.path);
                            glfwSwapInterval(1);
                        }
                        recording = false;
                        progress = 0;
                        use_config(config, true, true);
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
                if (ImGui::Button("Create zoom sequence")) {
                    ImGui::OpenPopup("Zoom sequence creator");
                }

                ImGui::SeparatorText("Parameters");
                bool update = false;
                ImGui::Text("Re"); ImGui::SetNextItemWidth(270); ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
                update |= ImGui::InputDouble("##re", &config.offset.x, 0.0, 0.0, "%.17g");
                ImGui::Text("Im"); ImGui::SetNextItemWidth(270); ImGui::SameLine();
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
                if (ImGui::Button("Save", ImVec2(83, 0))) {
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
                        fout.write(reinterpret_cast<const char*>(glm::value_ptr(config.offset)), sizeof(glm::dvec2));
                        fout.write(reinterpret_cast<const char*>(&config.zoom), sizeof(double));
                        fout.close();
                    }
                }
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
                if (ImGui::Button("Load", ImVec2(83, 0))) {
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
                        fin.read(reinterpret_cast<char*>(glm::value_ptr(config.offset)), sizeof(glm::dvec2));
                        fin.read(reinterpret_cast<char*>(&config.zoom), sizeof(double));
                        fin.close();
                        glUniform2d(glGetUniformLocation(shaderProgram, "offset"), config.offset.x, config.offset.y);
                        glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), config.zoom);
                        set_op(MV_COMPUTE);
                    }
                }

                ImGui::BeginGroup();
                ImGui::SeparatorText("Computation");
                if (ImGui::SliderFloat("Iteration coeff.", &config.iter_co, 1.01, 1.1)) {
                    glUniform1i(glGetUniformLocation(shaderProgram, "max_iters"),
                        max_iters(config.zoom, zoom_co, config.iter_co));
                    set_op(MV_COMPUTE);
                }
                if (ImGui::TreeNode("Equation")) {
                    static char infoLog[512]{'\0'};
                    static int success;
                    static GLuint shader = NULL;
                    static char* fragmentSource = read_resource(IDR_RNDR);
                    static bool reverted = false;
                    bool compile = false;
                    bool reload = false;

                    const char* preview = fractals[fractal].name.c_str();

                    if (ImGui::BeginCombo("Presets", preview)) {
                        for (int n = 0; n < fractals.size(); n++) {
                            const bool is_selected = (fractal == n);
                            if (ImGui::Selectable(fractals[n].name.c_str(), is_selected)) {
                                if (n == 0) {
                                    fractals[0].equation  = fractals[fractal].equation;
                                    fractals[0].condition = fractals[fractal].condition;
                                    fractals[0].initialz  = fractals[fractal].initialz;
                                    fractals[0].degree = config.degree;
                                } else {
                                    config.degree = fractals[n].degree;
                                    config.continuous_coloring = static_cast<int>(config.continuous_coloring && fractals[n].continuous_compatible);
                                }
                                fractal = n;
                                reload = compile = update = true;
                            }
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    if (fractal == 0) {
                        ImGui::PushItemWidth(265);
                        if (ImGui::InputText("##equation", fractals[0].equation.data(), 1024) || reverted) compile = true;
                        if (ImGui::InputText("##condition", fractals[0].condition.data(), 1024)) compile = true;
                        if (ImGui::InputText("##initialz", fractals[0].initialz.data(), 1024)) compile = true;
                        
                        ImGui::PopItemWidth();
                        ImGui::InputTextMultiline("##errorlist", infoLog, 512, ImVec2(265, 40), ImGuiInputTextFlags_ReadOnly);
                        ImGui::BeginDisabled(!success);
                        if (ImGui::Button("Reload", ImVec2(129, 0.f)) || reverted) reload = true;
                        ImGui::EndDisabled();
                        ImGui::SameLine();
                        if (ImGui::Button("Reset", ImVec2(129, 0.f))) {
                            fractal = 0;
                            reverted = true;
                        }
                    }
                    if (compile) {
                        char* modifiedSource = new char[strlen(fragmentSource) + 4096];
                        if (shader) glDeleteShader(shader);
                        shader = glCreateShader(GL_FRAGMENT_SHADER);
                        sprintf(modifiedSource, fragmentSource, fractals[fractal].equation.data(), 1, fractals[fractal].condition.data(), fractals[fractal].initialz.data(), fractals[fractal].condition.data());
                        glShaderSource(shader, 1, &modifiedSource, NULL);
                        glCompileShader(shader);
                        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
                        if (!success) {
                            glGetShaderInfoLog(shader, 512, NULL, infoLog);
                        }
                        else infoLog[0] = '\0';
                        delete[] modifiedSource;
                    }
                    if (reload) {
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

                        glDeleteBuffers(1, &paletteBuffer);
                        glGenBuffers(1, &paletteBuffer);
                        glBindBuffer(GL_SHADER_STORAGE_BUFFER, paletteBuffer);
                        glBufferData(GL_SHADER_STORAGE_BUFFER, max_colors * sizeof(glm::fvec4), paletteData.data(), GL_DYNAMIC_COPY);
                        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, paletteBuffer);
                        GLuint block_index = glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "spectrum");
                        GLuint ssbo_binding_point_index = 2;
                        glShaderStorageBlockBinding(shaderProgram, block_index, ssbo_binding_point_index);
                        glUniform1i(glGetUniformLocation(shaderProgram, "span"), span);
                        set_op(MV_COMPUTE, true);
                        reverted = false;
                    }
                    
                    int update = 0;
                    auto experiment = [&]<typename type>(const char* label, type * ptr, const type def, const float speed, const float min) {
                        ImGui::PushID(*reinterpret_cast<const int*>(label));
                        if (ImGui::Button("Round##")) {
                            *ptr = round(*ptr);
                            update = 1;
                        }
                        ImGui::PopID();
                        ImGui::PushID(*reinterpret_cast<const int*>(label));
                        ImGui::SameLine();
                        if (ImGui::Button("Reset##")) {
                            *ptr = def;
                            update = 1;
                        }
                        ImGui::PopID();
                        ImGui::SameLine();
                        ImGui::PushItemWidth(90);
                        update |= ImGui::DragFloat(label, ptr, speed, min, min == 0.f ? 0.f : FLT_MAX, "%.5f", ImGuiSliderFlags_NoRoundToFormat);
                        ImGui::PopItemWidth();
                    };  
                    experiment("Degree", &config.degree, 2.f, std::max(1e-4f, abs(round(config.degree) - config.degree)) * std::min(pow(1.2, config.degree), 1e+3) / 20.f, 2.f);
                    if (update) {
                        glUniform1f(glGetUniformLocation(shaderProgram, "degree"), config.degree);
                        set_op(MV_COMPUTE);
                    }
                    
                    ImGui::TreePop();
                }
                if (ImGui::Checkbox("SSAA", &config.ssaa)) {
                    int factor = (config.ssaa ? config.ssaa_factor : 1);
                    glBindFramebuffer(GL_FRAMEBUFFER, mandelbrotFrameBuffer);
                    glBindTexture(GL_TEXTURE_2D, mandelbrotTexBuffer);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, ss.x * factor,
                        ss.y * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

                    glBindFramebuffer(GL_FRAMEBUFFER, postprocFrameBuffer);
                    glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, ss.x * factor,
                        ss.y * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

                    glBindFramebuffer(GL_FRAMEBUFFER, juliaFrameBuffer);
                    glBindTexture(GL_TEXTURE_2D, juliaTexBuffer);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, julia_size * factor,
                        julia_size * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

                    glUniform1i(glGetUniformLocation(shaderProgram, "blur"), factor);

                    set_op(MV_COMPUTE);
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!fractals[fractal].continuous_compatible);
                if (ImGui::Checkbox("Continuous coloring", reinterpret_cast<bool*>(&config.continuous_coloring))) {
                    glUniform1i(glGetUniformLocation(shaderProgram, "continuous_coloring"), config.continuous_coloring);
                    set_op(MV_COMPUTE);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(fractal); // if fractal != 0
                if (ImGui::Checkbox("Normal illum.", reinterpret_cast<bool*>(&config.normal_map_effect))) {
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
                ImGui::SeparatorText("Coloring");
                if (ImGui::ColorEdit3("Set color", &config.set_color.x)) {
                    glUniform3f(glGetUniformLocation(shaderProgram, "set_color"), config.set_color.r, config.set_color.g, config.set_color.b);
                    set_op(MV_POSTPROC);
                }
                if (ImGui::SliderFloat("Multiplier", &config.iter_multiplier, 1, 128, "x%.4g", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat)) {
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
                    auto co = glm::vec4(glm::make_vec3(state.Colors[i].Color.data()), state.Colors[i].Position);
                    if (update |= paletteData.at(i) != co) {
                        paletteData[i] = co;
                    }
                }
                if (ImGui::Button("Save##", ImVec2(90.f, 0.f))) {
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
                        for (const glm::vec4& c : paletteData) {
                            fout.write(reinterpret_cast<const char*>(glm::value_ptr(c)), sizeof(float) * 4);
                        }
                        fout.close();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Load##", ImVec2(90.f, 0.f))) {
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
                            fin.read(reinterpret_cast<char*>(glm::value_ptr(paletteData[i])), 16);
                        }
                        fin.close();
                        resync = update = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset", ImVec2(90.f, 0.f))) {
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
                        state.Colors[i].Color = *reinterpret_cast<std::array<float,3>*>(glm::value_ptr(paletteData[i]));
                        state.Colors[i].Position = paletteData[i][3];
                    }
                }
                if (update) {
                    glBindBuffer(GL_SHADER_STORAGE_BUFFER, paletteBuffer);
                    memcpy(glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_WRITE_ONLY),
                        paletteData.data(), paletteData.size() * sizeof(glm::vec4));
                    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
                    set_op(MV_POSTPROC);
                }
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(80, 80, 80, 255));
                ImGui::Text("(c) 2017-2024 Yilmaz Alpaslan");
                ImGui::PopStyleColor();
                ImGui::EndGroup();
            }
            ImGui::End();

            int factor = (config.ssaa ? config.ssaa_factor : 1);

            if (rightClickHold) {
                double x, y;
                glfwGetCursorPos(window, &x, &y);

                ImGui::Begin("info", nullptr,
                    ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse |
                    ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoTitleBar);
                ImVec2 size = ImGui::GetWindowSize();
                ImVec2 pos = { (float)x + 10.0f, (float)y };
                if (size.x > ss.x - pos.x - 5)
                    pos.x = ss.x - size.x - 5;
                if (size.y > ss.y - pos.y - 5)
                    pos.y = ss.y - size.y - 5;
                if (pos.x < 5) pos.x = 5;
                if (pos.y < 5) pos.y = 5;
                ImGui::SetWindowPos(pos);
                glm::dvec2 c = pixel_to_complex(this, { x, y });
                glm::dvec2 z = c;
                int i;
                int maxiters = max_iters(config.zoom, zoom_co, config.iter_co);
                float* orbit = new float[maxiters * 2];
                for (i = 1; i < maxiters; i++) {
                    *reinterpret_cast<glm::vec2*>(orbit + (i - 1) * 2) = static_cast<glm::vec2>(complex_to_pixel(z, ss, config.zoom, config.offset));
                    double xx = z.x * z.x;
                    double yy = z.y * z.y;
                    if (xx + yy > 100)
                        goto display;
                    if (config.degree == 2.f)
                        z = glm::dvec2(xx - yy, 2.0f * z.x * z.y) + c;
                    else {
                        float r = sqrt(z.x * z.x + z.y * z.y);
                        float theta = atan2(z.y, z.x);
                        z = pow(glm::length(z), config.degree) * glm::dvec2(cos(config.degree * theta), sin(config.degree * theta)) + c;
                    }
                }
                i = -1;
            display:
                if (i > 0)
                    ImGui::Text("Re: %.17g\nIm: %.17g\nIterations before bailout: %d", c.x, c.y, i);
                else
                    ImGui::Text("Re: %.17g\nIm: %.17g\nPoint is in set", c.x, c.y);
                glViewport(0, 0, julia_size * factor, julia_size * factor);
                glBindFramebuffer(GL_FRAMEBUFFER, juliaFrameBuffer);
                glUniform1i(glGetUniformLocation(shaderProgram, "op"), 4);
                glUniform2d(glGetUniformLocation(shaderProgram, "mouseCoord"), c.x, c.y);
                glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), julia_size * factor, julia_size * factor);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                ImGui::SeparatorText("Julia Set");
                ImGui::Image((void*)(intptr_t)juliaTexBuffer, ImVec2(julia_size, julia_size));
                ImGui::End();

                glUniform1i(glGetUniformLocation(shaderProgram, "numVertices"), maxiters);
                glBufferData(GL_SHADER_STORAGE_BUFFER, maxiters * sizeof(glm::vec2), orbit, GL_DYNAMIC_COPY);
                delete[] orbit;
            }
            else {
                glUniform1i(glGetUniformLocation(shaderProgram, "numVertices"), 0);
            }
            ImGui::PopFont();
            if (!recording) {
                glViewport(0, 0, ss.x * factor, ss.y * factor);
                glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), ss.x * factor, ss.y * factor);
            }
            else {
                factor = (zsc.tcfg.ssaa ? zsc.tcfg.ssaa_factor : 1);
                glViewport(0, 0, zsc.tcfg.screenSize.x * factor, zsc.tcfg.screenSize.y * factor);
                glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), zsc.tcfg.screenSize.x * factor, zsc.tcfg.screenSize.y * factor);
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
                [[fallthrough]];
            case MV_RENDER:
                glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
                if (recording) {
                    glBindFramebuffer(GL_FRAMEBUFFER, finalFrameBuffer);
                    glUniform1i(glGetUniformLocation(shaderProgram, "op"), MV_RENDER);
                    glViewport(0, 0, zsc.tcfg.screenSize.x, zsc.tcfg.screenSize.y);
                    glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), zsc.tcfg.screenSize.x, zsc.tcfg.screenSize.y);
                    glDrawArrays(GL_TRIANGLES, 0, 6);

                    glBindTexture(GL_TEXTURE_2D, finalTexBuffer);
                    glBindFramebuffer(GL_FRAMEBUFFER, NULL);
                    glUniform1i(glGetUniformLocation(shaderProgram, "op"), MV_DISPLAY);
                    glViewport(0, 0, ss.x, ss.y);
                    glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), ss.x, ss.y);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                } else {
                    glBindFramebuffer(GL_FRAMEBUFFER, NULL);
                    glUniform1i(glGetUniformLocation(shaderProgram, "op"), MV_RENDER);
                    glViewport(0, 0, ss.x, ss.y);
                    glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), ss.x, ss.y);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                }
                set_op(MV_RENDER, true);
            }
            if (recording && !paused) {
                glBindTexture(GL_TEXTURE_2D, finalTexBuffer);
                int w = zsc.tcfg.screenSize.x, h = zsc.tcfg.screenSize.y;
                unsigned char* buffer = new unsigned char[4 * w * h];
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, buffer);

                cv::Mat frame(cv::Size(w, h), CV_8UC3, buffer);
                cv::cvtColor(frame, frame, cv::COLOR_RGB2BGR);
                cv::flip(frame, frame, 0);
                writer.write(frame);
                progress++;
                if (zsc.fps * zsc.duration == progress) {
                    use_config(config);
                    recording = false;
                    writer.release();
                    glfwSwapInterval(1);
                } else {
                    int framecount = (zsc.fps * zsc.duration);
                    //double x = static_cast<double>(progress) / framecount;
                    //double z = 2 * pow(x, 3) - 3 * pow(x, 2) + 1.f;
                    double coeff = pow(config.zoom / 5.0, 1.0 / framecount);
                    zsc.tcfg.zoom = 8.0 * pow(coeff, progress);
                    glUniform1i(glGetUniformLocation(shaderProgram, "max_iters"),
                        max_iters(zsc.tcfg.zoom, zoom_co, config.iter_co));
                    glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), zsc.tcfg.zoom);
                }
                set_op(MV_COMPUTE);
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