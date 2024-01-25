#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>

constexpr bool fullscreen = false;

float vertices[] = {
    -1.0f, -1.0f,
     1.0f, -1.0f,
     1.0f,  1.0f,
    -1.0f, -1.0f,
     1.0f,  1.0f,
    -1.0f,  1.0f
}; // just two triangles to cover the entire screen

const char* vertexShaderSource = R"(
#version 400 core

layout(location = 0) in vec2 aPos;
    
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* fragmentShaderSource = R"(
#version 400 core
    
out vec4 fragColor;

uniform ivec2  screenSize;
uniform dvec2  offset;
uniform double zoom;
uniform int    max_iters;

vec3 color(int i) {
    float val = float(i % 256) / 255;
    switch (int(floor(i / 256)) % 6) {
    case 0:
        return vec3(1.0f, val, 0.0f);
    case 1:
        return vec3(1.0f - val, 1.0f, 0.0f);
    case 2:
        return vec3(0.0f, 1.0f, val);
    case 3:
        return vec3(0.0f, 1.0f - val, 1.0f);
    case 4:
        return vec3(val, 0.0f, 1.0f);
    case 5:
        return vec3(1.0f, 0.0f, 1.0f - val);
    }
}

void main() {
    dvec2 c = (dvec2(gl_FragCoord.x / screenSize.x, (screenSize.y - gl_FragCoord.y) / screenSize.y) - dvec2(0.5, 0.5)) * dvec2(zoom, (screenSize.y * zoom) / screenSize.x) + offset;
    dvec2 z = c;

    for (int i = 0; i < max_iters; i++) {
        if (z.x * z.x + z.y * z.y >= 4) {
            vec3 c = color(i * 20);
            fragColor = vec4(c, 1);
            return;
        }
        z = dvec2(z.x * z.x - z.y * z.y, 2.0f * z.x * z.y) + c;
    }
    fragColor = vec4(0, 0, 0, 1);
}
)";


unsigned int shaderProgram = 0;

bool pending_render = true;



namespace consts {
    constexpr double zoom_co = 0.85;
    constexpr double iter_co = 1.060;

    constexpr double doubleClick_interval = 0.5; // seconds
}

namespace vars {
    glm::dvec2 offset = { -0.4, 0 };
    glm::ivec2 screenSize;

    double zoom = 3.0;
    int max_iters = 100;
}

// function to calculate the complex plane coordinate of a pixel
glm::dvec2 pixel_to_complex(glm::dvec2 pixelCoord, glm::ivec2 screenSize, double zoom, glm::dvec2 offset) {
    return ((glm::dvec2(pixelCoord.x / screenSize.x, pixelCoord.y / screenSize.y)) - glm::dvec2(0.5, 0.5)) * glm::dvec2(zoom, (screenSize.y * zoom) / screenSize.x) + offset;
}
glm::dvec2 pixel_to_complex(glm::dvec2 pixelCoord) {
    return ((glm::dvec2(pixelCoord.x / vars::screenSize.x, pixelCoord.y / vars::screenSize.y)) - glm::dvec2(0.5, 0.5)) * glm::dvec2(vars::zoom, (vars::screenSize.y * vars::zoom) / vars::screenSize.x) + vars::offset;
}

namespace events {
    glm::dvec2 oldPos = { 0, 0 };
    glm::dvec2 lastPresses = { -consts::doubleClick_interval, 0 };
    bool dragging = false;
    bool rightClickHold = false;

    void on_windowResize(GLFWwindow* window, int width, int height) {
        glViewport(0, 0, width, height);

        vars::screenSize = { width, height };
        if (shaderProgram)
            glUniform2i(glGetUniformLocation(shaderProgram, "screenSize"), width, height);
        pending_render = true;
    }

    void on_mouseButton(GLFWwindow* window, int button, int action, int mod) {
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
                glm::dvec2 pos = pixel_to_complex(events::oldPos);
                glm::dvec2 center = pixel_to_complex(static_cast<glm::dvec2>(vars::screenSize) / 2.0);
                vars::offset += pos - center;
                glUniform2d(glGetUniformLocation(shaderProgram, "offset"), vars::offset.x, vars::offset.y);
                dragging = false;
                pending_render = true;
            }
            else dragging = false;
        }
    }

    void on_cursorMove(GLFWwindow* window, double x, double y) {
        if (dragging) {
            lastPresses = { -consts::doubleClick_interval, 0 }; // reset to prevent accidental centering while quick dragging
            vars::offset.x -= ((x - oldPos.x) * vars::zoom) / vars::screenSize.x;
            vars::offset.y -= ((y - oldPos.y) * ((vars::zoom * vars::screenSize.y) / vars::screenSize.x)) / vars::screenSize.y;
            glUniform2d(glGetUniformLocation(shaderProgram, "offset"), vars::offset.x, vars::offset.y);
            oldPos = { x, y };
            pending_render = true;
        }
    }

    void on_mouseScroll(GLFWwindow* window, double x, double y) { // y is usually either 1 or -1 depending on direction, x is always 0
        vars::zoom *= pow(consts::zoom_co, y);
        vars::max_iters *= pow(consts::iter_co, y);
        glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), vars::zoom);
        glUniform1i(glGetUniformLocation(shaderProgram, "max_iters"), vars::max_iters);
        pending_render = true;
    }
}

void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
}

int main() {
    glfwInit();

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

    GLFWwindow* window;
    if (fullscreen) {
        window = glfwCreateWindow(mode->width, mode->height, "OpenGL Test", monitor, NULL);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, true);
    }
    else {
        window = glfwCreateWindow(600, 600, "OpenGL Test", NULL, NULL);
    }
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

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to create OpenGL window" << std::endl;
        return -1;
    }

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
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
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

    events::on_windowResize(window, 600, 600);

    glUniform2d(glGetUniformLocation(shaderProgram, "offset"), vars::offset.x, vars::offset.y);
    glUniform1d(glGetUniformLocation(shaderProgram, "zoom"), vars::zoom);
    glUniform1i(glGetUniformLocation(shaderProgram, "max_iters"), vars::max_iters);

    do {
        processInput(window);

        if (pending_render) {
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glfwSwapBuffers(window);
            pending_render = false;
        }
        glfwPollEvents();
    } while (!glfwWindowShouldClose(window));

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}