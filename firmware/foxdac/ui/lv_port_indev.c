/**
 * @file lv_port_indev_templ.c
 *
 */

 /*Copy this file as "lv_port_indev.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_indev.h"
#include "lvgl/lvgl.h"

#include "../drivers/encoder/encoder.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void encoder_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);

//static void button_init(void);
//static void button_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
//static int8_t button_get_pressed_id(void);
//static bool button_is_pressed(uint8_t id);

/**********************
 *  STATIC VARIABLES
 **********************/
lv_indev_t * indev_encoder;
lv_indev_t * indev_button;

static int32_t encoder_diff;
static lv_indev_state_t encoder_state;

uint8_t lv_indev_pause_encoder = 0;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_indev_init(void)
{
    /**
     * Here you will find example implementation of input devices supported by LittelvGL:
     *  - Touchpad
     *  - Mouse (with cursor support)
     *  - Keypad (supports GUI usage only with key)
     *  - Encoder (supports GUI usage only with: left, right, push)
     *  - Button (external buttons to press points on the screen)
     *
     *  The `..._read()` function are only examples.
     *  You should shape them according to your hardware
     */

    static lv_indev_drv_t indev_drv;

    /*------------------
     * Encoder
     * -----------------*/

    /*Register a encoder input device*/
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_ENCODER;
    indev_drv.read_cb = encoder_read;
    //indev_drv.feedback_cb = encoder_feedback; // TODO
    indev_encoder = lv_indev_drv_register(&indev_drv);

    /*Later you should create group(s) with `lv_group_t * group = lv_group_create()`,
     *add objects to the group with `lv_group_add_obj(group, obj)`
     *and assign this input device to group to navigate in it:
     *`lv_indev_set_group(indev_encoder, group);`*/

    /*------------------
     * Button
     * -----------------*/
//
//    /*Initialize your button if you have*/
//    button_init();
//
//    /*Register a button input device*/
//    lv_indev_drv_init(&indev_drv);
//    indev_drv.type = LV_INDEV_TYPE_BUTTON;
//    indev_drv.read_cb = button_read;
//    indev_button = lv_indev_drv_register(&indev_drv);
//
//    /*Assign buttons to points on the screen*/
//    static const lv_point_t btn_points[2] = {
//            {10, 10},   /*Button 0 -> x:10; y:10*/
//            {40, 100},  /*Button 1 -> x:40; y:100*/
//    };
//    lv_indev_set_button_points(indev_button, btn_points);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*------------------
 * Encoder
 * -----------------*/

/*Will be called by the library to read the encoder*/
static void encoder_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    data->enc_diff = lv_indev_pause_encoder ? 0 : encoder_get_delta();

    // if encoder pressed LV_INDEV_STATE_PR else LV_INDEV_STATE_REL
    data->state = LV_INDEV_STATE_REL;
}

/*------------------
 * Button
 * -----------------*/
//
///*Initialize your buttons*/
//static void button_init(void)
//{
//    /*Your code comes here*/
//}
//
///*Will be called by the library to read the button*/
//static void button_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
//{
//
//    static uint8_t last_btn = 0;
//
//    /*Get the pressed button's ID*/
//    int8_t btn_act = button_get_pressed_id();
//
//    if(btn_act >= 0) {
//        data->state = LV_INDEV_STATE_PR;
//        last_btn = btn_act;
//    } else {
//        data->state = LV_INDEV_STATE_REL;
//    }
//
//    /*Save the last pressed button's ID*/
//    data->btn_id = last_btn;
//}
//
///*Get ID  (0, 1, 2 ..) of the pressed button*/
//static int8_t button_get_pressed_id(void)
//{
//    uint8_t i;
//
//    /*Check to buttons see which is being pressed (assume there are 2 buttons)*/
//    for(i = 0; i < 2; i++) {
//        /*Return the pressed button's ID*/
//        if(button_is_pressed(i)) {
//            return i;
//        }
//    }
//
//    /*No button pressed*/
//    return -1;
//}
//
///*Test if `id` button is pressed or not*/
//static bool button_is_pressed(uint8_t id)
//{
//
//    /*Your code comes here*/
//
//    return false;
//}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
