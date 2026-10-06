#include "program.h"

#define MAX_SNAKE     400
#define PLAY_X0       2
#define PLAY_Y0       2
#define PLAY_X1       77
#define PLAY_Y1       22
#define MOVE_INTERVAL 10        /* ticks (100 ms at 100 Hz) */

static int snake_x[MAX_SNAKE];
static int snake_y[MAX_SNAKE];
static int snake_len;
static int dx, dy;
static int food_x, food_y;
static int score;
static int game_over;
static int quit_requested;
static int paused;

/* ---------- small helpers ---------- */

static void put_dec(syscalls_t* sys, int v) {
    if (v == 0) { sys->putchar('0'); return; }
    if (v < 0) { sys->putchar('-'); v = -v; }
    char tmp[12]; int n = 0;
    while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
    while (n > 0) sys->putchar(tmp[--n]);
}

static void draw_hud(syscalls_t* sys) {
    sys->gotoxy(2, 23);
    sys->set_color(0x0B);
    sys->print("Score: ");
    put_dec(sys, score);
    sys->print("   Len: ");
    put_dec(sys, snake_len);
    sys->print("   [WASD/Arrows] [P]ause [Q]uit    ");
    sys->set_color(0x07);
}

/* ---------- rendering ---------- */

static void draw_border(syscalls_t* sys) {
    sys->clear();
    sys->set_color(0x0F);
    sys->gotoxy(0, 0);
    for (int i = 0; i < 80; i++) sys->putchar('#');
    sys->gotoxy(0, 24);
    for (int i = 0; i < 80; i++) sys->putchar('#');
    for (int y = 1; y < 24; y++) {
        sys->gotoxy(0, y);  sys->putchar('#');
        sys->gotoxy(79, y); sys->putchar('#');
    }
    sys->set_color(0x07);
}

static void place_food(syscalls_t* sys) {
    while (1) {
        int x = PLAY_X0 + (int)(sys->rand() % (PLAY_X1 - PLAY_X0 + 1));
        int y = PLAY_Y0 + (int)(sys->rand() % (PLAY_Y1 - PLAY_Y0 + 1));
        int hit = 0;
        for (int i = 0; i < snake_len; i++) {
            if (snake_x[i] == x && snake_y[i] == y) { hit = 1; break; }
        }
        if (!hit) { food_x = x; food_y = y; return; }
    }
}

static void draw_food(syscalls_t* sys) {
    sys->gotoxy(food_x, food_y);
    sys->set_color(0x0C);
    sys->putchar('*');
    sys->set_color(0x07);
}

static void draw_full_snake(syscalls_t* sys) {
    for (int i = 0; i < snake_len; i++) {
        sys->gotoxy(snake_x[i], snake_y[i]);
        if (i == 0) { sys->set_color(0x0E); sys->putchar('@'); }
        else        { sys->set_color(0x0A); sys->putchar('o'); }
    }
    sys->set_color(0x07);
}

/* ---------- game setup ---------- */

static void new_game(syscalls_t* sys) {
    sys->clear();
    draw_border(sys);

    sys->gotoxy(28, 11); sys->set_color(0x0E);
    sys->print("=== MYOS SNAKE ===");
    sys->gotoxy(24, 13); sys->set_color(0x07);
    sys->print("W A S D or arrow keys to steer.");
    sys->gotoxy(24, 14);
    sys->print("Eat the * to grow.  P pauses.  Q quits.");
    sys->gotoxy(28, 16); sys->set_color(0x0A);
    sys->print("Press any key to start...");
    sys->set_color(0x07);
    sys->getchar();

    sys->clear();
    draw_border(sys);

    snake_len = 3;
    snake_x[0] = 40; snake_y[0] = 12;
    snake_x[1] = 39; snake_y[1] = 12;
    snake_x[2] = 38; snake_y[2] = 12;
    dx = 1; dy = 0;
    score = 0;
    game_over = 0;
    quit_requested = 0;
    paused = 0;

    place_food(sys);
    draw_full_snake(sys);
    draw_food(sys);
    draw_hud(sys);
}

/* ---------- one game step ---------- */

static unsigned int move_interval(void) {
    /* Character cells are 8 px wide x 16 px tall.
     * To make horizontal and vertical speeds look the same,
     * horizontal moves happen twice as often as vertical ones. */
    return (dx != 0) ? (MOVE_INTERVAL / 2) : MOVE_INTERVAL;
}

static void step(syscalls_t* sys) {
    int nx = snake_x[0] + dx;
    int ny = snake_y[0] + dy;

    if (nx < PLAY_X0 || nx > PLAY_X1 || ny < PLAY_Y0 || ny > PLAY_Y1) {
        game_over = 1;
        return;
    }
    for (int i = 0; i < snake_len - 1; i++) {
        if (snake_x[i] == nx && snake_y[i] == ny) { game_over = 1; return; }
    }

    int growing = (nx == food_x && ny == food_y);

    /* Erase tail unless we're growing */
    if (!growing) {
        sys->gotoxy(snake_x[snake_len - 1], snake_y[snake_len - 1]);
        sys->putchar(' ');
    }

    /* Shift body */
    int start = growing ? snake_len : snake_len - 1;
    for (int i = start; i > 0; i--) {
        snake_x[i] = snake_x[i - 1];
        snake_y[i] = snake_y[i - 1];
    }
    snake_x[0] = nx;
    snake_y[0] = ny;
    if (growing) snake_len++;

    /* Draw head and neck */
    sys->gotoxy(nx, ny);
    sys->set_color(0x0E);
    sys->putchar('@');
    if (snake_len > 1) {
        sys->gotoxy(snake_x[1], snake_y[1]);
        sys->set_color(0x0A);
        sys->putchar('o');
    }
    sys->set_color(0x07);

    if (growing) {
        score += 10;
        place_food(sys);
        draw_food(sys);
        draw_hud(sys);
    }
}

/* ---------- main loop ---------- */

static void game_loop(syscalls_t* sys) {
    unsigned int next_move = sys->ticks() + move_interval();

    while (!game_over && !quit_requested) {

        /* Drain all pending keys */
        while (sys->kbhit()) {
            char c = sys->getchar();

            if (c == 'q' || c == 'Q') { quit_requested = 1; break; }
            if (c == 'p' || c == 'P') { paused = !paused; continue; }

            int ndx = dx, ndy = dy;
            if      (c == 'w' || c == 'W' || c == KEY_UP)    { ndx = 0;  ndy = -1; }
            else if (c == 's' || c == 'S' || c == KEY_DOWN)  { ndx = 0;  ndy = 1;  }
            else if (c == 'a' || c == 'A' || c == KEY_LEFT)  { ndx = -1; ndy = 0;  }
            else if (c == 'd' || c == 'D' || c == KEY_RIGHT) { ndx = 1;  ndy = 0;  }

            /* Reject reversal and no-op */
            if (!(ndx == -dx && ndy == -dy) && !(ndx == dx && ndy == dy)) {
                dx = ndx; dy = ndy;
            }
        }

        if (quit_requested) break;

        unsigned int now = sys->ticks();

        if (!paused && now >= next_move) {
            step(sys);
            next_move = now + move_interval();
        }

        __asm__ volatile ("hlt");
    }
}

static void game_over_screen(syscalls_t* sys) {
    if (quit_requested) return;
    sys->gotoxy(30, 11); sys->set_color(0x0C);
    sys->print("=== GAME OVER ===");
    sys->gotoxy(32, 13); sys->set_color(0x0E);
    sys->print("Final score: ");
    put_dec(sys, score);
    sys->gotoxy(28, 15); sys->set_color(0x07);
    sys->print("R to play again, any other key to quit.");
    sys->set_color(0x07);
}

__attribute__((section(".text.entry")))
void _start(syscalls_t* sys) {
    while (1) {
        new_game(sys);
        game_loop(sys);

        if (quit_requested) break;

        game_over_screen(sys);
        char c = sys->getchar();
        if (c == 'r' || c == 'R') continue;
        break;
    }

    sys->clear();
    sys->set_color(0x0A);
    sys->print("\nThanks for playing!\n");
    sys->set_color(0x07);
}
