#define IMGUI_DEFINE_MATH_OPERATORS
#define STB_IMAGE_WRITE_IMPLEMENTATION

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

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <iomanip>
#include <ctime>
#include <string>

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

const char* vertexShaderSource = R"(
#version 430 core

layout(location = 0) in vec2 aPos;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* doublePrecFragmentShader = R"glsl(
#version 430 core

#extension GL_ARB_gpu_shader_fp64 : enable
#extension GL_NV_uniform_buffer_std430_layout : enable

double M_PI = 	 3.14159265358979323846LF;
double M_2PI =   M_PI * 2.0LF;
double M_PI2 =   M_PI / 2.0LF;
double M_E =     2.71828182845904523536LF;
double M_EHALF = 1.6487212707001281469LF;

out vec4 fragColor;

uniform ivec2  screenSize;
uniform dvec2  offset;
uniform double zoom;
uniform int    max_iters;
uniform float  spectrum_offset;
uniform float  iter_multiplier;
uniform double bailout_radius;
uniform int    continuous_coloring;
uniform vec3   set_color;
uniform int    degree;
uniform dvec2  mousePos; // position in pixels
uniform dvec2  mouseCoord; // position in the complex plane
uniform double julia_zoom;
uniform int    julia_maxiters;
uniform int    blur;

layout(binding=0) uniform sampler2D mandelbrotTex;
layout(binding=1) uniform sampler2D postprocTex;

uniform int    protocol;

vec2 cexp(vec2 z) {
    return exp(z.x) * vec2(cos(z.y), sin(z.y));
}
dvec2 cconj(dvec2 z) {
	return dvec2(z.x, -z.y);
}
uniform mat3 weight = mat3(
    0.0751136, 0.123841, 0.0751136,
    0.123841, 0.20418, 0.123841,
    0.0751136, 0.123841, 0.0751136
);

layout(std430, binding = 1) readonly buffer spectrum {
    vec4 spec[];
};
uniform int span;

vec3 color(float i) {
    if (i < 0.f) return set_color;
    i = mod(i + spectrum_offset, span) / span;
    for (int v = 0; v < spec.length(); v++) {
        if (spec[v].w >= i) {
            vec4 v2 = spec[v];
            vec4 v1;
            if (v > 0.f) v1 = spec[v - 1];
            else v2 = v1;
            vec4 dv = v2 - v1;
            return v1.rgb + (dv.rgb * (i - v1.w)) / dv.w;
        }
    }
    return vec3(i, i, i);
}

dvec2 advance(dvec2 z, dvec2 c, double xx, double yy) {
    switch (degree) {
    case 2:
        z = dvec2(xx - yy, 2.0f * z.x * z.y) + c;
        break;
    case 3:
        z = dvec2(xx * z.x - 3 * z.x * yy, 3 * xx * z.y - yy * z.y) + c;
        break;
    case 4:
        z = dvec2(xx * xx + yy * yy - 6 * xx * yy,
            4 * xx * z.x * z.y - 4 * z.x * yy * z.y) + c;
        break;
    case 5:
        z = dvec2(xx * xx * z.x + 5 * z.x * yy * yy - 10 * xx * z.x * yy,
            5 * xx * xx * z.y + yy * yy * z.y - 10 * xx * yy * z.y) + c;
        break;
    case 6:
        z = dvec2(xx * xx * xx - 15 * xx * xx * yy + 15 * xx * yy * yy - yy * yy * yy,
            6 * xx * xx * z.x * z.y - 20 * xx * z.x * yy * z.y + 6 * z.x * yy * yy * z.y) + c;
        break;
    case 7:
        z = dvec2(xx * xx * xx * z.x - 21 * xx * xx * z.x * yy + 35 * xx * z.x * yy * yy - 7 * z.x * yy * yy * yy,
            7 * xx * xx * xx * z.y - 35 * xx * xx * yy * z.y + 21 * xx * yy * yy * z.y - yy * yy * yy * z.y) + c;
        break;
    }
    return z;
}

void main() {
    if (protocol == 3) {
        dvec2 c = (dvec2(gl_FragCoord.x / screenSize.x, (screenSize.y - gl_FragCoord.y) / screenSize.y) - dvec2(0.5, 0.5)) * dvec2(julia_zoom, julia_zoom);
        dvec2 z = c;
        
        for (int i = 1; i < julia_maxiters; i++) {
            double xx = z.x * z.x;
            double yy = z.y * z.y;
            if (xx + yy >= bailout_radius) {
                fragColor = vec4(color(iter_multiplier * (i + 1 - log2(log2(float(length(z)))) / log2(degree))), 1.f);
                return;
            }
            z = advance(z, mouseCoord, xx, yy);
        }
        fragColor = vec4(set_color, 1.0);
    }
    
    if (protocol == 2) {
        double h2 = 1;
        vec2 dir = vec2(mousePos) - vec2(screenSize) / 2.f;
        float angle = atan(dir.y, dir.x);
        dvec2 v = dvec2(cexp(vec2(0.0f, angle * M_2PI / 360)));

        dvec2 c = (dvec2(gl_FragCoord.x / screenSize.x, (gl_FragCoord.y) / screenSize.y) - dvec2(0.5, 0.5)) * dvec2(zoom, (screenSize.y * zoom) / screenSize.x) + offset;
        dvec2 z = c;

        dvec2 der1 = dvec2(1.0, 0.0);
        dvec2 der2 = dvec2(0.0, 0.0);

        double xx = z.x * z.x;
        double yy = z.y * z.y;

        double p = xx - z.x / 2.0 + 0.0625 + yy;
        if (degree != 2 || degree == 2 && (4.0 * p * (p + (z.x - 0.25)) > yy && (xx + yy + 2 * z.x + 1) > 0.0625)) {
            for (int i = 0; i < max_iters; i++) {
                if (xx + yy > bailout_radius) {
                    double lo = 0.5 * log(float(xx + yy));
                    dvec2 u = z * der1 * ((1 + lo) * cconj(der1 * der1) - lo * cconj(z * der2));
                    u /= length(u);
                    double t = dot(u, v) + h2;
                    t /= h2 + 1.0;
                    if (t < 0) t = 0.2;

                    if (continuous_coloring == 1) {
                        fragColor = vec4(i + 2 - log2(log2(float(length(z)))) / log2(degree), i, t, 0.f);
                    } else {
                        fragColor = vec4(i, i, t, 0.f);
                    }
                    return;
                }
                dvec2 new_z = advance(z, c, xx, yy);
                dvec2 new_der1 = der1 * 2.0 * z + 1.0;
                dvec2 new_der2 = 2 * (der2 * z + der1 * der1);
                z = new_z;
                der1 = new_der1;
                der2 = new_der2;
                xx = z.x * z.x;
                yy = z.y * z.y;
            }
        }
        fragColor = vec4(-1.f, -1.f, 0.f, 0.f);
    }
    if (protocol == 1) {
        vec4 data = texture(mandelbrotTex, vec2(gl_FragCoord.x / screenSize.x, gl_FragCoord.y / screenSize.y));
        if (blur == 1) {
            fragColor = vec4(color(data.x * iter_multiplier), 1.f);
            return;
        }
        vec3 blurredColor = vec3(0.0);
        for (int i = -1; i <= 1; i++) {
            for (int j = -1; j <= 1; j++) {
                vec3 s = color(texture(mandelbrotTex, vec2((gl_FragCoord.x + float(i)) / screenSize.x, (gl_FragCoord.y + float(j)) / screenSize.y)).x * iter_multiplier);
                blurredColor += s * weight[i + 1][j + 1];
            }
        }
        fragColor = vec4(blurredColor, 1.f);
    }
    if (protocol == 0) {
        fragColor = texture(postprocTex, vec2(gl_FragCoord.x / screenSize.x, gl_FragCoord.y / screenSize.y));
    }
}
)glsl";

// TODO: https://blog.cyclemap.link/2011-06-09-glsl-part2-emu/ implement nth power of complex number, use quad-single precision

namespace ImGui {
    ImFont* font;

    static bool ToggleButton(const char* name, bool* v) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        float height = ImGui::GetFrameHeight();
        float width = height * 1.8f;
        float radius = height * 0.50f;
        float rounding = 0.4f;

        ImGui::InvisibleButton(name, ImVec2(width, height));
        if (ImGui::IsItemClicked()) {
            *v ^= 1;
            return true;
        }
        ImGuiContext& gg = *GImGui;
        float ANIM_SPEED = 0.055f;
        if (gg.LastActiveId == gg.CurrentWindow->GetID(name))
            float t_anim = ImSaturate(gg.LastActiveIdTimer / ANIM_SPEED);
        if (ImGui::IsItemHovered()) draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height),
            ImGui::GetColorU32(ImVec4(0.2196f, 0.2196f, 0.2196f, 1.0f)), height * rounding);
        else draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height),
            ImGui::GetColorU32(*v ? ImVec4(0.2196f, 0.2196f, 0.2196f, 1.0f) : ImVec4(0.08f, 0.08f, 0.08f, 1.0f)), height * rounding);
        ImVec2 center = ImVec2(radius + (*v ? 1 : 0) * (width - radius * 2.0f), radius);
        draw_list->AddRectFilled(ImVec2((p.x + center.x) - 9.0f, p.y + 1.5f),
            ImVec2((p.x + (width / 2) + center.x) - 9.0f, p.y + height - 1.5f), IM_COL32(255, 255, 255, 255), height * rounding);
        ImGui::SameLine(p.x + 22.f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.f);
        ImGui::Text(name);
        return false;
    }

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

struct Config {
    glm::dvec2 offset = { -0.4, 0 };
    glm::ivec2 screenSize = { 840, 540 };
    double zoom = 5.0;
    float  spectrum_offset = 850.f;
    float  iter_multiplier = 18.f;
    float  bailout_radius = 10.f;
    float  iter_co = 1.045f;
    int    continuous_coloring = 1;
    glm::fvec3 set_color = { 0.f, 0.f, 0.f };
    int    degree = 2;
    bool   ssaa = true;
    int    ssaa_factor = 2;
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

    glm::ivec2 screenPos;
    bool   fullscreen = false;
    Config config;
    ZoomSequenceConfig zsc;
    int    julia_size = 210;
    double julia_zoom = 3;
    double fps_update_interval = 0.03;

    glm::dvec2 oldPos = { 0, 0 };
    glm::dvec2 lastPresses = { -doubleClick_interval, 0 };
    bool   dragging = false;
    bool   rightClickHold = false;

    bool   rendering = false;
    bool   paused = false;
    float  progress = 0.f;

    GLuint shaderProgram = 0;

    GLuint mandelbrotFrameBuffer = NULL;
    GLuint postprocFrameBuffer = NULL;
    GLuint juliaFrameBuffer = NULL;

    GLuint mandelbrotTexBuffer = NULL;
    GLuint postprocTexBuffer = NULL;
    GLuint juliaTexBuffer = NULL;

    GLuint paletteBuffer = NULL;

    int32_t stateID = 10;
    ImGradientHDRState state;
    ImGradientHDRTemporaryState tempState;

    int protocol = MV_COMPUTE;
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
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

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

        glGenFramebuffers(1, &juliaFrameBuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, juliaFrameBuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, juliaTexBuffer, 0);

        unsigned int vertexShader;
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
        glShaderSource(fragmentShader, 1, &doublePrecFragmentShader, NULL);
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
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

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

        glUniform2d(glGetUniformLocation(shaderProgram, "offset"), config.offset.x, config.offset.y);
        glUniform1f(glGetUniformLocation(shaderProgram, "iter_multiplier"), config.iter_multiplier);
        glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), config.zoom);
        glUniform1i(glGetUniformLocation(shaderProgram, "max_iters"),
            max_iters(config.zoom, zoom_co, config.iter_co));
        glUniform1f(glGetUniformLocation(shaderProgram, "spectrum_offset"), config.spectrum_offset);
        glUniform1d(glGetUniformLocation(shaderProgram, "bailout_radius"), pow(config.bailout_radius, 2));
        glUniform1i(glGetUniformLocation(shaderProgram, "continuous_coloring"), config.continuous_coloring);
        glUniform3f(glGetUniformLocation(shaderProgram, "set_color"), config.set_color.x, config.set_color.y, config.set_color.z);
        glUniform1i(glGetUniformLocation(shaderProgram, "degree"), config.degree);
        glUniform1d(glGetUniformLocation(shaderProgram, "julia_zoom"), julia_zoom);
        glUniform1i(glGetUniformLocation(shaderProgram, "julia_maxiters"),
            max_iters(julia_zoom, zoom_co, config.iter_co, 3.0));
        glUniform1i(glGetUniformLocation(shaderProgram, "blur"), config.ssaa_factor);

        on_windowResize(window, config.screenSize.x, config.screenSize.y);

        for (const glm::vec4& c : paletteData) {
            state.AddColorMarker(c.w, { c.r, c.g, c.b }, 1.0f);
        }
    }

    static glm::dvec2 pixel_to_complex(glm::dvec2 pixelCoord, glm::ivec2 screenSize, double zoom, glm::dvec2 offset) {
        return ((glm::dvec2(pixelCoord.x / screenSize.x, (screenSize.y - pixelCoord.y) / screenSize.y)) - glm::dvec2(0.5, 0.5)) *
            glm::dvec2(zoom, (screenSize.y * zoom) / screenSize.x) + offset;
    }
    static glm::dvec2 pixel_to_complex(MV2* app, glm::dvec2 pixelCoord) {
        glm::ivec2 ss = (app->fullscreen ? monitorSize : app->config.screenSize);
        return ((glm::dvec2(pixelCoord.x / ss.x, (ss.y - pixelCoord.y) / ss.y)) - glm::dvec2(0.5, 0.5)) *
            glm::dvec2(app->config.zoom, (ss.y * app->config.zoom) / ss.x) + app->config.offset;
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
        app->protocol = MV_COMPUTE;
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
                app->protocol = MV_COMPUTE;
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
            app->config.offset.y -= ((app->oldPos.y - y) * ((app->config.zoom * ss.y) / ss.x)) / ss.y;
            glUniform2d(glGetUniformLocation(app->shaderProgram, "offset"), app->config.offset.x, app->config.offset.y);
            app->oldPos = { x, y };
            app->protocol = MV_COMPUTE;
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
        else {
            app->config.zoom *= pow(zoom_co, y * 1.5);
            glUniform1d(glGetUniformLocation(app->shaderProgram, "zoom"), app->config.zoom);
            glUniform1i(glGetUniformLocation(app->shaderProgram, "max_iters"),
                max_iters(app->config.zoom, zoom_co, app->config.iter_co));
            app->protocol = MV_COMPUTE;
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

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6);

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
                    char const* lFilterPatterns[1] = { "*.png" };
                    auto t = std::time(nullptr);
                    auto tm = *std::localtime(&t);
                    std::ostringstream oss;
                    oss << std::put_time(&tm, "MV2 %d-%m-%Y %H-%M-%S");
                    char const* path = tinyfd_saveFileDialog("Save screenshot", oss.str().c_str(), 1, lFilterPatterns, "PNG (*.png)");
                    if (glfwGetWindowMonitor(window) != nullptr) glfwRestoreWindow(window);
                    glfwShowWindow(window);
                    if (path) {
                        int factor = (config.ssaa ? config.ssaa_factor : 1);
                        int w, h;
                        if (fullscreen) {
                            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                            w = mode->width * factor, h = mode->height * factor;
                        }
                        else {
                            w = config.screenSize.x * factor, h = config.screenSize.y * factor;
                        }
                        unsigned char* buffer = new unsigned char[4 * w * h];
                        glActiveTexture(GL_TEXTURE1);
                        glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
                        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
                        stbi_flip_vertically_on_write(true);
                        stbi_write_png(path, w, h, 4, buffer, 4 * w);
                        delete[] buffer;
                    }
                }
                ImGui::SameLine();
                if (ImGui::BeginPopupModal("Zoom sequence creator", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    zsc.tcfg = config;
                    if (ImGui::Button("Browse...")) {
                        char const* lFilterPatterns[1] = { "*.mp4" };
                        auto t = std::time(nullptr);
                        auto tm = *std::localtime(&t);
                        std::ostringstream oss;
                        oss << std::put_time(&tm, "MV2 %d-%m-%Y %H-%M-%S");
                        char const* buf = tinyfd_saveFileDialog("Save sequence", oss.str().c_str(), 1, lFilterPatterns, "Video Files (*.mp4)");
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
                        { 426,  240  },
                        { 640,  480  },
                        { 1280, 720  },
                        { 1920, 1080 },
                        { 2560, 1440 },
                        { 3840, 2160 }
                    };
                    static int res = 3;
                    auto vec_to_str = [commonres](int i) {
                        return std::format("{}x{}", commonres.at(i).x, commonres.at(i).y);
                    };
                    std::string preview = vec_to_str(res);

                    if (ImGui::BeginCombo("Resolution", preview.c_str())) {
                        for (int i = 0; i < commonres.size(); i++) {
                            const bool is_selected = (res == i);
                            if (ImGui::Selectable(vec_to_str(i).c_str(), is_selected)) {
                                zsc.tcfg.screenSize = commonres.at(i);
                                res = i;
                            } 
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();

                    if (!rendering && ImGui::Button("Render", ImVec2(100, 0))) {
                        rendering = true;
                        progress = 0;
                    }
                    else if (rendering && ImGui::Button(paused ? "Resume" : "Pause", ImVec2(100, 0))) {
                        paused ^= 1;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                        rendering = false;
                        progress = 0;
                        ImGui::CloseCurrentPopup();
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
                    protocol = MV_COMPUTE;
                }
                ImGui::Text("Zoom"); ImGui::SetNextItemWidth(80); ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
                if (ImGui::InputDouble("##zoom", &config.zoom, 0.0, 0.0, "%.2e")) {
                    glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), config.zoom);
                    protocol = MV_COMPUTE;
                }

                ImGui::BeginGroup();
                ImGui::SeparatorText("Computation");
                if (ImGui::SliderFloat("Iteration coeff.", &config.iter_co, 1.01, 1.1)) {
                    glUniform1i(glGetUniformLocation(shaderProgram, "max_iters"),
                        max_iters(config.zoom, zoom_co, config.iter_co));
                    protocol = MV_COMPUTE;
                }
                if (ImGui::SliderFloat("Bailout radius", &config.bailout_radius, 2.0, 25.0)) {
                    glUniform1d(glGetUniformLocation(shaderProgram, "bailout_radius"), pow(config.bailout_radius, 2));
                    protocol = MV_COMPUTE;
                }
                if (ImGui::SliderInt("Order", &config.degree, 2, 7)) {
                    glUniform1i(glGetUniformLocation(shaderProgram, "degree"), config.degree);
                    protocol = MV_COMPUTE;
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

                    protocol = MV_COMPUTE;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Continuous Coloring", reinterpret_cast<bool*>(&config.continuous_coloring))) {
                    glUniform1i(glGetUniformLocation(shaderProgram, "continuous_coloring"), config.continuous_coloring);
                    protocol = MV_COMPUTE;
                }
                ImGui::SeparatorText("Coloring");
                if (ImGui::ColorEdit3("Set color", &config.set_color.x)) {
                    glUniform3f(glGetUniformLocation(shaderProgram, "set_color"), config.set_color.r, config.set_color.g, config.set_color.b);
                    protocol = MV_POSTPROC;
                }
                if (ImGui::SliderFloat("Multiplier", &config.iter_multiplier, 1, 128, "x%.4g", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat)) {
                    glUniform1f(glGetUniformLocation(shaderProgram, "iter_multiplier"), config.iter_multiplier);
                    protocol = MV_POSTPROC;
                }
                if (ImGui::SliderFloat("Offset", &config.spectrum_offset, 0, span)) {
                    glUniform1f(glGetUniformLocation(shaderProgram, "spectrum_offset"), config.spectrum_offset);
                    protocol = MV_POSTPROC;
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

                int size_diff = paletteData.size() - state.Colors.size();
                update = false;
                for (int i = 0; i < std::min(state.Colors.size(), paletteData.size()) && !update; i++) {
                    for (int j = 0; j < 4; j++) {
                        float v1 = (j < 3 ? state.Colors[i].Color[j] : state.Colors[i].Position);
                        float v2 = paletteData[i][j];
                        if (update |= v1 != v2) {
                            paletteData[i][j] = v1;
                        }
                    }
                }
                if (size_diff > 0) {
                    for (int i = 0; i < size_diff; i++) {
                        paletteData.pop_back();
                    }
                }
                else if (size_diff < 0) {
                    for (int i = paletteData.size(); i < state.Colors.size(); i++) {
                        glm::vec4 v{};
                        v.r = state.Colors[i].Color[0];
                        v.g = state.Colors[i].Color[1];
                        v.b = state.Colors[i].Color[2];
                        v.w = state.Colors[i].Position;
                        paletteData.push_back(v);
                    }
                }
                ImGui::EndGroup();
                if (ImGui::Button("Save", ImVec2(90.f, 0.f))) {

                }
                ImGui::SameLine();
                if (ImGui::Button("Load", ImVec2(90.f, 0.f))) {

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
                    state.ColorCount = paletteData.size();
                    for (int i = 0; i < state.ColorCount; i++) {
                        state.Colors[i].Color = *reinterpret_cast<std::array<float, 3>*>(glm::value_ptr(paletteData[i]));
                        state.Colors[i].Position = paletteData[i][3];
                    }
                    update = true;
                }
                if (update) {
                    glBindBuffer(GL_SHADER_STORAGE_BUFFER, paletteBuffer);
                    memcpy(glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_WRITE_ONLY),
                        paletteData.data(), paletteData.size() * sizeof(glm::vec4));
                    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
                    protocol = MV_POSTPROC;
                }
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(80, 80, 80, 255));
                ImGui::Text("(c) 2017-2024 Yilmaz Alpaslan");
                ImGui::PopStyleColor();
                ImGui::EndGroup();
            }
            ImGui::End();

            int factor = (config.ssaa ? config.ssaa_factor : 1);

            if (!rendering && rightClickHold) {
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
                for (i = 1; i < max_iters(config.zoom, zoom_co, config.iter_co); i++) {
                    double xx = z.x * z.x;
                    double yy = z.y * z.y;
                    if (xx + yy > pow(config.bailout_radius, 2))
                        goto display;
                    switch (config.degree) {
                    case 2:
                        z = glm::dvec2(xx - yy, 2.0f * z.x * z.y) + c;
                        break;
                    case 3:
                        z = glm::dvec2(xx * z.x - 3 * z.x * yy, 3 * xx * z.y - yy * z.y) + c;
                        break;
                    case 4:
                        z = glm::dvec2(xx * xx + yy * yy - 6 * xx * yy,
                            4 * xx * z.x * z.y - 4 * z.x * yy * z.y) + c;
                        break;
                    case 5:
                        z = glm::dvec2(xx * xx * z.x + 5 * z.x * yy * yy - 10 * xx * z.x * yy,
                            5 * xx * xx * z.y + yy * yy * z.y - 10 * xx * yy * z.y) + c;
                        break;
                    case 6:
                        z = glm::dvec2(xx * xx * xx - 15 * xx * xx * yy + 15 * xx * yy * yy - yy * yy * yy,
                            6 * xx * xx * z.x * z.y - 20 * xx * z.x * yy * z.y + 6 * z.x * yy * yy * z.y) + c;
                        break;
                    case 7:
                        z = glm::dvec2(xx * xx * xx * z.x - 21 * xx * xx * z.x * yy + 35 * xx * z.x * yy * yy - 7 * z.x * yy * yy * yy,
                            7 * xx * xx * xx * z.y - 35 * xx * xx * yy * z.y + 21 * xx * yy * yy * z.y - yy * yy * yy * z.y) + c;
                        break;
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
                glUniform1i(glGetUniformLocation(shaderProgram, "protocol"), 3);
                glUniform2d(glGetUniformLocation(shaderProgram, "mouseCoord"), c.x, c.y);
                glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"),
                    julia_size * factor, julia_size * factor);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                ImGui::SeparatorText("Julia Set");
                ImGui::Image((void*)(intptr_t)juliaTexBuffer, ImVec2(julia_size, julia_size));
                ImGui::End();
            }
            glViewport(0, 0, ss.x * factor, ss.y * factor);
            glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), ss.x* factor, ss.y* factor);
            switch (protocol) {
            case MV_COMPUTE:
                glBindTexture(GL_TEXTURE_2D, mandelbrotTexBuffer);
                glBindFramebuffer(GL_FRAMEBUFFER, mandelbrotFrameBuffer);
                glUniform1i(glGetUniformLocation(shaderProgram, "protocol"), MV_COMPUTE);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                [[fallthrough]];
            case MV_POSTPROC:
                glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
                glBindFramebuffer(GL_FRAMEBUFFER, postprocFrameBuffer);
                glUniform1i(glGetUniformLocation(shaderProgram, "protocol"), MV_POSTPROC);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                [[fallthrough]];
            case MV_RENDER:
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, ss.x, ss.y);
                glUniform1i(glGetUniformLocation(shaderProgram, "protocol"), MV_RENDER);
                glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), ss.x, ss.y);
                ImGui::Render();
                glDrawArrays(GL_TRIANGLES, 0, 6);
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                glfwSwapBuffers(window);
                protocol = MV_RENDER;
            }
            if (rendering) {
                
            }
        } while (!glfwWindowShouldClose(window));
    }
};

int main() {
    MV2 app;
    app.mainloop();
    
    return 0;
}