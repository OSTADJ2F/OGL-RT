#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include "Shader.h"
#include <cmath>
#include <glm/glm.hpp>
#include <vector>

// Global camera state
float cameraPos[3] = { 0.0f, 1.0f, -3.0f }; // Initial position: slightly above & behind the scene
float yaw = 3.14159f;  // Start facing the opposite direction (π radians = 180 degrees)
float pitch = 0.0f;  // Pitch (rotation around x-axis) in radians

// Toggles for features
bool denoiseEnabled = false;
bool giEnabled = false;
bool skyboxEnabled = false;  // Toggle for using the skybox

// Global variables for mouse handling
double lastX = 640, lastY = 360; // Center of an 800x600 window
bool firstMouse = true;

// ===== New Globals for Temporal Accumulation & Progressive Rendering =====
GLuint prevFrameTexture = 0, accumFramebuffer = 0;
int frameCount = 0;
float lastCameraPos[3] = { 0.0f, 1.0f, -3.0f };
float lastYaw = 0.0f, lastPitch = 0.0f;
// ============================================================================

struct Sphere {
    float center[3];
    float radius;
    float color[3];
};

std::vector<Sphere> spheres = {
    // Default spheres
    {{0.0f, 0.0f, 5.0f}, 1.0f, {1.0f, 0.0f, 0.0f}},      // Red sphere
    {{2.0f, 0.0f, 4.0f}, 0.7f, {0.0f, 1.0f, 0.0f}},      // Green sphere
    {{-2.0f, 0.5f, 6.0f}, 1.2f, {0.0f, 0.0f, 1.0f}},     // Blue sphere
    {{0.0f, -0.5f, 3.0f}, 0.5f, {1.0f, 1.0f, 0.0f}}      // Yellow sphere
};

// Mouse callback: updates yaw based on mouse movement, ignores vertical movement
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
        return;
    }

    float xoffset = static_cast<float>(xpos - lastX);
    lastX = xpos;
    lastY = ypos;  // Still update lastY even though we don't use yoffset

    float sensitivity = 0.005f;
    xoffset *= sensitivity;

    yaw += xoffset;  // Only update yaw, ignore pitch completely
}

// ===== New Function: setupFramebuffer =====
// Place this after the skybox texture setup in program order.
void setupFramebuffer(int width, int height) {
    if (prevFrameTexture) glDeleteTextures(1, &prevFrameTexture);
    if (accumFramebuffer) glDeleteFramebuffers(1, &accumFramebuffer);
    
    glGenTextures(1, &prevFrameTexture);
    glBindTexture(GL_TEXTURE_2D, prevFrameTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    
    glGenFramebuffers(1, &accumFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, accumFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, prevFrameTexture, 0);
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
// ============================================================================

// ===== New Function: framebuffer_size_callback =====
// Reset viewport, framebuffer, and frameCount on window resize.
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    setupFramebuffer(width, height);
    frameCount = 0;
}
// ============================================================================

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

    // ===== Register Resize Callback =====
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    // =================================================================

    // Initialize GLEW (after creating the context)
    GLenum err = glewInit();
    if (GLEW_OK != err) {
        std::cerr << "GLEW initialization error: " << glewGetErrorString(err) << "\n";
        return -1;
    }
    glViewport(0, 0, 1280, 720);

    // ===== Framebuffer Setup Call =====
    setupFramebuffer(1280, 720);
    // =================================================================

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

        // Toggle denoiser with 'V' (reset frameCount when toggled)
        bool currentVPressed = (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS);
        if (currentVPressed && !lastVPressed) {
            denoiseEnabled = !denoiseEnabled;
            frameCount = 0; // ===== Reset frameCount on denoiser toggle =====
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

        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            cameraPos[0] += forwardHorizontal[0] * moveSpeed;
            cameraPos[1] += forwardHorizontal[1] * moveSpeed;
            cameraPos[2] += forwardHorizontal[2] * moveSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            cameraPos[0] -= forwardHorizontal[0] * moveSpeed;
            cameraPos[1] -= forwardHorizontal[1] * moveSpeed;
            cameraPos[2] -= forwardHorizontal[2] * moveSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            cameraPos[0] -= rightHorizontal[0] * moveSpeed;
            cameraPos[1] -= rightHorizontal[1] * moveSpeed;
            cameraPos[2] -= rightHorizontal[2] * moveSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            cameraPos[0] += rightHorizontal[0] * moveSpeed;
            cameraPos[1] += rightHorizontal[1] * moveSpeed;
            cameraPos[2] += rightHorizontal[2] * moveSpeed;
        }
        // Always move straight up/down in world space, regardless of camera orientation
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
            cameraPos[1] += moveSpeed; // World up is always +Y
        }
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
            cameraPos[1] -= moveSpeed; // World down is always -Y
        }
        // ---------------------------

        // Update viewport (in case of fullscreen change)
        int fbWidth, fbHeight;
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        glViewport(0, 0, fbWidth, fbHeight);

        // ===== Camera Movement Detection for Temporal Accumulation =====
        float cameraDelta = sqrt(
            pow(cameraPos[0] - lastCameraPos[0], 2) +
            pow(cameraPos[1] - lastCameraPos[1], 2) +
            pow(cameraPos[2] - lastCameraPos[2], 2)
        ) + fabs(yaw - lastYaw) + fabs(pitch - lastPitch);

        if (cameraDelta > 0.001f)
            frameCount = 0;
        else
            frameCount++;

        for (int i = 0; i < 3; i++) lastCameraPos[i] = cameraPos[i];
        lastYaw = yaw;
        lastPitch = pitch;
        // =================================================================

        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);

        // --- Build the Camera Rotation (front, right, up) ---
        float front[3] = {
            sinf(yaw),    // x
            0.0f,         // y is always 0 since we're staying horizontal
            cosf(yaw)     // z
        };

        // Right vector is always perpendicular to front in xz plane
        float right[3] = {
            cosf(yaw),    // x
            0.0f,         // y is always 0
            -sinf(yaw)    // z
        };

        // Up vector is always world up since we're staying horizontal
        float up[3] = {
            0.0f,         // x
            1.0f,         // y
            0.0f          // z
        };

        // Send camera position and rotation to the shader
        GLint camPosLoc = glGetUniformLocation(shaderProgram, "uCamPos");
        glUniform3f(camPosLoc, cameraPos[0], cameraPos[1], cameraPos[2]);

        float camRot[9] = {
            right[0], up[0], -front[0],
            right[1], up[1], -front[1],
            right[2], up[2], -front[2]
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

        // Pass sphere data to the shader
        GLint numSpheresLoc = glGetUniformLocation(shaderProgram, "uNumSpheres");
        glUniform1i(numSpheresLoc, static_cast<GLint>(spheres.size()));

        for (size_t i = 0; i < spheres.size(); ++i) {
            std::string centerName = "uSpheres[" + std::to_string(i) + "].center";
            std::string radiusName = "uSpheres[" + std::to_string(i) + "].radius";
            std::string colorName = "uSpheres[" + std::to_string(i) + "].color";

            GLint centerLoc = glGetUniformLocation(shaderProgram, centerName.c_str());
            GLint radiusLoc = glGetUniformLocation(shaderProgram, radiusName.c_str());
            GLint colorLoc = glGetUniformLocation(shaderProgram, colorName.c_str());

            glUniform3fv(centerLoc, 1, spheres[i].center);
            glUniform1f(radiusLoc, spheres[i].radius);
            glUniform3fv(colorLoc, 1, spheres[i].color);
        }

        // ===== New Uniform Updates for Temporal Accumulation =====
        GLint prevFrameLoc = glGetUniformLocation(shaderProgram, "uPrevFrame");
        GLint cameraDeltaLoc = glGetUniformLocation(shaderProgram, "uCameraDelta");
        GLint frameCountLoc = glGetUniformLocation(shaderProgram, "uFrameCount");

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, prevFrameTexture);
        glUniform1i(prevFrameLoc, 1);
        glUniform1f(cameraDeltaLoc, cameraDelta);
        glUniform1i(frameCountLoc, frameCount);
        // =================================================================

        // Render the full-screen quad
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);

        // ===== New Frame Copying for Accumulation =====
        glBindFramebuffer(GL_FRAMEBUFFER, accumFramebuffer);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, fbWidth, fbHeight);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        // =================================================================

        glfwSwapBuffers(window);
    }

    // Cleanup resources
    glDeleteTextures(1, &prevFrameTexture);       // ===== Cleanup accumulated texture =====
    glDeleteFramebuffers(1, &accumFramebuffer);     // ===== Cleanup framebuffer =====
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
    glDeleteProgram(shaderProgram);
    glfwTerminate();
    return 0;
}
