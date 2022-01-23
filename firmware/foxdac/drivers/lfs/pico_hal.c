/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the BSD 3 clause license, which unfortunately
 * won't be written for another century.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * A little flash file system for the Raspberry Pico
 *
 */

#include <limits.h>

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/mutex.h"
#include "pico/time.h"

#include "pico_hal.h"

#define FS_SIZE (32 * 1024)

static int pico_hal_read(lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size);
static int pico_hal_prog(lfs_block_t block, lfs_off_t off, const void* buffer, lfs_size_t size);
static int pico_hal_erase(lfs_block_t block);
static int pico_lock(void);
static int pico_unlock(void);

// configuration of the filesystem is provided by this struct
// for Pico: prog size = 256, block size = 4096, so cache is 8K
// minimum cache = block size, must be multiple
struct lfs_config pico_cfg = {
    // block device operations
    .read = pico_hal_read,
    .prog = pico_hal_prog,
    .erase = pico_hal_erase,
#if LIB_PICO_MULTICORE
    .lock = pico_lock,
    .unlock = pico_unlock,
#endif
    // block device configuration
    .read_size = 1,
    .prog_size = FLASH_PAGE_SIZE,
    .block_size = FLASH_SECTOR_SIZE,
    .block_count = FS_SIZE / FLASH_SECTOR_SIZE,
    .cache_size = FLASH_SECTOR_SIZE / 4,
    .lookahead_size = 32,
    .block_cycles = 500};

// Pico specific hardware abstraction functions

// file system offset in flash
const char* FS_BASE = (char*)(PICO_FLASH_SIZE_BYTES - FS_SIZE);

static int pico_hal_read(lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size) {
    assert(block < pico_cfg.block_count);
    assert(off + size <= pico_cfg.block_size);
    // read flash via XIP mapped space
    memcpy(buffer, FS_BASE + XIP_NOCACHE_NOALLOC_BASE + (block * pico_cfg.block_size) + off, size);
    return LFS_ERR_OK;
}

static int pico_hal_prog(lfs_block_t block, lfs_off_t off, const void* buffer, lfs_size_t size) {
    assert(block < pico_cfg.block_count);
    // program with SDK
    uint32_t p = (uint32_t)FS_BASE + (block * pico_cfg.block_size) + off;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(p, buffer, size);
    restore_interrupts(ints);
    return LFS_ERR_OK;
}

static int pico_hal_erase(lfs_block_t block) {
    assert(block < pico_cfg.block_count);
    // erase with SDK
    uint32_t p = (uint32_t)FS_BASE + block * pico_cfg.block_size;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(p, pico_cfg.block_size);
    restore_interrupts(ints);
    return LFS_ERR_OK;
}

#if LIB_PICO_MULTICORE

static recursive_mutex_t fs_mtx;

static int pico_lock(void) {
    recursive_mutex_enter_blocking(&fs_mtx);
    return LFS_ERR_OK;
}

static int pico_unlock(void) {
    recursive_mutex_exit(&fs_mtx);
    return LFS_ERR_OK;
}
#endif

// utility functions

static uint32_t tm;

void hal_start(void) { tm = time_us_32(); }

float hal_elapsed(void) { return (time_us_32() - tm) / 1000000.0; }

// posix emulation

int pico_mount(bool format) {
#if LIB_PICO_MULTICORE
    recursive_mutex_init(&fs_mtx);
#endif
    if (format)
        lfs_format(&pico_cfg);
    // mount the filesystem
    return lfs_mount(&pico_cfg);
}

int pico_open(const char* path, int flags) {
    lfs_file_t* file = lfs_malloc(sizeof(lfs_file_t));
    if (file == NULL)
        return LFS_ERR_NOMEM;
    int err = lfs_file_open(file, path, flags);
    if (err != LFS_ERR_OK){
        lfs_free(file);
        return err;
    }
    return (int)file;
}

int pico_close(int file) {
    int res = lfs_file_close((lfs_file_t*)file);
    lfs_free((lfs_file_t*)file);
    return res;
}

lfs_size_t pico_write(int file, const void* buffer, lfs_size_t size) {
    return lfs_file_write((lfs_file_t*)file, buffer, size);
}

lfs_size_t pico_read(int file, void* buffer, lfs_size_t size) {
    return lfs_file_read((lfs_file_t*)file, buffer, size);
}

int pico_rewind(int file) { return lfs_file_rewind((lfs_file_t*)file); }

int pico_unmount(void) { return lfs_unmount(); }

int pico_remove(const char* path) { return lfs_remove(path); }

int pico_rename(const char* oldpath, const char* newpath) { return lfs_rename(oldpath, newpath); }

int pico_fsstat(struct pico_fsstat_t* stat) {
    stat->block_count = pico_cfg.block_count;
    stat->block_size = pico_cfg.block_size;
    stat->blocks_used = lfs_fs_size();
    return LFS_ERR_OK;
}

lfs_soff_t pico_lseek(int file, lfs_soff_t off, int whence) {
    return lfs_file_seek((lfs_file_t*)file, off, whence);
}

int pico_truncate(int file, lfs_off_t size) { return lfs_file_truncate((lfs_file_t*)file, size); }

lfs_soff_t pico_tell(int file) { return lfs_file_tell((lfs_file_t*)file); }

int pico_stat(const char* path, struct lfs_info* info) { return lfs_stat(path, info); }

lfs_ssize_t pico_getattr(const char* path, uint8_t type, void* buffer, lfs_size_t size) {
    return lfs_getattr(path, type, buffer, size);
}

int pico_setattr(const char* path, uint8_t type, const void* buffer, lfs_size_t size) {
    return lfs_setattr(path, type, buffer, size);
}

int pico_removeattr(const char* path, uint8_t type) { return lfs_removeattr(path, type); }

int pico_opencfg(int file, const char* path, int flags, const struct lfs_file_config* config) {
    return lfs_file_opencfg((lfs_file_t*)file, path, flags, config);
}

int pico_fflush(int file) { return lfs_file_sync((lfs_file_t*)file); }

lfs_soff_t pico_size(int file) { return lfs_file_size((lfs_file_t*)file); }

int pico_mkdir(const char* path) { return lfs_mkdir(path); }

int pico_dir_open(const char* path) {
	lfs_dir_t* dir = lfs_malloc(sizeof(lfs_dir_t));
	if (dir == NULL)
		return -1;
	if (lfs_dir_open(dir, path) != LFS_ERR_OK) {
		lfs_free(dir);
		return -1;
	}
	return (int)dir;
}

int pico_dir_close(int dir) {
	return lfs_dir_close((lfs_dir_t*)dir);
	lfs_free((void*)dir);
}

int pico_dir_read(int dir, struct lfs_info* info) { return lfs_dir_read((lfs_dir_t*)dir, info); }

int pico_dir_seek(int dir, lfs_off_t off) { return lfs_dir_seek((lfs_dir_t*)dir, off); }

lfs_soff_t pico_dir_tell(int dir) { return lfs_dir_tell((lfs_dir_t*)dir); }

int pico_dir_rewind(int dir) { return lfs_dir_rewind((lfs_dir_t*)dir); }

const char* pico_errmsg(int err) {
    static const struct {
        int err;
        char* text;
    } mesgs[] = {{LFS_ERR_OK, "No error"},
                 {LFS_ERR_IO, "Error during device operation"},
                 {LFS_ERR_CORRUPT, "Corrupted"},
                 {LFS_ERR_NOENT, "No directory entry"},
                 {LFS_ERR_EXIST, "Entry already exists"},
                 {LFS_ERR_NOTDIR, "Entry is not a dir"},
                 {LFS_ERR_ISDIR, "Entry is a dir"},
                 {LFS_ERR_NOTEMPTY, "Dir is not empty"},
                 {LFS_ERR_BADF, "Bad file number"},
                 {LFS_ERR_FBIG, "File too large"},
                 {LFS_ERR_INVAL, "Invalid parameter"},
                 {LFS_ERR_NOSPC, "No space left on device"},
                 {LFS_ERR_NOMEM, "No more memory available"},
                 {LFS_ERR_NOATTR, "No data/attr available"},
                 {LFS_ERR_NAMETOOLONG, "File name too long"}};

    for (int i = 0; i < sizeof(mesgs) / sizeof(mesgs[0]); i++)
        if (err == mesgs[i].err)
            return mesgs[i].text;
    return "Unknown error";
}
