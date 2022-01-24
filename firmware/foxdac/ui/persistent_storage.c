/*
 * persistent_storage.c
 *
 *  Wrapper around littlefs
 *  Only call from core 1; core 0 does not access flash after boot
 *
 *  Created on: 23 Jan 2022
 *      Author: alex
 */

#include "pico/stdlib.h"
#include "stdint.h"

#include "../drivers/lfs/pico_hal.h"

lfs_file_t vol_file, input_file, eq_curve_file;

void persist_init(void) {
    if (pico_mount(false) != LFS_ERR_OK) {
        printf("Error mounting FS, formatting\n");
        pico_mount(true);
    }

    struct pico_fsstat_t stat;
    pico_fsstat(&stat);
    printf("FS: blocks %d, block size %d, used %d\n", (int)stat.block_count, (int)stat.block_size,
           (int)stat.blocks_used);

    lfs_file_open(&vol_file, "vol", LFS_O_RDWR | LFS_O_CREAT);
    lfs_file_open(&input_file, "inp", LFS_O_RDWR | LFS_O_CREAT);
    lfs_file_open(&eq_curve_file, "eqc", LFS_O_RDWR | LFS_O_CREAT);
}

void persist_flush_all(void) {
    lfs_file_sync(&vol_file);
    lfs_file_sync(&input_file);
    lfs_file_sync(&eq_curve_file);
}

void persist_read(lfs_file_t* file, uint8_t* val, uint8_t* default_val, int len) {
    int tmp;

    lfs_file_rewind(file);
    lfs_ssize_t read_sz = lfs_file_read(file, val, len);

    if(read_sz != len) {
        // file empty or wrong size, return default
        memcpy(val, default_val, len);
    }
}

void persist_write(lfs_file_t* file, uint8_t* val, int len) {
    lfs_file_rewind(file);
    lfs_ssize_t read_sz = lfs_file_write(file, val, len);
}

uint8_t persist_read_byte(lfs_file_t* file, uint8_t default_val) {
    uint8_t tmp;
    persist_read(file, &tmp, &default_val, sizeof(uint8_t));
    return tmp;
}

void persist_write_byte(lfs_file_t* file, uint8_t val) {
    persist_write(file, &val, sizeof(uint8_t));
}
