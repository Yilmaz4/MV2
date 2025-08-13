#define VERSION "1.1"

#ifdef PLATFORM_WINDOWS
    #pragma comment(linker, "/ENTRY:mainCRTStartup")
#endif

#define IMGUI_DEFINE_MATH_OPERATORS
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#define MINIAUDIO_IMPLEMENTATION
#define _USE_MATH_DEFINES
#define GLM_ENABLE_EXPERIMENTAL

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
#include <imgui_ext.h>
#include <nlohmann/json.hpp>
#include <miniaudio.h>

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
#include <atomic>
#include <sstream>
#include <vector>
#include <iomanip>
#include <ctime>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <complex>
#include <regex>

using namespace glm;

#define MV_COMPUTE  2
#define MV_POSTPROC 1
#define MV_RENDER   0

#define U8(t) reinterpret_cast<const char*>(t)

void GLAPIENTRY glMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    if (type != GL_DEBUG_TYPE_ERROR) return;
    fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n", "** GL ERROR **", type, severity, message);
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

float vertices[] = {
    -1.0f, -1.0f,
     1.0f, -1.0f,
     1.0f,  1.0f,
    -1.0f, -1.0f,
     1.0f,  1.0f,
    -1.0f,  1.0f
};

const char* vertexShaderSource = R"(
#version 460 core

layout(location = 0) in vec2 aPos;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}

)";

constexpr double zoom_co = 0.85;
constexpr double doubleClick_interval = 0.4;
ivec2 monitorSize;

struct Slider {
    std::string name;
    double def = 0.f;
    double value = 0.f;
    float slider = 0.f;
    float min = 0.f, max = 0.f;
    float step = 1.f;

    Slider(const std::string& name = "", double def = 0.0, float min = 0.f, float max = 0.f, float step = 1.f)
        : name(name), def(def), value(def), slider(static_cast<float>(def)), min(min), max(max), step(step) {}
};

struct Fractal {
    std::string name = "Mandelbrot";
    std::string equation = "cpow(z, power) + c";
    std::string condition = "distance(z, c) > 10";
    std::string initialz = "c";
    float power = 2.f;
    bool continuous_compatible = true;
    bool julia_compatible = true;
    bool hflip = false, vflip = false;

    std::vector<Slider> sliders;
};

std::vector<Fractal> fractals = {
    Fractal({.name = "Custom"}),
    Fractal({.name = "Mandelbrot",
        .equation = "cpow(z, power) + c",
        .condition = "xsq + ysq > 100",
        .initialz = "c",
        .power = 2.f,
        .continuous_compatible = true,
        .julia_compatible = true
    }),
    Fractal({.name = "Julia Set",
        .equation = "cpow(z, power) + dvec2(Re, Im)",
        .condition = "xsq + ysq > 100",
        .initialz = "c",
        .power = 2.f,
        .continuous_compatible = true,
        .julia_compatible = false,
        .sliders = { Slider("Re", 0.f), Slider("Im", 0.f) }
    }),
    Fractal({.name = "Nova",
        .equation = "z - cdivide(cpow(z, power) - dvec2(1, 0), power * cpow(z, power - 1)) + c",
        .condition = "distance(z, prevz) < 10e-5",
        .initialz = "dvec2(1, 0)",
        .power = 3.f,
        .continuous_compatible = true,
        .julia_compatible = false,
    }),
    Fractal({.name = "Burning ship",
        .equation = "cpow(dvec2(abs(z.x), abs(z.y)), power) + c",
        .condition = "xsq + ysq > 100",
        .initialz = "c",
        .power = 2.f,
        .continuous_compatible = true,
        .julia_compatible = true,
        .vflip = true,
    }),
    Fractal({.name = "Newton",
        .equation = "z - cmultiply(dvec2(Re, Im), cdivide(cpow(z, power) - dvec2(1.f, 0.f), power * cpow(z, power - 1.f)))",
        .condition = "distance(z, prevz) < 10e-5",
        .initialz = "c",
        .power = 3.f,
        .continuous_compatible = true,
        .julia_compatible = false,
        .sliders = { Slider("Re", 1.f), Slider("Im", 0.f) }
    }),
    Fractal({.name = "Magnet 1",
        .equation = "cpow(cdivide(cpow(z, power) + c - dvec2(1, 0), power * z + c - dvec2(power, 0)), power)",
        .condition = "length(z) >= 100 || length(z - dvec2(1,0)) <= 1e-5",
        .initialz="dvec2(0)",
        .power = 2.f,
        .continuous_compatible = false,
        .julia_compatible = true,
    }),
    Fractal({.name = "Magnet 2",
        .equation = "cpow(cdivide(cpow(z, power + 1) + 3 * cmultiply(c - dvec2(1, 0), z) + cmultiply(c - dvec2(1, 0), c - dvec2(2, 0)), 3 * cpow(z, power) + 3 * cmultiply(c - dvec2(2, 0), z) + cmultiply(c - dvec2(1, 0), c - dvec2(power, 0)) + dvec2(1, 0)), power)",
        .condition = "length(z) >= 100 || length(z - dvec2(1,0)) <= 1e-5",
        .initialz = "dvec2(0)",
        .power = 2.f,
        .continuous_compatible = false,
        .julia_compatible = true,
    }),
    Fractal({.name = "Lambda",
        .equation = "cmultiply(c, cmultiply(z, cpow(dvec2(1, 0) - z, power - 1)))",
        .condition = "xsq + ysq > 100",
        .initialz = "dvec2(0.5f, 0.f)",
        .power = 2.f,
        .continuous_compatible = true,
        .julia_compatible = true,
    }),
    Fractal({.name = "Tricorn",
        .equation = "cpow(cconj(z), power) + c",
        .condition = "xsq + ysq > 100",
        .initialz = "c",
        .power = 2.f,
        .continuous_compatible = true,
        .julia_compatible = true,
    }),
    Fractal({.name = "Feather",
        .equation = "cmultiply(cpow(z, 2.f), cdivide(z, dvec2(1.0 + z.x * z.x, z.y * z.y))) + c",
        .condition = "xsq + ysq > 100",
        .initialz = "c",
        .power = 0.f,
        .continuous_compatible = false,
        .julia_compatible = true,
    }),
    Fractal({.name = "SFX",
        .equation = "z * (xsq + ysq) - cmultiply(z, c * c)",
        .condition = "xsq + ysq > 100",
        .initialz = "c",
        .power = 0.f,
        .continuous_compatible = false,
        .julia_compatible = false,
    }),
};


struct Config {
    dvec2  center = { -0.4, 0.0 };
    ivec2  frameSize = { 1200, 800 };
    double zoom = 5.0;
    float  theta = 0.f;
    bool   vflip = false;
    bool   hflip = false;
    float  spectrum_offset = 0.f;
    float  iter_multiplier = 12.f;
    bool   auto_adjust_iter = true;
    int    max_iters = 100;
    float  iter_co = 1.04f;
    bool   continuous_coloring = true;
    bool   normal_map_effect = false;
    fvec3 set_color = { 0.f, 0.f, 0.f };
    int    ssaa = 2;
    int    transfer_function = 0;
    // experimental
    float  degree = 1.f;
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
    ImFont* commitmono = nullptr;
    ImFont* adwaita = nullptr;

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
    int max_vertices = 200;
    bool sync_zoom_julia = true;
    bool juliaset = true;
    bool juliaset_disabled_incompat = false;
    bool orbit = true;
    bool audio = true;
    bool cmplxinfo = true;
    bool always_refresh_main = false;

    bool persist_orbit = false;
    bool enable_orbit = false;
    bool orbit_refreshed = false;

    dvec2 oldPos = { 0, 0 };
    dvec2 lastPresses = { -doubleClick_interval, 0 };
    bool dragging = false;
    bool rightClickHold = false;
    dvec2 tempCenter = config.center;
    double tempZoom = config.zoom;

    int zoomTowards = 1; // 0: center, 1: cursor

    dvec2 cmplxCoord;
    int numIterations;

    bool recording = false;
    bool paused = false;
    int progress = 0;

    GLuint shaderProgram = 0;
    GLuint vertexShader = 0;

    GLuint computeFrameBuffer = 0;
    GLuint postprocFrameBuffer = 0;
    GLuint finalFrameBuffer = 0;
    GLuint juliaFrameBuffer = 0;

    GLuint computeTexBuffer = 0;
    GLuint postprocTexBuffer = 0;
    GLuint finalTexBuffer = 0;
    GLuint juliaTexBuffer = 0;

    GLuint paletteBuffer = 0;
    GLuint orbitInBuffer = 0;
    GLuint orbitOutBuffer = 0;
    GLuint sliderBuffer = 0;
    GLuint kernelBuffer = 0;

    int32_t stateID = 10;
    ImGradientHDRState state;
    ImGradientHDRTemporaryState tempState;

    ma_device ma_dev;
    float g_sampleRate = 100.0f;
    int g_index = 0;
    
    vec2* orbit_buffer_front = new vec2[max_vertices + 2]{};
    vec2* orbit_buffer_back = new vec2[max_vertices + 2]{};
    std::atomic<bool> ready_to_copy = false;
    int index_repeat = 0;
    int index_repeat_new = 0;
    bool persist_audio = false;

    int op = MV_COMPUTE;
public:
    MV2() {
        glfwInit();

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
        if ((session && std::string(session) == "Hyprland") || (hyprSig != nullptr)) {
            system("hyprctl keyword windowrulev2 renderunfocused, class:mv2 > /dev/null");
        }

        window = glfwCreateWindow(config.frameSize.x, config.frameSize.y, "Mandelbrot Voyage", NULL, NULL);
        if (window == nullptr) {
            std::cout << "Failed to create OpenGL window" << std::endl;
            return;
        }

#ifdef PLATFORM_WINDOWS
        BOOL use_dark_mode = true;
        DwmSetWindowAttribute(glfwGetWin32Window(window), 20, &use_dark_mode, sizeof(use_dark_mode));
#endif

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

        ImFontGlyphRangesBuilder builder;
        builder.AddText(U8(u8"↷"));
        builder.AddText(U8(u8"↶"));
        ImVector<ImWchar> ranges;
        builder.BuildRanges(&ranges);

        auto font1 = b::embed<"assets/commitmono.otf">();
        commitmono = io.Fonts->AddFontFromMemoryTTF((void*)font1.data(), font1.size(), 11.f, nullptr, ranges.Data);

        auto font2 = b::embed<"assets/adwaita.ttf">();
        adwaita = io.Fonts->AddFontFromMemoryTTF((void*)font2.data(), font2.size(), 11.f, nullptr, ranges.Data);

        IM_ASSERT(commitmono != NULL);
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

        glGenTextures(1, &computeTexBuffer);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, computeTexBuffer);
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, config.frameSize.x * config.ssaa,
            config.frameSize.y * config.ssaa, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        

        glGenTextures(1, &postprocTexBuffer);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, config.frameSize.x * config.ssaa,
            config.frameSize.y * config.ssaa, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenTextures(1, &finalTexBuffer);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, finalTexBuffer);
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1920, 1080, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenTextures(1, &juliaTexBuffer);
        glBindTexture(GL_TEXTURE_2D, juliaTexBuffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, julia_size * config.ssaa,
            julia_size * config.ssaa, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenFramebuffers(1, &computeFrameBuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, computeFrameBuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, computeTexBuffer, 0);

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

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glUseProgram(shaderProgram);

        glUniform1i(glGetUniformLocation(shaderProgram, "computeTex"), 0);
        glUniform1i(glGetUniformLocation(shaderProgram, "postprocTex"), 1);
        glUniform1i(glGetUniformLocation(shaderProgram, "finalTex"), 2);

        glGenBuffers(1, &orbitInBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, orbitInBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, orbitInBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, (max_vertices + 2) * sizeof(vec2), nullptr, GL_DYNAMIC_COPY);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "orbit_in"), 0);

        glGenBuffers(1, &orbitOutBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, orbitOutBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, orbitOutBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, (max_vertices + 2) * sizeof(dvec2), nullptr, GL_DYNAMIC_COPY);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "orbit_out"), 1);

        glUniform1i(glGetUniformLocation(shaderProgram, "numVertices"), 0);

        glGenBuffers(1, &paletteBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, paletteBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, max_colors * sizeof(fvec4), paletteData.data(), GL_DYNAMIC_COPY);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, paletteBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "spectrum"), 2);
        glUniform1i(glGetUniformLocation(shaderProgram, "span"), span);

        glGenBuffers(1, &sliderBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, sliderBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sliderBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "variables"), 3);

        glGenBuffers(1, &kernelBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, kernelBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, kernelBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "kernel"), 4);
        upload_kernel(config.ssaa);

        use_config(config, true, false);
        on_windowResize(window, config.frameSize.x * dpi_scale, config.frameSize.y * dpi_scale);

        for (const vec4& c : paletteData) {
            state.AddColorMarker(c.w, { c.r, c.g, c.b }, 1.0f);
        }

        ma_result result;
        ma_device_config config  = ma_device_config_init(ma_device_type_playback);
        config.playback.format   = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate        = 96000u;
        config.dataCallback      = data_callback;
        config.pUserData         = this;

        result = ma_device_init(nullptr, &config, &ma_dev);
        if (result != MA_SUCCESS) {
            std::cerr << "Failed to initialize audio device\n";
        }

        ma_device_start(&ma_dev);
    }
private:
    void use_config(Config config, bool variables = true, bool textures = true) {
        if (variables) {
            glUniform2i(glGetUniformLocation(shaderProgram, "frameSize"), config.frameSize.x, config.frameSize.y);
            glUniform2d(glGetUniformLocation(shaderProgram, "center"), config.center.x, config.center.y);
            glUniform1f(glGetUniformLocation(shaderProgram, "theta"), config.theta * M_PI / 180.f);
            glUniform1i(glGetUniformLocation(shaderProgram, "hflip"), config.hflip);
            glUniform1i(glGetUniformLocation(shaderProgram, "vflip"), config.vflip);
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
            glUniform1i(glGetUniformLocation(shaderProgram, "transfer_function"), config.transfer_function);

            glUniform1i(glGetUniformLocation(shaderProgram, "show_orbit"), false);
            glUniform2i(glGetUniformLocation(shaderProgram, "orbit_start"), -1, -1);

            glUniform1f(glGetUniformLocation(shaderProgram, "power"), config.degree);
            glUniform1i(glGetUniformLocation(shaderProgram, "fractal"), fractal);

            glUniform1f(glGetUniformLocation(shaderProgram, "angle"), config.angle);
            glUniform1f(glGetUniformLocation(shaderProgram, "height"), config.height);

            glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "orbit_in"), 0);
            glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "orbit_out"), 1);
            glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "spectrum"), 2);
            glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "variables"), 3);
            glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "newton_roots"), 5);
        }
        if (textures) {
            glBindFramebuffer(GL_FRAMEBUFFER, computeFrameBuffer);
            glBindTexture(GL_TEXTURE_2D, computeTexBuffer);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, config.frameSize.x * config.ssaa, config.frameSize.y * config.ssaa, 0, GL_RGB, GL_FLOAT, NULL);

            glBindFramebuffer(GL_FRAMEBUFFER, postprocFrameBuffer);
            glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, config.frameSize.x * config.ssaa, config.frameSize.y * config.ssaa, 0, GL_RGBA, GL_FLOAT, NULL);
        }
    }

    void set_op(int p, bool override = false) {
        if (p > op || override) op = p;
    }

    static dvec2 cmultiply(dvec2 a, dvec2 b) {
        return dvec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
    }
    static dvec2 cdivide(dvec2 a, dvec2 b) {
        return dvec2((a.x * b.x + a.y * b.y), (a.y * b.x - a.x * b.y)) / (b.x * b.x + b.y * b.y);
    }

    dvec2 pixel_to_complex(dvec2 pixelCoord) {
        ivec2 ss = (fullscreen ? monitorSize : config.frameSize);

        return config.center + cmultiply(((dvec2(pixelCoord.x / ss.x, (ss.y - pixelCoord.y) / ss.y)) - dvec2(0.5, 0.5)) *
            dvec2(config.zoom, (ss.y * config.zoom) / ss.x), dvec2(cos(config.theta * M_PI / 180.f), sin(config.theta * M_PI / 180.f))) * dvec2(config.hflip ? -1.0 : 1.0, config.vflip ? -1.0 : 1.0);
    }
    static dvec2 pixel_to_complex(dvec2 pixelCoord, ivec2 ss, double zoom, dvec2 center, float theta, bool hflip, bool vflip) {
        return center + cmultiply(((dvec2(pixelCoord.x / ss.x, (ss.y - pixelCoord.y) / ss.y)) - dvec2(0.5, 0.5)) *
            dvec2(zoom, (ss.y * zoom) / ss.x), dvec2(cos(theta * M_PI / 180.f), sin(theta * M_PI / 180.f))) * dvec2(hflip ? -1.0 : 1.0, vflip ? -1.0 : 1.0);
    }
    
    dvec2 complex_to_pixel(dvec2 complexCoord) {
        ivec2 ss = (fullscreen ? monitorSize : config.frameSize);

        complexCoord = config.center + cmultiply(complexCoord - config.center, dvec2(cos(config.theta * M_PI / 180.f), -sin(config.theta * M_PI / 180.f))) * dvec2(config.hflip ? -1.0 : 1.0, config.vflip ? -1.0 : 1.0);
        dvec2 normalizedCoord = (complexCoord - config.center);
        normalizedCoord /= dvec2(config.zoom, (ss.y * config.zoom) / ss.x);
        dvec2 pixelCoordNormalized = normalizedCoord + dvec2(0.5, 0.5);
        return dvec2(pixelCoordNormalized.x * ss.x, ss.y - pixelCoordNormalized.y * ss.y);
    }
    static dvec2 complex_to_pixel(dvec2 complexCoord, ivec2 ss, double zoom, dvec2 center, float theta, bool hflip, bool vflip) {
        complexCoord = center + cmultiply(complexCoord - center, dvec2(cos(theta * M_PI / 180.f), -sin(theta * M_PI / 180.f))) * dvec2(hflip ? -1.0 : 1.0, vflip ? -1.0 : 1.0);
        dvec2 normalizedCoord = (complexCoord - center);
        normalizedCoord /= dvec2(zoom, (ss.y * zoom) / ss.x);
        dvec2 pixelCoordNormalized = normalizedCoord + dvec2(0.5, 0.5);
        return dvec2(pixelCoordNormalized.x * ss.x, ss.y - pixelCoordNormalized.y * ss.y);
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
        if (app->config.frameSize.x == width && app->config.frameSize.y == height && app->fullscreen) {
            app->set_op(MV_RENDER);
            return;
        }
        glViewport(0, 0, width, height);

        if (!app->fullscreen) app->config.frameSize = { width, height };
        if (app->shaderProgram)
            glUniform2i(glGetUniformLocation(app->shaderProgram, "frameSize"), width, height);
        app->set_op(MV_COMPUTE);

        glBindFramebuffer(GL_FRAMEBUFFER, app->computeFrameBuffer);
        glBindTexture(GL_TEXTURE_2D, app->computeTexBuffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, width * app->config.ssaa, height * app->config.ssaa, 0, GL_RGB, GL_FLOAT, NULL);

        glBindFramebuffer(GL_FRAMEBUFFER, app->postprocFrameBuffer);
        glBindTexture(GL_TEXTURE_2D, app->postprocTexBuffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width * app->config.ssaa, height * app->config.ssaa, 0, GL_RGBA, GL_FLOAT, NULL);
    }

    static void on_mouseButton(GLFWwindow* window, int button, int action, int mod) {
        MV2* app = static_cast<MV2*>(glfwGetWindowUserPointer(window));
        if (!app->shaderProgram) return;
        ivec2 ss = (app->fullscreen ? monitorSize : app->config.frameSize);
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

                    GLuint shader = 0;
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
                    dvec2 cmplx = app->pixel_to_complex(dvec2(x, y));
                    fractals[2].sliders[0].slider = fractals[2].sliders[0].value = cmplx.x;
                    fractals[2].sliders[1].slider = fractals[2].sliders[1].value = cmplx.y;
                    app->tempCenter = app->config.center;
                    app->tempZoom = app->config.zoom;
                    app->config.zoom = app->sync_zoom_julia ? pow(app->config.zoom, 1.f / app->config.degree) * 1.2f : 3.0;
                    app->config.center = dvec2(0.0, 0.0);
                    switch_shader();

                    if (app->juliaset) {
                        app->juliaset = false;
                        app->juliaset_disabled_incompat = true;
                    }
                }
                else if (app->rightClickHold && app->fractal == 2) {
                    app->fractal = 1;
                    app->config.center = app->tempCenter;
                    app->config.zoom = app->tempZoom;
                    switch_shader();

                    if (app->juliaset_disabled_incompat) {
                        app->juliaset = true;
                        app->juliaset_disabled_incompat = false;
                    }
                }
                else {
                    glfwGetCursorPos(window, &app->oldPos.x, &app->oldPos.y);
                    app->oldPos *= app->dpi_scale;
                    dvec2 pos = app->pixel_to_complex(app->oldPos);
                    app->config.center += pos - app->config.center;
                    glUniform2d(glGetUniformLocation(app->shaderProgram, "center"), app->config.center.x, app->config.center.y);
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

                if (app->juliaset) {
                    app->julia_zoom = app->sync_zoom_julia ? pow(app->config.zoom, 1.f / app->config.degree) * 1.2f : 3.0;
                    glUniform1d(glGetUniformLocation(app->shaderProgram, "julia_zoom"), app->julia_zoom);
                    glUniform1i(glGetUniformLocation(app->shaderProgram, "julia_maxiters"),
                        max_iters(app->julia_zoom, zoom_co, app->config.iter_co, 3.0));
                }
                if (app->orbit) {
                    app->enable_orbit = true;
                }
                if (app->audio) {
                    app->g_index = app->index_repeat;
                }
                app->refresh_rightclick();
                break;
            case GLFW_RELEASE:
                app->rightClickHold = false;
                if (!app->persist_orbit) {
                    glUniform1i(glGetUniformLocation(app->shaderProgram, "show_orbit"), false);
                }
                glUniform2i(glGetUniformLocation(app->shaderProgram, "orbit_start"), -1, -1);
                app->set_op(MV_POSTPROC);
            }
        }
    }

    static void on_cursorMove(GLFWwindow* window, double x, double y) {
        MV2* app = static_cast<MV2*>(glfwGetWindowUserPointer(window));
        x *= app->dpi_scale;
        y *= app->dpi_scale;
        if (!app->shaderProgram) return;
        ivec2 ss = (app->fullscreen ? monitorSize : app->config.frameSize);
        glUniform2d(glGetUniformLocation(app->shaderProgram, "mousePos"), x, y);
        if (ImGui::GetIO().WantCaptureMouse)
            return;
        if (app->dragging) {
            app->lastPresses = { -doubleClick_interval, 0 };
            app->config.center -= cmultiply(dvec2((x - app->oldPos.x) * app->config.zoom, -(y - app->oldPos.y) * ((app->config.zoom * ss.y) / ss.x)) / dvec2(ss), dvec2(cos(app->config.theta * M_PI / 180.f), sin(app->config.theta * M_PI / 180.f))) * dvec2(app->config.hflip ? -1.0 : 1.0, app->config.vflip ? -1.0 : 1.0);
            glUniform2d(glGetUniformLocation(app->shaderProgram, "center"), app->config.center.x, app->config.center.y);
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
            app->refresh_rightclick();
        }
        else if (!ImGui::GetIO().WantCaptureMouse) {
            double new_zoom = app->config.zoom * pow(zoom_co, y * 1.5);

            if (app->zoomTowards == 1) {
                double cursor_x, cursor_y;
                glfwGetCursorPos(window, &cursor_x, &cursor_y);
                cursor_x *= app->dpi_scale;
                cursor_y *= app->dpi_scale;
                
                dvec2 new_pos = complex_to_pixel(app->pixel_to_complex(dvec2(cursor_x, cursor_y)), app->config.frameSize, new_zoom, app->config.center, app->config.theta, app->config.hflip, app->config.vflip);

                app->config.center = pixel_to_complex(static_cast<dvec2>(app->config.frameSize) / 2.0 + (new_pos - dvec2(cursor_x, cursor_y)), app->config.frameSize, new_zoom, app->config.center, app->config.theta, app->config.hflip, app->config.vflip);
                
                glUniform2d(glGetUniformLocation(app->shaderProgram, "center"), app->config.center.x, app->config.center.y);
            }

            app->config.zoom = new_zoom;
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
                glfwSetWindowMonitor(window, nullptr, app->screenPos.x, app->screenPos.y, app->config.frameSize.x, app->config.frameSize.y, 0);
            }
        }
    }

    void copy_orbit_buffer() {
        ivec2 fs = (fullscreen ? monitorSize : config.frameSize);

        void* srcPtr = glMapNamedBufferRange(orbitOutBuffer, 0, (max_vertices + 2) * sizeof(dvec2), GL_MAP_READ_BIT);
        void* dstPtr = glMapNamedBufferRange(orbitInBuffer, 0, (max_vertices + 2) * sizeof(vec2), GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);

        if (srcPtr && dstPtr) {
            dvec2* src = reinterpret_cast<dvec2*>(srcPtr);
            vec2* dst = reinterpret_cast<vec2*>(dstPtr);
            
            index_repeat_new = 0;
            ready_to_copy.store(false);
            for (size_t i = 0; i < max_vertices + 2; i++) {
                if (i != max_vertices + 1) {
                    if (distance(src[i], src[max_vertices + 1]) < 1e-2)
                        index_repeat_new = i;
                    orbit_buffer_back[i] = (vec2)src[i];
                }
                dst[i] = static_cast<vec2>(complex_to_pixel(src[i]));
                dst[i].y = fs.y - dst[i].y;
            }
            ready_to_copy = true;
        }
        glUnmapNamedBuffer(orbitInBuffer);
        glUnmapNamedBuffer(orbitOutBuffer);
    }

    void refresh_rightclick() {
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        x *= dpi_scale;
        y *= dpi_scale;
        ivec2 fs = (fullscreen ? monitorSize : config.frameSize);

        cmplxCoord = pixel_to_complex({ x, y });
        
        if (cmplxinfo) {
            float texel[3];
            glBindFramebuffer(GL_FRAMEBUFFER, computeFrameBuffer);
            glReadBuffer(GL_FRONT);
            glReadPixels(x * config.ssaa, (fs.y - y) * config.ssaa, 1, 1, GL_RGB, GL_FLOAT, texel);
            numIterations = static_cast<int>(texel[1]);
        }
        if (juliaset) {
            glViewport(0, 0, julia_size * config.ssaa, julia_size * config.ssaa);
            glBindFramebuffer(GL_FRAMEBUFFER, juliaFrameBuffer);
            glUniform1i(glGetUniformLocation(shaderProgram, "op"), 3);
            glUniform2d(glGetUniformLocation(shaderProgram, "mouseCoord"), cmplxCoord.x, cmplxCoord.y);
            glUniform2i(glGetUniformLocation(shaderProgram, "frameSize"), julia_size * config.ssaa, julia_size * config.ssaa);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        if (orbit) {
            glUniform2i(glGetUniformLocation(shaderProgram, "orbit_start"), (int)x, (int)(fs.y - y));
            glUniform1i(glGetUniformLocation(shaderProgram, "numVertices"), max_vertices + 2);
            
            copy_orbit_buffer();
            orbit_refreshed = true;
            set_op(MV_POSTPROC);
        }
        if (always_refresh_main) {
            glUniform2d(glGetUniformLocation(shaderProgram, "mouseCoord"), cmplxCoord.x, cmplxCoord.y);
            set_op(MV_COMPUTE);
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

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, orbitInBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, orbitInBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "orbit_in"), 0);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, orbitOutBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, orbitOutBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "orbit_out"), 1);

        glUniform1i(glGetUniformLocation(shaderProgram, "numVertices"), 0);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, paletteBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, paletteBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "spectrum"), 2);
        glUniform1i(glGetUniformLocation(shaderProgram, "span"), span);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, sliderBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sliderBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "variables"), 3);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, kernelBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, kernelBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "kernel"), 4);
        glUniform1i(glGetUniformLocation(shaderProgram, "radius"), config.ssaa);
    }

    void update_shader() const {
        glUniform1f(glGetUniformLocation(shaderProgram, "power"), config.degree);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, sliderBuffer);
        std::vector<float> values(fractals[fractal].sliders.size());
        for (int i = 0; i < values.size(); i++) {
            values[i] = fractals[fractal].sliders[i].value;
        }
        glBufferData(GL_SHADER_STORAGE_BUFFER, values.size() * sizeof(float), values.data(), GL_DYNAMIC_DRAW);
    }

    static float hermite(float y0, float y1, float y2, float y3, float t, float tension) {
        float m0 = (y2 - y0) * tension;
        float m1 = (y3 - y1) * tension;

        float t2 = t * t;
        float t3 = t2 * t;

        float h00 =  2.0f * t3 - 3.0f * t2 + 1.0f;
        float h10 =         t3 - 2.0f * t2 + t;
        float h01 = -2.0f * t3 + 3.0f * t2;
        float h11 =         t3 -        t2;

        return h00 * y1 + h10 * m0 + h01 * y2 + h11 * m1;
    }

    static void data_callback(ma_device* device, void* output, const void* input, ma_uint32 frameCount) {
        MV2* app = reinterpret_cast<MV2*>(device->pUserData);

        float* out = (float*)output;
        (void)input;

        double x, y;
        glfwGetCursorPos(app->window, &x, &y);
        x *= app->dpi_scale;
        y *= app->dpi_scale;

        dvec2 cmplxCoord = app->pixel_to_complex({ x, y });

        for (ma_uint32 i = 0; i < frameCount; i++) {
            static float t = 0.f;

            int num = app->max_vertices + 2;
            vec2 a, b, c, d;

            int range = app->max_vertices - app->index_repeat + 2;

            auto idx = [&](int i) {
                return ((i - app->index_repeat) % range + range) % range + app->index_repeat;
            };

            a = app->orbit_buffer_front[idx(app->g_index)];
            b = app->orbit_buffer_front[idx(app->g_index + 1)];
            c = app->orbit_buffer_front[idx(app->g_index + 2)];
            d = app->orbit_buffer_front[idx(app->g_index + 3)];

            float volume = 1.f;
            if (app->numIterations != -1 || !app->rightClickHold) {
                volume = 0.f;
            }

            out[i * 2 + 0] = std::clamp(hermite(a.x, b.x, c.x, d.x, t, 0.2f), -1.f, 1.f) * volume;
            out[i * 2 + 1] = std::clamp(hermite(a.y, b.y, c.y, d.y, t, 0.2f), -1.f, 1.f) * volume;

            if (idx(app->g_index + 1) - idx(app->g_index) != 0 && app->ready_to_copy.exchange(false)) {
                std::swap(app->orbit_buffer_front, app->orbit_buffer_back);
                app->index_repeat = app->index_repeat_new;
            }

            if (a.x * a.x + a.y * a.y < 100.f) {
                t += 1.f / app->g_sampleRate;
                if (t > 1.f) {
                    t = t - 1.f;
                    app->g_index += 1;
                }
            }
        }
    }

public:
    void mainloop() {
        do {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ivec2 fs = (fullscreen ? monitorSize : config.frameSize);
            
            double currentTime = glfwGetTime();

            if (currentTime < 1.6f) {
                config.degree = (currentTime < 0.1f) ? 1.f : (2.f - pow(1.f - (currentTime - 0.2f) / 1.5f, 13));
                update_shader();
                set_op(MV_COMPUTE);
            }
            else if (!startup_anim_complete) {
                config.degree = 2.f;
                startup_anim_complete = true;
                update_shader();
                set_op(MV_COMPUTE);
            }

            ImGui::PushFont(commitmono);

            ImGui::SetNextWindowPos({ 5.f * dpi_scale, 5.f * dpi_scale });
            ImGui::SetNextWindowSize(ImVec2(320.f, 0.f));
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
                    zsc.tcfg.frameSize = commonres[res];
                    auto vec_to_str = [commonres](int i) {
                        return std::format("{}x{}", commonres.at(i).x, commonres.at(i).y);
                        };
                    std::string preview = vec_to_str(res);

                    if (ImGui::BeginCombo("Resolution", preview.c_str())) {
                        for (int i = 0; i < commonres.size(); i++) {
                            const bool is_selected = (res == i);
                            if (ImGui::Selectable(vec_to_str(i).c_str(), is_selected)) {
                                zsc.tcfg.frameSize = commonres.at(i);
                                glBindFramebuffer(GL_FRAMEBUFFER, finalFrameBuffer);
                                glBindTexture(GL_TEXTURE_2D, finalTexBuffer);
                                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, zsc.tcfg.frameSize.x, zsc.tcfg.frameSize.y, 0, GL_RGB, GL_FLOAT, NULL);
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
                        //writer = cv::VideoWriter(zsc.path, cv::VideoWriter::fourcc('m', 'j', 'p', 'g'), zsc.fps, cv::Size(zsc.tcfg.frameSize.x, zsc.tcfg.frameSize.y));

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
                    // TODO: Render at full resolution without SSAA for taking a screenshot
                    int w, h;
                    if (fullscreen) {
                        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                        w = mode->width, h = mode->height;
                    } else {
                        w = config.frameSize.x;
                        h = config.frameSize.y;
                    }
                    w *= config.ssaa;
                    h *= config.ssaa;

                    unsigned char* buffer = new unsigned char[4 * w * h];

                    GLuint fbo;
                    glGenFramebuffers(1, &fbo);
                    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, postprocTexBuffer, 0);

                    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                        std::cerr << "Framebuffer is not complete!" << std::endl;
                        delete[] buffer;
                        glBindFramebuffer(GL_FRAMEBUFFER, 0);
                        glDeleteFramebuffers(1, &fbo);
                        return;
                    }

                    glReadBuffer(GL_COLOR_ATTACHMENT0);
                    glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, buffer);

                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glDeleteFramebuffers(1, &fbo);

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

                ImGui::DragFloat("Sample Rate", &g_sampleRate, 10.f, 0.f, 5000.f);
                
                ImGui::SeparatorText("Parameters");
                bool update = false;

                ImGui::Text("Re");
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                update |= ImGui::InputDouble("##re", &config.center.x, 0.0, 0.0, "%.17g");

                ImGui::Text("Im");
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                update |= ImGui::InputDouble("##im", &config.center.y, 0.0, 0.0, "%.17g");
                
                if (update) {
                    glUniform2d(glGetUniformLocation(shaderProgram, "center"), config.center.x, config.center.y);
                    set_op(MV_COMPUTE);
                }
                ImGui::Text("Zoom"); ImGui::SetNextItemWidth(80); ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
                if (ImGui::InputDouble("##zoom", &config.zoom, 0.0, 0.0, "%.2e")) {
                    glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), config.zoom);
                    set_op(MV_COMPUTE);
                }

                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
                ImGui::Text("Rotation"); ImGui::SetNextItemWidth(40); ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
                if (ImGui::DragFloat("##theta", &config.theta, 1.f, 0.f, 360.f, "%.0f°")) {
                    glUniform1f(glGetUniformLocation(shaderProgram, "theta"), config.theta * M_PI / 180.f);
                    set_op(MV_COMPUTE);
                }

                ImGui::PopFont();
                ImGui::PushFont(adwaita);

                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
                if (ImGui::Button(U8(u8"↻"), ImVec2(ImGui::GetContentRegionAvail().x / 4.f - 5.f, 0.f))) {
                    if (config.theta < 90.f || config.theta == 360.f) config.theta = 90.f;
                    else if (config.theta < 180.f) config.theta = 180.f;
                    else if (config.theta < 270.f) config.theta = 270.f;
                    else if (config.theta < 360.f) config.theta = 0.f;
                    glUniform1f(glGetUniformLocation(shaderProgram, "theta"), config.theta * M_PI / 180.f);
                    set_op(MV_COMPUTE);
                }
                ImGui::PopFont();
                ImGui::PushFont(commitmono);
                ImGui::SetItemTooltip("Rotate 90° clockwise");
                ImGui::PopFont();
                ImGui::PushFont(adwaita);
                
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
                if (ImGui::Button(U8(u8"↺"), ImVec2(ImGui::GetContentRegionAvail().x / 3.f - 4.f, 0.f))) {
                    if (config.theta > 270.f || config.theta == 0.f) config.theta = 270.f;
                    else if (config.theta > 180.f) config.theta = 180.f;
                    else if (config.theta > 90.f) config.theta = 90.f;
                    else if (config.theta > 0.f) config.theta = 0.f;
                    glUniform1f(glGetUniformLocation(shaderProgram, "theta"), config.theta * M_PI / 180.f);
                    set_op(MV_COMPUTE);
                }
                ImGui::PopFont();
                ImGui::PushFont(commitmono);
                ImGui::SetItemTooltip("Rotate 90° anti-clockwise");

                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
                if (config.vflip) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.32f, 0.33f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.32f, 0.33f, 1.00f));
                }
                if (ImGui::Button(U8(u8" "), ImVec2(ImGui::GetContentRegionAvail().x / 2.f - 3.f, 0.f))) {
                    if (config.vflip) ImGui::PopStyleColor(2);
                    config.vflip ^= 1;
                    glUniform1i(glGetUniformLocation(shaderProgram, "vflip"), config.vflip);
                    set_op(MV_COMPUTE);
                }
                else if (config.vflip) {
                    ImGui::PopStyleColor(2);
                }
                ImGui::SetItemTooltip("Flip vertically");

                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
                if (config.hflip) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.32f, 0.33f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.32f, 0.33f, 1.00f));
                }
                if (ImGui::Button(U8(u8" "), ImVec2(ImGui::GetContentRegionAvail().x, 0.f))) {
                    if (config.hflip) ImGui::PopStyleColor(2);
                    config.hflip ^= 1;
                    glUniform1i(glGetUniformLocation(shaderProgram, "hflip"), config.hflip);
                    set_op(MV_COMPUTE);
                }
                else if (config.hflip) {
                    ImGui::PopStyleColor(2);
                }
                ImGui::SetItemTooltip("Flip horizontally");
                
                if (ImGui::Button("Save", ImVec2(ImGui::GetContentRegionAvail().x / 3.f - 2.f, 0))) {
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
                        fout.write(reinterpret_cast<const char*>(value_ptr(config.center)), sizeof(dvec2));
                        fout.write(reinterpret_cast<const char*>(&config.zoom), sizeof(double));
                        fout.close();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Load", ImVec2(ImGui::GetContentRegionAvail().x / 2.f - 1.f, 0))) {
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
                        fin.read(reinterpret_cast<char*>(value_ptr(config.center)), sizeof(dvec2));
                        fin.read(reinterpret_cast<char*>(&config.zoom), sizeof(double));
                        fin.close();
                        glUniform2d(glGetUniformLocation(shaderProgram, "center"), config.center.x, config.center.y);
                        glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), config.zoom);
                        set_op(MV_COMPUTE);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset##params", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                    config.center = Config().center;
                    config.zoom = Config().zoom;
                    glUniform2d(glGetUniformLocation(shaderProgram, "center"), config.center.x, config.center.y);
                    glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), config.zoom);
                    glUniform1f(glGetUniformLocation(shaderProgram, "theta"), config.theta * M_PI / 180.f);
                    set_op(MV_COMPUTE);
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
                            config.ssaa = pow(2, i);
                            
                            glBindFramebuffer(GL_FRAMEBUFFER, computeFrameBuffer);
                            glBindTexture(GL_TEXTURE_2D, computeTexBuffer);
                            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, fs.x * config.ssaa,
                                fs.y * config.ssaa, 0, GL_RGB, GL_FLOAT, NULL);

                            glBindFramebuffer(GL_FRAMEBUFFER, postprocFrameBuffer);
                            glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
                            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, fs.x * config.ssaa,
                                fs.y * config.ssaa, 0, GL_RGBA, GL_FLOAT, NULL);

                            glBindFramebuffer(GL_FRAMEBUFFER, juliaFrameBuffer);
                            glBindTexture(GL_TEXTURE_2D, juliaTexBuffer);
                            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, julia_size * config.ssaa,
                                julia_size * config.ssaa, 0, GL_RGBA, GL_FLOAT, NULL);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

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
                static GLuint shader = 0;
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
                                fractals[0].power     = config.degree;
                                fractals[0].sliders   = fractals[fractal].sliders;
                            } else {
                                config.degree = fractals[n].power;
                                config.continuous_coloring = static_cast<int>(config.continuous_coloring && fractals[n].continuous_compatible);
                                config.hflip = fractals[n].hflip;
                                config.vflip = fractals[n].vflip;
                            }
                            fractal = n;
                            update_shader();
                            if (!fractals[fractal].julia_compatible && juliaset) {
                                juliaset = false;
                                juliaset_disabled_incompat = true;
                            }
                            else if (fractals[fractal].julia_compatible && juliaset_disabled_incompat) {
                                juliaset = true;
                                juliaset_disabled_incompat = false;
                            }
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
                    if (ImGui::Button("Reload##eq", ImVec2(ImGui::GetContentRegionAvail().x / 2.f - 1.f, 0.f)) || reverted) reload = true;
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button("Reset##eq", ImVec2(ImGui::GetContentRegionAvail().x, 0.f))) {
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
                auto slider = [&](const char* label, float* ptr, double* real_ptr, int index, const float def, float speed, float min, float max, bool is_deletable) {
                    ImGui::PushID(ptr);
                    if (ImGui::Button("Round##", ImVec2(80, 0))) {
                        *ptr = round(*ptr);
                        update_fractal = 1;
                    }
                    ImGui::SameLine();
                    if (is_deletable && ImGui::Button("Delete", ImVec2(80, 0))) {
                        to_delete.push_back(index);
                        update_fractal = 1;
                    }
                    else if (!is_deletable && ImGui::Button("Reset", ImVec2(80, 0))) {
                        *ptr = def;
                        update_fractal = 1;
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80);
                    update_fractal |= ImGui::DragFloat(label, ptr, speed, min, max, "%.5f", ImGuiSliderFlags_NoRoundToFormat);
                    if (update_fractal && real_ptr) {
                        *real_ptr = *ptr;
                    }
                    ImGui::PopID();
                };
                float mouseSpeed = cbrt(pow(ImGui::GetIO().MouseDelta.x, 2) + pow(ImGui::GetIO().MouseDelta.y, 2));
                if (fractals[fractal].power != 0.f) {
                    slider("Power", &config.degree, nullptr, -1, fractals[fractal].power, std::max(1e-4f, abs(round(config.degree) - config.degree)) * mouseSpeed * std::min(pow(1.1f, config.degree), 1e+3f) / 40.f, (fractal == 0) ? -FLT_MAX : 2.f, FLT_MAX, false);
                }
                int numSliders = fractals[fractal].sliders.size();
                for (int i = 0; i < numSliders; i++) {
                    Slider& s = fractals[fractal].sliders[i];
                    slider(s.name.c_str(), &s.slider, &s.value, i, s.def, std::max(1e-2f, abs(s.slider) * mouseSpeed / 1000.f), s.min, s.max, fractal == 0);
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
                    ImGui::BeginDisabled(fractals[0].sliders.size() == 0);
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
                        if (ImGui::ColorEdit3("In-set color", glm::value_ptr(config.set_color))) {
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
                    ImGui::BeginDisabled(!fractals[fractal].julia_compatible);
                    ImGui::Checkbox("Julia set", &juliaset);
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Orbit", &orbit)) {
                        if (!orbit) {
                            glUniform1i(glGetUniformLocation(shaderProgram, "show_orbit"), false);
                            set_op(MV_POSTPROC);
                        }
                    }
                    ImGui::Checkbox("Sound", &audio);

                    ImGui::Separator();

                    ImGui::SetNextItemWidth(90);
                    ImGui::BeginDisabled(!juliaset);
                    if (ImGui::InputInt("Julia preview size", &julia_size, 5, 20)) {
                        if (julia_size < 10) julia_size = 10;
                        glBindTexture(GL_TEXTURE_2D, juliaTexBuffer);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, julia_size * config.ssaa, julia_size * config.ssaa, 0, GL_RGBA, GL_FLOAT, NULL);
                    }
                    ImGui::Checkbox("Same zoom in Julia set", &sync_zoom_julia);
                    ImGui::EndDisabled();

                    ImGui::Separator();

                    ImGui::SetNextItemWidth(90);
                    ImGui::BeginDisabled(!orbit);
                    if (ImGui::InputInt("Maximum vertices shown", &max_vertices, 5, 20)) {
                        if (max_vertices < 2) max_vertices = 2;
                        glUniform1i(glGetUniformLocation(shaderProgram, "numVertices"), max_vertices + 2);
                        glBindBuffer(GL_SHADER_STORAGE_BUFFER, orbitInBuffer);
                        glBufferData(GL_SHADER_STORAGE_BUFFER, (max_vertices + 2) * sizeof(vec2), nullptr, GL_DYNAMIC_COPY);
                        glBindBuffer(GL_SHADER_STORAGE_BUFFER, orbitOutBuffer);
                        glBufferData(GL_SHADER_STORAGE_BUFFER, (max_vertices + 2) * sizeof(dvec2), nullptr, GL_DYNAMIC_COPY);
                        if (orbit_buffer_front) delete[] orbit_buffer_front;
                        orbit_buffer_front = new vec2[max_vertices + 2]{};
                        if (orbit_buffer_back) delete[] orbit_buffer_back;
                        orbit_buffer_back = new vec2[max_vertices + 2]{};
                    }
                    if (ImGui::Checkbox("Keep orbit after releasing mouse", &persist_orbit)) {
                        if (!persist_orbit) {
                            glUniform1i(glGetUniformLocation(shaderProgram, "show_orbit"), false);
                            set_op(MV_POSTPROC);
                        }
                    }
                    ImGui::EndDisabled();

                    ImGui::Separator();

                    ImGui::Checkbox("Always refresh main fractal", &always_refresh_main);

                    ImGui::TreePop();
                }
                if (ImGui::TreeNode("Zoom and navigation")) {
                    ImGui::RadioButton("Zoom towards the mouse cursor", &zoomTowards, 1);
                    ImGui::RadioButton("Zoom towards the center", &zoomTowards, 0);

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
                    if (size.x > fs.x / dpi_scale - pos.x - 5.f * dpi_scale)
                        pos.x = fs.x / dpi_scale - size.x - 5.f * dpi_scale;
                    if (size.y > fs.y / dpi_scale - pos.y - 5.f * dpi_scale)
                        pos.y = fs.y / dpi_scale - size.y - 5.f * dpi_scale;
                    if (pos.x < 5.f * dpi_scale) pos.x = 5.f * dpi_scale;
                    if (pos.y < 5.f * dpi_scale) pos.y = 5.f * dpi_scale;
                    ImGui::SetWindowPos(pos);
                }
                if (cmplxinfo) {
                    if (numIterations > 0) ImGui::Text("Re: %.17g\nIm: %.17g\nIterations before bailout: %d", cmplxCoord.x, cmplxCoord.y, numIterations);
                    else if (numIterations == -1) ImGui::Text("Re: %.17g\nIm: %.17g\nPoint is in set", cmplxCoord.x, cmplxCoord.y);
                    else ImGui::Text("Re: %.17g\nIm: %.17g\nPoint out of bounds", cmplxCoord.x, cmplxCoord.y);
                }
                if (juliaset) {
                    if (cmplxinfo) ImGui::SeparatorText("Julia Set");
                    ImGui::Image((void*)(intptr_t)juliaTexBuffer, ImVec2(julia_size, julia_size), {0, 1}, {1, 0});
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 150, 255));
                    ImGui::Text("Double-click to switch");
                    ImGui::PopStyleColor();
                }
                if (cmplxinfo || juliaset) ImGui::End();
            }
            ImGui::PopFont();
            if (recording) {
                glViewport(0, 0, zsc.tcfg.frameSize.x * zsc.tcfg.ssaa, zsc.tcfg.frameSize.y * zsc.tcfg.ssaa);
                glUniform2i(glGetUniformLocation(shaderProgram, "frameSize"), zsc.tcfg.frameSize.x * zsc.tcfg.ssaa, zsc.tcfg.frameSize.y * zsc.tcfg.ssaa);
            } else {
                glViewport(0, 0, fs.x * config.ssaa, fs.y * config.ssaa);
                glUniform2i(glGetUniformLocation(shaderProgram, "frameSize"), fs.x * config.ssaa, fs.y * config.ssaa);
            }
            glUniform1i(glGetUniformLocation(shaderProgram, "ssaa_factor"), config.ssaa);
            switch (op) {
            case MV_COMPUTE:
                glBindFramebuffer(GL_FRAMEBUFFER, computeFrameBuffer);
                glUniform1i(glGetUniformLocation(shaderProgram, "op"), MV_COMPUTE);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                if (persist_orbit)
                    copy_orbit_buffer();
                [[fallthrough]];
            case MV_POSTPROC:
                glBindTexture(GL_TEXTURE_2D, computeTexBuffer);
                glBindFramebuffer(GL_FRAMEBUFFER, postprocFrameBuffer);
                glUniform1i(glGetUniformLocation(shaderProgram, "op"), MV_POSTPROC);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
                [[fallthrough]];
            case MV_RENDER:
                glUniform1i(glGetUniformLocation(shaderProgram, "ssaa_factor"), 1);
                if (recording) {
                    glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
                    glBindFramebuffer(GL_FRAMEBUFFER, finalFrameBuffer);
                    glUniform1i(glGetUniformLocation(shaderProgram, "op"), MV_RENDER);
                    glViewport(0, 0, zsc.tcfg.frameSize.x, zsc.tcfg.frameSize.y);
                    glUniform2i(glGetUniformLocation(shaderProgram, "frameSize"), zsc.tcfg.frameSize.x, zsc.tcfg.frameSize.y);
                    glDrawArrays(GL_TRIANGLES, 0, 6);

                    glBindTexture(GL_TEXTURE_2D, finalTexBuffer);
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glViewport(0, 0, fs.x, fs.y);
                    glUniform2i(glGetUniformLocation(shaderProgram, "frameSize"), fs.x, fs.y);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    set_op(MV_RENDER, true);
                } else {
                    glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glUniform1i(glGetUniformLocation(shaderProgram, "op"), MV_RENDER);
                    glViewport(0, 0, fs.x, fs.y);
                    glUniform2i(glGetUniformLocation(shaderProgram, "frameSize"), fs.x, fs.y);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    set_op(MV_RENDER, true);
                }
            }

            if (enable_orbit) {
                glUniform1i(glGetUniformLocation(shaderProgram, "show_orbit"), true);
                copy_orbit_buffer();
                set_op(MV_POSTPROC);
                enable_orbit = false;
            }
            
            if (orbit_refreshed) {
                copy_orbit_buffer();
                set_op(MV_POSTPROC);
                orbit_refreshed = false;
            }

            if (recording && !paused) {
                int w = zsc.tcfg.frameSize.x, h = zsc.tcfg.frameSize.y;
                unsigned char* buffer = new unsigned char[4 * w * h];
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_FLOAT, buffer);

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