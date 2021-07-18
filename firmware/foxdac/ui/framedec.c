#include<stdint.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#define BADAPPLE_FRAMES 5257

static uint8_t frame[1024];
static int frread = 2;
static int cur_frame = 0;
static uint8_t running = 0;

static int rle_decode(uint8_t *out, const uint8_t *in, int l) {
  int j, tb;
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
    cur_frame = 0;
    frread = 2;
    running = 1;
}

void badapple_stop(void)
{
    cur_frame = 0;
    frread = 2;
    running = 0;
}

void badapple_next_frame(void) {
    int rle_len = (((int)badapple[frread - 2]) << 8) | ((int) badapple[frread - 1]);

    int out_len = rle_decode(frame, &badapple[frread], rle_len);
    frread += rle_len + 2;

    cur_frame++;

    if(cur_frame == BADAPPLE_FRAMES) {
        badapple_stop();
    }
}

//int frread = 2;
//for(int frames = 0; frames < BADAPPLE_FRAMES; frames++) {
//  int rle_len = (((int)badapple[frread - 2]) << 8) | ((int) badapple[frread - 1]);
//
//  int out_len = rle_decode(frame, &badapple[frread], rle_len);
//  frread += rle_len + 2;
//}
