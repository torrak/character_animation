// A compact demo: orbit camera, simple FSM + crossfade, dust particles, infinite grid
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

#include <learnopengl/filesystem.h>
#include <learnopengl/shader_m.h>
#include <learnopengl/camera.h>
#include <learnopengl/animator.h>
#include <learnopengl/model_animation.h>

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);

// settings
const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

// simple orbit camera (spherical)
float camYaw = -90.0f;
float camPitch = -10.0f;
float camDistance = 4.0f;
glm::vec3 camTarget(0.0f, 0.8f, 0.0f);
// allow camera rotation with mouse drag (left OR right)
bool mouseDragDown = false;
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// character controller / FSM
enum MovementState { MS_IDLE, MS_WALK, MS_RUN };
enum ActionState { AS_NONE, AS_JUMP, AS_PUNCH, AS_KICK };

MovementState movementState = MS_IDLE;
ActionState actionState = AS_NONE;
float movementSpeedRun = 4.5f;
float movementSpeedWalk = 2.0f;
glm::vec3 playerPosition(0.0f, 0.0f, 0.0f);
float playerYaw = 0.0f; // facing direction

// Jump / physics state (added)
bool isInAir = false;
float verticalVelocity = 0.0f;
const float gravity = -9.8f;
const float jumpInitialVelocity = 4.5f;
const float groundY = 0.0f; // playerPosition.y is 0 when on ground (model translate uses y - 0.4f)

// blending
bool isBlending = false;
float blendDuration = 0.25f;
float blendTimer = 0.0f;

// stepping cadence -> dust spawn
float stepTimer = 0.0f;
float walkCadence = 0.45f; // seconds per step
float runCadence = 0.28f;

// particles
struct Particle {
    glm::vec3 pos;
    float life; // 1..0
    float size;
};
std::vector<Particle> particles;
const int MAX_PARTICLES = 200;

// particle GL
GLuint particleVAO = 0;
GLuint particleVBO = 0;

// fullscreen quad for infinite grid
GLuint quadVAO = 0;
GLuint quadVBO = 0;

// helper: elementwise lerp of mat4 by column (fast approximation for demo)
static glm::mat4 BlendMatrices(const glm::mat4& A, const glm::mat4& B, float t) {
    glm::mat4 R;
    for (int c = 0; c < 4; ++c) {
        glm::vec4 ca = A[c];
        glm::vec4 cb = B[c];
        R[c] = glm::mix(ca, cb, t);
    }
    return R;
}

int main()
{
    // glfw init
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Skeletal Controller Demo", NULL, NULL);
    if (!window) {
        std::cout << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);

    // do not capture cursor globally (we only orbit while dragging)
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD\n";
        return -1;
    }

    stbi_set_flip_vertically_on_load(true);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // shaders
    Shader animShader("anim_model.vs", "anim_model.fs");
    Shader gridShader("infinite_grid.vs", "infinite_grid.fs");
    Shader particleShader("particle.vs", "particle.fs");

    // fullscreen quad (NDC covering)
    float quadVertices[] = {
        -1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f, 0.0f
    };
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // simple particle VAO (positions updated each frame)
    glGenVertexArrays(1, &particleVAO);
    glGenBuffers(1, &particleVBO);
    glBindVertexArray(particleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, particleVBO);
    glBufferData(GL_ARRAY_BUFFER, MAX_PARTICLES * sizeof(glm::vec4), nullptr, GL_DYNAMIC_DRAW); // vec4: xyz + size
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), (void*)0);
    glBindVertexArray(0);

    // load model and animations (use your five Mixamo .dae files)
    Model ourModel(FileSystem::getPath("resources/objects/vampire/idle.dae")); // skeleton / mesh source
    Animation idleAnim(FileSystem::getPath("resources/objects/vampire/idle.dae"), &ourModel);
    Animation walkAnim(FileSystem::getPath("resources/objects/vampire/walk.dae"), &ourModel);
    Animation runAnim(FileSystem::getPath("resources/objects/vampire/walk.dae"), &ourModel);
    Animation jumpAnim(FileSystem::getPath("resources/objects/vampire/jump.dae"), &ourModel);
    Animation punchAnim(FileSystem::getPath("resources/objects/vampire/punch.dae"), &ourModel);

    // two animators (A = current, B = target for crossfade)
    Animator animatorA(&idleAnim);
    Animator animatorB(&idleAnim);

    // start state
    MovementState prevMovement = MS_IDLE;
    actionState = AS_NONE;
    isBlending = false;

    // model transform
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(0.0f, -0.4f, 0.0f));
    model = glm::scale(model, glm::vec3(.5f, .5f, .5f));

    // main loop
    while (!glfwWindowShouldClose(window))
    {
        // time
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // input
        processInput(window);

        // --- Jump physics update (added) ---
        if (isInAir) {
            verticalVelocity += gravity * deltaTime;
            playerPosition.y += verticalVelocity * deltaTime;
            if (playerPosition.y <= groundY) {
                // landed
                playerPosition.y = groundY;
                isInAir = false;
                verticalVelocity = 0.0f;

                // finish jump: if jump action was active, queue movement animation and blend back
                if (actionState == AS_JUMP) {
                    actionState = AS_NONE;
                    Animation* moveAnim = &idleAnim;
                    if (movementState == MS_WALK) moveAnim = &walkAnim;
                    else if (movementState == MS_RUN) moveAnim = &runAnim;
                    animatorB = Animator(moveAnim);
                    animatorB.UpdateAnimation(0.0f);
                    blendTimer = 0.0f;
                    isBlending = true;
                }
            }
        }
        // -----------------------------------

        // update FSM: determine movement state from keys
        bool pressingW = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
        bool pressingS = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
        bool shiftDown = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

        MovementState desiredMovement = MS_IDLE;
        if (pressingW || pressingS) desiredMovement = shiftDown ? MS_WALK : MS_RUN;

        // handle action triggers (one-shot override)
        static bool spaceDebounced = false;
        static bool jDebounced = false;
        static bool kDebounced = false;
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && !spaceDebounced && !isInAir) {
            // start jump: physics + animation
            isInAir = true;
            verticalVelocity = jumpInitialVelocity;

            actionState = AS_JUMP;
            spaceDebounced = true;
            // kick off jump animation in animatorB
            animatorB = Animator(&jumpAnim);
            animatorB.UpdateAnimation(0.0f); // initialize
            blendTimer = 0.0f;
            isBlending = true;
        }
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_RELEASE) spaceDebounced = false;

        if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS && !jDebounced) {
            actionState = AS_PUNCH;
            jDebounced = true;
            animatorB = Animator(&punchAnim);
            animatorB.UpdateAnimation(0.0f);
            blendTimer = 0.0f;
            isBlending = true;
        }
        if (glfwGetKey(window, GLFW_KEY_J) == GLFW_RELEASE) jDebounced = false;

        if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS && !kDebounced) {
            actionState = AS_KICK;
            kDebounced = true;
            // fall back to punch if no separate kick anim
            animatorB = Animator(&punchAnim);
            animatorB.UpdateAnimation(0.0f);
            blendTimer = 0.0f;
            isBlending = true;
        }
        if (glfwGetKey(window, GLFW_KEY_K) == GLFW_RELEASE) kDebounced = false;

        // if no action lock then allow movement
        bool movementAllowed = (actionState == AS_NONE);

        // rotation (A/D) - turn left / right
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) playerYaw += 90.0f * deltaTime;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) playerYaw -= 90.0f * deltaTime;

        // forward/back movement (only if allowed)
        if (movementAllowed && (pressingW || pressingS)) {
            float speed = (desiredMovement == MS_WALK ? movementSpeedWalk : movementSpeedRun);
            glm::vec3 forwardDir = glm::normalize(glm::vec3(glm::sin(glm::radians(playerYaw)), 0.0f, glm::cos(glm::radians(playerYaw))));
            if (pressingW) playerPosition += forwardDir * speed * deltaTime;
            if (pressingS) playerPosition -= forwardDir * speed * deltaTime;
        }

        // update movement state and crossfade to animation if changed (but skip when an action is active)
        if (actionState == AS_NONE) {
            if (desiredMovement != movementState) {
                // choose animation
                Animation* nextAnim = &idleAnim;
                if (desiredMovement == MS_WALK) nextAnim = &walkAnim;
                else if (desiredMovement == MS_RUN) nextAnim = &runAnim;
                animatorB = Animator(nextAnim);
                animatorB.UpdateAnimation(0.0f);
                blendTimer = 0.0f;
                isBlending = true;
                movementState = desiredMovement;
            }
        }

        // update animators
        animatorA.UpdateAnimation(deltaTime);
        animatorB.UpdateAnimation(deltaTime);

        // handle blending timer
        if (isBlending) {
            blendTimer += deltaTime;
            float t = glm::clamp(blendTimer / blendDuration, 0.0f, 1.0f);

            // get matrices from both animators and blend them
            auto Atrans = animatorA.GetFinalBoneMatrices();
            auto Btrans = animatorB.GetFinalBoneMatrices();
            std::vector<glm::mat4> blended;
            blended.resize(std::max(Atrans.size(), Btrans.size()));
            for (size_t i = 0; i < blended.size(); ++i) {
                glm::mat4 a = i < Atrans.size() ? Atrans[i] : glm::mat4(1.0f);
                glm::mat4 b = i < Btrans.size() ? Btrans[i] : glm::mat4(1.0f);
                blended[i] = BlendMatrices(a, b, t);
            }

            // send blended to shader
            animShader.use();
            for (int i = 0; i < (int)blended.size(); ++i)
                animShader.setMat4("finalBonesMatrices[" + std::to_string(i) + "]", blended[i]);

            if (t >= 1.0f) {
                // switch animatorA to animatorB's animation (adopt B fully)
                animatorA = animatorB;
                isBlending = false;

                // if we just finished a one-shot action, return to movement animation
                // IMPORTANT: for jump we wait until landing (handled in physics). For other actions we return immediately.
                if (actionState != AS_NONE && actionState != AS_JUMP) {
                    actionState = AS_NONE;
                    Animation* moveAnim = &idleAnim;
                    if (movementState == MS_WALK) moveAnim = &walkAnim;
                    else if (movementState == MS_RUN) moveAnim = &runAnim;
                    animatorB = Animator(moveAnim);
                    animatorB.UpdateAnimation(0.0f);
                    blendTimer = 0.0f;
                    isBlending = true;
                }
            }
        }
        else {
            // not blending - send animatorA matrices
            auto transforms = animatorA.GetFinalBoneMatrices();
            animShader.use();
            for (int i = 0; i < (int)transforms.size(); ++i)
                animShader.setMat4("finalBonesMatrices[" + std::to_string(i) + "]", transforms[i]);
        }

        // spawn footstep dust: simple cadence based on movement state
        if (movementAllowed && (movementState == MS_WALK || movementState == MS_RUN)) {
            stepTimer += deltaTime;
            float cadence = (movementState == MS_WALK ? walkCadence : runCadence);
            if (stepTimer >= cadence) {
                stepTimer = 0.0f;
                // spawn two particles (left/right) approximated at player's feet
                for (int i = 0; i < 2; ++i) {
                    if (particles.size() >= (size_t)MAX_PARTICLES) break;
                    glm::vec3 ofs = glm::vec3((i == 0 ? -0.12f : 0.12f), 0.02f, 0.0f);
                    // rotate offset by player yaw
                    float s = glm::sin(glm::radians(playerYaw));
                    float c = glm::cos(glm::radians(playerYaw));
                    glm::vec3 rotated = glm::vec3(ofs.x * c - ofs.z * s, ofs.y, ofs.x * s + ofs.z * c);
                    Particle p;
                    p.pos = playerPosition + rotated;
                    p.life = 1.0f;
                    p.size = (movementState == MS_RUN ? 0.6f : 0.35f);
                    particles.push_back(p);
                }
            }
        }
        else {
            stepTimer = 0.0f;
        }

        // update particles
        for (auto& p : particles) {
            p.life -= deltaTime * 0.8f;
            p.size += deltaTime * 0.5f;
        }
        particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.life <= 0.0f; }), particles.end());

        // render
        glClearColor(0.06f, 0.07f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // compute orbit camera
        glm::vec3 camPos;
        {
            float yawr = glm::radians(camYaw);
            float pitchr = glm::radians(camPitch);
            camPos.x = camTarget.x + camDistance * glm::cos(pitchr) * glm::cos(yawr);
            camPos.y = camTarget.y + camDistance * glm::sin(pitchr);
            camPos.z = camTarget.z + camDistance * glm::cos(pitchr) * glm::sin(yawr);
        }
        glm::mat4 view = glm::lookAt(camPos, camTarget, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 200.0f);

        // infinite grid - draw full-screen quad first (background)
        gridShader.use();
        gridShader.setMat4("invProjection", glm::inverse(projection));
        gridShader.setMat4("invView", glm::inverse(view));
        gridShader.setVec3("camPos", camPos);
        glBindVertexArray(quadVAO);
        glDisable(GL_DEPTH_TEST);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glEnable(GL_DEPTH_TEST);

        // character model (apply yaw/position)
        glm::mat4 modelCharacter = glm::mat4(1.0f);
        modelCharacter = glm::translate(modelCharacter, glm::vec3(playerPosition.x, playerPosition.y - 0.4f, playerPosition.z));
        modelCharacter = glm::rotate(modelCharacter, glm::radians(playerYaw), glm::vec3(0.0f, 1.0f, 0.0f));
        modelCharacter = glm::scale(modelCharacter, glm::vec3(.5f, .5f, .5f));

        animShader.use();
        animShader.setMat4("projection", projection);
        animShader.setMat4("view", view);
        animShader.setMat4("model", modelCharacter);

        // draw model (it uses finalBonesMatrices uploaded earlier)
        ourModel.Draw(animShader);

        // draw particles (point sprites)
        if (!particles.empty()) {
            // upload positions (vec4: xyz + size)
            std::vector<glm::vec4> buf;
            buf.reserve(particles.size());
            for (auto& p : particles) buf.emplace_back(glm::vec4(p.pos, p.size * 100.0f * p.life)); // size scaled for gl_PointSize in shader
            glBindBuffer(GL_ARRAY_BUFFER, particleVBO);
            glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(glm::vec4), buf.data(), GL_DYNAMIC_DRAW);

            glEnable(GL_PROGRAM_POINT_SIZE);
            particleShader.use();
            particleShader.setMat4("projection", projection);
            particleShader.setMat4("view", view);
            particleShader.setFloat("time", currentFrame);

            glBindVertexArray(particleVAO);
            glEnable(GL_BLEND);
            glDepthMask(GL_FALSE); // particles fade out
            glDrawArrays(GL_POINTS, 0, (GLsizei)buf.size());
            glDepthMask(GL_TRUE);
            glDisable(GL_PROGRAM_POINT_SIZE);
        }

        // update window title
        std::string state = (movementState == MS_IDLE ? "Idle" : (movementState == MS_WALK ? "Walk" : "Run"));
        if (actionState != AS_NONE) state += " +Action";
        std::string mode = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? "WALK" : "RUN");
        std::string title = "Skeletal Controller - " + state + " (" + mode + ")";
        glfwSetWindowTitle(window, title.c_str());

        // swap
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // cleanup
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
    glDeleteVertexArrays(1, &particleVAO);
    glDeleteBuffers(1, &particleVBO);

    glfwTerminate();
    return 0;
}

// input handling
void processInput(GLFWwindow* window)
{
    // ESC: Quit
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    // Other inputs are handled per-frame where needed
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    (void)window;
    glViewport(0, 0, width, height);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    (void)mods;
    // Start orbit drag on left OR right mouse button press; release when all are up.
    if (button == GLFW_MOUSE_BUTTON_RIGHT || button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            mouseDragDown = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            firstMouse = true;
        }
        else if (action == GLFW_RELEASE) {
            mouseDragDown = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            firstMouse = true;
        }
    }
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    (void)window;
    if (!mouseDragDown) return;

    if (firstMouse) {
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
    }

    float xoffset = (float)xpos - lastX;
    float yoffset = lastY - (float)ypos;
    lastX = (float)xpos;
    lastY = (float)ypos;

    float sensitivity = 0.15f;
    camYaw += xoffset * sensitivity;
    camPitch += yoffset * sensitivity;
    camPitch = glm::clamp(camPitch, -89.0f, 89.0f);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    (void)window; (void)xoffset;
    camDistance -= (float)yoffset * 0.4f;
    camDistance = glm::clamp(camDistance, 1.5f, 25.0f);
}