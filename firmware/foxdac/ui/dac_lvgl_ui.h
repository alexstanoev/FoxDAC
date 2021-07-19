#ifndef TESTBOARD_UI_H
#define TESTBOARD_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

extern lv_obj_t * MainUI;
extern lv_obj_t * VolumeImg;
extern lv_obj_t * VolumeSlider;
extern lv_obj_t * SamplFreqLbl;
extern lv_obj_t * VolumeLbl;
extern lv_obj_t * UsbImg;
extern lv_obj_t * OpticalImg1;
extern lv_obj_t * OpticalImg2;
extern lv_obj_t * OpticalImg3;
extern lv_obj_t * Logo;
extern lv_obj_t * LogoImg;


LV_IMG_DECLARE(img_speaker_png);   // assets/speaker.png
LV_IMG_DECLARE(img_usb_png);   // assets/usb.png
LV_IMG_DECLARE(img_toslink_1_png);   // assets/toslink_1.png
LV_IMG_DECLARE(img_toslink_2_png);   // assets/toslink_2.png
LV_IMG_DECLARE(img_toslink_3_png);   // assets/toslink_3.png
LV_IMG_DECLARE(img_fox_logo_png);   // assets/fox_logo.png

void UI_SetVolume(int32_t vol);
void DAC_BuildPages(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
