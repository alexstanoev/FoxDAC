/*
 * breakout.c
 *
 *  Created on: 2 Sep 2021
 *      Author: alex
 */

#include "pico/stdlib.h"
#include "math.h"

#include "lvgl/lvgl.h"

#include "../drivers/encoder/encoder.h"
#include "../drivers/ssd1306/ssd1306.h"

#define N_ROWS 5
#define N_COLS 7

#define CANVAS_W 128
#define CANVAS_H 64

#define BRICK_GAP_W 2
#define BRICK_GAP_H 2

#define BRICK_W (((CANVAS_W - BRICK_START_X) / N_COLS) - BRICK_GAP_W)
#define BRICK_H 5

#define BRICK_START_X 7
#define BRICK_START_Y 5

#define PADDLE_W 24
#define PADDLE_H 3

#define INITIAL_VEL 2.5f
#define MAX_VEL 3.5f

typedef struct {
    int x, y;
} ivec2_t;

typedef struct {
    float x, y;
} fvec2_t;

typedef struct {
    fvec2_t pos, vec;
    float vel;
} ball_t;

static ivec2_t paddle;
static ball_t ball;

#define NEW_BALL_X() (ball.pos.x + ball.vec.x * ball.vel)
#define NEW_BALL_Y() (ball.pos.y + ball.vec.y * ball.vel)

// up to 8 bricks per row (stores 1 if broken)
static uint8_t brick_rows[N_ROWS] = { 0 };
static uint8_t game_running = 0, ball_moving = 0, bricks_broken = 0, lives = 0;

static lv_timer_t * refresh_timer;

static void draw_screen(lv_timer_t * timer);

// set to 1 to stop lvgl from polling the encoder for volume / drawing on the display
extern uint8_t lv_indev_pause_encoder;
extern uint8_t lv_port_pause_drawing;

// encoder IRQ microstepping
extern volatile bool count_microsteps;

static void init_paddle_pos(void) {
    ball_moving = 0;

    paddle.x = (CANVAS_W / 2) - BRICK_START_X;
    paddle.y = CANVAS_H - (PADDLE_H + 2);

    ball.pos.x = paddle.x + (PADDLE_W / 2);
    ball.pos.y = paddle.y - 5;

    ball.vec.x = 0.0f;
    ball.vec.y = 0.0f;
}

void breakout_init(void) {
    refresh_timer = lv_timer_create(draw_screen, 40, NULL);
    lv_timer_pause(refresh_timer);
}

void breakout_start(void) {
    lv_indev_pause_encoder = 1;
    lv_port_pause_drawing = 1;
    count_microsteps = 1;
    encoder_get_delta();

    for(uint8_t row = 0; row < N_ROWS; row++) {
        brick_rows[row] = 0;
    }

    init_paddle_pos();
    ball.vel = 0.0f;
    bricks_broken = 0;
    lives = 3;
    game_running = 1;

    lv_timer_resume(refresh_timer);
}

void breakout_stop(void) {
    lv_timer_pause(refresh_timer);

    encoder_get_delta();
    count_microsteps = 0;
    lv_indev_pause_encoder = 0;
    lv_port_pause_drawing = 0;
    game_running = 0;

    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(lv_disp_get_default());
}

static void draw_lives(void) {
    ssd1306_SetCursor(BRICK_START_X, CANVAS_H - 8);
    for(uint8_t i = 0; i < lives; i++) {
        ssd1306_WriteChar('.', Font_6x8, White);
    }
}

static void draw_bricks(void) {
    for(uint8_t row = 0; row < N_ROWS; row++) {
        uint8_t y = BRICK_START_Y + (row * BRICK_GAP_H) + (row * BRICK_H);

        for(uint8_t col = 0; col < N_COLS; col++) {
            if(!(brick_rows[row] & (1 << col))) {
                uint8_t x = BRICK_START_X + (col * BRICK_GAP_W) + (col * BRICK_W);

                ssd1306_DrawRectangle(x, y, x + BRICK_W, y + BRICK_H, White);
            }
        }
    }
}

static void draw_paddle(void) {
    // move paddle and clamp to screen
    int32_t enc_delta = encoder_get_delta();

    // restart
    if(!game_running && enc_delta != 0) {
        init_paddle_pos();
        ball.vel = 0.0f;
        bricks_broken = 0;
        lives = 3;
        game_running = 1;
    }

    if(game_running) {
        paddle.x += 2 * enc_delta;
        paddle.x = MAX(1, paddle.x);
        paddle.x = MIN(CANVAS_W - (PADDLE_W + 1), paddle.x);
    }

    ssd1306_DrawRectangle(paddle.x, paddle.y, paddle.x + PADDLE_W, paddle.y + PADDLE_H, White);

    // start the ball when the encoder moves
    if(game_running && !ball_moving && enc_delta != 0) {
        ball_moving = 1;
        ball.vec.y = -1;
        if(ball.vel == 0.0f) {
            ball.vel = INITIAL_VEL;
        }
    }
}

static void draw_ball(void) {
    ssd1306_DrawCircle(ball.pos.x, ball.pos.y, 3, White);
}

static void update_ball(void) {
    float x = NEW_BALL_X();
    float y = NEW_BALL_Y();

    // collision with vertical walls
    if (x > CANVAS_W - 1 || x < 0) {
        ball.vec.x *= -1;
        x = NEW_BALL_X();
    }

    // collision with top wall
    if (y <= 0) {
        ball.vec.y *= -1;
        y = NEW_BALL_Y();
    }

    // collision with bottom wall
    if (y > CANVAS_H - 1) {
        init_paddle_pos();
        lives--;
        return;
    }

    // collision with paddle
    if (x >= paddle.x - 1 && x <= paddle.x + PADDLE_W + 1 && ceil(y) > paddle.y - 1 && ceil(y) < paddle.y + PADDLE_H + 1) {
        float rel = (x - paddle.x + 1) / (PADDLE_W + 1);
        float angle = -1 * M_PI * (1 + 4 * rel) / 6;

        ball.vec.x = cosf(angle);
        ball.vec.y = sinf(angle);

        x = NEW_BALL_X();
        y = NEW_BALL_Y();
    }

    // collision with bricks
    for(uint8_t row = 0; row < N_ROWS; row++) {
        uint8_t brick_y = BRICK_START_Y + (row * BRICK_GAP_H) + (row * BRICK_H);

        for(uint8_t col = 0; col < N_COLS; col++) {
            if(!(brick_rows[row] & (1 << col))) {
                uint8_t brick_x = BRICK_START_X + (col * BRICK_GAP_W) + (col * BRICK_W);

                if(x >= brick_x && x <= brick_x + BRICK_W && y >= brick_y && y <= brick_y + BRICK_H) {
                    // collided with brick
                    brick_rows[row] |= (1 << col);
                    bricks_broken++;

                    // speed the ball up
                    ball.vel = INITIAL_VEL + (bricks_broken * 0.05f);
                    ball.vel = MIN(ball.vel, MAX_VEL);

                    ball.vec.y *= -1;
                    y = NEW_BALL_Y();

                    break;
                }
            }
        }
    }

    ball.pos.x = x;
    ball.pos.y = y;
}

static void check_win(void) {
    if(bricks_broken == (N_ROWS * N_COLS)) {
        if(game_running) {
            for(uint8_t row = 0; row < N_ROWS; row++) {
                brick_rows[row] = 0;
            }

            init_paddle_pos();
        }
        game_running = 0;

        ssd1306_SetCursor(((CANVAS_W / 2) - ((8 * 11) / 2)) + 1, 10);
        ssd1306_WriteString("You Won!", Font_11x18, White);
    } else if(lives == 0) {
        if(game_running) {
            for(uint8_t row = 0; row < N_ROWS; row++) {
                brick_rows[row] = 0;
            }

            init_paddle_pos();
        }
        game_running = 0;

        ssd1306_SetCursor((CANVAS_W / 2) - ((9 * 11) / 2) + 1, 10);
        ssd1306_WriteString("You Lost!", Font_11x18, White);
    }
}

static void draw_screen(lv_timer_t * timer) {
    ssd1306_Fill(Black);

    update_ball();
    draw_lives();
    draw_bricks();
    draw_paddle();
    draw_ball();
    check_win();

    ssd1306_UpdateScreen();
}
