//
//  main.cpp
//  pongbat
//
//  Created by David Ludwig <dludwig@pobox.com> on 7/4/15, and licensed into
//  the public domain.  See the 'unlicense' at this bottom of this file
//  for details.
//


//   
//    #   #           #                  #   #                    #                      
//    ## ##   ####         # ##          #   #   ###    ####   ####   ###   # ##    #### 
//    # # #  #   #    #    ##  #         #####  #####  #   #  #   #  #####  ##     ###   
//    # # #  #  ##    #    #   #         #   #  #      #  ##  #   #  #      #        ### 
//    #   #   ## #    #    #   #         #   #   ###    ## #   ####   ###   #      ####  
//   
#pragma mark - Main Headers

#include <SDL.h>

#ifdef __EMSCRIPTEN__       // Web-browser API(s)
#include <emscripten.h>
#endif

#if __MACOSX__
#include <unistd.h>         // for chdir()
#endif


//   
//    ####          #                            ###           #       #                        
//    #   #   ###   ####   #   #   ####         #   #  ####   ####           ###   # ##    #### 
//    #   #  #####  #   #  #   #  #   #         #   #  #   #   #       #    #   #  ##  #  ###   
//    #   #  #      #   #  #  ##   ####         #   #  #   #   #       #    #   #  #   #    ### 
//    ####    ###   ####    ## #      #          ###   ####     ##     #     ###   #   #  ####  
//                                 ###                 #                                        
//
#pragma mark - Debug Options
//#define DEBUG_PADDLE_DRAWING 1                  // Uncomment to draw unseen paddle parts
//#define DEBUG_KEYS 1                            // Uncomment to enable debug keys (via keyboard)


//
//    ###                                     
//     #    ## #    ####   ####   ###    #### 
//     #    # # #  #   #  #   #  #####  ###   
//     #    # # #  #  ##   ####  #        ### 
//    ###   #   #   ## #      #   ###   ####  
//                         ###                
//
#pragma mark - Images

// stb_image is a single-file, C/C++ header-only, image loader
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

// Global screen.  All game elements are drawn here.  In the future, menu items
// will probably be drawn here, too.
static SDL_Surface * Screen = 0;
static const uint16_t ScreenWidth = 640;
static const uint16_t ScreenHeight = 480;

// Setup endian-specific constants.
//
// BUG: These likely fail on Emscripten, when running on a big-endian machine.
//   Consider assigning them dynamically, in this case.
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
static const uint32_t ImageRMask = 0xff000000;
static const uint32_t ImageGMask = 0x00ff0000;
static const uint32_t ImageBMask = 0x0000ff00;
static const uint32_t ImageAMask = 0x000000ff;
static const uint8_t  ImageAShift = 0;          // Number of bits to right-shift to get alpha-channel
#else
static const uint32_t ImageRMask = 0x000000ff;
static const uint32_t ImageGMask = 0x0000ff00;
static const uint32_t ImageBMask = 0x00ff0000;
static const uint32_t ImageAMask = 0xff000000;
static const uint8_t  ImageAShift = 24;         // Number of bits to right-shift to get alpha-channel
#endif

// Images are accessed via ID numbers
typedef uint8_t ImageID;
static SDL_Surface * Images[64];
static ImageID ImageNext = 1;

// Game-specific Image IDs
static ImageID ImageIDBallBlue;
static ImageID ImageIDBallNoPlayer;
static ImageID ImageIDBallRed;
static ImageID ImageIDPaddleBlue;
static ImageID ImageIDPaddleRed;

// ImageIDAlloc -- allocate a new ImageID
ImageID ImageIDAlloc()
{
    if (ImageNext < SDL_arraysize(Images)) {
        return ImageNext++;
    } else {
        return 0;
    }
}

// ImageLoad -- load an image from disk, to a new ImageID
ImageID ImageLoad(const char * filename)
{
    if (ImageNext > (SDL_arraysize(Images) - 1)) {
        SDL_Log("%s, Out of ImageIDs!", __FUNCTION__);
        return 0;
    }
    
    ImageID id;
    int w, h, n;
    SDL_Surface * surface = NULL;
    void * data = stbi_load(filename, &w, &h, &n, 4);
    if ( ! data) {
        SDL_Log("%s, stbi_load failed for %s, \"%s\"",
                __FUNCTION__,
                (filename ? filename : "(null)"),
                stbi_failure_reason());
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

// ImageCreate -- create a new image, to a new ImageID
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

// ImageGetAlphaUnshifted -- gets a pixel's alpha channel, without shifting it to the MSB
static uint32_t ImageGetAlphaUnshifted(SDL_Surface * image, uint16_t x, uint16_t y)
{
    return ((uint32_t *)image->pixels)[x + (y * image->w)] & ImageAMask;
}


//   
//    #   #          #     #     
//    ## ##   ####  ####   ####  
//    # # #  #   #   #     #   # 
//    # # #  #  ##   #     #   # 
//    #   #   ## #    ##   #   # 
//                               
#pragma mark - Math

// MathRound -- cross-platform float-to-int rounding
int MathRound(float x)
{
    return (x + 0.5f);
}

// RectSet -- sets the contents of an SDL_Rect
void RectSet(SDL_Rect * r, int x, int y, int w, int h)
{
    r->x = x;
    r->y = y;
    r->w = w;
    r->h = h;
}


//   
//    #   #  #   #  ####  
//    #   #  #   #  #   # 
//    #####  #   #  #   # 
//    #   #  #   #  #   # 
//    #   #   ###   ####  
//                        
// Heads Up Display -- for (eventually) displaying scores, time-left, laser recharge status, etc.
//
#pragma mark - HUD
static const uint16_t HUDHeight = 32;


//   
//    ####           ##     ##          
//    #   #   ####    #      #     #### 
//    ####   #   #    #      #    ###   
//    #   #  #  ##    #      #      ### 
//    ####    ## #   ###    ###   ####  
//                                      
#pragma mark - Balls
static const float BallRadius = 10.f;
static const float BallChopVelocityY = 2.0f;    // 'chop' ball's Y-velocity to this, on particular collisions
enum BallType : uint8_t {
    BallTypeNoPlayer = 0,
    BallTypeBlue,
    BallTypeRed
};
struct Ball {
    float cx;       // Center X
    float cy;       // Center Y
    float vx;       // Velocity, X
    float vy;       // Velocity, Y
    BallType type;  // Blue?  Red?  Other?
    
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


//   
//    ####              #      #   ##                 
//    #   #   ####   ####   ####    #     ###    #### 
//    ####   #   #  #   #  #   #    #    #####  ###   
//    #      #  ##  #   #  #   #    #    #        ### 
//    #       ## #   ####   ####   ###    ###   ####  
//                                                    
#pragma mark - Paddles
static const float PaddleVStep = 0.1f;
static const int16_t PaddleMaxH = 150;
static const uint16_t PaddleWidth = 16;
static const float PaddleToBallFriction = 1.f;
struct Paddle {
    float y;            // Y (paddle-top)
    float vy;           // Velocity, Y
    uint16_t x : 14;    // X (paddle-left)
    signed ballBounceDirection : 2;     // Which direction should colliding ball(s) be sent in (along the X axis)
    BallType ballType;  // Convert colliding ball(s) to this BallType
    int16_t cutTop;     // offset from y, to paddle's actual top, after cut(s)
    int16_t cutBottom;  // offset from y, to paddle's actual bottom, after cut(s)
    
    SDL_Scancode keyUp;     // press this to move up
    SDL_Scancode keyDown;   // press this to move down
    SDL_Scancode keyLaser;  // press this to fire laser
    
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
    
    // GetImage -- convert paddleIndex (0 or 1) to appropriate SDL_Surface
    static SDL_Surface * GetImage(uint8_t paddleIndex) {
        switch (paddleIndex) {
            case 0:  return Images[ImageIDPaddleBlue];
            case 1:  return Images[ImageIDPaddleRed];
            default: return NULL;
        }
    }
    
    // CalcEdge -- computes a new top, or bottom, for paddle; called as part of paddle-cutting algorithm
    static int16_t CalcEdge(SDL_Surface * paddleImage, int16_t ystart, int16_t yend, int16_t ystep) {
        int16_t y;
        for (y = ystart; y != (yend + ystep); y += ystep) {
            for (int16_t x = 0; x < PaddleWidth; ++x) {
                uint32_t a = ImageGetAlphaUnshifted(paddleImage, x, y);
                if ((a >> ImageAShift) == 0xff) {
                    return y;
                }
            }
        }
        return y;
    }
} Paddles[2];


//   
//    #                                        
//    #       ####   ####   ###   # ##    #### 
//    #      #   #  ###    #####  ##     ###   
//    #      #  ##    ###  #      #        ### 
//    #####   ## #  ####    ###   #      ####  
//                                             
#pragma mark - Lasers
static const float LaserMagnitudeStep = -0.4f;      // Adjust laser magnitude by this much, per game-tick
static const float LaserInitialMagnitude = 8.f;     // Default laser magnitutde; TODO: make this adjustable, per-paddle (for laser-upgrades)
static const uint8_t LaserCutInterval = 3;          // Only perform cuts once per this number of game-ticks
struct Laser {
    float cy;                   // laser center, on Y axis
    float magnitude;            // laser height = magnitude * 2.f
    uint8_t gameTicksUntilCut;  // default is set via 'LaserCutInterval'
    
    // GetRect -- try getting laser's SDL_Rect, in Screen coordinates
    //   Returns 0 on success, non-zero on failure.  Result rect will be output to 'r'.
    uint8_t GetRect(SDL_Rect * r, uint8_t paddleIndex) const {
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


//   
//     ####                               ###            #     #    
//    #       ####  ## #    ###            #    # ##          ####  
//    #  ##  #   #  # # #  #####           #    ##  #    #     #    
//    #   #  #  ##  # # #  #               #    #   #    #     #    
//     ####   ## #  #   #   ###           ###   #   #    #      ##  
//                                                                  
#pragma mark - Game Init

// GameInit -- [re]initializes a new round of gameplay
static void GameInit()
{
    // Paddle position
    for (uint8_t i = 0; i < SDL_arraysize(Paddles); ++i) {
        Paddles[i].y = (ScreenHeight - HUDHeight - PaddleMaxH) / 2.f;
        Paddles[i].vy = 0.f;
        Paddles[i].cutTop = 0;
        Paddles[i].cutBottom = PaddleMaxH;
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


//
//     ####                              #####                        #
//    #       ####  ## #    ###          #      #   #   ###   # ##   ####    ####
//    #  ##  #   #  # # #  #####         ####    # #   #####  ##  #   #     ###
//    #   #  #  ##  # # #  #             #       # #   #      #   #   #       ###
//     ####   ## #  #   #   ###          #####    #     ###   #   #    ##   ####
//
#pragma mark - Game Events

// GameEventHandler -- processes game-specific SDL_Events, *RARELY-USED*
//   Most input is handled by inspecting current input state from inside GameUpdate().
static void GameEventHandler(const SDL_Event * event)
{
#if DEBUG_KEYS
    switch (event->type) {
        case SDL_KEYDOWN: {
            switch (event->key.keysym.sym) {
                case SDLK_r: {
                    GameInit();
                } break;
            }
        } break;
    }
#endif
}


//   
//     ####                               ###           ##     ##      #             #                        
//    #       ####  ## #    ###          #   #   ###     #      #            ####          ###   # ##    #### 
//    #  ##  #   #  # # #  #####         #      #   #    #      #      #    ###      #    #   #  ##  #  ###   
//    #   #  #  ##  # # #  #             #   #  #   #    #      #      #      ###    #    #   #  #   #    ### 
//     ####   ## #  #   #   ###           ###    ###    ###    ###     #    ####     #     ###   #   #  ####  
//                                                                                                            
#pragma mark - Game Collisions

// GameIsBallPaddleCollision -- determines if a paddle and a ball are colliding
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
    
    // Next, do a pixel-alpha check, both in the ball and in the paddle
    SDL_Surface * ballImage = Balls[ballIndex].GetImage();
    SDL_Surface * paddleImage = Paddle::GetImage(paddleIndex);
    for (uint16_t y = intersection.y; y < (intersection.y + intersection.h); ++y) {
        for (uint16_t x = intersection.x; x < (intersection.x + intersection.w); ++x) {
            const uint16_t bx = x - ballRect.x;
            const uint16_t by = y - ballRect.y;
            const uint32_t balphachannel = ImageGetAlphaUnshifted(ballImage, bx, by);

            const uint16_t px = x - paddleRect.x;
            const uint16_t py = y - paddleRect.y;
            const uint32_t palphachannel = ImageGetAlphaUnshifted(paddleImage, px, py);
            
            // Make sure the ball and the paddle are opaque (at the current pixel)
            if (balphachannel &&                            // A simple 'is non-zero' check will work fine for ball-alpha.
                ((palphachannel >> ImageAShift) == 0xff))   // A fancier, 'is not fully-opaque' check is used for paddles,
                                                            // as cut paddle parts may be translucent, when debugging
                                                            // paddle-slicing.
            {
                return SDL_TRUE;
            }
        }
    }
    
    // Nope, no collision
    return SDL_FALSE;
}


//   
//     ####                              #   #             #          #           
//    #       ####  ## #    ###          #   #  ####    ####   ####  ####    ###  
//    #  ##  #   #  # # #  #####         #   #  #   #  #   #  #   #   #     ##### 
//    #   #  #  ##  # # #  #             #   #  #   #  #   #  #  ##   #     #     
//     ####   ## #  #   #   ###           ###   ####    ####   ## #    ##    ###  
//                                              #                                 
#pragma mark - Game Update

// GameUpdate -- updates game-state; called 100 times per second
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
        
        // Move the paddle, stopping at walls
        Paddles[i].y += Paddles[i].vy;
        if ((Paddles[i].y + (float)(Paddles[i].cutTop + 1)) <= 0.f) {
            // Stop at the top wall
            Paddles[i].y = (float)-(Paddles[i].cutTop + 1);
            Paddles[i].vy = 0.f;
        } else if ((Paddles[i].y + (float)(Paddles[i].cutBottom)) >= (float)(ScreenHeight - HUDHeight)) {
            // Stop at the bottom wall
            Paddles[i].y = (float)(ScreenHeight - HUDHeight) - (float)(Paddles[i].cutBottom);
            Paddles[i].vy = 0.f;
        }
        
        // Fire lasers
        if (Lasers[i].magnitude == 0.f) {
            if (keyState[Paddles[i].keyLaser]) {
                Lasers[i].magnitude = LaserInitialMagnitude;
                Lasers[i].cy = ((float)(Paddles[i].cutBottom - Paddles[i].cutTop) / 2.f) + (float)Paddles[i].cutTop + Paddles[i].Top();
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
                            SDL_Surface * paddleImage = Paddle::GetImage(j);
                            if (paddleImage) {
#if DEBUG_PADDLE_DRAWING
                                SDL_FillRect(paddleImage, &intersection, SDL_MapRGBA(paddleImage->format, 0x00, 0x00, 0x00, 0x34));
#else
                                SDL_FillRect(paddleImage, &intersection, SDL_MapRGBA(paddleImage->format, 0x00, 0x00, 0x00, 0x00));
#endif
                            }
                            
                            if ((intersection.y <= Paddles[j].cutTop) && ((intersection.y + intersection.h) >= Paddles[j].cutTop)) {
                                Paddles[j].cutTop = Paddle::CalcEdge(paddleImage, intersection.y + intersection.h, PaddleMaxH - 1, 1);
                            }
                            if ((intersection.y <= Paddles[j].cutBottom) && ((intersection.y + intersection.h) >= Paddles[j].cutBottom)) {
                                // TODO: explain, in comments, why '1' is added to CalcEdge result.  Yes, this is needed, maybe.
                                Paddles[j].cutBottom = 1 + Paddle::CalcEdge(paddleImage, intersection.y, 0, -1);
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


//   
//     ####                              ####                       
//    #       ####  ## #    ###          #   #  # ##    ####  #   # 
//    #  ##  #   #  # # #  #####         #   #  ##     #   #  # # # 
//    #   #  #  ##  # # #  #             #   #  #      #  ##  # # # 
//     ####   ## #  #   #   ###          ####   #       ## #   # #  
//                                                                  
#pragma mark - Game Draw

// GameDraw -- draws screen; SHOULD NOT ALTER GAME STATE (use GameUpdate() for that!!!)
//   This may be called at a different interval than GameUpdate().
//   It is NOT guaranteed to be called at a fixed rate!
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
#if DEBUG_PADDLE_DRAWING
        // Highlight the paddle's vertical bounds (used when firing lasers, and
        // for determining paddle-to-wall collisions).
        r.x = Paddles[i].Left() - 4;
        r.y = MathRound(Paddles[i].Top()) + Paddles[i].cutTop;
        r.w = PaddleWidth + 8;
        r.h = (Paddles[i].cutBottom - Paddles[i].cutTop);
        SDL_FillRect(Screen, &r, SDL_MapRGBA(Screen->format, 0xff, 0xff, 0xff, 0x80));
#endif
        
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


//   
//      #                 
//     # #   ####   ####  
//    #   #  #   #  #   # 
//    #####  #   #  #   # 
//    #   #  ####   ####  
//           #      #     
//   
// Handles lower-level window + renderer calls, including, but not limited to:
//  - calling GameUpdate() at a fixed rate
//  - calling GameDraw() at a variable, dynamic rate
//  - loading images  (TODO: consider moving game-specific image loads into GamePreload(), or some other one-time function call)
//  - setting up and managing low-level rendering resources, such as GPU textures
//  - processing platform-specific events (SDL handles this, for the most part)
//  - providing appropriate app entry point(s), and registering timing callback(s), if necessary (such as for Emscripten-use)
//
#pragma mark - App

static const uint16_t DefaultWindowWidth = 640;     // Window size can be anything.  App will scale the game's content and add black bars, as needed.
static const uint16_t DefaultWindowHeight = 480;
static SDL_Window * Window = 0;                     // platform-native window (or view, or canvas, or whatever)
static SDL_Renderer * Renderer = 0;                 // platform-native renderer (use WebGL on Emscripten, OpenGL on OSX, D3D on Windows, etc.)
static SDL_Texture * ScreenTexture = 0;             // 'Screen' surface gets copied here, once per draw ; used for window-scaling
static uint8_t AppRunning = 1;                      // 1 for running, 0 for dead-app
static uint32_t NextGameTickAt = 0;                 // When will the next game-tick occur, as measured in milliseconds, and compared against SDL_GetTicks()

// AppTexturesReload -- reloads GPU textures, of which there are few, as almost all content is rendered in software, by the main CPU
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

// AppUpdate -- called frequently, typically many times per second, often at monitor's refresh rate
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
        
        // Let the game handle event(s), as needed.
        GameEventHandler(&event);
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

// AppInit -- performs one-time app initialization
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
    
    // Load/create, all images
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
    // TODO: Call GameInit() more frequently, to restart game.
    // NOTE: 'R' debug key will invoke GameInit(), which will forcefully restart the game!
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

