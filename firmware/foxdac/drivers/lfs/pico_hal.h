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

#ifndef _HAL_
#define _HAL_

#include "lfs.h"

#ifdef __cplusplus
extern "C" {
#endif

// utility functions

void hal_start(void);

float hal_elapsed(void);

// posix emulation

extern int pico_errno;

struct pico_fsstat_t {
    lfs_size_t block_size;
    lfs_size_t block_count;
    lfs_size_t blocks_used;
};

// Mounts a littlefs
//
// Requires a littlefs object and config struct. Optionally formats
// a new file system.
//
// Returns a negative error code on failure.
int pico_mount(bool format);

// Unmounts a littlefs
//
// Returns a negative error code on failure.
int pico_unmount(void);

// Removes a file or directory
//
// If removing a directory, the directory must be empty.
// Returns a negative error code on failure.
int pico_remove(const char* path);

// Open a file
//
// The mode that the file is opened in is determined by the flags, which
// are values from the enum lfs_open_flags that are bitwise-ored together.
//
// Returns opened file handle. Returns a negative error code on failure.
int pico_open(const char* path, int flags);

// Close a file
//
// Any pending writes are written out to storage as though
// sync had been called and releases any allocated resources.
//
// Returns a negative error code on failure.
int pico_close(int file);

// Return file system statistics
//
// Fills out the pico_fsstat_t structure, based on the specified file or
// directory. Returns a negative error code on failure.
int pico_fsstat(struct pico_fsstat_t* stat);

// Change the position of the file to the beginning of the file
//
// Equivalent to pico_lseek(lfs, file, 0, LFS_SEEK_SET)
// Returns a negative error code on failure.
int pico_rewind(int file);

// Rename or move a file or directory
//
// If the destination exists, it must match the source in type.
// If the destination is a directory, the directory must be empty.
//
// Returns a negative error code on failure.
int pico_rename(const char* oldpath, const char* newpath);

// Read data from file
//
// Takes a buffer and size indicating where to store the read data.
// Returns the number of bytes read, or a negative error code on failure.
lfs_size_t pico_read(int file, void* buffer, lfs_size_t size);

// Write data to file
//
// Takes a buffer and size indicating the data to write. The file will not
// actually be updated on the storage until either sync or close is called.
//
// Returns the number of bytes written, or a negative error code on failure.
lfs_size_t pico_write(int file, const void* buffer, lfs_size_t size);

// Change the position of the file
//
// The change in position is determined by the offset and whence flag.
// Returns the new position of the file, or a negative error code on failure.
lfs_soff_t pico_lseek(int file, lfs_soff_t off, int whence);

// Truncates the size of the file to the specified size
//
// Returns a negative error code on failure.
int pico_truncate(int file, lfs_off_t size);

// Return the position of the file
//
// Equivalent to pico_lseek(file, 0, LFS_SEEK_CUR)
// Returns the position of the file, or a negative error code on failure.
lfs_soff_t pico_tell(int file);

// Find info about a file or directory
//
// Fills out the info structure, based on the specified file or directory.
// Returns a negative error code on failure.
int pico_stat(const char* path, struct lfs_info* info);

// Get a custom attribute
//
// Custom attributes are uniquely identified by an 8-bit type and limited
// to LFS_ATTR_MAX bytes. When read, if the stored attribute is smaller than
// the buffer, it will be padded with zeros. If the stored attribute is larger,
// then it will be silently truncated. If no attribute is found, the error
// LFS_ERR_NOATTR is returned and the buffer is filled with zeros.
//
// Returns the size of the attribute, or a negative error code on failure.
// Note, the returned size is the size of the attribute on disk, irrespective
// of the size of the buffer. This can be used to dynamically allocate a buffer
// or check for existance.
lfs_ssize_t pico_getattr(const char* path, uint8_t type, void* buffer, lfs_size_t size);

// Set custom attributes
//
// Custom attributes are uniquely identified by an 8-bit type and limited
// to LFS_ATTR_MAX bytes. If an attribute is not found, it will be
// implicitly created.
//
// Returns a negative error code on failure.
int pico_setattr(const char* path, uint8_t type, const void* buffer, lfs_size_t size);

// Removes a custom attribute
//
// If an attribute is not found, nothing happens.
//
// Returns a negative error code on failure.
int pico_removeattr(const char* path, uint8_t type);

// Open a file with extra configuration
//
// The mode that the file is opened in is determined by the flags, which
// are values from the enum lfs_open_flags that are bitwise-ored together.
//
// The config struct provides additional config options per file as described
// above. The config struct must be allocated while the file is open, and the
// config struct must be zeroed for defaults and backwards compatibility.
//
// Returns a negative error code on failure.
int pico_opencfg(int file, const char* path, int flags, const struct lfs_file_config* config);

// Synchronize a file and storage
//
// Any pending writes are written out to storage.
// Returns a negative error code on failure.
int pico_fflush(int file);

// Return the size of the file
//
// Similar to pico_lseek(file, 0, LFS_SEEK_END)
// Returns the size of the file, or a negative error code on failure.
lfs_soff_t pico_size(int file);

// Create a directory
//
// Returns a negative error code on failure.
int pico_mkdir(const char* path);

// Open a directory
//
// Once open a directory can be used with read to iterate over files.
// Returns a negative error code on failure.
int pico_dir_open(const char* path);

// Close a directory
//
// Releases any allocated resources.
// Returns a negative error code on failure.
int pico_dir_close(int dir);

// Read an entry in the directory
//
// Fills out the info structure, based on the specified file or directory.
// Returns a positive value on success, 0 at the end of directory,
// or a negative error code on failure.
int pico_dir_read(int dir, struct lfs_info* info);

// Change the position of the directory
//
// The new off must be a value previous returned from tell and specifies
// an absolute offset in the directory seek.
//
// Returns a negative error code on failure.
int pico_dir_seek(int dir, lfs_off_t off);

// Return the position of the directory
//
// The returned offset is only meant to be consumed by seek and may not make
// sense, but does indicate the current position in the directory iteration.
//
// Returns the position of the directory, or a negative error code on failure.
lfs_soff_t pico_dir_tell(int dir);

// Change the position of the directory to the beginning of the directory
//
// Returns a negative error code on failure.
int pico_dir_rewind(int dir);

// Return pointer to string representation of error code.
//
// Returns a negative error code on failure.
const char* pico_errmsg(int err);

#ifdef __cplusplus
}
#endif

#endif // _HAL_
