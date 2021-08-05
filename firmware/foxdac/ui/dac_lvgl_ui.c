#include "dac_lvgl_ui.h"
#include "lv_port_indev.h"

#include "../drivers/tpa6130/tpa6130.h"

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

static uint8_t ui_inited = 0;

static lv_style_t StyleSelected;

#define INPUT_LEN 4
static lv_obj_t** input_list[INPUT_LEN] = { &UsbImg, &OpticalImg1, &OpticalImg2, &OpticalImg3 };

///////////////////// IMAGES ////////////////////
LV_IMG_DECLARE(img_speaker_png);   // assets/speaker.png
LV_IMG_DECLARE(img_usb_png);   // assets/usb.png
LV_IMG_DECLARE(img_toslink_1_png);   // assets/toslink_1.png
LV_IMG_DECLARE(img_toslink_2_png);   // assets/toslink_2.png
LV_IMG_DECLARE(img_toslink_3_png);   // assets/toslink_3.png
LV_IMG_DECLARE(img_fox_logo_png);   // assets/fox_logo.png

///////////////////// ANIMATIONS ////////////////////

///////////////////// FUNCTIONS2 ////////////////////
static void VolumeSlider_eventhandler(lv_event_t * event)
{
    int32_t value = lv_slider_get_value(VolumeSlider);
    tpa6130_set_volume(value);

    ui_set_vol_text(tpa6130_get_volume_str(value));
}

void UI_SetVolume(int32_t vol)
{
    if(!ui_inited) return;
    printf("v: %d\n", vol);
    //lv_slider_set_value(VolumeSlider, vol, LV_ANIM_ON);
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
    lv_obj_set_size(VolumeSlider, 96, 4);
    lv_obj_align(VolumeSlider, LV_ALIGN_CENTER, 10, 23);
    lv_obj_set_style_border_width(VolumeSlider, 1, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(VolumeSlider, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(VolumeSlider, 1, LV_STATE_EDITED);
    lv_obj_set_style_outline_width(VolumeSlider, 0, LV_STATE_EDITED);
    lv_slider_set_range(VolumeSlider, 0, 63);
    lv_slider_set_mode(VolumeSlider, LV_BAR_MODE_NORMAL);
    lv_slider_set_value(VolumeSlider, 10, LV_ANIM_OFF);
    lv_slider_set_left_value(VolumeSlider, 0, LV_ANIM_OFF);
    lv_obj_add_event_cb(VolumeSlider, VolumeSlider_eventhandler, LV_EVENT_VALUE_CHANGED, NULL);
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

    lv_style_init(&StyleSelected);

    /*Set a background color and a radius*/
    lv_style_set_radius(&StyleSelected, 5);

    /* Add outline*/
    lv_color_t white = { .full = 1 };

    lv_style_set_outline_width(&StyleSelected, 1);
    lv_style_set_outline_color(&StyleSelected, white);
    lv_style_set_outline_pad(&StyleSelected, 2);

    /*Create an object with the new style*/
    lv_obj_add_style(OpticalImg2, &StyleSelected, LV_PART_MAIN);

    // assign encoder to volume slider
    lv_group_t * EncGroup = lv_group_create();
    lv_group_add_obj(EncGroup, VolumeSlider);
    lv_group_focus_obj(VolumeSlider);
    lv_indev_set_group(indev_encoder, EncGroup);
    lv_group_set_editing(EncGroup, true);

    // assign buttons to input list
    lv_group_t * InputGroup = lv_group_create();
    lv_group_add_obj(InputGroup, UsbImg);
    lv_group_add_obj(InputGroup, OpticalImg1);
    lv_group_add_obj(InputGroup, OpticalImg2);
    lv_group_add_obj(InputGroup, OpticalImg3);

    // init

    lv_scr_load_anim(Logo, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);

    lv_scr_load_anim(MainUI, LV_SCR_LOAD_ANIM_MOVE_TOP, 300, 3000, false);

    ui_inited = 1;
}

void ui_select_input(uint8_t input) {
    if(input >= INPUT_LEN) {
        return;
    }

    for(uint8_t i = 0; i < INPUT_LEN; i++) {
        lv_obj_remove_style(*input_list[i], &StyleSelected, LV_PART_MAIN);
    }

    lv_obj_add_style(*input_list[input], &StyleSelected, LV_PART_MAIN);
}

void ui_set_sr_text(const char* text) {
    lv_label_set_text(SamplFreqLbl, text);
}

void ui_set_vol_text(const char* text) {
    lv_label_set_text(VolumeLbl, text);
}
