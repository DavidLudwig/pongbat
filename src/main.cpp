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


// Images
#pragma mark - Images
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
static const uint32_t ImageRMask = 0xff000000;
static const uint32_t ImageGMask = 0x00ff0000;
static const uint32_t ImageBMask = 0x0000ff00;
static const uint32_t ImageAMask = 0x000000ff;
static const uint8_t  ImageAShift = 0;          // Number of bits to right-shift (from uint32_t) to get alpha-channel
#else
static const uint32_t ImageRMask = 0x000000ff;
static const uint32_t ImageGMask = 0x0000ff00;
static const uint32_t ImageBMask = 0x00ff0000;
static const uint32_t ImageAMask = 0xff000000;
static const uint8_t  ImageAShift = 24;         // Number of bits to right-shift (from uint32_t) to get alpha-channel
#endif

typedef uint8_t ImageID;
static SDL_Surface * Images[64];
static ImageID ImageNext = 1;
static ImageID ImageIDBallBlue;
static ImageID ImageIDBallNoPlayer;
static ImageID ImageIDBallRed;
static ImageID ImageIDPaddleBlue;
static ImageID ImageIDPaddleRed;

ImageID ImageIDAlloc()
{
    if (ImageNext < SDL_arraysize(Images)) {
        return ImageNext++;
    } else {
        return 0;
    }
}

ImageID ImageLoad(const char * filename)
{
    ImageID id;
    int w, h, n;
    SDL_Surface * surface = NULL;
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
    
    surface = SDL_CreateRGBSurfaceFrom(data, w, h, 32, w * 4, ImageRMask, ImageGMask, ImageBMask, ImageAMask);
    if ( ! surface) {
        SDL_Log("%s, SDL_CreateRGBSurfaceFrom failed: %s",
                __FUNCTION__,
                SDL_GetError());
        stbi_image_free(data);
        return 0;
    }
    
    id = ImageIDAlloc();
    if ( ! id) {
        SDL_Log("%s, ImageIDAlloc() failed", __FUNCTION__);
        SDL_FreeSurface(surface);
        stbi_image_free(data);
        return 0;
    }
    
    Images[id] = surface;
    return id;
}

ImageID ImageCreate(int w, int h)
{
    ImageID id;
    SDL_Surface * surface = SDL_CreateRGBSurface(0, w, h, 32, ImageRMask, ImageGMask, ImageBMask, ImageAMask);
    if ( ! surface) {
        SDL_Log("%s, SDL_CreateRGBSurface failed: %s",
                __FUNCTION__,
                SDL_GetError());
        return 0;
    }
    
    id = ImageIDAlloc();
    if ( ! id) {
        SDL_Log("%s, ImageIDAlloc() failed", __FUNCTION__);
        SDL_FreeSurface(surface);
        return 0;
    }
    
    Images[id] = surface;
    return id;
}


// Math
#pragma mark - Math
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


// App
#pragma mark - App
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


// HUD
#pragma mark - HUD
static const uint16_t HUDHeight = 32;


// Balls
#pragma mark - Balls
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
    
    void GetRect(SDL_Rect * r) const {
        r->x = MathRound(Left());
        r->y = MathRound(Top());
        r->w = MathRound(BallRadius * 2.f);
        r->h = MathRound(BallRadius * 2.f);
    }
    
    SDL_Surface * GetImage() const {
        switch (type) {
            case BallTypeBlue:      return Images[ImageIDBallBlue];
            case BallTypeNoPlayer:  return Images[ImageIDBallNoPlayer];
            case BallTypeRed:       return Images[ImageIDBallRed];
            default:                return NULL;
        }
    }
} Balls[32];
static uint8_t BallCount = 0;


// Paddles
#pragma mark - Paddles
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
    SDL_Scancode keyLaser;
    
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
    
    void GetRect(SDL_Rect * r) const {
        r->x = x;
        r->y = MathRound(y);
        r->w = PaddleWidth;
        r->h = PaddleMaxH;
    }
    
    static SDL_Surface * GetImage(uint8_t paddleIndex) {
        switch (paddleIndex) {
            case 0:  return Images[ImageIDPaddleBlue];
            case 1:  return Images[ImageIDPaddleRed];
            default: return NULL;
        }
    }
} Paddles[2];


// Lasers
#pragma mark - Lasers
static const float LaserMagnitudeStep = -0.4f;
static const float LaserInitialMagnitude = 8.f;
static const uint8_t LaserCutInterval = 3;
struct Laser {
    float cy;
    float magnitude;            // laser height = magnitude * 2.f
    uint8_t gameTicksUntilCut;  // default is set via 'LaserCutInterval'
    
    uint8_t GetRect(SDL_Rect * r, int paddleIndex) const {
        if (magnitude == 0.f) {
            return -1;
        }
        switch (paddleIndex) {
            case 0:  r->x = Paddles[0].Right();  r->w = ScreenWidth - Paddles[paddleIndex].Right();  break;
            case 1:  r->x = 0;                   r->w = Paddles[paddleIndex].Left();                 break;
            default:
                return -1;
        }
        r->y = MathRound(cy - magnitude);
        r->h = MathRound(magnitude * 2.f);
        return 0;
    }
} Lasers[2];



#pragma mark - Misc Game + App Code

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

    // Paddle contents
    SDL_FillRect(Images[ImageIDPaddleBlue], NULL, SDL_MapRGB(Images[ImageIDPaddleBlue]->format, 0x00, 0x00, 0xff));
    SDL_FillRect(Images[ImageIDPaddleRed],  NULL, SDL_MapRGB(Images[ImageIDPaddleRed]->format,  0xff, 0x00, 0x00));
    
    // Paddle input keys
    // TODO: set to SDL_SCANCODE_UNKNOWN for AI control?
    Paddles[0].keyUp = SDL_SCANCODE_LSHIFT;
    Paddles[0].keyDown = SDL_SCANCODE_LCTRL;
    Paddles[0].keyLaser = SDL_SCANCODE_Z;
    Paddles[1].keyUp = SDL_SCANCODE_RETURN;
    Paddles[1].keyDown = SDL_SCANCODE_RSHIFT;
    Paddles[1].keyLaser = SDL_SCANCODE_SLASH;
    
    // Reset lasers
    for (uint8_t i = 0; i < SDL_arraysize(Lasers); ++i) {
        Lasers[i].magnitude = 0.f;
    }
    
    // Spawn a ball
    Balls[0].cx = ScreenWidth / 2.f;
    Balls[0].cy = (ScreenHeight - HUDHeight) / 2.f;
    Balls[0].vx = -1.3f;
    Balls[0].vy = -0.7f;
    Balls[0].type = BallTypeNoPlayer;
    BallCount = 1;
}

static SDL_bool GameIsBallPaddleCollision(uint8_t ballIndex, uint8_t paddleIndex)
{
    // First, check to see if the ball + paddle's rects match up
    SDL_Rect ballRect, paddleRect, intersection;
    Balls[ballIndex].GetRect(&ballRect);
    Paddles[paddleIndex].GetRect(&paddleRect);
    if ( ! SDL_IntersectRect(&ballRect, &paddleRect, &intersection)) {
        // The ball + paddle definitely don't collide!
        return SDL_FALSE;
    }
    
    //return SDL_TRUE;
    
    SDL_Surface * ballImage = Balls[ballIndex].GetImage();
    SDL_Surface * paddleImage = Paddle::GetImage(paddleIndex);
    for (uint16_t x = intersection.x; x < (intersection.x + intersection.w); ++x) {
        for (uint16_t y = intersection.y; y < (intersection.y + intersection.h); ++y) {
            const uint16_t bx = x - ballRect.x;
            const uint16_t by = y - ballRect.y;
            const uint16_t px = x - paddleRect.x;
            const uint16_t py = y - paddleRect.y;
            const uint32_t balphachannel = (((uint32_t *)  ballImage->pixels)[bx + (by *   ballImage->w)] & ImageAMask);
            const uint32_t palphachannel = (((uint32_t *)paddleImage->pixels)[px + (py * paddleImage->w)] & ImageAMask);
            if (balphachannel &&
                ((palphachannel >> ImageAShift) == 0xff))
            {
                return SDL_TRUE;
            }
        }
    }
    
    return SDL_FALSE;
}

static void GameUpdate()
{
    // Get pressed-state for all keyboard keys
    const Uint8 * keyState = SDL_GetKeyboardState(NULL);
    
    // Laser-magnitude updates
    for (uint8_t i = 0; i < SDL_arraysize(Lasers); ++i) {
        if (Lasers[i].magnitude == 0.f) {
            continue;
        }
        Lasers[i].magnitude += LaserMagnitudeStep;
        if (Lasers[i].magnitude < 0.f) {
            Lasers[i].magnitude = 0.f;
        }
    }

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
        
        // Fire lasers
        if (Lasers[i].magnitude == 0.f) {
            if (keyState[Paddles[i].keyLaser]) {
                Lasers[i].magnitude = LaserInitialMagnitude;
                Lasers[i].cy = ((Paddles[i].Bottom() - Paddles[i].Top()) / 2.f) + Paddles[i].Top();
                Lasers[i].gameTicksUntilCut = 0;
            }
        }
    }
    
    // Laser-cuts
    for (uint8_t i = 0; i < SDL_arraysize(Lasers); ++i) {
        if (Lasers[i].gameTicksUntilCut > 0) {
            Lasers[i].gameTicksUntilCut--;
        }
        
        if (Lasers[i].gameTicksUntilCut == 0) {
            SDL_Rect laserRect;
            if (Lasers[i].GetRect(&laserRect, i) == 0) {
                for (uint8_t j; j < SDL_arraysize(Paddles); ++j) {
                    if (i != j) {
                        SDL_Rect paddleRect, intersection;
                        Paddles[j].GetRect(&paddleRect);
                        if (SDL_IntersectRect(&laserRect, &paddleRect, &intersection)) {
                            intersection.x -= paddleRect.x;
                            intersection.y -= paddleRect.y;
                            ImageID paddleImageID;
                            switch (j) {
                                case 0:  paddleImageID = ImageIDPaddleBlue; break;
                                case 1:  paddleImageID = ImageIDPaddleRed;  break;
                                default: paddleImageID = 0;                 break;
                            }
                            if (paddleImageID) {
//                                SDL_FillRect(Images[paddleImageID], &intersection, SDL_MapRGBA(Images[paddleImageID]->format, 0x00, 0x00, 0x00, 0x12));
                                SDL_FillRect(Images[paddleImageID], &intersection, SDL_MapRGBA(Images[paddleImageID]->format, 0x00, 0x00, 0x00, 0x00));
                            }
                        }
                    }
                }
            }
            
            Lasers[i].gameTicksUntilCut = LaserCutInterval;
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
            if (GameIsBallPaddleCollision(i, j)) {
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
    for (uint8_t i = 0; i < SDL_arraysize(Paddles); ++i) {
        Paddles[i].GetRect(&r);
        SDL_BlitSurface(Paddle::GetImage(i), NULL, Screen, &r);
    }

    // Lasers
    for (uint8_t i = 0; i < SDL_arraysize(Lasers); ++i) {
        if (Lasers[i].GetRect(&r, i) == 0) {
            SDL_FillRect(Screen, &r, SDL_MapRGB(Screen->format, 0xff, 0xff, 0x00));
        }
    }
    
    // Balls
    for (uint8_t i = 0; i < BallCount; ++i) {
        Balls[i].GetRect(&r);
        
        SDL_Surface * ballImage = Balls[i].GetImage();
        if (ballImage) {
            SDL_BlitSurface(ballImage, NULL, Screen, &r);
        } else {
            SDL_FillRect(Screen, &r, SDL_MapRGB(Screen->format, 0, 0, 0));
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
    
    if ( ! ((ImageIDBallBlue = ImageLoad("Data/Images/BallBlue.png")) &&
            (ImageIDBallNoPlayer = ImageLoad("Data/Images/BallNoPlayer.png")) &&
            (ImageIDBallRed = ImageLoad("Data/Images/BallRed.png")) &&
            (ImageIDPaddleBlue = ImageCreate(PaddleWidth, PaddleMaxH)) &&
            (ImageIDPaddleRed = ImageCreate(PaddleWidth, PaddleMaxH))
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

