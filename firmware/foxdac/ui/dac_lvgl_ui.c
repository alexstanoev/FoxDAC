#include "dac_lvgl_ui.h"

///////////////////// VARIABLES ////////////////////
lv_obj_t * MainUI;
lv_obj_t * VolumeImg;
lv_obj_t * VolumeSlider;
lv_obj_t * SamplFreqLbl;
lv_obj_t * VolumeLbl;
lv_obj_t * UsbImg;
lv_obj_t * OpticalImg1;
lv_obj_t * OpticalImg2;
lv_obj_t * OpticalImg3;
lv_obj_t * Logo;
lv_obj_t * LogoImg;

///////////////////// IMAGES ////////////////////
LV_IMG_DECLARE(img_speaker_png);   // assets/speaker.png
LV_IMG_DECLARE(img_usb_png);   // assets/usb.png
LV_IMG_DECLARE(img_toslink_1_png);   // assets/toslink_1.png
LV_IMG_DECLARE(img_toslink_2_png);   // assets/toslink_2.png
LV_IMG_DECLARE(img_toslink_3_png);   // assets/toslink_3.png
LV_IMG_DECLARE(img_fox_logo_png);   // assets/fox_logo.png

///////////////////// ANIMATIONS ////////////////////

///////////////////// FUNCTIONS2 ////////////////////
static void VolumeSlider_eventhandler(lv_obj_t * obj, lv_event_t event)
{
}

///////////////////// SCREENS ////////////////////
void DAC_BuildPages(void)
{
    MainUI = lv_obj_create(NULL);

    VolumeImg = lv_img_create(MainUI);
    lv_img_set_src(VolumeImg, &img_speaker_png);
    lv_obj_set_size(VolumeImg, 16, 16);
    lv_obj_align(VolumeImg, LV_ALIGN_CENTER, -54, 23);

    lv_obj_clear_state(VolumeImg, LV_STATE_DISABLED);

    VolumeSlider = lv_slider_create(MainUI);
    lv_obj_set_size(VolumeSlider, 107, 4);
    lv_obj_align(VolumeSlider, LV_ALIGN_CENTER, 9, 23);
    lv_slider_set_range(VolumeSlider, 0, 100);
    lv_slider_set_mode(VolumeSlider, LV_BAR_MODE_NORMAL);
    lv_slider_set_value(VolumeSlider, 25, LV_ANIM_OFF);
    lv_slider_set_left_value(VolumeSlider, 0, LV_ANIM_OFF);
    //lv_obj_set_event_cb(VolumeSlider, VolumeSlider_eventhandler);
    lv_obj_clear_state(VolumeSlider, LV_STATE_DISABLED);

    SamplFreqLbl = lv_label_create(MainUI);
    lv_label_set_long_mode(SamplFreqLbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_align(SamplFreqLbl, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(SamplFreqLbl, "44.1 kHz");
    lv_obj_set_size(SamplFreqLbl, 77, 16);  // force: -23
    lv_obj_clear_state(SamplFreqLbl, LV_STATE_DISABLED);

    lv_obj_align(SamplFreqLbl, LV_ALIGN_CENTER, -23, -24); // force: 77

    VolumeLbl = lv_label_create(MainUI);
    lv_label_set_long_mode(VolumeLbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_align(VolumeLbl, LV_TEXT_ALIGN_RIGHT);
    lv_label_set_text(VolumeLbl, "-5dB");
    lv_obj_set_size(VolumeLbl, 34, 16);  // force: 44
    lv_obj_clear_state(VolumeLbl, LV_STATE_DISABLED);

    lv_obj_align(VolumeLbl, LV_ALIGN_CENTER, 44, -23); // force: 34

    UsbImg = lv_img_create(MainUI);
    lv_img_set_src(UsbImg, &img_usb_png);
    lv_obj_set_size(UsbImg, 24, 24);
    lv_obj_align(UsbImg, LV_ALIGN_CENTER, -49, -1);

    lv_obj_clear_state(UsbImg, LV_STATE_DISABLED);

    OpticalImg1 = lv_img_create(MainUI);
    lv_img_set_src(OpticalImg1, &img_toslink_1_png);
    lv_obj_set_size(OpticalImg1, 26, 26);
    lv_obj_align(OpticalImg1, LV_ALIGN_CENTER, -18, -1);

    lv_obj_clear_state(OpticalImg1, LV_STATE_DISABLED);

    OpticalImg2 = lv_img_create(MainUI);
    lv_img_set_src(OpticalImg2, &img_toslink_2_png);
    lv_obj_set_size(OpticalImg2, 26, 26);
    lv_obj_align(OpticalImg2, LV_ALIGN_CENTER, 14, -1);

    lv_obj_clear_state(OpticalImg2, LV_STATE_DISABLED);

    OpticalImg3 = lv_img_create(MainUI);
    lv_img_set_src(OpticalImg3, &img_toslink_3_png);
    lv_obj_set_size(OpticalImg3, 26, 26);
    lv_obj_align(OpticalImg3, LV_ALIGN_CENTER, 46, -1);

    lv_obj_clear_state(OpticalImg3, LV_STATE_DISABLED);

    Logo = lv_obj_create(NULL);

    LogoImg = lv_img_create(Logo);
    lv_img_set_src(LogoImg, &img_fox_logo_png);
    lv_obj_set_size(LogoImg, 128, 64);
    lv_obj_align(LogoImg, LV_ALIGN_CENTER, 0, 0);

    lv_obj_clear_state(LogoImg, LV_STATE_DISABLED);

    // selection box


    static lv_style_t style;
    lv_style_init(&style);

    /*Set a background color and a radius*/
    lv_style_set_radius(&style, 5);

    /* Add outline*/
    lv_color_t white = { .full = 1 };

    lv_style_set_outline_width(&style, 1);
    lv_style_set_outline_color(&style, white);
    lv_style_set_outline_pad(&style, 2);

    /*Create an object with the new style*/
    lv_obj_add_style(UsbImg, &style, LV_PART_MAIN);

    // init

    lv_scr_load_anim(Logo, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);

    lv_scr_load_anim(MainUI, LV_SCR_LOAD_ANIM_MOVE_TOP, 300, 3000, false);

}

