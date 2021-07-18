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

///////////////////// FUNCTIONS ////////////////////
#define BAR_PROPERTY_VALUE 0
#define BAR_PROPERTY_VALUE_WITH_ANIM 1

void SetBarProperty(lv_obj_t * target, int id, int val)
{
    if(id == BAR_PROPERTY_VALUE_WITH_ANIM) lv_bar_set_value(target, val, LV_ANIM_ON);
    if(id == BAR_PROPERTY_VALUE) lv_bar_set_value(target, val, LV_ANIM_OFF);
}

#define BASIC_PROPERTY_POSITION_X 0
#define BASIC_PROPERTY_POSITION_Y 1
#define BASIC_PROPERTY_WIDTH 2
#define BASIC_PROPERTY_HEIGHT 3
#define BASIC_PROPERTY_CLICKABLE 4
#define BASIC_PROPERTY_HIDDEN 5
#define BASIC_PROPERTY_DRAGABLE 6
#define BASIC_PROPERTY_DISABLED 7

void SetBasicProperty(lv_obj_t * target, int id, int val)
{
    if(id == BASIC_PROPERTY_POSITION_X) lv_obj_set_x(target, val);
    if(id == BASIC_PROPERTY_POSITION_Y) lv_obj_set_y(target, val);
    if(id == BASIC_PROPERTY_WIDTH) lv_obj_set_width(target, val);
    if(id == BASIC_PROPERTY_HEIGHT) lv_obj_set_height(target, val);
}

void SetBasicPropertyB(lv_obj_t * target, int id, bool val)
{
    if(id == BASIC_PROPERTY_CLICKABLE) lv_obj_set_click(target, val);
    if(id == BASIC_PROPERTY_HIDDEN) lv_obj_set_hidden(target, val);
    if(id == BASIC_PROPERTY_DRAGABLE) lv_obj_set_drag(target, val);
    if(id == BASIC_PROPERTY_DISABLED) {
        if(val) lv_obj_add_state(target, LV_STATE_DISABLED);
        else lv_obj_clear_state(target, LV_STATE_DISABLED);
    }
}

#define BUTTON_PROPERTY_TOGGLE 0
#define BUTTON_PROPERTY_CHECKED 1

void SetButtonProperty(lv_obj_t * target, int id, bool val)
{
    if(id == BUTTON_PROPERTY_TOGGLE) lv_btn_toggle(target);
    if(id == BUTTON_PROPERTY_CHECKED) lv_btn_set_state(target, val ? LV_BTN_STATE_CHECKED_RELEASED : LV_BTN_STATE_RELEASED);
}

#define DROPDOWN_PROPERTY_SELECTED 0

void SetDropdownProperty(lv_obj_t * target, int id, int val)
{
    if(id == DROPDOWN_PROPERTY_SELECTED) lv_dropdown_set_selected(target, val);
}

#define IMAGE_PROPERTY_IMAGE 0

void SetImageProperty(lv_obj_t * target, int id, uint8_t * val)
{
    if(id == IMAGE_PROPERTY_IMAGE) lv_img_set_src(target, val);
}

#define LABEL_PROPERTY_TEXT 0

void SetLabelProperty(lv_obj_t * target, int id, char * val)
{
    if(id == LABEL_PROPERTY_TEXT) lv_label_set_text(target, val);
}

#define ROLLER_PROPERTY_SELECTED 0
#define ROLLER_PROPERTY_SELECTED_WITH_ANIM 1

void SetRollerProperty(lv_obj_t * target, int id, int val)
{
    if(id == ROLLER_PROPERTY_SELECTED_WITH_ANIM) lv_roller_set_selected(target, val, LV_ANIM_ON);
    if(id == ROLLER_PROPERTY_SELECTED) lv_roller_set_selected(target, val, LV_ANIM_OFF);
}

#define SLIDER_PROPERTY_VALUE 0
#define SLIDER_PROPERTY_VALUE_WITH_ANIM 1

void SetSliderProperty(lv_obj_t * target, int id, int val)
{
    if(id == SLIDER_PROPERTY_VALUE_WITH_ANIM) lv_slider_set_value(target, val, LV_ANIM_ON);
    if(id == SLIDER_PROPERTY_VALUE) lv_slider_set_value(target, val, LV_ANIM_OFF);
}

void ChangeScreen(lv_obj_t * target, int fademode, int spd, int delay)
{
    lv_scr_load_anim(target, fademode, spd, delay, false);
}

void SetOpacity(lv_obj_t * target, int val)
{
    lv_obj_set_style_local_opa_scale(target, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, val);
}

void anim_callback_set_x(lv_anim_t * a, lv_anim_value_t v)
{
    lv_obj_set_x(a->user_data, v);
}

void anim_callback_set_y(lv_anim_t * a, lv_anim_value_t v)
{
    lv_obj_set_y(a->user_data, v);
}

void anim_callback_set_width(lv_anim_t * a, lv_anim_value_t v)
{
    lv_obj_set_width(a->user_data, v);
}

void anim_callback_set_height(lv_anim_t * a, lv_anim_value_t v)
{
    lv_obj_set_height(a->user_data, v);
}

///////////////////// ANIMATIONS ////////////////////

///////////////////// FUNCTIONS2 ////////////////////
static void VolumeSlider_eventhandler(lv_obj_t * obj, lv_event_t event)
{
}

///////////////////// SCREENS ////////////////////
void DAC_BuildPages(void)
{
    MainUI = lv_obj_create(NULL, NULL);

    VolumeImg = lv_img_create(MainUI, NULL);
    lv_img_set_src(VolumeImg, &img_speaker_png);
    lv_obj_set_click(VolumeImg, false);
    lv_obj_set_hidden(VolumeImg, false);
    lv_obj_set_size(VolumeImg, 16, 16);
    lv_obj_align(VolumeImg, MainUI, LV_ALIGN_CENTER, -54, 23);
    lv_obj_set_drag(VolumeImg, false);

    lv_obj_clear_state(VolumeImg, LV_STATE_DISABLED);

    VolumeSlider = lv_slider_create(MainUI, NULL);
    lv_obj_set_size(VolumeSlider, 107, 4);
    lv_obj_align(VolumeSlider, MainUI, LV_ALIGN_CENTER, 9, 23);
    lv_slider_set_range(VolumeSlider, 0, 100);
    lv_slider_set_type(VolumeSlider, LV_SLIDER_TYPE_NORMAL);
    lv_slider_set_value(VolumeSlider, 25, LV_ANIM_OFF);
    lv_slider_set_left_value(VolumeSlider, 0, LV_ANIM_OFF);
    lv_obj_set_click(VolumeSlider, true);
    lv_obj_set_hidden(VolumeSlider, false);
    lv_obj_set_event_cb(VolumeSlider, VolumeSlider_eventhandler);
    lv_obj_clear_state(VolumeSlider, LV_STATE_DISABLED);
    lv_obj_set_drag(VolumeSlider, false);

    SamplFreqLbl = lv_label_create(MainUI, NULL);
    lv_label_set_long_mode(SamplFreqLbl, LV_LABEL_LONG_CROP);
    lv_label_set_align(SamplFreqLbl, LV_LABEL_ALIGN_LEFT);
    lv_label_set_text(SamplFreqLbl, "44.1 kHz");
    lv_obj_set_size(SamplFreqLbl, 77, 16);  // force: -23
    lv_obj_set_click(SamplFreqLbl, false);
    lv_obj_set_hidden(SamplFreqLbl, false);
    lv_obj_clear_state(SamplFreqLbl, LV_STATE_DISABLED);
    lv_obj_set_drag(SamplFreqLbl, false);

    lv_obj_align(SamplFreqLbl, MainUI, LV_ALIGN_CENTER, -23, -24); // force: 77

    VolumeLbl = lv_label_create(MainUI, NULL);
    lv_label_set_long_mode(VolumeLbl, LV_LABEL_LONG_EXPAND);
    lv_label_set_align(VolumeLbl, LV_LABEL_ALIGN_RIGHT);
    lv_label_set_text(VolumeLbl, "-5dB");
    lv_obj_set_size(VolumeLbl, 34, 16);  // force: 44
    lv_obj_set_click(VolumeLbl, false);
    lv_obj_set_hidden(VolumeLbl, false);
    lv_obj_clear_state(VolumeLbl, LV_STATE_DISABLED);
    lv_obj_set_drag(VolumeLbl, false);

    lv_obj_align(VolumeLbl, MainUI, LV_ALIGN_CENTER, 44, -23); // force: 34

    UsbImg = lv_img_create(MainUI, NULL);
    lv_img_set_src(UsbImg, &img_usb_png);
    lv_obj_set_click(UsbImg, false);
    lv_obj_set_hidden(UsbImg, false);
    lv_obj_set_size(UsbImg, 24, 24);
    lv_obj_align(UsbImg, MainUI, LV_ALIGN_CENTER, -49, -1);
    lv_obj_set_drag(UsbImg, false);

    lv_obj_clear_state(UsbImg, LV_STATE_DISABLED);

    OpticalImg1 = lv_img_create(MainUI, NULL);
    lv_img_set_src(OpticalImg1, &img_toslink_1_png);
    lv_obj_set_click(OpticalImg1, false);
    lv_obj_set_hidden(OpticalImg1, false);
    lv_obj_set_size(OpticalImg1, 26, 26);
    lv_obj_align(OpticalImg1, MainUI, LV_ALIGN_CENTER, -18, -1);
    lv_obj_set_drag(OpticalImg1, false);

    lv_obj_clear_state(OpticalImg1, LV_STATE_DISABLED);

    OpticalImg2 = lv_img_create(MainUI, NULL);
    lv_img_set_src(OpticalImg2, &img_toslink_2_png);
    lv_obj_set_click(OpticalImg2, false);
    lv_obj_set_hidden(OpticalImg2, false);
    lv_obj_set_size(OpticalImg2, 26, 26);
    lv_obj_align(OpticalImg2, MainUI, LV_ALIGN_CENTER, 14, -1);
    lv_obj_set_drag(OpticalImg2, false);

    lv_obj_clear_state(OpticalImg2, LV_STATE_DISABLED);

    OpticalImg3 = lv_img_create(MainUI, NULL);
    lv_img_set_src(OpticalImg3, &img_toslink_3_png);
    lv_obj_set_click(OpticalImg3, false);
    lv_obj_set_hidden(OpticalImg3, false);
    lv_obj_set_size(OpticalImg3, 26, 26);
    lv_obj_align(OpticalImg3, MainUI, LV_ALIGN_CENTER, 46, -1);
    lv_obj_set_drag(OpticalImg3, false);

    lv_obj_clear_state(OpticalImg3, LV_STATE_DISABLED);

    Logo = lv_obj_create(NULL, NULL);

    LogoImg = lv_img_create(Logo, NULL);
    lv_img_set_src(LogoImg, &img_fox_logo_png);
    lv_obj_set_click(LogoImg, false);
    lv_obj_set_hidden(LogoImg, false);
    lv_obj_set_size(LogoImg, 128, 64);
    lv_obj_align(LogoImg, Logo, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_drag(LogoImg, false);

    lv_obj_clear_state(LogoImg, LV_STATE_DISABLED);

}

