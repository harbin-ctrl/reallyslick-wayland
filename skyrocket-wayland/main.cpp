#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "driver.h"

int strtol_minmaxdef(const char *optarg, const int base, const int min, const int max, const int type, const int def, const char *errmsg) {
    if (!optarg) return def;
    int val = strtol(optarg, NULL, base);
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

int main(int argc, char **argv) {
    bool benchmark = false;
    bool fullscreen = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--benchmark") == 0) benchmark = true;
        if (strcmp(argv[i], "--fullscreen") == 0) fullscreen = true;
    }

    SDL_SetHint(SDL_HINT_VIDEODRIVER, "wayland,x11");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);

    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (fullscreen) {
        windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    SDL_Window *window = SDL_CreateWindow(
        "Skyrocket",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        800, 600,
        windowFlags
    );

    if (!window) {
        fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        fprintf(stderr, "Failed to create GL context: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (benchmark) {
        SDL_GL_SetSwapInterval(0); // Disable VSync for benchmarking
    }

    xstuff_t xstuff;
    int w, h;
    SDL_GL_GetDrawableSize(window, &w, &h);
    xstuff.windowWidth = w;
    xstuff.windowHeight = h;

    char *fake_argv[] = { (char*)"skyrocket", NULL };
    hack_handle_opts(1, fake_argv);

    hack_init(&xstuff);

    // Setup accumulation texture for double buffering fix
    GLuint accumTex = 0;
    glGenTextures(1, &accumTex);
    glBindTexture(GL_TEXTURE_2D, accumTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    bool texValid = false;

    bool running = true;
    bool useAccumBuffer = false;
    SDL_Event event;
    Uint32 lastTime = SDL_GetTicks();
    double currentTime = 0.0;
    
    int frameCount = 0;
    Uint32 startTime = SDL_GetTicks();

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q) {
                    running = false;
                } else if (event.key.keysym.sym == SDLK_f || event.key.keysym.sym == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                }
            } else if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                    event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    SDL_GL_GetDrawableSize(window, &w, &h);
                    xstuff.windowWidth = w;
                    xstuff.windowHeight = h;
                    hack_reshape(&xstuff);
                    
                    glBindTexture(GL_TEXTURE_2D, accumTex);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
                    texValid = false;
                }
            }
        }

        Uint32 now = SDL_GetTicks();
        float frameTime = (now - lastTime) / 1000.0f;
        currentTime += frameTime;
        lastTime = now;

        glClear(GL_COLOR_BUFFER_BIT);

        // hack_draw applies blur quad and draws new particles
        hack_draw(&xstuff, currentTime, frameTime);

        SDL_GL_SwapWindow(window);
        
        if (!benchmark) {
            SDL_Delay(16); // limit fps
        } else {
            frameCount++;
            if (frameCount >= 1000) {
                Uint32 endTime = SDL_GetTicks();
                float totalSeconds = (endTime - startTime) / 1000.0f;
                printf("Benchmark completed: 1000 frames in %.2f seconds (%.2f FPS)\n", totalSeconds, 1000.0f / totalSeconds);
                running = false;
            }
        }
    }

    hack_cleanup(&xstuff);
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
