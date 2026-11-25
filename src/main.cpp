#include "camera.hpp"
#include "scene.hpp"
#include "shader.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

struct AppState {
    SDL_Window* window = nullptr;
    SDL_GLContext glContext = nullptr;
    ShaderProgram shader;
    ShaderProgram overlay2d;
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

    bool show2dOverlay = true;
    bool customEditMode = false;
    int selectedNode = -1;

    OrbitCamera camera;
    int width = 1280;
    int height = 720;
    int sceneIndex = 0;
    bool paused = false;
    bool dragging = false;
    float downX = 0.0f;
    float downY = 0.0f;
    float dragDist = 0.0f;
    bool running = true;

    double simTime = 0.0;
    Uint64 lastTicks = 0;

    // FPS tracking
    double fpsAccum = 0.0;
    int fpsFrames = 0;
    double currentFps = 0.0;

    // Cached uniform locations (3D)
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

    // Cached uniform locations (2D overlay)
    GLint u2dResolution = -1;
    GLint u2dTime = -1;
    GLint u2dHasSelection = -1;
};

static AppState* gApp = nullptr;

extern "C" {
void sdf_set_scene(int scene) {
    if (gApp && scene >= 0 && scene <= 3) {
        gApp->sceneIndex = scene;
        gApp->customEditMode = false;
        gApp->selectedNode = -1;
    }
}

void sdf_toggle_pause() {
    if (gApp) gApp->paused = !gApp->paused;
}

void sdf_reset_camera() {
    if (gApp) {
        gApp->camera.reset();
        gApp->customEditMode = false;
        gApp->selectedNode = -1;
    }
}

void sdf_set_res_scale(float scale) {
    if (gApp) gApp->resScale = std::clamp(scale, 0.25f, 1.0f);
}

void sdf_set_quality(int q) {
    if (gApp) gApp->quality = (q > 0) ? 1 : 0;
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

void sdf_toggle_2d_overlay() {
    if (gApp) gApp->show2dOverlay = !gApp->show2dOverlay;
}

int sdf_get_selected_node() {
    return gApp ? gApp->selectedNode : -1;
}

int sdf_get_node_count() {
    return gApp ? gApp->scene.nodeCount() : 0;
}

float sdf_get_node_prop(int idx, int prop) {
    if (!gApp) return 0.0f;
    const SdfNodeGpu* n = gApp->scene.nodeAt(idx);
    if (!n) return 0.0f;
    if (prop >= 0 && prop <= 2) return n->pos[prop];
    if (prop >= 3 && prop <= 5) return n->size[prop - 3];
    if (prop == 6) return n->type;
    if (prop == 7) return n->op;
    if (prop == 8) return n->blend;
    return 0.0f;
}

void sdf_set_node_pos(int idx, float x, float y, float z) {
    if (!gApp) return;
    gApp->customEditMode = true;
    if (SdfNodeGpu* n = gApp->scene.nodeAt(idx)) {
        n->pos[0] = x; n->pos[1] = y; n->pos[2] = z;
        gApp->scene.recomputeBounds();
    }
}

void sdf_set_node_size(int idx, float sx, float sy, float sz) {
    if (!gApp) return;
    gApp->customEditMode = true;
    if (SdfNodeGpu* n = gApp->scene.nodeAt(idx)) {
        n->size[0] = std::max(0.05f, sx);
        n->size[1] = std::max(0.05f, sy);
        n->size[2] = std::max(0.05f, sz);
        gApp->scene.recomputeBounds();
    }
}

void sdf_set_node_op(int idx, int op) {
    if (!gApp) return;
    gApp->customEditMode = true;
    if (SdfNodeGpu* n = gApp->scene.nodeAt(idx)) {
        n->op = static_cast<float>(std::clamp(op, 0, 3));
        if (op == static_cast<int>(SdfOp::SmoothUnion) && n->blend < 0.05f) {
            n->blend = gApp->blendK;
        }
    }
}

void sdf_add_node(int type) {
    if (!gApp) return;
    gApp->customEditMode = true;
    SdfNodeGpu node{};
    node.pos[0] = 0.0f; node.pos[1] = 0.5f; node.pos[2] = 0.0f;
    node.type = static_cast<float>(std::clamp(type, 0, 3));
    node.size[0] = 0.45f; node.size[1] = 0.22f; node.size[2] = 0.45f;
    node.op = static_cast<float>(SdfOp::SmoothUnion);
    node.color[0] = 0.92f; node.color[1] = 0.72f; node.color[2] = 0.28f;
    node.blend = gApp->blendK;
    gApp->scene.addNode(node);
    gApp->scene.recomputeBounds();
    gApp->selectedNode = gApp->scene.nodeCount() - 1;
}

void sdf_remove_node(int idx) {
    if (!gApp) return;
    gApp->customEditMode = true;
    gApp->scene.removeNode(idx);
    if (gApp->selectedNode >= gApp->scene.nodeCount()) {
        gApp->selectedNode = gApp->scene.nodeCount() - 1;
    }
}
}

static void pickAtScreenPos(AppState& app, float winX, float winY) {
    int winW = 1, winH = 1;
    SDL_GetWindowSize(app.window, &winW, &winH);
    if (winW <= 0 || winH <= 0) return;

    // Match fragment shader UV: (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y
    // Note: SDL mouse Y is top-down, GL gl_FragCoord.y is bottom-up.
    float uvX = (winX - 0.5f * static_cast<float>(winW)) / static_cast<float>(winH);
    float uvY = ((static_cast<float>(winH) - winY) - 0.5f * static_cast<float>(winH)) / static_cast<float>(winH);

    float pos[3], forward[3], right[3], up[3];
    app.camera.computeBasis(pos, forward, right, up);

    const float focalLength = 1.65f;
    float rd[3] = {
        right[0] * uvX + up[0] * uvY + forward[0] * focalLength,
        right[1] * uvX + up[1] * uvY + forward[1] * focalLength,
        right[2] * uvX + up[2] * uvY + forward[2] * focalLength
    };
    float len = std::sqrt(rd[0] * rd[0] + rd[1] * rd[1] + rd[2] * rd[2]);
    if (len > 1e-6f) {
        rd[0] /= len; rd[1] /= len; rd[2] /= len;
    }
    app.selectedNode = app.scene.pickNode(pos, rd);
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

    app.u2dResolution = app.overlay2d.uniformLocation("uResolution");
    app.u2dTime = app.overlay2d.uniformLocation("uTime");
    app.u2dHasSelection = app.overlay2d.uniformLocation("uHasSelection");
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
                    case SDLK_1: sdf_set_scene(0); break;
                    case SDLK_2: sdf_set_scene(1); break;
                    case SDLK_3: sdf_set_scene(2); break;
                    case SDLK_4: sdf_set_scene(3); break;
                    case SDLK_SPACE: app.paused = !app.paused; break;
                    case SDLK_R: sdf_reset_camera(); break;
                    case SDLK_V: app.show2dOverlay = !app.show2dOverlay; break;
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
                    app.downX = event.button.x;
                    app.downY = event.button.y;
                    app.dragDist = 0.0f;
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    app.dragging = false;
                    if (app.dragDist < 5.0f) {
                        pickAtScreenPos(app, event.button.x, event.button.y);
                    }
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if (app.dragging) {
                    app.dragDist += std::abs(event.motion.xrel) + std::abs(event.motion.yrel);
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

    if (!app->customEditMode) {
        app->scene.buildPreset(app->sceneIndex, static_cast<float>(app->simTime), app->blendK, app->opOverride);
    }
    glBindBuffer(GL_UNIFORM_BUFFER, app->sceneUbo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(SceneUniformData), &app->scene.gpuData());
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

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

    // Blit offscreen 3D FBO to default framebuffer
    glBindFramebuffer(GL_READ_FRAMEBUFFER, app->fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, renderW, renderH, 0, 0, app->width, app->height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Crisp native-res 2D SDF overlay pass
    if (app->show2dOverlay) {
        glViewport(0, 0, app->width, app->height);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        app->overlay2d.use();
        glUniform2f(app->u2dResolution, static_cast<float>(app->width), static_cast<float>(app->height));
        glUniform1f(app->u2dTime, static_cast<float>(app->simTime));
        glUniform1i(app->u2dHasSelection, (app->selectedNode >= 0) ? 1 : 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDisable(GL_BLEND);
    }

    glBindVertexArray(0);
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

    if (!app.shader.loadFromFiles(shaderDir + "/fullscreen.vert", shaderDir + "/sdf_raymarch.frag") ||
        !app.overlay2d.loadFromFiles(shaderDir + "/fullscreen.vert", shaderDir + "/sdf_2d.frag")) {
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
    app.overlay2d.destroy();
    app.shader.destroy();
    SDL_GL_DestroyContext(app.glContext);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}
