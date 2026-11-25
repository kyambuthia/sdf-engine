#include "camera.hpp"
#include "shader.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <string>

struct AppState {
    SDL_Window* window = nullptr;
    SDL_GLContext glContext = nullptr;
    ShaderProgram shader;
    GLuint vao = 0;

    OrbitCamera camera;
    int width = 1280;
    int height = 720;
    int sceneIndex = 0;
    bool paused = false;
    bool dragging = false;
    bool running = true;

    double simTime = 0.0;
    Uint64 lastTicks = 0;

    // Cached uniform locations
    GLint uResolution = -1;
    GLint uTime = -1;
    GLint uScene = -1;
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
}

static void cacheUniforms(AppState& app) {
    app.uResolution = app.shader.uniformLocation("uResolution");
    app.uTime = app.shader.uniformLocation("uTime");
    app.uScene = app.shader.uniformLocation("uScene");
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
                glViewport(0, 0, app.width, app.height);
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
        app->simTime += dt;
    }

    SDL_GetWindowSizeInPixels(app->window, &app->width, &app->height);
    glViewport(0, 0, app->width, app->height);

    float pos[3], forward[3], right[3], up[3];
    app->camera.computeBasis(pos, forward, right, up);

    app->shader.use();
    glUniform2f(app->uResolution, static_cast<float>(app->width), static_cast<float>(app->height));
    glUniform1f(app->uTime, static_cast<float>(app->simTime));
    glUniform1i(app->uScene, app->sceneIndex);
    glUniform3fv(app->uCamPos, 1, pos);
    glUniform3fv(app->uCamForward, 1, forward);
    glUniform3fv(app->uCamRight, 1, right);
    glUniform3fv(app->uCamUp, 1, up);

    glBindVertexArray(app->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
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

    if (!app.shader.loadFromFiles(shaderDir + "/fullscreen.vert", shaderDir + "/sdf_raymarch.frag")) {
        SDL_GL_DestroyContext(app.glContext);
        SDL_DestroyWindow(app.window);
        SDL_Quit();
        return 1;
    }

    cacheUniforms(app);
    glGenVertexArrays(1, &app.vao);
    app.lastTicks = SDL_GetTicks();

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(mainLoop, &app, 0, true);
#else
    while (app.running) {
        mainLoop(&app);
    }
#endif

    glDeleteVertexArrays(1, &app.vao);
    app.shader.destroy();
    SDL_GL_DestroyContext(app.glContext);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}
