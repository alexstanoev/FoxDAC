#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif


#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_IMG_SPEAKER_PNG
#define LV_ATTRIBUTE_IMG_IMG_SPEAKER_PNG
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_IMG_SPEAKER_PNG uint8_t img_speaker_png_map[] = {
  0x00, 0x01, 0x00, 0xff,   /*Color of index 0*/
  0xfc, 0xff, 0xfd, 0xff,   /*Color of index 1*/

  0x00, 0x00, 
  0x00, 0x00, 
  0x00, 0x80, 
  0x01, 0x82, 
  0x03, 0x8a, 
  0x07, 0xaa, 
  0x7f, 0xaa, 
  0x7f, 0xaa, 
  0x7f, 0xaa, 
  0x7f, 0xaa, 
  0x7f, 0xaa, 
  0x07, 0xaa,
  0x03, 0x8a,
  0x01, 0x82,
  0x00, 0x80, 
  0x00, 0x00, 
};

const lv_img_dsc_t img_speaker_png = {
  .header.always_zero = 0,
  .header.w = 16,
  .header.h = 16,
  .data_size = 40,
  .header.cf = LV_IMG_CF_INDEXED_1BIT,
  .data = img_speaker_png_map,
};
