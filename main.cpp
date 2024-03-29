﻿#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

#include <ImGradientHDR.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#include <tinyfiledialogs/tinyfiledialogs.h>

#include <iostream>
#include <vector>

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
uniform int    span;

double log(double x) {
	double
		Ln2Hi = 6.93147180369123816490e-01LF, /* 3fe62e42 fee00000 */
		Ln2Lo = 1.90821492927058770002e-10LF, /* 3dea39ef 35793c76 */
        L0    = 7.0710678118654752440e-01LF,  /* 1/sqrt(2) */
		L1    = 6.666666666666735130e-01LF,   /* 3FE55555 55555593 */
		L2    = 3.999999999940941908e-01LF,   /* 3FD99999 9997FA04 */
		L3    = 2.857142874366239149e-01LF,   /* 3FD24924 94229359 */
		L4    = 2.222219843214978396e-01LF,   /* 3FCC71C5 1D8E78AF */
		L5    = 1.818357216161805012e-01LF,   /* 3FC74664 96CB03DE */
		L6    = 1.531383769920937332e-01LF,   /* 3FC39A09 D078C69F */
		L7    = 1.479819860511658591e-01LF;   /* 3FC2F112 DF3E5244 */
	if( isinf(x) )
        return 1.0/0.0; /* return +inf */
	if( isnan(x) || x < 0 )
        return -0.0; /* nan */
	if( x == 0 )
        return -1.0/0.0; /* return -inf */
    int ki;
    double f1 = frexp(x, ki);
    
    if (f1 < L0) {
		f1 *= 2.0;
		ki--;
	}
	
	double f = f1 - 1.0;
	double k = double(ki);
	double s = f / (2.0 + f);
	double s2 = s * s;
	double s4 = s2 * s2;
	double t1 = s2 * (L1 + s4 * (L3 + s4 * (L5 + s4 * L7)));
	double t2 = s4 * (L2 + s4 * (L4 + s4 * L6));
	double R = t1 + t2;
	double hfsq = 0.5 * f * f;
    
    return k*Ln2Hi - ((hfsq - (s*(hfsq+R) + k*Ln2Lo)) - f);
}

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

                    fragColor = vec4(i + 2 - log2(log2(float(length(z)))) / log2(degree), i, t, 0.f);
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
GLuint shaderProgram = 0;

GLuint mandelbrotFrameBuffer;
GLuint postprocFrameBuffer;
GLuint juliaFrameBuffer;

GLuint mandelbrotTexBuffer;
GLuint postprocTexBuffer;
GLuint juliaTexBuffer;

GLuint spectrumBuffer;

int protocol = MV_COMPUTE;

namespace spectrum {
    std::vector<glm::vec4> data = {
        {0.0f, 0.0274f, 0.3921f, 0.f},
        {0.1254f, 0.4196f, 0.7960f, 0.16f},
        {0.9294f, 1.f, 1.f, 0.42f},
        {1.f, 0.6666f, 0.f, 0.6425f},
        {0.f, 0.0078f, 0.f, 0.8575f},
        {0.0f, 0.0274f, 0.3921f, 1.f}
    };

    int span = 1000;
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
    glm::ivec2 monitorSize;
}

namespace vars {
    glm::dvec2 offset = { -0.4, 0 };
    glm::ivec2 screenSize = { 840, 540 };
    glm::ivec2 screenPos;
    bool   fullscreen = false;
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

    int    julia_size = 210;
    double julia_zoom = 3;

    double fps_update_interval = 0.1;
}

namespace utils {
    static glm::dvec2 pixel_to_complex(glm::dvec2 pixelCoord, glm::ivec2 screenSize, double zoom, glm::dvec2 offset) {
        return ((glm::dvec2(pixelCoord.x / screenSize.x, (screenSize.y - pixelCoord.y) / screenSize.y)) - glm::dvec2(0.5, 0.5)) *
            glm::dvec2(zoom, (screenSize.y * zoom) / screenSize.x) + offset;
    }
    static glm::dvec2 pixel_to_complex(glm::dvec2 pixelCoord) {
        glm::ivec2 ss = (vars::fullscreen ? consts::monitorSize : vars::screenSize);
        return ((glm::dvec2(pixelCoord.x / ss.x, (ss.y - pixelCoord.y) / ss.y)) - glm::dvec2(0.5, 0.5)) *
            glm::dvec2(vars::zoom, (ss.y * vars::zoom) / ss.x) + vars::offset;
    }
    static int max_iters(double zoom, double zoom_co, double iter_co, double initial_zoom = 5.0) {
        //return sqrt(2 * sqrt(abs(1 - sqrt(5 * 1/zoom)))) * 66.5;
        return 100 * pow(iter_co, log2(zoom / initial_zoom) / log2(zoom_co));
    }

    static void screenshot(const char* filepath, GLFWwindow* window) { // https://lencerf.github.io/post/2019-09-21-save-the-opengl-rendering-to-image-file/
        int factor = (vars::ssaa ? vars::ssaa_factor : 1);
        int w = vars::screenSize.x * factor, h = vars::screenSize.y * factor;
        GLsizei nrChannels = 3;
        GLsizei stride = nrChannels * w;
        stride += (stride % 4) ? (4 - stride % 4) : 0;
        GLsizei bufferSize = stride * h;
        std::vector<char> buffer(bufferSize);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glBindFramebuffer(GL_FRAMEBUFFER, postprocFrameBuffer);
        glReadBuffer(GL_FRONT);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, buffer.data());
        stbi_flip_vertically_on_write(true);
        stbi_write_png(filepath, w, h, nrChannels, buffer.data(), stride);
    }
}

namespace events {
    glm::dvec2 oldPos = { 0, 0 };
    glm::dvec2 lastPresses = { -consts::doubleClick_interval, 0 };
    bool dragging = false;
    bool rightClickHold = false;

    static void on_windowResize(GLFWwindow* window, int width, int height) {
        glViewport(0, 0, width, height);

        if (!vars::fullscreen)
            vars::screenSize = { width, height };
        if (shaderProgram)
            glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), width, height);
        protocol = MV_COMPUTE;
        int factor = (vars::ssaa ? vars::ssaa_factor : 1);

        glBindFramebuffer(GL_FRAMEBUFFER, mandelbrotFrameBuffer);
        glBindTexture(GL_TEXTURE_2D, mandelbrotTexBuffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width * factor, height * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        glBindFramebuffer(GL_FRAMEBUFFER, postprocFrameBuffer);
        glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width * factor, height * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }

    static void on_mouseButton(GLFWwindow* window, int button, int action, int mod) {
        glm::ivec2 ss = (vars::fullscreen ? consts::monitorSize : vars::screenSize);
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
                glm::dvec2 center = utils::pixel_to_complex(static_cast<glm::dvec2>(ss) / 2.0);
                vars::offset += pos - center;
                glUniform2d(glGetUniformLocation(shaderProgram, "offset"), vars::offset.x, vars::offset.y);
                dragging = false;
                protocol = MV_COMPUTE;
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
        glm::ivec2 ss = (vars::fullscreen ? consts::monitorSize : vars::screenSize);
        glUniform2d(glGetUniformLocation(shaderProgram, "mousePos"), x / ss.x, y / ss.y);
        if (ImGui::GetIO().WantCaptureMouse)
            return;
        if (dragging) {
            lastPresses = { -consts::doubleClick_interval, 0 }; // reset to prevent accidental centering while rapidly dragging
            vars::offset.x -= ((x - oldPos.x) * vars::zoom) / ss.x;
            vars::offset.y -= ((oldPos.y - y) * ((vars::zoom * ss.y) / ss.x)) / ss.y;
            glUniform2d(glGetUniformLocation(shaderProgram, "offset"), vars::offset.x, vars::offset.y);
            oldPos = { x, y };
            protocol = MV_COMPUTE;
        }
    }

    static void on_mouseScroll(GLFWwindow* window, double x, double y) { // y is usually either 1 or -1 depending on direction, x is always 0
        if (rightClickHold) {
            vars::julia_zoom *= pow(consts::zoom_co, y * 1.5);
            glUniform1d(glGetUniformLocation(shaderProgram, "julia_zoom"), vars::julia_zoom);
            glUniform1i(glGetUniformLocation(shaderProgram, "julia_maxiters"), utils::max_iters(vars::julia_zoom, consts::zoom_co, vars::iter_co, 3.0));
        }
        else {
            vars::zoom *= pow(consts::zoom_co, y * 1.5);
            glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), vars::zoom);
            glUniform1i(glGetUniformLocation(shaderProgram, "max_iters"), utils::max_iters(vars::zoom, consts::zoom_co, vars::iter_co));
            protocol = MV_COMPUTE;
        }
    }

    static void on_keyPress(GLFWwindow* window, int key, int scancode, int action, int mods) {
        if (action != GLFW_PRESS) return;
        switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, true);
            break;
        case GLFW_KEY_F11:
            if (glfwGetWindowMonitor(window) == nullptr) {
                vars::fullscreen = true;
                GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                glfwGetWindowPos(window, &vars::screenPos.x, &vars::screenPos.y);
                glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
            }
            else {
                glfwSetWindowMonitor(window, nullptr, vars::screenPos.x, vars::screenPos.y, vars::screenSize.x, vars::screenSize.y, 0);
                vars::fullscreen = false;
            }
        }
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
void toggleButton(T* v, const char* id, const char* name, PFNGLUNIFORM1IPROC uniformType) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float height = ImGui::GetFrameHeight();
    float width = height * 1.8f;
    float radius = height * 0.50f;
    float rounding = 0.4f;

    ImGui::InvisibleButton(id, ImVec2(width, height));
    if (ImGui::IsItemClicked()) {
        *v ^= 1;
        uniformType(glGetUniformLocation(shaderProgram, id), *v);
        protocol = MV_POSTPROC;
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
    consts::monitorSize.x = mode->width;
    consts::monitorSize.y = mode->height;

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
    glfwSetKeyCallback(window, events::on_keyPress);

    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.Fonts->AddFontDefault();
    ImFont* font_title = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 11.f);
    IM_ASSERT(font_title != NULL);
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking

    ImGui::StyleColorsDark();

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
    colors[ImGuiCol_Button] = ImVec4(0.25f, 0.25f, 0.25f, 0.54f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.39f, 0.39f, 0.39f, 0.54f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.32f, 0.33f, 1.00f);
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

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to create OpenGL window" << std::endl;
        return -1;
    }

    int factor = (vars::ssaa ? vars::ssaa_factor : 1);

    glGenTextures(1, &mandelbrotTexBuffer);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mandelbrotTexBuffer);
    glUniform1i(glGetUniformLocation(shaderProgram, "mandelbrotTex"), 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, vars::screenSize.x * factor, vars::screenSize.y * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &postprocTexBuffer);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
    glUniform1i(glGetUniformLocation(shaderProgram, "postprocTex"), 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, vars::screenSize.x * factor, vars::screenSize.y * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenTextures(1, &juliaTexBuffer);
    glBindTexture(GL_TEXTURE_2D, juliaTexBuffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, vars::julia_size * factor, vars::julia_size * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
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
    glUniform1f(glGetUniformLocation(shaderProgram, "iter_multiplier"), vars::iter_multiplier);
    glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), vars::zoom);
    glUniform1i(glGetUniformLocation(shaderProgram, "max_iters"), utils::max_iters(vars::zoom, consts::zoom_co, vars::iter_co));
    glUniform1f(glGetUniformLocation(shaderProgram, "spectrum_offset"), vars::spectrum_offset);
    glUniform1d(glGetUniformLocation(shaderProgram, "bailout_radius"), pow(vars::bailout_radius, 2));
    glUniform1i(glGetUniformLocation(shaderProgram, "continuous_coloring"), vars::continuous_coloring);
    glUniform3f(glGetUniformLocation(shaderProgram, "set_color"), vars::set_color.x, vars::set_color.y, vars::set_color.z);
    glUniform1i(glGetUniformLocation(shaderProgram, "degree"), vars::degree);
    glUniform1d(glGetUniformLocation(shaderProgram, "julia_zoom"), vars::julia_zoom);
    glUniform1i(glGetUniformLocation(shaderProgram, "julia_maxiters"), utils::max_iters(vars::julia_zoom, consts::zoom_co, vars::iter_co, 3.0));
    glUniform1i(glGetUniformLocation(shaderProgram, "blur"), vars::ssaa_factor);

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
        glm::ivec2 ss = (vars::fullscreen ? consts::monitorSize : vars::screenSize);

        ImGui::PushFont(font_title);
        ImGui::SetNextWindowPos({ 10, 10 });
        ImGui::SetNextWindowCollapsed(true, 1 << 1);
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

            if (ImGui::Button("Take screenshot")) {
                char const* lFilterPatterns[2] = { "*.jpg", "*.png" };
                char const* path = tinyfd_saveFileDialog("Save screenshot", "screenshot.png", 1, lFilterPatterns, "Image files (*.png, *.jpg)");
                if (path) utils::screenshot(path, window);
            }
            //ImGui::SameLine();
            //ImGui::Button("Create video"); ImGui::SameLine();
            //ImGui::Button("Reset all");

            ImGui::SeparatorText("Parameters");
            bool update = false;
            ImGui::Text("Re");   ImGui::SetNextItemWidth(320); ImGui::SameLine(); ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
            update |= ImGui::InputDouble("##re", &vars::offset.x, 0.0, 0.0, "%.17g");
            ImGui::Text("Im");   ImGui::SetNextItemWidth(320); ImGui::SameLine(); ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
            update |= ImGui::InputDouble("##im", &vars::offset.y, 0.0, 0.0, "%.17g");
            if (update) {
                glUniform2d(glGetUniformLocation(shaderProgram, "offset"), vars::offset.x, vars::offset.y);
                protocol = MV_COMPUTE;
            }
            ImGui::Text("Zoom"); ImGui::SetNextItemWidth(80); ImGui::SameLine(); ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.f);
            if (ImGui::InputDouble("##zoom", &vars::zoom, 0.0, 0.0, "%.2e")) {
                glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), vars::zoom);
                protocol = MV_COMPUTE;
            }

            ImGui::BeginGroup();
            ImGui::SeparatorText("Computation");
            //ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            if (ImGui::SliderFloat("Iteration coefficient", &vars::iter_co, 1.01, 1.1)) {
                glUniform1i(glGetUniformLocation(shaderProgram, "max_iters"), utils::max_iters(vars::zoom, consts::zoom_co, vars::iter_co));
                protocol = MV_COMPUTE;
            }
            //ImGui::PushItemFlag(ImGuiItemFlags_Disabled, false);
            if (ImGui::SliderFloat("Bailout radius", &vars::bailout_radius, 2.0, 25.0)) {
                glUniform1d(glGetUniformLocation(shaderProgram, "bailout_radius"), pow(vars::bailout_radius, 2));
                protocol = MV_COMPUTE;
            }
            if (ImGui::SliderInt("Degree", &vars::degree, 2, 7)) {
                glUniform1i(glGetUniformLocation(shaderProgram, "degree"), vars::degree);
                protocol = MV_COMPUTE;
            }
            if (ImGui::Checkbox("Super Sampling AA", &vars::ssaa)) {
                int factor = ((vars::ssaa) ? vars::ssaa_factor : 1);
                glBindFramebuffer(GL_FRAMEBUFFER, mandelbrotFrameBuffer);
                glBindTexture(GL_TEXTURE_2D, mandelbrotTexBuffer);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, ss.x * factor, ss.y * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

                glBindFramebuffer(GL_FRAMEBUFFER, postprocFrameBuffer);
                glBindTexture(GL_TEXTURE_2D, postprocTexBuffer);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, ss.x * factor, ss.y * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

                glBindFramebuffer(GL_FRAMEBUFFER, juliaFrameBuffer);
                glBindTexture(GL_TEXTURE_2D, juliaTexBuffer);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, vars::julia_size * factor, vars::julia_size * factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

                glUniform1i(glGetUniformLocation(shaderProgram, "blur"), factor);

                protocol = MV_COMPUTE;
            }
            ImGui::SeparatorText("Coloring");
            if (ImGui::ColorEdit3("In-set color", &vars::set_color.x)) {
                glUniform3f(glGetUniformLocation(shaderProgram, "set_color"), vars::set_color.r, vars::set_color.g, vars::set_color.b);
                protocol = MV_POSTPROC;
            }
            if (ImGui::SliderFloat("Iteration Multiplier", &vars::iter_multiplier, 1, 128, "x%.4g", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat)) {
                glUniform1f(glGetUniformLocation(shaderProgram, "iter_multiplier"), vars::iter_multiplier);
                protocol = MV_POSTPROC;
            }
            if (ImGui::SliderFloat("Spectrum Offset", &vars::spectrum_offset, 0, spectrum::span)) {
                glUniform1f(glGetUniformLocation(shaderProgram, "spectrum_offset"), vars::spectrum_offset);
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
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, spectrumBuffer);
                memcpy(glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_WRITE_ONLY), spectrum::data.data(), spectrum::data.size() * sizeof(glm::vec4));
                glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
                protocol = MV_COMPUTE;
            }
            ImGui::EndGroup();
            //ImGui::Button("Save", ImVec2(80.f, 0.f)); ImGui::SameLine();
            //ImGui::Button("Load", ImVec2(80.f, 0.f)); ImGui::SameLine();
            //ImGui::Button("Reset", ImVec2(80.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(80, 80, 80, 255));
            ImGui::Text("(c) 2017-2024 Yilmaz Alpaslan");
            ImGui::PopStyleColor();
            ImGui::EndGroup();
        }
        ImGui::End();

        int factor = ((vars::ssaa) ? vars::ssaa_factor : 1);

        if (events::rightClickHold) {
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
            glm::dvec2 c = utils::pixel_to_complex({x, y});
            glm::dvec2 z = c;
            int i;
            for (i = 1; i < utils::max_iters(vars::zoom, consts::zoom_co, vars::iter_co); i++) {
                double xx = z.x * z.x;
                double yy = z.y * z.y;
                if (xx + yy > pow(vars::bailout_radius, 2))
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
            glUniform1i(glGetUniformLocation(shaderProgram, "protocol"), 3);
            glUniform2d(glGetUniformLocation(shaderProgram, "mouseCoord"), c.x, c.y);
            glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), vars::julia_size * factor, vars::julia_size * factor);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            ImGui::SeparatorText("Julia Set");
            ImGui::Image((void*)(intptr_t)juliaTexBuffer, ImVec2(vars::julia_size, vars::julia_size));
            ImGui::End();
        }
        glViewport(0, 0, ss.x * factor, ss.y * factor);
        glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), ss.x * factor, ss.y * factor);
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
    } while (!glfwWindowShouldClose(window));

    
    return 0;
}