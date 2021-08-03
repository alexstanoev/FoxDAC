#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "lvgl/lvgl.h"

#include "../drivers/ssd1306/ssd1306.h"

#define BAD_APPLE 1

#if BAD_APPLE
#include "badapple_rle.h"
#else
const uint8_t badapple[3] = { 0 };
const uint8_t badapple_len = 0;
#endif

static uint32_t fr_byte_idx = 2;
static uint32_t cur_frame = 0;
static uint8_t running = 0;

static lv_timer_t * timer;

// https://rosettacode.org/wiki/Run-length_encoding/C
static int32_t rle_decode(uint8_t *out, const uint8_t *in, int32_t l) {
  int32_t j, tb;
  uint8_t i;
  uint8_t c;

  for(tb=0 ; l>0 ; l -= 2 ) {
    i = *in++;
    c = *in++;
    tb += i;
    while(i-- > 0) *out++ = c;
  }

  return tb;
}

void badapple_start(void)
{
    // big hack
    extern uint8_t lv_port_pause_drawing;
    lv_port_pause_drawing = 1;

    lv_timer_resume(timer);

    cur_frame = 0;
    fr_byte_idx = 2;
    running = 1;
}

void badapple_stop(void)
{
    // undo big hack
    extern uint8_t lv_port_pause_drawing;
    lv_port_pause_drawing = 0;

    cur_frame = 0;
    fr_byte_idx = 2;
    running = 0;

    lv_timer_pause(timer);

    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(lv_disp_get_default());
}

static void badapple_next_frame(lv_timer_t * timer) {
    extern uint8_t SSD1306_Buffer[SSD1306_BUFFER_SIZE];

    int32_t rle_len = (((int32_t) badapple[fr_byte_idx - 2]) << 8) | ((int32_t) badapple[fr_byte_idx - 1]);

    rle_decode(SSD1306_Buffer, &badapple[fr_byte_idx], rle_len);
    fr_byte_idx += rle_len + 2;

    cur_frame++;

    if(fr_byte_idx >= badapple_len) {
        badapple_stop();
        return;
    }

    ssd1306_UpdateScreen();
}

void badapple_init(void) {
    timer = lv_timer_create(badapple_next_frame, 41, NULL); // 24fps = 41.6ms
    lv_timer_pause(timer);
}
