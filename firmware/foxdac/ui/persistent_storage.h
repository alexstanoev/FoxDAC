/*
 * persistent_storage.h
 *
 *  Created on: 23 Jan 2022
 *      Author: alex
 */

#ifndef FOXDAC_UI_PERSISTENT_STORAGE_H_
#define FOXDAC_UI_PERSISTENT_STORAGE_H_

#include "../drivers/lfs/pico_hal.h"

extern lfs_file_t vol_file, input_file, eq_curve_file;

void persist_init(void);
void persist_flush_all(void);
void persist_read(lfs_file_t* file, uint8_t* val, uint8_t* default_val, int len);
void persist_write(lfs_file_t* file, uint8_t* val, int len);
uint8_t persist_read_byte(lfs_file_t* file, uint8_t default_val);
void persist_write_byte(lfs_file_t* file, uint8_t val);

#endif /* FOXDAC_UI_PERSISTENT_STORAGE_H_ */
