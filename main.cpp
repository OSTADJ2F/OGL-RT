#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include "Shader.h"
#include <cmath>

// Global camera state
float cameraPos[3] = { 0.0f, 1.0f, -3.0f }; // Initial position: slightly above & behind the scene
float yaw = 0.0f;    // Yaw (rotation around y-axis) in radians
float pitch = 0.0f;  // Pitch (rotation around x-axis) in radians

// Toggles for features
bool denoiseEnabled = false;
bool giEnabled = false;
bool skyboxEnabled = false;  // Toggle for using the skybox

// Global variables for mouse handling
double lastX = 640, lastY = 360; // Center of an 800x600 window
bool firstMouse = true;

// Mouse callback: updates yaw and pitch based on mouse movement
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }
    float xoffset = static_cast<float>(xpos - lastX);
    float yoffset = static_cast<float>(lastY - ypos);
    lastX = xpos;
    lastY = ypos;

    float sensitivity = 0.005f; // Adjust sensitivity as desired
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw += xoffset;
    pitch += yoffset;

    // Clamp pitch to avoid flipping
    if (pitch > 1.57f)
        pitch = 1.57f;
    if (pitch < -1.57f)
        pitch = -1.57f;
}

int main() {
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }

    // Set OpenGL version (3.3 core profile)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Create a window
    GLFWwindow* window = glfwCreateWindow(1280, 720, "GPU Ray Tracer", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    // Save initial windowed mode parameters
    int windowedX, windowedY, windowedWidth, windowedHeight;
    glfwGetWindowPos(window, &windowedX, &windowedY);
    glfwGetWindowSize(window, &windowedWidth, &windowedHeight);

    // Set mouse callback and capture the cursor for FPS-style control
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Initialize GLEW (after creating the context)
    GLenum err = glewInit();
    if (GLEW_OK != err) {
        std::cerr << "GLEW initialization error: " << glewGetErrorString(err) << "\n";
        return -1;
    }
    glViewport(0, 0, 1280, 720);

    // Create and compile the shader program (loads vertex_shader.glsl and fragment_shader.glsl)
    GLuint shaderProgram = createShaderProgram("vertex_shader.glsl", "fragment_shader.glsl");

    // Load the HDR skybox image ("skybox.hdr") using stb_image.
    int width, height, nrComponents;
    stbi_set_flip_vertically_on_load(true);
    float* data = stbi_loadf("skybox.hdr", &width, &height, &nrComponents, 0);
    GLuint skyboxTexture = 0;
    if (data) {
        glGenTextures(1, &skyboxTexture);
        glBindTexture(GL_TEXTURE_2D, skyboxTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        stbi_image_free(data);
    }
    else {
        std::cerr << "Failed to load HDR skybox." << std::endl;
    }

    // Set up a full-screen quad (triangle strip covering the viewport)
    float quadVertices[] = {
        -1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
    };
    GLuint quadVAO, quadVBO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);

    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);  // Vertex attribute 0: position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    // Timing and key toggle variables
    float lastFrame = 0.0f;
    bool lastVPressed = false;
    bool lastGPressed = false;
    bool lastBPressed = false;
    bool lastFPressed = false;
    bool fullscreen = false;

    // Main rendering loop
    while (!glfwWindowShouldClose(window)) {
        // Calculate delta time
        float currentFrame = static_cast<float>(glfwGetTime());
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        const float moveSpeed = 2.0f * deltaTime;

        // Process input events
        glfwPollEvents();

        // Terminate when Esc is pressed
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, true);
        }

        // Toggle fullscreen when F is pressed (on key down)
        bool currentFPressed = (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS);
        if (currentFPressed && !lastFPressed) {
            fullscreen = !fullscreen;
            if (fullscreen) {
                GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
            }
            else {
                glfwSetWindowMonitor(window, nullptr, windowedX, windowedY, windowedWidth, windowedHeight, 0);
            }
        }
        lastFPressed = currentFPressed;

        // Toggle denoiser with 'V'
        bool currentVPressed = (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS);
        if (currentVPressed && !lastVPressed) {
            denoiseEnabled = !denoiseEnabled;
            std::cout << "Denoiser toggled " << (denoiseEnabled ? "ON" : "OFF") << "\n";
        }
        lastVPressed = currentVPressed;

        // Toggle global illumination with 'G'
        bool currentGPressed = (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS);
        if (currentGPressed && !lastGPressed) {
            giEnabled = !giEnabled;
            std::cout << "Global Illumination toggled " << (giEnabled ? "ON" : "OFF") << "\n";
        }
        lastGPressed = currentGPressed;

        // Toggle skybox with 'B'
        bool currentBPressed = (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS);
        if (currentBPressed && !lastBPressed) {
            skyboxEnabled = !skyboxEnabled;
            std::cout << "Skybox toggled " << (skyboxEnabled ? "ON" : "OFF") << "\n";
        }
        lastBPressed = currentBPressed;

        // --- FPS-style Movement ---
        float forwardHorizontal[3] = { sin(yaw), 0.0f, cos(yaw) };
        float rightHorizontal[3] = { forwardHorizontal[2], 0.0f, -forwardHorizontal[0] };

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            cameraPos[0] += forwardHorizontal[0] * moveSpeed;
            cameraPos[1] += forwardHorizontal[1] * moveSpeed;
            cameraPos[2] += forwardHorizontal[2] * moveSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            cameraPos[0] -= forwardHorizontal[0] * moveSpeed;
            cameraPos[1] -= forwardHorizontal[1] * moveSpeed;
            cameraPos[2] -= forwardHorizontal[2] * moveSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            cameraPos[0] -= rightHorizontal[0] * moveSpeed;
            cameraPos[1] -= rightHorizontal[1] * moveSpeed;
            cameraPos[2] -= rightHorizontal[2] * moveSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            cameraPos[0] += rightHorizontal[0] * moveSpeed;
            cameraPos[1] += rightHorizontal[1] * moveSpeed;
            cameraPos[2] += rightHorizontal[2] * moveSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
            cameraPos[1] += moveSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
            cameraPos[1] -= moveSpeed;
        }
        // ---------------------------

        // Update viewport (in case of fullscreen change)
        int fbWidth, fbHeight;
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        glViewport(0, 0, fbWidth, fbHeight);

        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);

        // --- Build the Camera Rotation (front, right, up) from yaw & pitch ---
        float cosPitch = cosf(pitch);
        float sinPitch = sinf(pitch);
        float cosYaw = cosf(yaw);
        float sinYaw = sinf(yaw);

        // 1) Standard "look" vector in a right-handed coordinate system:
        //    - yaw rotates around Y
        //    - pitch rotates around X
        //    This formula ensures "pitch" always moves the camera up/down in its local X axis
        float front[3] = {
            cosPitch * sinYaw,  // X
            sinPitch,           // Y
            cosPitch * cosYaw   // Z
        };

        // Normalize front (just in case)
        {
            float len = sqrtf(front[0] * front[0] + front[1] * front[1] + front[2] * front[2]);
            front[0] /= len;
            front[1] /= len;
            front[2] /= len;
        }

        // 2) Compute right = front x worldUp
        float worldUp[3] = { 0.0f, 1.0f, 0.0f };
        float right[3] = {
            front[1] * worldUp[2] - front[2] * worldUp[1],
            front[2] * worldUp[0] - front[0] * worldUp[2],
            front[0] * worldUp[1] - front[1] * worldUp[0]
        };
        // Normalize right
        {
            float rLen = sqrtf(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
            right[0] /= rLen;
            right[1] /= rLen;
            right[2] /= rLen;
        }

        // 3) Compute up = right x front
        float up[3] = {
            right[1] * front[2] - right[2] * front[1],
            right[2] * front[0] - right[0] * front[2],
            right[0] * front[1] - right[1] * front[0]
        };
        // Normalize up
        {
            float uLen = sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
            up[0] /= uLen;
            up[1] /= uLen;
            up[2] /= uLen;
        }

        // Now send cameraPos, right, up, and front down to the shader:
        GLint camPosLoc = glGetUniformLocation(shaderProgram, "uCamPos");
        glUniform3f(camPosLoc, cameraPos[0], cameraPos[1], cameraPos[2]);

        float camRot[9] = {
            right[0], up[0], front[0],
            right[1], up[1], front[1],
            right[2], up[2], front[2]
        };
        GLint camRotLoc = glGetUniformLocation(shaderProgram, "uCamRot");
        glUniformMatrix3fv(camRotLoc, 1, GL_FALSE, camRot);


        // Update time uniform for animations
        GLint timeLoc = glGetUniformLocation(shaderProgram, "uTime");
        glUniform1f(timeLoc, currentFrame);

        // Pass feature toggles to shader
        GLint denoiseLoc = glGetUniformLocation(shaderProgram, "uDenoise");
        glUniform1i(denoiseLoc, denoiseEnabled ? 1 : 0);
        GLint giLoc = glGetUniformLocation(shaderProgram, "uGI");
        glUniform1i(giLoc, giEnabled ? 1 : 0);
        GLint skyboxLoc = glGetUniformLocation(shaderProgram, "uSkybox");
        glUniform1i(skyboxLoc, skyboxEnabled ? 1 : 0);

        // Bind the skybox HDR texture to texture unit 0 and pass its unit index.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, skyboxTexture);
        GLint skyboxTexLoc = glGetUniformLocation(shaderProgram, "uSkyboxTex");
        glUniform1i(skyboxTexLoc, 0);

        // Render the full-screen quad
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);

        glfwSwapBuffers(window);
    }

    // Cleanup resources
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
    glDeleteProgram(shaderProgram);
    glfwTerminate();
    return 0;
}
