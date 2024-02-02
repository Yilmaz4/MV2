#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

#include <ImGradientHDR.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>


#include <iostream>
#include <vector>

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
uniform int    spectrum_offset;
uniform int    iter_multiplier;
uniform double bailout_radius;
uniform int    continuous_coloring;
uniform dvec3  set_color;
uniform int    degree;

uniform sampler2D tex;
uniform int use_tex;

uniform dvec2  mouseCoord;
uniform int    julia;
uniform double julia_zoom;
uniform int    julia_maxiters;

layout(std430, binding = 1) readonly buffer spectrum
{
    vec4 spec[];
};
uniform int    span;

vec4 blur13(sampler2D image, vec2 uv, vec2 resolution, vec2 direction) {
    vec4 color = vec4(0.0);
    vec2 off1 = vec2(1.411764705882353) * direction;
    vec2 off2 = vec2(3.2941176470588234) * direction;
    vec2 off3 = vec2(5.176470588235294) * direction;
    color += texture2D(image, uv) * 0.1964825501511404;
    color += texture2D(image, uv + (off1 / resolution)) * 0.2969069646728344;
    color += texture2D(image, uv - (off1 / resolution)) * 0.2969069646728344;
    color += texture2D(image, uv + (off2 / resolution)) * 0.09447039785044732;
    color += texture2D(image, uv - (off2 / resolution)) * 0.09447039785044732;
    color += texture2D(image, uv + (off3 / resolution)) * 0.010381362401148057;
    color += texture2D(image, uv - (off3 / resolution)) * 0.010381362401148057;
    return color;
}

vec3 color(float i) {
    i = mod(i, span) / span;
    for (int v = 0; v < spec.length(); v++) {
        if (spec[v].w >= i) {
            vec4 v1 = (v > 0 ? spec[v - 1] : spec[spec.length() - 1]);
            vec4 v2 = spec[v];
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
    if (julia == 1) {
        dvec2 c = (dvec2(gl_FragCoord.x / screenSize.x, (screenSize.y - gl_FragCoord.y) / screenSize.y) - dvec2(0.5, 0.5)) * dvec2(julia_zoom, julia_zoom);
        dvec2 z = c;
        
        for (int i = 0; i < julia_maxiters; i++) {
            double xx = z.x * z.x;
            double yy = z.y * z.y;
            if (xx + yy >= bailout_radius) {
                float mu = iter_multiplier * (i + 1 - log2(log2(float(length(z)))) / log2(degree));
                float mv = iter_multiplier * i;
                vec3 co = color(continuous_coloring == 0 ? mv : mu);
                fragColor = vec4(co, 1);
                return;
            }
            z = advance(z, mouseCoord, xx, yy);
        }
        fragColor = vec4(set_color, 1.0);
    }
    else if (use_tex == 1) {
        fragColor = texture(tex, vec2(gl_FragCoord.x / screenSize.x, gl_FragCoord.y / screenSize.y));
    }
    else {
        dvec2 c = (dvec2(gl_FragCoord.x / screenSize.x, (screenSize.y - gl_FragCoord.y) / screenSize.y) - dvec2(0.5, 0.5)) * dvec2(zoom, (screenSize.y * zoom) / screenSize.x) + offset;
        dvec2 z = c;

        double xx = z.x * z.x;
        double yy = z.y * z.y;

        double p = xx - z.x / 2.0 + 0.0625 + yy;
        if (degree != 2 || degree == 2 && (4.0 * p * (p + (z.x - 0.25)) > yy && (xx + yy + 2 * z.x + 1) > 0.0625)) {
            for (int i = 0; i < max_iters; i++) {
                if (xx + yy >= bailout_radius) {
                    float m1 = iter_multiplier * (i + 1 - log2(log2(float(length(z)))) / log2(degree));
                    float m2 = iter_multiplier * i;
                    fragColor = vec4(color(continuous_coloring != 0 ? m1 : m2), 0);
                    return;
                }
                z = advance(z, c, xx, yy);
                xx = z.x * z.x;
                yy = z.y * z.y;
            }
        }
        fragColor = vec4(set_color, 1.0);
    }
}
)glsl";

// TODO: https://blog.cyclemap.link/2011-06-09-glsl-part2-emu/ implement nth power of complex number, use quad-single precision
GLuint shaderProgram = 0;

GLuint mandelbrotFrameBuffer;
GLuint mandelbrotTexBuffer;

GLuint juliaFrameBuffer;
GLuint juliaTexBuffer;

GLuint spectrumBuffer;

int pending_flag = 1;

namespace spectrum {
    std::vector<glm::vec4> data = {
        {0.0f, 0.0274f, 0.3921f, 0.f},
        {0.1254f, 0.4196f, 0.7960f, 0.16f},
        {0.9294f, 1.f, 1.f, 0.42f},
        {1.f, 0.6666f, 0.f, 0.6425f},
        {0.f, 0.0078f, 0.f, 0.8575f},
        {0.0f, 0.0274f, 0.3921f, 1.000000f}
    };

    int span = 1e+3;
    constexpr int max_colors = 8;

    void bind_ssbo(void) {
        glGenBuffers(1, &spectrumBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, spectrumBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, max_colors * sizeof(glm::fvec4), data.data(), GL_DYNAMIC_COPY);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, spectrumBuffer);
        GLuint block_index = glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "spectrum");
        GLuint ssbo_binding_point_index = 2;
        glShaderStorageBlockBinding(shaderProgram, block_index, ssbo_binding_point_index);
        glUniform1i(glGetUniformLocation(shaderProgram, "span"), spectrum::span);
    }
}

namespace consts {
    constexpr double zoom_co = 0.85;

    constexpr double doubleClick_interval = 0.2; // seconds
}

namespace vars {
    glm::dvec2 offset = { -0.4, 0 };
    glm::ivec2 screenSize = { 760, 540 };
    double zoom = 5.0;
    int    spectrum_offset = 0;
    int    iter_multiplier = 10;
    float  bailout_radius = 10.0;
    float  iter_co = 1.060;
    int    continuous_coloring = 1;
    glm::dvec3 set_color = { 0., 0., 0. };
    int    degree = 2;

    bool   ssaa = true;
    int    ssaa_factor = 2;

    int    julia_size = 210;
    double julia_zoom = 3;

    double fps_update_interval = 0.1;
}

namespace utils {
    static glm::dvec2 pixel_to_complex(glm::dvec2 pixelCoord, glm::ivec2 screenSize, double zoom, glm::dvec2 offset) {
        return ((glm::dvec2(pixelCoord.x / screenSize.x, pixelCoord.y / screenSize.y)) - glm::dvec2(0.5, 0.5)) *
            glm::dvec2(zoom, (screenSize.y * zoom) / screenSize.x) + offset;
    }
    static glm::dvec2 pixel_to_complex(glm::dvec2 pixelCoord) {
        return ((glm::dvec2(pixelCoord.x / vars::screenSize.x, pixelCoord.y / vars::screenSize.y)) - glm::dvec2(0.5, 0.5)) *
            glm::dvec2(vars::zoom, (vars::screenSize.y * vars::zoom) / vars::screenSize.x) + vars::offset;
    }
    static int max_iters(double zoom, double zoom_co, double iter_co, double initial_zoom = 5.0) {
        return 100 * pow(iter_co, log2(zoom / initial_zoom) / log2(zoom_co));
    }
}

namespace events {
    glm::dvec2 oldPos = { 0, 0 };
    glm::dvec2 lastPresses = { -consts::doubleClick_interval, 0 };
    bool dragging = false;
    bool rightClickHold = false;

    static void on_windowResize(GLFWwindow* window, int width, int height) {
        glViewport(0, 0, width, height);

        vars::screenSize = { width, height };
        if (shaderProgram)
            glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), width, height);
        pending_flag = 2;
        glBindFramebuffer(GL_FRAMEBUFFER, mandelbrotFrameBuffer);
        glBindTexture(GL_TEXTURE_2D, mandelbrotTexBuffer);
        int factor = (vars::ssaa ? vars::ssaa_factor : 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, vars::screenSize.x * factor, vars::screenSize.y * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }

    static void on_mouseButton(GLFWwindow* window, int button, int action, int mod) {
        if (ImGui::GetIO().WantCaptureMouse) return;
        switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT:
            if (action == GLFW_PRESS) {
                dragging = true;
                // shift vector to the left and add new timestamp
                lastPresses.x = lastPresses.y;
                lastPresses.y = glfwGetTime();
                glfwGetCursorPos(window, &events::oldPos.x, &events::oldPos.y);
            }
            else if (lastPresses.y - lastPresses.x < consts::doubleClick_interval) { // button released, check if interval between last two presses is less than the max interval
                glfwGetCursorPos(window, &events::oldPos.x, &events::oldPos.y);
                glm::dvec2 pos = utils::pixel_to_complex(events::oldPos);
                glm::dvec2 center = utils::pixel_to_complex(static_cast<glm::dvec2>(vars::screenSize) / 2.0);
                vars::offset += pos - center;
                glUniform2d(glGetUniformLocation(shaderProgram, "offset"), vars::offset.x, vars::offset.y);
                dragging = false;
                pending_flag = 2;
            }
            else {
                dragging = false;
            }
            break;
        case GLFW_MOUSE_BUTTON_RIGHT:
            switch (action) {
            case GLFW_PRESS:
                rightClickHold = true;
                vars::julia_zoom = 3.0;
                glUniform1d(glGetUniformLocation(shaderProgram, "julia_zoom"), vars::julia_zoom);
                glUniform1i(glGetUniformLocation(shaderProgram, "julia_maxiters"), utils::max_iters(vars::julia_zoom, consts::zoom_co, vars::iter_co, 3.0));
                break;
            case GLFW_RELEASE:
                rightClickHold = false;
            }
        }
    }

    static void on_cursorMove(GLFWwindow* window, double x, double y) {
        if (ImGui::GetIO().WantCaptureMouse)
            return;
        if (dragging) {
            lastPresses = { -consts::doubleClick_interval, 0 }; // reset to prevent accidental centering while rapidly dragging
            vars::offset.x -= ((x - oldPos.x) * vars::zoom) / vars::screenSize.x;
            vars::offset.y -= ((y - oldPos.y) * ((vars::zoom * vars::screenSize.y) / vars::screenSize.x)) / vars::screenSize.y;
            glUniform2d(glGetUniformLocation(shaderProgram, "offset"), vars::offset.x, vars::offset.y);
            oldPos = { x, y };
            pending_flag = 2;
        }
    }

    static void on_mouseScroll(GLFWwindow* window, double x, double y) { // y is usually either 1 or -1 depending on direction, x is always 0
        if (rightClickHold) {
            vars::julia_zoom *= pow(consts::zoom_co, y);
            glUniform1d(glGetUniformLocation(shaderProgram, "julia_zoom"), vars::julia_zoom);
            glUniform1i(glGetUniformLocation(shaderProgram, "julia_maxiters"), utils::max_iters(vars::julia_zoom, consts::zoom_co, vars::iter_co, 3.0));
        }
        else {
            vars::zoom *= pow(consts::zoom_co, y);
            glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), vars::zoom);
            glUniform1i(glGetUniformLocation(shaderProgram, "max_iters"), utils::max_iters(vars::zoom, consts::zoom_co, vars::iter_co));
            pending_flag = 2;
        }
    }
}

static void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
}

static void HelpMarker(const char* desc) { // code from imgui demo
    ImGui::TextDisabled("[?]");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

template <typename T> requires std::integral<T>
void toggleButton(T* v, const char* id, const char* name, PFNGLUNIFORM1IPROC uniform) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float height = ImGui::GetFrameHeight();
    float width = height * 1.8f;
    float radius = height * 0.50f;
    float rounding = 0.4f;

    ImGui::InvisibleButton(id, ImVec2(width, height));
    if (ImGui::IsItemClicked()) {
        *v ^= 1;
        uniform(glGetUniformLocation(shaderProgram, id), *v);
        pending_flag = 2;
    }
    ImGuiContext& gg = *GImGui;
    float ANIM_SPEED = 0.055f;
    if (gg.LastActiveId == gg.CurrentWindow->GetID(id))
        float t_anim = ImSaturate(gg.LastActiveIdTimer / ANIM_SPEED);
    if (ImGui::IsItemHovered())
        draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ImGui::GetColorU32(ImVec4(0.2196f, 0.2196f, 0.2196f, 1.0f)), height * rounding);
    else
        draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ImGui::GetColorU32(vars::continuous_coloring ? ImVec4(0.2196f, 0.2196f, 0.2196f, 1.0f) : ImVec4(0.08f, 0.08f, 0.08f, 1.0f)), height * rounding);
    ImVec2 center = ImVec2(radius + (vars::continuous_coloring ? 1 : 0) * (width - radius * 2.0f), radius);
    draw_list->AddRectFilled(ImVec2((p.x + center.x) - 9.0f, p.y + 1.5f),
        ImVec2((p.x + (width / 2) + center.x) - 9.0f, p.y + height - 1.5f), IM_COL32(255, 255, 255, 255), height * rounding);
    ImGui::SameLine(35.f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.f);
    ImGui::Text(name);
}

int main() {
    glfwInit();

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());

    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

    GLFWwindow* window = glfwCreateWindow(vars::screenSize.x, vars::screenSize.y, "Mandelbrot Voyage II", NULL, NULL);
    if (window == nullptr) {
        std::cout << "Failed to create OpenGL window" << std::endl;
        return -1;
    }

    glfwMakeContextCurrent(window);

    glfwSetFramebufferSizeCallback(window, events::on_windowResize);
    glfwSetCursorPosCallback(window, events::on_cursorMove);
    glfwSetMouseButtonCallback(window, events::on_mouseButton);
    glfwSetScrollCallback(window, events::on_mouseScroll);

    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.Fonts->AddFontDefault();
    ImFont* font_title = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 12.f);
    IM_ASSERT(font_title != NULL);
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking

    ImGui::StyleColorsDark();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.19f, 0.19f, 0.19f, 0.92f);
    colors[ImGuiCol_Border] = ImVec4(0.19f, 0.19f, 0.19f, 0.29f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    colors[ImGuiCol_Button] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
    colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.20f, 0.20f, 0.36f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_DockingPreview] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_NavHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.35f);

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(8.00f, 8.00f);
    style.FramePadding = ImVec2(5.00f, 2.00f);
    style.CellPadding = ImVec2(6.00f, 6.00f);
    style.ItemSpacing = ImVec2(6.00f, 6.00f);
    style.ItemInnerSpacing = ImVec2(6.00f, 6.00f);
    style.TouchExtraPadding = ImVec2(0.00f, 0.00f);
    style.IndentSpacing = 25;
    style.ScrollbarSize = 15;
    style.GrabMinSize = 10;
    style.WindowBorderSize = 1;
    style.ChildBorderSize = 1;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 1;
    style.TabBorderSize = 1;
    style.WindowRounding = 7;
    style.ChildRounding = 4;
    style.FrameRounding = 3;
    style.PopupRounding = 4;
    style.ScrollbarRounding = 9;
    style.GrabRounding = 3;
    style.LogSliderDeadzone = 4;
    style.TabRounding = 4;
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 6.f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifdef __EMSCRIPTEN__
    ImGui_ImplGlfw_InstallEmscriptenCanvasResizeCallback("#canvas");
#endif
    ImGui_ImplOpenGL3_Init("#version 400");

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to create OpenGL window" << std::endl;
        return -1;
    }

    int factor = (vars::ssaa ? vars::ssaa_factor : 1);

    glGenFramebuffers(1, &mandelbrotFrameBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, mandelbrotFrameBuffer);
    glGenTextures(1, &mandelbrotTexBuffer);
    glBindTexture(GL_TEXTURE_2D, mandelbrotTexBuffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, vars::screenSize.x * factor, vars::screenSize.y * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mandelbrotTexBuffer, 0);


    glGenFramebuffers(1, &juliaFrameBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, juliaFrameBuffer);
    glGenTextures(1, &juliaTexBuffer);
    glBindTexture(GL_TEXTURE_2D, juliaTexBuffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, vars::julia_size * factor, vars::julia_size * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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
        return -1;
    }

    unsigned int fragmentShader;
    fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &doublePrecFragmentShader, NULL);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cout << infoLog << std::endl;
        return -1;
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

    spectrum::bind_ssbo();

    glUniform2d(glGetUniformLocation(shaderProgram, "offset"), vars::offset.x, vars::offset.y);
    glUniform1i(glGetUniformLocation(shaderProgram, "iter_multiplier"), vars::iter_multiplier);
    glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), vars::zoom);
    glUniform1i(glGetUniformLocation(shaderProgram, "max_iters"), utils::max_iters(vars::zoom, consts::zoom_co, vars::iter_co));
    glUniform1i(glGetUniformLocation(shaderProgram, "spectrum_offset"), vars::spectrum_offset);
    glUniform1d(glGetUniformLocation(shaderProgram, "bailout_radius"), pow(vars::bailout_radius, 2));
    glUniform1i(glGetUniformLocation(shaderProgram, "continuous_coloring"), vars::continuous_coloring);
    glUniform3d(glGetUniformLocation(shaderProgram, "set_color"), vars::set_color.x, vars::set_color.y, vars::set_color.z);
    glUniform1i(glGetUniformLocation(shaderProgram, "degree"), vars::degree);
    glUniform1d(glGetUniformLocation(shaderProgram, "julia_zoom"), vars::julia_zoom);
    glUniform1i(glGetUniformLocation(shaderProgram, "julia_maxiters"), utils::max_iters(vars::julia_zoom, consts::zoom_co, vars::iter_co, 3.0));

    events::on_windowResize(window, vars::screenSize.x, vars::screenSize.y);

    double lastFrame = glfwGetTime();
    double lastUpdate = glfwGetTime();
    double fps = 0;

    int32_t stateID = 10;
    ImGradientHDRState state;
    ImGradientHDRTemporaryState tempState;

    for (const glm::vec4& c : spectrum::data) {
        state.AddColorMarker(c.w, { c.r, c.g, c.b }, 1.0f);
    }

    bool open = false;

    do { // mainloop
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6);

        static float f = 0.0f;
        static int counter = 0;

        ImGui::PushFont(font_title);
        ImGui::SetNextWindowPos({ 10, 10 });
        if (ImGui::Begin("Settings", nullptr, 
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoMove
        )) {
            double currentTime = glfwGetTime();
            if (currentTime - lastUpdate > vars::fps_update_interval) {
                fps = 1 / (currentTime - lastFrame);
                lastUpdate = currentTime;
            }
            ImGui::Text("FPS: %.3g   Frametime: %.3g ms", fps, 1000.0 * (currentTime - lastFrame));
            lastFrame = currentTime;
        
            ImGui::BeginGroup();
            ImGui::SeparatorText("Computation configuration");
            //ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            if (ImGui::SliderFloat("Iteration coefficient", &vars::iter_co, 1.01, 1.1)) {
                glUniform1i(glGetUniformLocation(shaderProgram, "max_iters"), utils::max_iters(vars::zoom, consts::zoom_co, vars::iter_co));
                pending_flag = 2;
            }
            //ImGui::PushItemFlag(ImGuiItemFlags_Disabled, false);
            if (ImGui::SliderFloat("Bailout radius", &vars::bailout_radius, 2.0, 25.0)) {
                glUniform1d(glGetUniformLocation(shaderProgram, "bailout_radius"), pow(vars::bailout_radius, 2));
                pending_flag = 2;
            }
            if (ImGui::SliderInt("Degree", &vars::degree, 2, 7)) {
                glUniform1i(glGetUniformLocation(shaderProgram, "degree"), vars::degree);
                pending_flag = 2;
            }
            ImGui::SeparatorText("Rendering settings");
            if (ImGui::SliderInt("Iteration Multiplier", &vars::iter_multiplier, 1, 128, "x%d", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat)) {
                glUniform1i(glGetUniformLocation(shaderProgram, "iter_multiplier"), vars::iter_multiplier);
                pending_flag = 2;
            }
            if (ImGui::SliderInt("Spectrum Offset", &vars::spectrum_offset, 0, 256 * 7)) {
                glUniform1i(glGetUniformLocation(shaderProgram, "spectrum_offset"), vars::spectrum_offset);
                pending_flag = 2;
            }
            if (ImGui::Checkbox("SSAA (super sampling)", &vars::ssaa)) {
                int factor = ((vars::ssaa) ? vars::ssaa_factor : 1);
                glBindFramebuffer(GL_FRAMEBUFFER, mandelbrotFrameBuffer);
                glBindTexture(GL_TEXTURE_2D, mandelbrotTexBuffer);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, vars::screenSize.x * factor, vars::screenSize.y * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

                glBindFramebuffer(GL_FRAMEBUFFER, juliaFrameBuffer);
                glBindTexture(GL_TEXTURE_2D, juliaTexBuffer);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, vars::julia_size * factor, vars::julia_size * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

                pending_flag = 2;
            }
            if (vars::ssaa) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, false);
            else ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(30);
            if (ImGui::DragInt("Factor", &vars::ssaa_factor, 0.2f, 2, 8, "%dX")) {
                glBindFramebuffer(GL_FRAMEBUFFER, mandelbrotFrameBuffer);
                glBindTexture(GL_TEXTURE_2D, mandelbrotTexBuffer);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, vars::screenSize.x * vars::ssaa_factor, vars::screenSize.y * vars::ssaa_factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

                glBindFramebuffer(GL_FRAMEBUFFER, juliaFrameBuffer);
                glBindTexture(GL_TEXTURE_2D, juliaTexBuffer);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, vars::julia_size * vars::ssaa_factor, vars::julia_size * vars::ssaa_factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

                pending_flag = 2;
            }
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, false);
            toggleButton(&vars::continuous_coloring, "continuous_coloring", "Continuous coloring", glUniform1i);

            bool isMarkerShown = true;
            ImGradientHDR(stateID, state, tempState, isMarkerShown);

            if (tempState.selectedMarkerType == ImGradientHDRMarkerType::Color) {
                auto selectedColorMarker = state.GetColorMarker(tempState.selectedIndex);
                if (selectedColorMarker != nullptr) {
                    ImGui::ColorEdit3("", selectedColorMarker->Color.data(), ImGuiColorEditFlags_Float);
                }
            }

            if (tempState.selectedMarkerType != ImGradientHDRMarkerType::Unknown) {
                ImGui::SameLine();
                if (tempState.selectedMarkerType == ImGradientHDRMarkerType::Color && (ImGui::Button("Delete") || glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS)) {
                    state.RemoveColorMarker(tempState.selectedIndex);
                    tempState = ImGradientHDRTemporaryState{};
                }
            }

            int size_diff = spectrum::data.size() - state.Colors.size();
            bool d = 0;
            for (int i = 0; i < std::min(state.Colors.size(), spectrum::data.size()) && !d; i++) {
                for (int j = 0; j < 4; j++) {
                    float v1 = (j < 3 ? state.Colors[i].Color[j] : state.Colors[i].Position);
                    float v2 = spectrum::data[i][j];
                    if (d |= v1 != v2) {
                        spectrum::data[i][j] = v1;
                    }
                }
            }
            if (size_diff > 0) {
                for (int i = 0; i < size_diff; i++) {
                    spectrum::data.pop_back();
                }
            }
            else if (size_diff < 0) {
                for (int i = spectrum::data.size(); i < state.Colors.size(); i++) {
                    glm::vec4 v{};
                    v.r = state.Colors[i].Color[0];
                    v.g = state.Colors[i].Color[1];
                    v.b = state.Colors[i].Color[2];
                    v.w = state.Colors[i].Position;
                    spectrum::data.push_back(v);
                }
            }
            if (d) {
                std::cout << "changed" << std::endl;
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, spectrumBuffer);
                memcpy(glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_WRITE_ONLY), spectrum::data.data(), spectrum::data.size() * sizeof(glm::vec4));
                glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
                pending_flag = 2;
            }
            ImGui::EndGroup();
        }
        ImGui::End();

        int factor = ((vars::ssaa) ? vars::ssaa_factor : 1);

        if (events::rightClickHold) {
            double x, y;
            glfwGetCursorPos(window, &x, &y);
            
            ImGui::Begin("info", nullptr, ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoTitleBar);
            ImVec2 size = ImGui::GetWindowSize();
            ImVec2 pos = { (float)x + 10.0f, (float)y };
            if (size.x > vars::screenSize.x - pos.x - 5)
                pos.x = vars::screenSize.x - size.x - 5;
            if (size.y > vars::screenSize.y - pos.y - 5)
                pos.y = vars::screenSize.y - size.y - 5;
            if (pos.x < 5) pos.x = 5;
            if (pos.y < 5) pos.y = 5;
            ImGui::SetWindowPos(pos);
            glm::dvec2 c = utils::pixel_to_complex({x, y});
            glm::dvec2 z = c;
            int i;
            for (i = 0; i < utils::max_iters(vars::zoom, consts::zoom_co, vars::iter_co); i++) {
                double xx = z.x * z.x;
                double yy = z.y * z.y;
                if (xx + yy > vars::bailout_radius)
                    goto display;
                switch (vars::degree) {
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
            glViewport(0, 0, vars::julia_size * factor, vars::julia_size * factor);
            glBindFramebuffer(GL_FRAMEBUFFER, juliaFrameBuffer);
            glUniform1i(glGetUniformLocation(shaderProgram, "use_tex"), 0);
            glUniform1i(glGetUniformLocation(shaderProgram, "julia"), 1);
            glUniform2d(glGetUniformLocation(shaderProgram, "mouseCoord"), c.x, c.y);
            glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), vars::julia_size * factor, vars::julia_size * factor);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            ImGui::SeparatorText("Julia Set");
            ImGui::Image((void*)(intptr_t)juliaTexBuffer, ImVec2(vars::julia_size, vars::julia_size));
            ImGui::End();
        }

        if (pending_flag) {
            glViewport(0, 0, vars::screenSize.x * factor, vars::screenSize.y * factor);
            glBindTexture(GL_TEXTURE_2D, mandelbrotTexBuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, mandelbrotFrameBuffer);
            glUniform1i(glGetUniformLocation(shaderProgram, "julia"), 0);
            glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), vars::screenSize.x * factor, vars::screenSize.y * factor);
            glUniform1i(glGetUniformLocation(shaderProgram, "use_tex"), 0);
            pending_flag -= 1;
            glDrawArrays(GL_TRIANGLES, 0, 6);
            
            glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), vars::screenSize.x, vars::screenSize.y);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glUniform1i(glGetUniformLocation(shaderProgram, "use_tex"), 1);
            ImGui::Render();
            glDrawArrays(GL_TRIANGLES, 0, 6);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }
        else {
            glViewport(0, 0, vars::screenSize.x, vars::screenSize.y);
            glUniform1i(glGetUniformLocation(shaderProgram, "julia"), 0);
            glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), vars::screenSize.x, vars::screenSize.y);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glUniform1i(glGetUniformLocation(shaderProgram, "use_tex"), 1);
            ImGui::Render();
            glDrawArrays(GL_TRIANGLES, 0, 6);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }
        
    } while (!glfwWindowShouldClose(window));

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}