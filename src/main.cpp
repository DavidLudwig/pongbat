//
//  main.cpp
//  pongbat
//
//  Created by David Ludwig <dludwig@pobox.com> on 7/4/15, and licensed into
//  the public domain.  See the 'unlicense' at this bottom of this file
//  for details.
//

#pragma mark - Main Headers

#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#if __MACOSX__
#include <unistd.h>     // for chdir(), ...
#endif


#pragma mark - Images
// Images
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
static const uint32_t ImageRMask = 0xff000000;
static const uint32_t ImageGMask = 0x00ff0000;
static const uint32_t ImageBMask = 0x0000ff00;
static const uint32_t ImageAMask = 0x000000ff;
#else
static const uint32_t ImageRMask = 0x000000ff;
static const uint32_t ImageGMask = 0x0000ff00;
static const uint32_t ImageBMask = 0x00ff0000;
static const uint32_t ImageAMask = 0xff000000;
#endif

typedef uint8_t ImageID;
static SDL_Surface * Images[64];
static ImageID ImageNext = 1;
static ImageID ImageIDBallBlue;
static ImageID ImageIDBallNoPlayer;
static ImageID ImageIDBallRed;

ImageID LoadImage(const char * filename)
{
    int w, h, n;
    void * data = stbi_load(filename, &w, &h, &n, 4);
    if ( ! data) {
        SDL_Log("%s, stbi_load failed for %s, \"%s\"",
                __FUNCTION__,
                (filename ? filename : NULL),
                stbi_failure_reason());
        return 0;
    }
    
    if (ImageNext > (SDL_arraysize(Images) - 1)) {
        SDL_Log("%s, Out of ImageIDs!", __FUNCTION__);
        return 0;
    }

    Images[ImageNext] = SDL_CreateRGBSurfaceFrom(data, w, h, 32, w * 4, ImageRMask, ImageGMask, ImageBMask, ImageAMask);
    if ( ! Images[ImageNext]) {
        SDL_Log("%s, SDL_CreateRGBSurfaceFrom failed: %s",
                __FUNCTION__,
                SDL_GetError());
        return 0;
    }
    
    return ImageNext++;
}


#pragma mark - Math
// Math
int MathRound(float x)
{
    return (x + 0.5f);
}

void RectSet(SDL_Rect * r, int x, int y, int w, int h)
{
    r->x = x;
    r->y = y;
    r->w = w;
    r->h = h;
}


#pragma mark - App
// App
static const uint16_t DefaultWindowWidth = 640;
static const uint16_t DefaultWindowHeight = 480;
static SDL_Window * Window = 0;
static SDL_Renderer * Renderer = 0;
static SDL_Surface * Screen = 0;
static SDL_Texture * ScreenTexture = 0;
static const uint16_t ScreenWidth = 640;
static const uint16_t ScreenHeight = 480;
static uint8_t AppRunning = 1;
static uint32_t NextGameTickAt = 0;


#pragma mark - HUD
// HUD
static const uint16_t HUDHeight = 32;


#pragma mark - Balls
// Balls
static const float BallRadius = 10.f;
static const float BallChopVelocityY = 2.0f;
enum BallType : uint8_t {
    BallTypeNoPlayer = 0,
    BallTypeBlue,
    BallTypeRed
};
struct Ball {
    float cx;   // Center X
    float cy;   // Center Y
    float vx;
    float vy;
    BallType type;
    
    float Left() const {
        return cx - BallRadius;
    }
    
    float Right() const {
        return cx + BallRadius;
    }
    
    float Top() const {
        return cy - BallRadius;
    }
    
    float Bottom() const {
        return cy + BallRadius;
    }
    
//    float Speed() const {
//        return SDL_sqrtf((vx * vx) + (vy * vy));
//    }
    
    // Limits y-velocity to a global threshold.  This prevents the ball from
    // bouncing up and down too fast.
    void ChopVY() {
        if (vy > BallChopVelocityY) {
            vy = BallChopVelocityY;
//            SDL_Log("vy, fix: %f", vy);
        } else if (vy < -BallChopVelocityY) {
            vy = -BallChopVelocityY;
//            SDL_Log("vy, fix: %f", vy);
        }
    }
    
} Balls[32];
static uint8_t BallCount = 0;


#pragma mark - Paddles
// Paddles
static const float PaddleVStep = 0.1f;
static const int16_t PaddleMaxH = 150;
static const uint16_t PaddleWidth = 16;
static const float PaddleToBallFriction = 1.f;
struct Paddle {
    float y;
    float vy;
    uint16_t x : 14;
    signed ballBounceDirection : 2;
    BallType ballType;
    
    SDL_Scancode keyUp;
    SDL_Scancode keyDown;
    
    uint16_t Left() const {
        return x;
    }
    
    uint16_t Right() const {
        return x + PaddleWidth;
    }
    
    float Top() const {
        return y;
    }
    
    float Bottom() const {
        return y + PaddleMaxH;
    }
} Paddles[2];


#pragma mark - Misc Game + App Code

static void PaddleDraw(uint16_t x, int16_t y, Uint8 r, Uint8 g, Uint8 b)
{
    int16_t yoff_max = SDL_min(PaddleMaxH, ScreenHeight - HUDHeight - y);   // Make sure drawing doesn't occur below ScreenHeight (DavidL: is this needed?)
    for (int16_t yoff = 0; yoff < yoff_max; ++yoff) {
        for (int16_t xoff = 0; xoff < PaddleWidth; ++xoff) {
            int32_t pixoff = (((y + yoff) * Screen->pitch) + ((x + xoff) << 2));
            SDL_assert_paranoid(pixoff >= 0);
            SDL_assert_paranoid(pixoff < ((ScreenHeight - HUDHeight) * Screen->pitch));
            ((uint8_t *)(Screen->pixels))[pixoff + 0] = r;
            ((uint8_t *)(Screen->pixels))[pixoff + 1] = g;
            ((uint8_t *)(Screen->pixels))[pixoff + 2] = b;
        }
    }
}

static void GameInit()
{
    // Paddle position
    for (uint8_t i = 0; i < SDL_arraysize(Paddles); ++i) {
        Paddles[i].y = (ScreenHeight - HUDHeight - PaddleMaxH) / 2.f;
        Paddles[i].vy = 0.f;
    }
    
    Paddles[0].x = 16;
    Paddles[0].ballBounceDirection = 1;
    Paddles[0].ballType = BallTypeBlue;

    Paddles[1].x = ScreenWidth - 32;
    Paddles[1].ballBounceDirection = -1;
    Paddles[1].ballType = BallTypeRed;
    
    // Paddle input keys
    // TODO: set to SDL_SCANCODE_UNKNOWN for AI control?
    Paddles[0].keyUp = SDL_SCANCODE_LSHIFT;
    Paddles[0].keyDown = SDL_SCANCODE_LCTRL;
    Paddles[1].keyUp = SDL_SCANCODE_RETURN;
    Paddles[1].keyDown = SDL_SCANCODE_RSHIFT;
    
    // Spawn a ball
    Balls[0].cx = ScreenWidth / 2.f;
    Balls[0].cy = (ScreenHeight - HUDHeight) / 2.f;
    Balls[0].vx = -1.3f;
    Balls[0].vy = -0.7f;
    Balls[0].type = BallTypeNoPlayer;
    BallCount = 1;
}

static void GameUpdate()
{
    // Get pressed-state for all keyboard keys
    const Uint8 * keyState = SDL_GetKeyboardState(NULL);
    
    // Paddle updates
    for (uint8_t i = 0; i < SDL_arraysize(Paddles); ++i) {
        // Adjust paddle velocity
        if (keyState[Paddles[i].keyDown]) {
            // Going down
            Paddles[i].vy += PaddleVStep;
        } else if (keyState[Paddles[i].keyUp]) {
            // Going up
            Paddles[i].vy -= PaddleVStep;
        } else if (Paddles[i].vy < 0.f) {
            // Slow a downward-moving paddle
            Paddles[i].vy += PaddleVStep;
            if (Paddles[i].vy > 0.f) {
                // Make sure it doesn't start going in the other direction!
                Paddles[i].vy = 0.f;
            }
        } else if (Paddles[i].vy > 0.f) {
            // Slow an upward-moving paddle
            Paddles[i].vy -= PaddleVStep;
            if (Paddles[i].vy < 0.f) {
                // Make sure it doesn't start going in the other direction!
                Paddles[i].vy = 0.f;
            }
        }
        
        // Move the paddle, making sure not to go past walls
        Paddles[i].y += Paddles[i].vy;
        if (Paddles[i].Top() <= 0.f) {
            // Stop at the top wall
            Paddles[i].y = 0.f;
            Paddles[i].vy = 0.f;
        } else if (Paddles[i].Bottom() >= (ScreenHeight - HUDHeight)) {
            // Stop at the bottom wall
            Paddles[i].y = ScreenHeight - HUDHeight - PaddleMaxH;
            Paddles[i].vy = 0.f;
        }
    }
    
    // Ball updates
    for (uint8_t i = 0; i < BallCount; ++i) {
        // Move the ball
        Balls[i].cx += Balls[i].vx;
        Balls[i].cy += Balls[i].vy;

        // Ball/wall collisions
        if ((Balls[i].Bottom()) > (float)(ScreenHeight - HUDHeight)) {
            // Collision, bottom-wall
            Balls[i].cy = ((float)(ScreenHeight - HUDHeight)) - BallRadius;
            Balls[i].vy *= -1.f;
            Balls[i].ChopVY();
        } else if ((Balls[i].Top()) < 0.f) {
            // Collision, top-wall
            Balls[i].cy = BallRadius;
            Balls[i].vy *= -1.f;
            Balls[i].ChopVY();
        } else if ((Balls[i].Right()) > (float)ScreenWidth) {
            // Collision, right-wall
            Balls[i].cx = ((float)ScreenWidth) - BallRadius;
            Balls[i].vx *= -1.f;
        } else if ((Balls[i].Left()) < 0.f) {
            // Collision, left-wall
            Balls[i].cx = BallRadius;
            Balls[i].vx *= -1.f;
        }
        
        // Ball/paddle collisions
        for (uint8_t j = 0; j < SDL_arraysize(Paddles); ++j) {
            // For now, do collisions with the entire paddle, and not a cut-up
            // one.  Eventually, paddles will be slice-able, probably with bits
            // representing valid slices.
            if ((Balls[i].Left() < Paddles[j].Right()) &&
                (Balls[i].Right() > Paddles[j].Left()) &&
                (Balls[i].Top() < Paddles[j].Bottom()) &&
                (Balls[i].Bottom() > Paddles[j].Top()))
            {
                // Make sure that successive collisions with the same ball +
                // paddle, do not result in multiple bounces!
                //
                // I.e. only bounce the ball once per collision.
                if ((Balls[i].vx * Paddles[j].ballBounceDirection) < 0) {
                    // Bounce the ball!
                    Balls[i].vx *= -1.f;
                    Balls[i].vy += (Paddles[j].vy * PaddleToBallFriction);
//                    SDL_Log("speed, paddle: %f", Balls[i].Speed());
//                    SDL_Log("vy, paddle: %f", Balls[i].vy);
                    Balls[i].type = Paddles[j].ballType;
                }
            }
        }
    }
}

static void GameDraw()
{
    SDL_Rect r;
    
    // Background
    RectSet(&r, 0, 0, ScreenWidth, ScreenHeight - HUDHeight);
    SDL_FillRect(Screen, &r, SDL_MapRGB(Screen->format, 0xaa, 0xaa, 0xaa));
    
    // HUD
    RectSet(&r, 0, ScreenHeight - HUDHeight, ScreenWidth, HUDHeight);
    SDL_FillRect(Screen, &r, SDL_MapRGB(Screen->format, 0x55, 0x55, 0x55));
    
    // Paddles
    PaddleDraw(Paddles[0].x, (int16_t)Paddles[0].y, 0xff, 0x00, 0x00);
    PaddleDraw(Paddles[1].x, (int16_t)Paddles[1].y, 0x00, 0x00, 0xff);
    
    // Balls
    for (uint8_t i = 0; i < BallCount; ++i) {
        SDL_Rect ballRect = {
            MathRound(Balls[i].Left()),
            MathRound(Balls[i].Top()),
            MathRound(BallRadius * 2),
            MathRound(BallRadius * 2)
        };
        
        ImageID ballImageID;
        switch (Balls[i].type) {
            case BallTypeBlue:      ballImageID = ImageIDBallBlue;      break;
            case BallTypeNoPlayer:  ballImageID = ImageIDBallNoPlayer;  break;
            case BallTypeRed:       ballImageID = ImageIDBallRed;       break;
            default:                ballImageID = 0;
        }
        if ( ! ballImageID) {
            SDL_FillRect(Screen, &ballRect, SDL_MapRGB(Screen->format, 0, 0, 0));
        } else {
            SDL_BlitSurface(Images[ballImageID], NULL, Screen, &ballRect);
        }
    }
}

static uint8_t AppTexturesReload()
{
    if (ScreenTexture) {
        SDL_DestroyTexture(ScreenTexture);
        ScreenTexture = NULL;
    }
    
    if ( ! ScreenTexture) {
        ScreenTexture = SDL_CreateTexture(Renderer, Screen->format->format, SDL_TEXTUREACCESS_STREAMING, ScreenWidth, ScreenHeight);
        if ( ! ScreenTexture) {
            SDL_Log("%s, SDL_CreateTexture failed [Screen Texture]: %s", __FUNCTION__, SDL_GetError());
            return -1;
        }
    }
    
    return 0;
}

static void AppUpdate()
{
    // Make note of the app's current time
    uint32_t tick = SDL_GetTicks();
    
    // Cycle *ALL* SDL events.  This is necessary for many platforms.
    // (SDL_PollEvent() will often, but not always, pump OS-level events.)
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT: {
                AppRunning = 0;
            } break;
                
            case SDL_RENDER_DEVICE_RESET: {
                // The renderer got reset, along with all of its resources.
                // This can happen with Direct3D, possibly other APIs.
                //
                // Make sure all texture(s) get re-created.
                if (AppTexturesReload() != 0) {
                    exit(1);
                }
            } break;
        }
    }

    //
    // Update game-state at a fixed rate, while drawing as fast as we can.
    //
    if (NextGameTickAt == 0) {
        NextGameTickAt = tick;
    }
    while (tick >= NextGameTickAt) {
        GameUpdate();
        NextGameTickAt += 10;
    }
    GameDraw();

    // Copy Screen to a texture, then draw the texture to the display, scaling
    // as appropriate.
    SDL_SetRenderDrawColor(Renderer, 0, 0, 0, 0);
    SDL_RenderClear(Renderer);
    SDL_UpdateTexture(ScreenTexture, NULL, Screen->pixels, Screen->pitch);
    SDL_RenderCopy(Renderer, ScreenTexture, NULL, NULL);
    SDL_RenderPresent(Renderer);
}

static uint8_t AppInit()
{
    SDL_SetHint("SDL_HINT_RENDER_VSYNC", "1");
    
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("%s, SDL_Init(SDL_INIT_VIDEO) failed: %s", __FUNCTION__, SDL_GetError());
        return -1;
    }
    
    if (SDL_CreateWindowAndRenderer(DefaultWindowWidth, DefaultWindowHeight,
                                    SDL_WINDOW_RESIZABLE,
                                    &Window,
                                    &Renderer) != 0) {
        SDL_Log("%s, SDL_CreateWindowAndRenderer failed: %s", __FUNCTION__, SDL_GetError());
        return 1;
    }
    
    if (SDL_RenderSetLogicalSize(Renderer, ScreenWidth, ScreenHeight)) {
        SDL_Log("%s, SDL_RenderSetLogicalSize failed: %s", __FUNCTION__, SDL_GetError());
        return -1;
    }
    
    Screen = SDL_CreateRGBSurface(0, ScreenWidth, ScreenHeight, 32, 0, 0, 0, 0);
    if ( ! Screen) {
        SDL_Log("%s, SDL_CreateRGBSurface failed [screen creation]: %s", __FUNCTION__, SDL_GetError());
        return -1;
    }
    
    if (AppTexturesReload() != 0) {
        return -1;
    }
    
    return 0;
}

int main(int argc, char * argv[])
{
    // Init SDL, and other low-level systems
    if (AppInit() != 0) {
        return 1;
    }

    // Make sure 'Data' directory (game-assets) can be easily found
#if __MACOSX__
    chdir(SDL_GetBasePath());
#endif
    
    if ( ! ((ImageIDBallBlue = LoadImage("Data/Images/BallBlue.png")) &&
            (ImageIDBallNoPlayer = LoadImage("Data/Images/BallNoPlayer.png")) &&
            (ImageIDBallRed = LoadImage("Data/Images/BallRed.png"))
            ))
    {
        return 1;
    }

    // Make sure a (fixed frame-rate) game-update occurs on the first AppUpdate() call
    NextGameTickAt = 0;

    // Start a new round of gameplay
    GameInit();

    // Game loop
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(AppUpdate, 0, 1);
#else
    while (AppRunning) {
        AppUpdate();
    }
#endif
    
    return 0;
}

//
// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.
//
// In jurisdictions that recognize copyright laws, the author or authors
// of this software dedicate any and all copyright interest in the
// software to the public domain. We make this dedication for the benefit
// of the public at large and to the detriment of our heirs and
// successors. We intend this dedication to be an overt act of
// relinquishment in perpetuity of all present and future rights to this
// software under copyright law.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//
// For more information, please refer to <http://unlicense.org/>
//

