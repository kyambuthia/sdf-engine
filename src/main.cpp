#include "camera.hpp"
#include "scene.hpp"
#include "shader.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <cstdio>
#include <string>

struct AppState {
    SDL_Window* window = nullptr;
    SDL_GLContext glContext = nullptr;
    ShaderProgram shader;
    GLuint vao = 0;
    GLuint sceneUbo = 0;
    SdfScene scene;

    // Offscreen FBO for dynamic resolution scaling
    GLuint fbo = 0;
    GLuint fboTex = 0;
    int fboWidth = 0;
    int fboHeight = 0;
    float resScale = 0.75f;
    int quality = 1; // 0 = Fast, 1 = High
    float blendK = 0.45f;
    float animSpeed = 1.0f;
    float fogDensity = 0.0018f;
    float shadowStrength = 1.0f;
    int opOverride = -1;

    OrbitCamera camera;
    int width = 1280;
    int height = 720;
    int sceneIndex = 0;
    bool paused = false;
    bool dragging = false;
    bool running = true;

    double simTime = 0.0;
    Uint64 lastTicks = 0;

    // FPS tracking
    double fpsAccum = 0.0;
    int fpsFrames = 0;
    double currentFps = 0.0;

    // Cached uniform locations
    GLint uResolution = -1;
    GLint uTime = -1;
    GLint uScene = -1;
    GLint uQuality = -1;
    GLint uFogDensity = -1;
    GLint uShadowStrength = -1;
    GLint uCamPos = -1;
    GLint uCamForward = -1;
    GLint uCamRight = -1;
    GLint uCamUp = -1;
};

static AppState* gApp = nullptr;

extern "C" {
void sdf_set_scene(int scene) {
    if (gApp && scene >= 0 && scene <= 3) {
        gApp->sceneIndex = scene;
    }
}

void sdf_toggle_pause() {
    if (gApp) {
        gApp->paused = !gApp->paused;
    }
}

void sdf_reset_camera() {
    if (gApp) {
        gApp->camera.reset();
    }
}

void sdf_set_res_scale(float scale) {
    if (gApp) {
        gApp->resScale = std::clamp(scale, 0.25f, 1.0f);
    }
}

void sdf_set_quality(int q) {
    if (gApp) {
        gApp->quality = (q > 0) ? 1 : 0;
    }
}

float sdf_get_fps() {
    return gApp ? static_cast<float>(gApp->currentFps) : 0.0f;
}

void sdf_set_blend(float k) {
    if (gApp) gApp->blendK = std::clamp(k, 0.01f, 1.5f);
}

void sdf_set_speed(float sp) {
    if (gApp) gApp->animSpeed = std::clamp(sp, 0.0f, 4.0f);
}

void sdf_set_fog(float f) {
    if (gApp) gApp->fogDensity = std::clamp(f, 0.0f, 0.02f);
}

void sdf_set_shadow(float sh) {
    if (gApp) gApp->shadowStrength = std::clamp(sh, 0.0f, 1.0f);
}

void sdf_set_op_override(int op) {
    if (gApp) gApp->opOverride = std::clamp(op, -1, 3);
}
}

static void ensureFBO(AppState& app, int targetW, int targetH) {
    if (targetW < 1) targetW = 1;
    if (targetH < 1) targetH = 1;
    if (app.fbo != 0 && app.fboWidth == targetW && app.fboHeight == targetH) {
        return;
    }

    if (app.fboTex != 0) glDeleteTextures(1, &app.fboTex);
    if (app.fbo != 0) glDeleteFramebuffers(1, &app.fbo);

    glGenFramebuffers(1, &app.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, app.fbo);

    glGenTextures(1, &app.fboTex);
    glBindTexture(GL_TEXTURE_2D, app.fboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, targetW, targetH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, app.fboTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    app.fboWidth = targetW;
    app.fboHeight = targetH;
}

static void cacheUniforms(AppState& app) {
    app.uResolution = app.shader.uniformLocation("uResolution");
    app.uTime = app.shader.uniformLocation("uTime");
    app.uScene = app.shader.uniformLocation("uScene");
    app.uQuality = app.shader.uniformLocation("uQuality");
    app.uFogDensity = app.shader.uniformLocation("uFogDensity");
    app.uShadowStrength = app.shader.uniformLocation("uShadowStrength");
    app.uCamPos = app.shader.uniformLocation("uCamPos");
    app.uCamForward = app.shader.uniformLocation("uCamForward");
    app.uCamRight = app.shader.uniformLocation("uCamRight");
    app.uCamUp = app.shader.uniformLocation("uCamUp");
}

static void handleEvents(AppState& app) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                app.running = false;
                break;

            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                SDL_GetWindowSizeInPixels(app.window, &app.width, &app.height);
                break;

            case SDL_EVENT_KEY_DOWN:
                switch (event.key.key) {
                    case SDLK_1: app.sceneIndex = 0; break;
                    case SDLK_2: app.sceneIndex = 1; break;
                    case SDLK_3: app.sceneIndex = 2; break;
                    case SDLK_4: app.sceneIndex = 3; break;
                    case SDLK_SPACE: app.paused = !app.paused; break;
                    case SDLK_R: app.camera.reset(); break;
                    case SDLK_EQUALS:
                    case SDLK_PLUS:
                        app.camera.zoom(1.0f);
                        break;
                    case SDLK_MINUS:
                        app.camera.zoom(-1.0f);
                        break;
                    case SDLK_ESCAPE:
                        app.running = false;
                        break;
                    default:
                        break;
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    app.dragging = true;
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    app.dragging = false;
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if (app.dragging) {
                    app.camera.orbit(event.motion.xrel, event.motion.yrel);
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                app.camera.zoom(event.wheel.y);
                break;

            case SDL_EVENT_FINGER_MOTION:
                app.camera.orbit(event.tfinger.dx * static_cast<float>(app.width),
                                 event.tfinger.dy * static_cast<float>(app.height));
                break;

            default:
                break;
        }
    }
}

static void mainLoop(void* userdata) {
    auto* app = static_cast<AppState*>(userdata);

    handleEvents(*app);
    if (!app->running) {
#ifdef __EMSCRIPTEN__
        emscripten_cancel_main_loop();
#endif
        return;
    }

    const Uint64 now = SDL_GetTicks();
    const double dt = static_cast<double>(now - app->lastTicks) * 0.001;
    app->lastTicks = now;
    if (!app->paused) {
        app->simTime += dt * static_cast<double>(app->animSpeed);
    }

    app->fpsAccum += dt;
    app->fpsFrames++;
    if (app->fpsAccum >= 0.5) {
        app->currentFps = static_cast<double>(app->fpsFrames) / app->fpsAccum;
        app->fpsAccum = 0.0;
        app->fpsFrames = 0;
#ifndef __EMSCRIPTEN__
        char title[128];
        std::snprintf(title, sizeof(title), "SDF Engine — %.1f FPS", app->currentFps);
        SDL_SetWindowTitle(app->window, title);
#endif
    }

    SDL_GetWindowSizeInPixels(app->window, &app->width, &app->height);
    const int renderW = std::max(1, static_cast<int>(app->width * app->resScale));
    const int renderH = std::max(1, static_cast<int>(app->height * app->resScale));

    ensureFBO(*app, renderW, renderH);

    glBindFramebuffer(GL_FRAMEBUFFER, app->fbo);
    glViewport(0, 0, renderW, renderH);

    float pos[3], forward[3], right[3], up[3];
    app->camera.computeBasis(pos, forward, right, up);

    app->shader.use();
    glUniform2f(app->uResolution, static_cast<float>(renderW), static_cast<float>(renderH));
    glUniform1f(app->uTime, static_cast<float>(app->simTime));
    glUniform1i(app->uScene, app->sceneIndex);
    glUniform1i(app->uQuality, app->quality);
    glUniform1f(app->uFogDensity, app->fogDensity);
    glUniform1f(app->uShadowStrength, app->shadowStrength);
    glUniform3fv(app->uCamPos, 1, pos);
    glUniform3fv(app->uCamForward, 1, forward);
    glUniform3fv(app->uCamRight, 1, right);
    glUniform3fv(app->uCamUp, 1, up);

    glBindVertexArray(app->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    // Blit offscreen FBO to screen
    glBindFramebuffer(GL_READ_FRAMEBUFFER, app->fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, renderW, renderH, 0, 0, app->width, app->height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    SDL_GL_SwapWindow(app->window);
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    AppState app;
    gApp = &app;

    app.window = SDL_CreateWindow(
        "SDF Engine (SDL3 + OpenGL ES 3 / WebGL2)",
        app.width,
        app.height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    if (!app.window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    app.glContext = SDL_GL_CreateContext(app.window);
    if (!app.glContext) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(app.window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_SetSwapInterval(1);
    SDL_GetWindowSizeInPixels(app.window, &app.width, &app.height);

#ifdef __EMSCRIPTEN__
    const std::string shaderDir = "/shaders";
#else
    const std::string shaderDir = "shaders";
#endif

    if (!app.shader.loadFromFiles(shaderDir + "/fullscreen.vert", shaderDir + "/sdf_raymarch.frag")) {
        SDL_GL_DestroyContext(app.glContext);
        SDL_DestroyWindow(app.window);
        SDL_Quit();
        return 1;
    }

    cacheUniforms(app);
    GLuint blockIdx = glGetUniformBlockIndex(app.shader.id, "SceneBlock");
    if (blockIdx != GL_INVALID_INDEX) {
        glUniformBlockBinding(app.shader.id, blockIdx, 0);
    }
    glGenBuffers(1, &app.sceneUbo);
    glBindBuffer(GL_UNIFORM_BUFFER, app.sceneUbo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(SceneUniformData), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, app.sceneUbo);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glGenVertexArrays(1, &app.vao);
    app.lastTicks = SDL_GetTicks();

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(mainLoop, &app, 0, true);
#else
    while (app.running) {
        mainLoop(&app);
    }
#endif

    if (app.fboTex != 0) glDeleteTextures(1, &app.fboTex);
    if (app.fbo != 0) glDeleteFramebuffers(1, &app.fbo);
    if (app.sceneUbo != 0) glDeleteBuffers(1, &app.sceneUbo);
    glDeleteVertexArrays(1, &app.vao);
    app.shader.destroy();
    SDL_GL_DestroyContext(app.glContext);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}
