/*
 * ui.h
 *
 *  Created on: 15 Jul 2021
 *      Author: alex
 */

#ifndef FOXDAC_UI_H_
#define FOXDAC_UI_H_

void oled_init(void);
void ui_init(void);
void ui_loop(void);
void ui_update_activity(void);

void badapple_start(void);
void badapple_stop(void);
void badapple_next_frame(void);

void breakout_init(void);
void breakout_start(void);
void breakout_stop(void);

#endif /* FOXDAC_UI_H_ */
