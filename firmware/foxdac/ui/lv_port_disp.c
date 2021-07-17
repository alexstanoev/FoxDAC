/**
 * @file lv_port_disp_templ.c
 *
 */

// https://blog.lvgl.io/2019-05-06/oled

 /*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/

#include "../drivers/ssd1306/ssd1306.h"

#include "lv_port_disp.h"
#include "lvgl/lvgl.h"

/*********************
 *      DEFINES
 *********************/

#define DISP_BUF_SIZE (128*64/8)
//#define DISP_BUF_SIZE (128*64)


/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);

static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);
//static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//        const lv_area_t * fill_area, lv_color_t color);
static void rounder_cb(struct _lv_disp_drv_t * disp_drv, lv_area_t * area);

static void set_px_cb(struct _lv_disp_drv_t * disp_drv, uint8_t * buf, lv_coord_t buf_w, lv_coord_t x, lv_coord_t y,
        lv_color_t color, lv_opa_t opa);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_disp_init(void)
{
    /*-------------------------
     * Initialize your display
     * -----------------------*/
    disp_init();

    /*-----------------------------
     * Create a buffer for drawing
     *----------------------------*/

    /**
     * LVGL requires a buffer where it internally draws the widgets.
     * Later this buffer will passed to your display driver's `flush_cb` to copy its content to your display.
     * The buffer has to be greater than 1 display row
     *
     * There are 3 buffering configurations:
     * 1. Create ONE buffer:
     *      LVGL will draw the display's content here and writes it to your display
     *
     * 2. Create TWO buffer:
     *      LVGL will draw the display's content to a buffer and writes it your display.
     *      You should use DMA to write the buffer's content to the display.
     *      It will enable LVGL to draw the next part of the screen to the other buffer while
     *      the data is being sent form the first buffer. It makes rendering and flushing parallel.
     *
     * 3. Double buffering
     *      Set 2 screens sized buffers and set disp_drv.full_refresh = 1.
     *      This way LVGL will always provide the whole rendered screen in `flush_cb`
     *      and you only need to change the frame buffer's address.
     */

//    /* Example for 1) */
//    static lv_disp_draw_buf_t draw_buf_dsc_1;
//    static lv_color_t buf_1[MY_DISP_HOR_RES * 10];                          /*A buffer for 10 rows*/
//    lv_disp_draw_buf_init(&draw_buf_dsc_1, buf_1, NULL, MY_DISP_HOR_RES * 10);   /*Initialize the display buffer*/
//
//    /* Example for 2) */
//    static lv_disp_draw_buf_t draw_buf_dsc_2;
//    static lv_color_t buf_2_1[MY_DISP_HOR_RES * 10];                        /*A buffer for 10 rows*/
//    static lv_color_t buf_2_1[MY_DISP_HOR_RES * 10];                        /*An other buffer for 10 rows*/
//    lv_disp_draw_buf_init(&draw_buf_dsc_2, buf_2_1, buf_2_1, MY_DISP_HOR_RES * 10);   /*Initialize the display buffer*/
//
//    /* Example for 3) also set disp_drv.full_refresh = 1 below*/
//    static lv_disp_draw_buf_t draw_buf_dsc_3;
//    static lv_color_t buf_3_1[MY_DISP_HOR_RES * MY_DISP_VER_RES];            /*A screen sized buffer*/
//    static lv_color_t buf_3_1[MY_DISP_HOR_RES * MY_DISP_VER_RES];            /*An other screen sized buffer*/
//    lv_disp_draw_buf_init(&draw_buf_dsc_3, buf_3_1, buf_3_2, MY_DISP_VER_RES * LV_VER_RES_MAX);   /*Initialize the display buffer*/

    static lv_color_t gbuf[DISP_BUF_SIZE];  /*Memory area used as display buffer */
    static lv_disp_draw_buf_t draw_buf_dsc;
    lv_disp_draw_buf_init(&draw_buf_dsc, gbuf, NULL, DISP_BUF_SIZE);   /*Initialize the display buffer*/


    //lv_disp_buf_init(&disp_buf, gbuf, NULL, DISP_BUF_SIZE); /* DISP_BUF_SIZE can be smaller than the actual display */

    /*-----------------------------------
     * Register the display in LVGL
     *----------------------------------*/

    static lv_disp_drv_t disp_drv;                         /*Descriptor of a display driver*/

    lv_disp_drv_init(&disp_drv);                    /*Basic initialization*/

    /*Set up the functions to access to your display*/

    /*Set the resolution of the display*/
    disp_drv.hor_res = 128;
    disp_drv.ver_res = 64;

    /*Used to copy the buffer's content to the display*/
    disp_drv.flush_cb = &disp_flush;

    disp_drv.rounder_cb = &rounder_cb;

    /*Set a display buffer*/
    disp_drv.draw_buf = &draw_buf_dsc;

    disp_drv.set_px_cb = &set_px_cb;

    disp_drv.antialiasing = 0;

    /*Required for Example 3)*/
    //disp_drv.full_refresh = 1;

    /* Fill a memory array with a color if you have GPU.
     * Note that, in lv_conf.h you can enable GPUs that has built-in support in LVGL.
     * But if you have a different GPU you can use with this callback.*/
    //disp_drv.gpu_fill_cb = gpu_fill;

    /*Finally register the driver*/
    lv_disp_drv_register(&disp_drv);


    lv_theme_t * th = lv_theme_mono_init(lv_disp_get_default(), true, &lv_font_montserrat_10);
    lv_disp_set_theme(lv_disp_get_default(), th);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*Initialize your display and the required peripherals.*/
static void disp_init(void)
{
    /*You code here*/
}

#define BIT_SET(a,b) ((a) |= (1U<<(b)))
#define BIT_CLEAR(a,b) ((a) &= ~(1U<<(b)))
static void set_px_cb(struct _lv_disp_drv_t * disp_drv, uint8_t * buf, lv_coord_t buf_w, lv_coord_t x, lv_coord_t y,
        lv_color_t color, lv_opa_t opa) {

	//if(x > 128 || y > 64) return;

//  uint16_t byte_index = x + (( y>>3 ) * buf_w);
//  uint8_t  bit_index  = y & 0x7;
//
//  if ( color.full == 1 ) {
//    BIT_SET( buf[ byte_index ] , bit_index );
//  } else {
//    BIT_CLEAR( buf[ byte_index ] , bit_index );
//  }

    // Draw in the right color
    if(color.full == 1) {
        buf[x + (y / 8) * buf_w] |= 1 << (y % 8);
    } else {
        buf[x + (y / 8) * buf_w] &= ~(1 << (y % 8));
    }

	//if(color.full == 0) printf("set x %u y %u = %u\n", x, y, color.full);

	//ssd1306_DrawPixel(x, y * 8, (color.full == 1));

}

/*Flush the content of the internal buffer the specific area on the display
 *You can use DMA or any hardware acceleration to do this operation in the background but
 *'lv_disp_flush_ready()' has to be called when finished.*/

static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{

	//if(area->y2 > 64) area->y2 = 64;
	//if(area->x2 > 128) area->x2 = 128;

    uint8_t row1 = area->y1>>3;
    uint8_t row2 = area->y2>>3;
    uint8_t *buf = (uint8_t*) color_p;

    uint32_t buf_pos = 0;

    for(uint8_t row = row1; row <= row2; row++) {

    	//printf("row %u x1 %u x2 %u\n", row, area->x1, area->x2);

    	uint16_t col = area->x1;// + 2;
        ssd1306_WriteCommand(0xB0 | row); // Set the current RAM page address.
        ssd1306_WriteCommand(0x00 | (col & 0xF));
        ssd1306_WriteCommand(0x10 | ((col>>4) & 0xF));

        ssd1306_WriteData(buf, (area->x2 - area->x1));
        buf += area->x2 - area->x1;
    }

    //printf("y1 %u y2 %u x1 %u z2 %u\n", area->y1 >> 3, area->y2 >> 3, area->x1, area->x2);

    //ssd1306_WriteData(buf, DISP_BUF_SIZE);
    //ssd1306_UpdateScreen();

    /* IMPORTANT!!!
     * Inform the graphics library that you are ready with the flushing*/
    lv_disp_flush_ready(disp_drv);
}

void rounder_cb(struct _lv_disp_drv_t * disp_drv, lv_area_t * area) {
  area->y1 = (area->y1 & (~0x7));
  area->y2 = (area->y2 & (~0x7)) + 7;
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
