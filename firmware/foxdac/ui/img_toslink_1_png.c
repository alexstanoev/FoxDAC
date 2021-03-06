#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif


#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_IMG_TOSLINK_1_PNG
#define LV_ATTRIBUTE_IMG_IMG_TOSLINK_1_PNG
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_IMG_TOSLINK_1_PNG uint8_t img_toslink_1_png_map[] = {
  0x04, 0x02, 0x04, 0xff, 	/*Color of index 0*/
  0xfc, 0xfe, 0xfc, 0xff, 	/*Color of index 1*/

  0x00, 0x00, 0x00, 0x00, 
  0x03, 0xff, 0xf0, 0x00, 
  0x07, 0xff, 0xf8, 0x00, 
  0x0f, 0xff, 0xfc, 0x00, 
  0x1f, 0xff, 0xfe, 0x00, 
  0x3e, 0x00, 0x1f, 0x00, 
  0x7c, 0x00, 0x0f, 0x80, 
  0x78, 0x0c, 0x07, 0x80, 
  0x78, 0x3f, 0x07, 0x80, 
  0x78, 0x7f, 0x87, 0x80, 
  0xf8, 0xff, 0xc7, 0xc0, 
  0xf8, 0xe1, 0xc7, 0xc0, 
  0xf9, 0xe1, 0xe7, 0xc0, 
  0xf9, 0xe1, 0xe7, 0xc0, 
  0x78, 0xe1, 0xc7, 0x80, 
  0x78, 0xff, 0xc7, 0x80, 
  0x78, 0x7f, 0x87, 0x80, 
  0x78, 0x3f, 0x07, 0x80, 
  0x78, 0x0c, 0x07, 0x80, 
  0x78, 0x00, 0x07, 0x80, 
  0x78, 0x00, 0x07, 0x80, 
  0x7f, 0xff, 0xff, 0x80, 
  0x78, 0x7f, 0xff, 0x80, 
  0x78, 0x7f, 0xff, 0x80, 
  0x3f, 0xff, 0xff, 0x00, 
  0x00, 0x00, 0x00, 0x00, 
};

const lv_img_dsc_t img_toslink_1_png = {
  .header.always_zero = 0,
  .header.w = 26,
  .header.h = 26,
  .data_size = 112,
  .header.cf = LV_IMG_CF_INDEXED_1BIT,
  .data = img_toslink_1_png_map,
};

