/* CASTLE.COM - a Wolfenstein-style raycaster for MYOS.
 *
 * 320x200, VGA mode 13h, textured wall colors.
 * WASD to move, arrow keys to turn, Q to quit.
 *
 * Uses the DDA raycasting algorithm (lodev.org style):
 *   For each screen column, cast a ray from the player into the map,
 *   find the nearest wall, compute its perpendicular distance, and draw
 *   a vertical strip of height proportional to 1/distance.
 */

#include "program.h"

#define W 320
#define H 200
#define MAP_W 16
#define MAP_H 16

#define CEIL_COLOR   8
#define FLOOR_COLOR  7

#define FRAME_TICKS  3             /* 100 Hz / FRAME_TICKS = fps (3 => ~33fps) */
#define MOVE_SPEED   0.035f        /* world units per frame at 33 fps ≈ 1.2 u/s */
#define ROT_SIN      0.045f        /* radians per frame at 33 fps ≈ 85°/s */
#define ROT_SIN      0.025f       /* radians per frame */
#define ROT_COS      0.9997f      /* cos(ROT_SIN) */

static unsigned char* shadow;

/* 1 = red, 2 = green, 3 = blue, 4 = yellow, 5 = white pillar */
static const unsigned char map[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,2,2,0,0,2,2,0,0,0,0,0,0,0,1},
    {1,0,2,0,0,0,0,2,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,2,2,2,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,2,0,0,3,3,3,1},
    {1,0,3,3,3,0,0,0,0,2,0,0,3,0,0,1},
    {1,0,3,0,0,0,0,0,0,2,0,0,3,0,0,1},
    {1,0,3,0,0,0,4,4,4,4,0,0,3,0,0,1},
    {1,0,3,3,3,0,4,0,0,0,0,0,3,0,0,1},
    {1,0,0,0,0,0,4,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,4,4,4,4,4,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,4,0,0,0,0,1},
    {1,0,5,0,0,0,0,0,0,0,4,0,0,5,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

/* Start in a wide-open cell (map[5][8] == 0) facing west, so we look
 * down the corridor toward the blue wall. */
static float posX = 8.5f, posY = 5.5f;
static float dirX = -1.0f, dirY = 0.0f;
/* Camera plane must be on the player's LEFT side.  With our map array
 * indexed as map[y][x] where y increases downward, the perpendicular on
 * the left is (dirY, -dirX) scaled by the half-FOV tangent. */
static float planeX = 0.0f, planeY = -0.66f;

static void px(int x, int y, unsigned char c) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    shadow[y * W + x] = c;
}

/* Returns 1 if the tile at world coordinate (x, y) is a wall. */
static int is_wall(float x, float y) {
    int mx = (int)x;
    int my = (int)y;
    if (mx < 0 || mx >= MAP_W || my < 0 || my >= MAP_H) return 1;
    return map[my][mx] != 0;
}

static void render_frame(void) {
    /* Fill ceiling and floor */
    for (int y = 0; y < H / 2; y++) {
        unsigned char* row = shadow + y * W;
        for (int x = 0; x < W; x++) row[x] = CEIL_COLOR;
    }
    for (int y = H / 2; y < H; y++) {
        unsigned char* row = shadow + y * W;
        for (int x = 0; x < W; x++) row[x] = FLOOR_COLOR;
    }

    /* Cast one ray per screen column */
    for (int x = 0; x < W; x++) {
        float cameraX = 2.0f * (float)x / (float)W - 1.0f;
        float rayDirX = dirX + planeX * cameraX;
        float rayDirY = dirY + planeY * cameraX;

        int   mapX = (int)posX;
        int   mapY = (int)posY;

        float deltaDistX = (rayDirX == 0.0f) ? 1e30f
                          : (rayDirX < 0 ? -1.0f / rayDirX : 1.0f / rayDirX);
        float deltaDistY = (rayDirY == 0.0f) ? 1e30f
                          : (rayDirY < 0 ? -1.0f / rayDirY : 1.0f / rayDirY);

        int   stepX, stepY;
        float sideDistX, sideDistY;

        if (rayDirX < 0) {
            stepX = -1;
            sideDistX = (posX - (float)mapX) * deltaDistX;
        } else {
            stepX = 1;
            sideDistX = ((float)mapX + 1.0f - posX) * deltaDistX;
        }
        if (rayDirY < 0) {
            stepY = -1;
            sideDistY = (posY - (float)mapY) * deltaDistY;
        } else {
            stepY = 1;
            sideDistY = ((float)mapY + 1.0f - posY) * deltaDistY;
        }

        /* DDA: step through the grid until we hit a wall */
        int hit = 0;
        int side = 0;
        int cell = 1;
        int guard = 0;
        while (!hit && guard++ < 128) {
            if (sideDistX < sideDistY) {
                sideDistX += deltaDistX;
                mapX += stepX;
                side = 0;
            } else {
                sideDistY += deltaDistY;
                mapY += stepY;
                side = 1;
            }
            if (mapX < 0 || mapX >= MAP_W || mapY < 0 || mapY >= MAP_H) {
                hit = 1; cell = 1;
            } else if (map[mapY][mapX] != 0) {
                hit = 1; cell = map[mapY][mapX];
            }
        }

        float perpDist;
        if (side == 0)
            perpDist = ((float)mapX - posX + (1 - stepX) / 2.0f) / rayDirX;
        else
            perpDist = ((float)mapY - posY + (1 - stepY) / 2.0f) / rayDirY;
        if (perpDist < 0.05f) perpDist = 0.05f;

        int lineHeight = (int)((float)H / perpDist);
        int drawStart = -lineHeight / 2 + H / 2;
        int drawEnd   =  lineHeight / 2 + H / 2;
        if (drawStart < 0) drawStart = 0;
        if (drawEnd > H)   drawEnd = H;

        /* Pick color from cell type and side */
        unsigned char color;
        switch (cell) {
            case 1: color = side ? 4  : 12; break;   /* red    */
            case 2: color = side ? 2  : 10; break;   /* green  */
            case 3: color = side ? 1  : 9;  break;   /* blue   */
            case 4: color = side ? 6  : 14; break;   /* yellow */
            case 5: color = side ? 8  : 15; break;   /* white  */
            default: color = 7;             break;
        }

        for (int y = drawStart; y < drawEnd; y++) {
            shadow[y * W + x] = color;
        }
    }
}

static void rotate(float sinA, float cosA) {
    float oldDirX = dirX;
    dirX = dirX * cosA - dirY * sinA;
    dirY = oldDirX * sinA + dirY * cosA;

    float oldPlaneX = planeX;
    planeX = planeX * cosA - planeY * sinA;
    planeY = oldPlaneX * sinA + planeY * cosA;
}

static void move(float dx, float dy) {
    float nx = posX + dx;
    float ny = posY + dy;
    if (!is_wall(nx, posY)) posX = nx;
    if (!is_wall(posX, ny)) posY = ny;
}

static void draw_crosshair(void) {
    int cx = W / 2;
    int cy = H / 2;
    unsigned char c = 15;
    for (int i = -3; i <= 3; i++) {
        if (i == 0) continue;
        px(cx + i, cy, c);
        px(cx, cy + i, c);
    }
}

__attribute__((section(".text.entry")))
void _start(syscalls_t* sys) {
    sys->gfx_init();

    shadow = (unsigned char*)sys->malloc(W * H);
    if (!shadow) { sys->gfx_exit(); while (1) {} }

    unsigned int last_move = sys->ticks();

    while (1) {
        /* --- Quit keys --- */
        while (sys->kbhit()) {
            char c = sys->getchar();
            if (c == 'q' || c == 'Q' || c == 27) {
                sys->gfx_exit();
                while (1) { }
            }
        }

        /* --- Continuous movement based on held keys --- */
        int fwd   = sys->key_down('w') || sys->key_down('W');
        int back  = sys->key_down('s') || sys->key_down('S');
        int left  = sys->key_down('a') || sys->key_down('A');
        int right = sys->key_down('d') || sys->key_down('D');
        int rleft  = sys->key_down(KEY_LEFT);
        int rright = sys->key_down(KEY_RIGHT);

        if (fwd)    move( dirX * MOVE_SPEED,  dirY * MOVE_SPEED);
        if (back)   move(-dirX * MOVE_SPEED, -dirY * MOVE_SPEED);
        if (left)   move( dirY * MOVE_SPEED, -dirX * MOVE_SPEED);
        if (right)  move(-dirY * MOVE_SPEED,  dirX * MOVE_SPEED);

        if (rleft)  rotate(-ROT_SIN, ROT_COS);
        if (rright) rotate( ROT_SIN, ROT_COS);

        /* --- Render --- */
        render_frame();
        draw_crosshair();

        sys->gfx_blit(shadow);

        /* --- Frame-rate cap: wait until at least FRAME_TICKS have elapsed
         * since the last frame.  Ticks are 100 Hz, so FRAME_TICKS = 3
         * gives ~30 fps, FRAME_TICKS = 2 gives ~50 fps. --- */
        unsigned int now = sys->ticks();
        while ((now - last_move) < FRAME_TICKS) {
            __asm__ volatile ("hlt");
            now = sys->ticks();
        }
        last_move = now;
    }
}
