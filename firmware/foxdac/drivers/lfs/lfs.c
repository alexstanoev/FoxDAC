/*
 * The little filesystem
 *
 * Copyright (c) 2017, Arm Limited. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "lfs.h"
#include "lfs_util.h"

lfs_t lfs;

#define LFS_BLOCK_NULL ((lfs_block_t)-1)
#define LFS_BLOCK_INLINE ((lfs_block_t)-2)

/// Caching block device operations ///
static inline void lfs_cache_drop(lfs_cache_t* rcache) {
    // do not zero, cheaper if cache is readonly or only going to be
    // written with identical data (during relocates)
    (void)lfs;
    rcache->block = LFS_BLOCK_NULL;
}

static inline void lfs_cache_zero(lfs_cache_t* pcache) {
    // zero to avoid information leak
    memset(pcache->buffer, 0xff, lfs.cfg->cache_size);
    pcache->block = LFS_BLOCK_NULL;
}

static int lfs_bd_read(const lfs_cache_t* pcache, lfs_cache_t* rcache, lfs_size_t hint,
                       lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size) {
    uint8_t *data = buffer;
    if (block >= lfs.cfg->block_count || off + size > lfs.cfg->block_size) {
        return LFS_ERR_CORRUPT;
    }

    while (size > 0) {
        lfs_size_t diff = size;

        if (pcache && block == pcache->block &&
                off < pcache->off + pcache->size) {
            if (off >= pcache->off) {
                // is already in pcache?
                diff = lfs_min(diff, pcache->size - (off-pcache->off));
                memcpy(data, &pcache->buffer[off-pcache->off], diff);

                data += diff;
                off += diff;
                size -= diff;
                continue;
            }

            // pcache takes priority
            diff = lfs_min(diff, pcache->off-off);
        }

        if (block == rcache->block &&
                off < rcache->off + rcache->size) {
            if (off >= rcache->off) {
                // is already in rcache?
                diff = lfs_min(diff, rcache->size - (off-rcache->off));
                memcpy(data, &rcache->buffer[off-rcache->off], diff);

                data += diff;
                off += diff;
                size -= diff;
                continue;
            }

            // rcache takes priority
            diff = lfs_min(diff, rcache->off-off);
        }

        if (size >= hint && off % lfs.cfg->read_size == 0 && size >= lfs.cfg->read_size) {
            // bypass cache?
            diff = lfs_aligndown(diff, lfs.cfg->read_size);
            int err = lfs.cfg->read(block, off, data, diff);
            if (err) {
                return err;
            }

            data += diff;
            off += diff;
            size -= diff;
            continue;
        }

        // load to cache, first condition can no longer fail
        LFS_ASSERT(block < lfs.cfg->block_count);
        rcache->block = block;
        rcache->off = lfs_aligndown(off, lfs.cfg->read_size);
        rcache->size = lfs_min(
            lfs_min(lfs_alignup(off + hint, lfs.cfg->read_size), lfs.cfg->block_size) - rcache->off,
            lfs.cfg->cache_size);
        int err = lfs.cfg->read(rcache->block, rcache->off, rcache->buffer, rcache->size);
        LFS_ASSERT(err <= 0);
        if (err) {
            return err;
        }
    }

    return LFS_ERR_OK;
}

enum {
    LFS_CMP_EQ = 0,
    LFS_CMP_LT = 1,
    LFS_CMP_GT = 2,
};

static int lfs_bd_cmp(const lfs_cache_t* pcache, lfs_cache_t* rcache, lfs_size_t hint,
                      lfs_block_t block, lfs_off_t off, const void* buffer, lfs_size_t size) {
    const uint8_t *data = buffer;
    lfs_size_t diff = 0;

    for (lfs_off_t i = 0; i < size; i += diff) {
        uint8_t dat[8];

        diff = lfs_min(size-i, sizeof(dat));
        int res = lfs_bd_read(pcache, rcache, hint - i, block, off + i, &dat, diff);
        if (res) {
            return res;
        }

        res = memcmp(dat, data + i, diff);
        if (res) {
            return res < 0 ? LFS_CMP_LT : LFS_CMP_GT;
        }
    }

    return LFS_CMP_EQ;
}

#ifndef LFS_READONLY
static int lfs_bd_flush(lfs_cache_t* pcache, lfs_cache_t* rcache, bool validate) {
    if (pcache->block != LFS_BLOCK_NULL && pcache->block != LFS_BLOCK_INLINE) {
        LFS_ASSERT(pcache->block < lfs.cfg->block_count);
        lfs_size_t diff = lfs_alignup(pcache->size, lfs.cfg->prog_size);
        int err = lfs.cfg->prog(pcache->block, pcache->off, pcache->buffer, diff);
        LFS_ASSERT(err <= 0);
        if (err) {
            return err;
        }

        if (validate) {
            // check data on disk
            lfs_cache_drop(rcache);
            int res =
                lfs_bd_cmp(NULL, rcache, diff, pcache->block, pcache->off, pcache->buffer, diff);
            if (res < 0) {
                return res;
            }

            if (res != LFS_CMP_EQ) {
                return LFS_ERR_CORRUPT;
            }
        }

        lfs_cache_zero(pcache);
    }

    return LFS_ERR_OK;
}
#endif

#ifndef LFS_READONLY
static int lfs_bd_sync(lfs_cache_t* pcache, lfs_cache_t* rcache, bool validate) {
    lfs_cache_drop(rcache);
    return lfs_bd_flush(pcache, rcache, validate);
}
#endif

#ifndef LFS_READONLY
static int lfs_bd_prog(lfs_cache_t* pcache, lfs_cache_t* rcache, bool validate, lfs_block_t block,
                       lfs_off_t off, const void* buffer, lfs_size_t size) {
    const uint8_t *data = buffer;
    LFS_ASSERT(block == LFS_BLOCK_INLINE || block < lfs.cfg->block_count);
    LFS_ASSERT(off + size <= lfs.cfg->block_size);

    while (size > 0) {
        if (block == pcache->block && off >= pcache->off &&
            off < pcache->off + lfs.cfg->cache_size) {
            // already fits in pcache?
            lfs_size_t diff = lfs_min(size, lfs.cfg->cache_size - (off - pcache->off));
            memcpy(&pcache->buffer[off-pcache->off], data, diff);

            data += diff;
            off += diff;
            size -= diff;

            pcache->size = lfs_max(pcache->size, off - pcache->off);
            if (pcache->size == lfs.cfg->cache_size) {
                // eagerly flush out pcache if we fill up
                int err = lfs_bd_flush(pcache, rcache, validate);
                if (err) {
                    return err;
                }
            }

            continue;
        }

        // pcache must have been flushed, either by programming and
        // entire block or manually flushing the pcache
        LFS_ASSERT(pcache->block == LFS_BLOCK_NULL);

        // prepare pcache, first condition can no longer fail
        pcache->block = block;
        pcache->off = lfs_aligndown(off, lfs.cfg->prog_size);
        pcache->size = 0;
    }

    return LFS_ERR_OK;
}
#endif

#ifndef LFS_READONLY
static int lfs_bd_erase(lfs_block_t block) {
    LFS_ASSERT(block < lfs.cfg->block_count);
    int err = lfs.cfg->erase(block);
    LFS_ASSERT(err <= 0);
    return err;
}
#endif


/// Small type-level utilities ///
// operations on block pairs
static inline void lfs_pair_swap(lfs_block_t pair[2]) {
    lfs_block_t t = pair[0];
    pair[0] = pair[1];
    pair[1] = t;
}

static inline bool lfs_pair_isnull(const lfs_block_t pair[2]) {
    return pair[0] == LFS_BLOCK_NULL || pair[1] == LFS_BLOCK_NULL;
}

static inline int lfs_pair_cmp(
        const lfs_block_t paira[2],
        const lfs_block_t pairb[2]) {
    return !(paira[0] == pairb[0] || paira[1] == pairb[1] ||
             paira[0] == pairb[1] || paira[1] == pairb[0]);
}

static inline bool lfs_pair_sync(
        const lfs_block_t paira[2],
        const lfs_block_t pairb[2]) {
    return (paira[0] == pairb[0] && paira[1] == pairb[1]) ||
           (paira[0] == pairb[1] && paira[1] == pairb[0]);
}

static inline void lfs_pair_fromle32(lfs_block_t pair[2]) {
    pair[0] = lfs_fromle32(pair[0]);
    pair[1] = lfs_fromle32(pair[1]);
}

static inline void lfs_pair_tole32(lfs_block_t pair[2]) {
    pair[0] = lfs_tole32(pair[0]);
    pair[1] = lfs_tole32(pair[1]);
}

// operations on 32-bit entry tags
typedef uint32_t lfs_tag_t;
typedef int32_t lfs_stag_t;

#define LFS_MKTAG(type, id, size) \
    (((lfs_tag_t)(type) << 20) | ((lfs_tag_t)(id) << 10) | (lfs_tag_t)(size))

#define LFS_MKTAG_IF(cond, type, id, size) \
    ((cond) ? LFS_MKTAG(type, id, size) : LFS_MKTAG(LFS_FROM_NOOP, 0, 0))

#define LFS_MKTAG_IF_ELSE(cond, type1, id1, size1, type2, id2, size2) \
    ((cond) ? LFS_MKTAG(type1, id1, size1) : LFS_MKTAG(type2, id2, size2))

static inline bool lfs_tag_isvalid(lfs_tag_t tag) {
    return !(tag & 0x80000000);
}

static inline bool lfs_tag_isdelete(lfs_tag_t tag) {
    return ((int32_t)(tag << 22) >> 22) == -1;
}

static inline uint16_t lfs_tag_type1(lfs_tag_t tag) {
    return (tag & 0x70000000) >> 20;
}

static inline uint16_t lfs_tag_type3(lfs_tag_t tag) {
    return (tag & 0x7ff00000) >> 20;
}

static inline uint8_t lfs_tag_chunk(lfs_tag_t tag) {
    return (tag & 0x0ff00000) >> 20;
}

static inline int8_t lfs_tag_splice(lfs_tag_t tag) {
    return (int8_t)lfs_tag_chunk(tag);
}

static inline uint16_t lfs_tag_id(lfs_tag_t tag) {
    return (tag & 0x000ffc00) >> 10;
}

static inline lfs_size_t lfs_tag_size(lfs_tag_t tag) {
    return tag & 0x000003ff;
}

static inline lfs_size_t lfs_tag_dsize(lfs_tag_t tag) {
    return sizeof(tag) + lfs_tag_size(tag + lfs_tag_isdelete(tag));
}

// operations on attributes in attribute lists
struct lfs_mattr {
    lfs_tag_t tag;
    const void *buffer;
};

struct lfs_diskoff {
    lfs_block_t block;
    lfs_off_t off;
};

#define LFS_MKATTRS(...) \
    (struct lfs_mattr[]){__VA_ARGS__}, \
    sizeof((struct lfs_mattr[]){__VA_ARGS__}) / sizeof(struct lfs_mattr)

// operations on global state
static inline void lfs_gstate_xor(lfs_gstate_t *a, const lfs_gstate_t *b) {
    for (int i = 0; i < 3; i++) {
        ((uint32_t*)a)[i] ^= ((const uint32_t*)b)[i];
    }
}

static inline bool lfs_gstate_iszero(const lfs_gstate_t *a) {
    for (int i = 0; i < 3; i++) {
        if (((uint32_t*)a)[i] != 0) {
            return false;
        }
    }
    return true;
}

static inline bool lfs_gstate_hasorphans(const lfs_gstate_t *a) {
    return lfs_tag_size(a->tag);
}

static inline uint8_t lfs_gstate_getorphans(const lfs_gstate_t *a) {
    return lfs_tag_size(a->tag);
}

static inline bool lfs_gstate_hasmove(const lfs_gstate_t *a) {
    return lfs_tag_type1(a->tag);
}

static inline bool lfs_gstate_hasmovehere(const lfs_gstate_t *a,
        const lfs_block_t *pair) {
    return lfs_tag_type1(a->tag) && lfs_pair_cmp(a->pair, pair) == 0;
}

static inline void lfs_gstate_fromle32(lfs_gstate_t *a) {
    a->tag     = lfs_fromle32(a->tag);
    a->pair[0] = lfs_fromle32(a->pair[0]);
    a->pair[1] = lfs_fromle32(a->pair[1]);
}

static inline void lfs_gstate_tole32(lfs_gstate_t *a) {
    a->tag     = lfs_tole32(a->tag);
    a->pair[0] = lfs_tole32(a->pair[0]);
    a->pair[1] = lfs_tole32(a->pair[1]);
}

// other endianness operations
static void lfs_ctz_fromle32(struct lfs_ctz *ctz) {
    ctz->head = lfs_fromle32(ctz->head);
    ctz->size = lfs_fromle32(ctz->size);
}

#ifndef LFS_READONLY
static void lfs_ctz_tole32(struct lfs_ctz *ctz) {
    ctz->head = lfs_tole32(ctz->head);
    ctz->size = lfs_tole32(ctz->size);
}
#endif

static inline void lfs_superblock_fromle32(lfs_superblock_t *superblock) {
    superblock->version     = lfs_fromle32(superblock->version);
    superblock->block_size  = lfs_fromle32(superblock->block_size);
    superblock->block_count = lfs_fromle32(superblock->block_count);
    superblock->name_max    = lfs_fromle32(superblock->name_max);
    superblock->file_max    = lfs_fromle32(superblock->file_max);
    superblock->attr_max    = lfs_fromle32(superblock->attr_max);
}

static inline void lfs_superblock_tole32(lfs_superblock_t *superblock) {
    superblock->version     = lfs_tole32(superblock->version);
    superblock->block_size  = lfs_tole32(superblock->block_size);
    superblock->block_count = lfs_tole32(superblock->block_count);
    superblock->name_max    = lfs_tole32(superblock->name_max);
    superblock->file_max    = lfs_tole32(superblock->file_max);
    superblock->attr_max    = lfs_tole32(superblock->attr_max);
}

#ifndef LFS_NO_ASSERT
static bool lfs_mlist_isopen(struct lfs_mlist *head,
        struct lfs_mlist *node) {
    for (struct lfs_mlist **p = &head; *p; p = &(*p)->next) {
        if (*p == (struct lfs_mlist*)node) {
            return true;
        }
    }

    return false;
}
#endif

static void lfs_mlist_remove(struct lfs_mlist* mlist) {
    for (struct lfs_mlist** p = &lfs.mlist; *p; p = &(*p)->next) {
        if (*p == mlist) {
            *p = (*p)->next;
            break;
        }
    }
}

static void lfs_mlist_append(struct lfs_mlist* mlist) {
    mlist->next = lfs.mlist;
    lfs.mlist = mlist;
}

/// Internal operations predeclared here ///
#ifndef LFS_READONLY
static int lfs_dir_commit(lfs_mdir_t* dir, const struct lfs_mattr* attrs, int attrcount);
static int lfs_dir_compact(lfs_mdir_t* dir, const struct lfs_mattr* attrs, int attrcount,
                           lfs_mdir_t* source, uint16_t begin, uint16_t end);

static lfs_ssize_t lfs_file_rawwrite(lfs_file_t* file, const void* buffer, lfs_size_t size);
static int lfs_file_rawsync(lfs_file_t* file);
static int lfs_file_outline(lfs_file_t* file);
static int lfs_file_flush(lfs_file_t* file);

static int lfs_fs_preporphans(int8_t orphans);
static void lfs_fs_prepmove(uint16_t id, const lfs_block_t pair[2]);
static int lfs_fs_pred(const lfs_block_t dir[2], lfs_mdir_t* pdir);
static lfs_stag_t lfs_fs_parent(const lfs_block_t dir[2], lfs_mdir_t* parent);
static int lfs_fs_relocate(const lfs_block_t oldpair[2], lfs_block_t newpair[2]);
static int lfs_fs_forceconsistency(void);
#endif

static int lfs_dir_rawrewind(lfs_dir_t* dir);

static lfs_ssize_t lfs_file_rawread(lfs_file_t* file, void* buffer, lfs_size_t size);
static int lfs_file_rawclose(lfs_file_t* file);
static lfs_soff_t lfs_file_rawsize(lfs_file_t* file);

static lfs_ssize_t lfs_fs_rawsize(void);
static int lfs_fs_rawtraverse(int (*cb)(void* data, lfs_block_t block), void* data,
                              bool includeorphans);

static int lfs_deinit(void);
static int lfs_rawunmount(void);

/// Block allocator ///
#ifndef LFS_READONLY
static int lfs_alloc_lookahead(void *p, lfs_block_t block) {
    lfs_t *lfs = (lfs_t*)p;
    lfs_block_t off = ((block - lfs->free.off) + lfs->cfg->block_count) % lfs->cfg->block_count;

    if (off < lfs->free.size) {
        lfs->free.buffer[off / 32] |= 1U << (off % 32);
    }

    return LFS_ERR_OK;
}
#endif

// indicate allocated blocks have been committed into the filesystem, this
// is to prevent blocks from being garbage collected in the middle of a
// commit operation
static void lfs_alloc_ack(void) { lfs.free.ack = lfs.cfg->block_count; }

// drop the lookahead buffer, this is done during mounting and failed
// traversals in order to avoid invalid lookahead state
static void lfs_alloc_drop(void) {
    lfs.free.size = 0;
    lfs.free.i = 0;
    lfs_alloc_ack();
}

#ifndef LFS_READONLY
static int lfs_alloc(lfs_block_t* block) {
    while (true) {
        while (lfs.free.i != lfs.free.size) {
            lfs_block_t off = lfs.free.i;
            lfs.free.i += 1;
            lfs.free.ack -= 1;

            if (!(lfs.free.buffer[off / 32] & (1U << (off % 32)))) {
                // found a free block
                *block = (lfs.free.off + off) % lfs.cfg->block_count;

                // eagerly find next off so an alloc ack can
                // discredit old lookahead blocks
                while (lfs.free.i != lfs.free.size &&
                       (lfs.free.buffer[lfs.free.i / 32] & (1U << (lfs.free.i % 32)))) {
                    lfs.free.i += 1;
                    lfs.free.ack -= 1;
                }

                return LFS_ERR_OK;
            }
        }

        // check if we have looked at all blocks since last ack
        if (lfs.free.ack == 0) {
            LFS_ERROR("No more free space %" PRIu32, lfs.free.i + lfs.free.off);
            return LFS_ERR_NOSPC;
        }

        lfs.free.off = (lfs.free.off + lfs.free.size) % lfs.cfg->block_count;
        lfs.free.size = lfs_min(8 * lfs.cfg->lookahead_size, lfs.free.ack);
        lfs.free.i = 0;

        // find mask of free blocks from tree
        memset(lfs.free.buffer, 0, lfs.cfg->lookahead_size);
        int err = lfs_fs_rawtraverse(lfs_alloc_lookahead, &lfs, true);
        if (err) {
            lfs_alloc_drop();
            return err;
        }
    }
}
#endif

/// Metadata pair and directory operations ///
static lfs_stag_t lfs_dir_getslice(const lfs_mdir_t* dir, lfs_tag_t gmask, lfs_tag_t gtag,
                                   lfs_off_t goff, void* gbuffer, lfs_size_t gsize) {
    lfs_off_t off = dir->off;
    lfs_tag_t ntag = dir->etag;
    lfs_stag_t gdiff = 0;

    if (lfs_gstate_hasmovehere(&lfs.gdisk, dir->pair) && lfs_tag_id(gmask) != 0 &&
        lfs_tag_id(lfs.gdisk.tag) <= lfs_tag_id(gtag)) {
        // synthetic moves
        gdiff -= LFS_MKTAG(0, 1, 0);
    }

    // iterate over dir block backwards (for faster lookups)
    while (off >= sizeof(lfs_tag_t) + lfs_tag_dsize(ntag)) {
        off -= lfs_tag_dsize(ntag);
        lfs_tag_t tag = ntag;
        int err =
            lfs_bd_read(NULL, &lfs.rcache, sizeof(ntag), dir->pair[0], off, &ntag, sizeof(ntag));
        if (err) {
            return err;
        }

        ntag = (lfs_frombe32(ntag) ^ tag) & 0x7fffffff;

        if (lfs_tag_id(gmask) != 0 &&
                lfs_tag_type1(tag) == LFS_TYPE_SPLICE &&
                lfs_tag_id(tag) <= lfs_tag_id(gtag - gdiff)) {
            if (tag == (LFS_MKTAG(LFS_TYPE_CREATE, 0, 0) |
                    (LFS_MKTAG(0, 0x3ff, 0) & (gtag - gdiff)))) {
                // found where we were created
                return LFS_ERR_NOENT;
            }

            // move around splices
            gdiff += LFS_MKTAG(0, lfs_tag_splice(tag), 0);
        }

        if ((gmask & tag) == (gmask & (gtag - gdiff))) {
            if (lfs_tag_isdelete(tag)) {
                return LFS_ERR_NOENT;
            }

            lfs_size_t diff = lfs_min(lfs_tag_size(tag), gsize);
            err = lfs_bd_read(NULL, &lfs.rcache, diff, dir->pair[0], off + sizeof(tag) + goff,
                              gbuffer, diff);
            if (err) {
                return err;
            }

            memset((uint8_t*)gbuffer + diff, 0, gsize - diff);

            return tag + gdiff;
        }
    }

    return LFS_ERR_NOENT;
}

static lfs_stag_t lfs_dir_get(const lfs_mdir_t* dir, lfs_tag_t gmask, lfs_tag_t gtag,
                              void* buffer) {
    return lfs_dir_getslice(dir, gmask, gtag, 0, buffer, lfs_tag_size(gtag));
}

static int lfs_dir_getread(const lfs_mdir_t* dir, const lfs_cache_t* pcache, lfs_cache_t* rcache,
                           lfs_size_t hint, lfs_tag_t gmask, lfs_tag_t gtag, lfs_off_t off,
                           void* buffer, lfs_size_t size) {
    uint8_t *data = buffer;
    if (off + size > lfs.cfg->block_size) {
        return LFS_ERR_CORRUPT;
    }

    while (size > 0) {
        lfs_size_t diff = size;

        if (pcache && pcache->block == LFS_BLOCK_INLINE &&
                off < pcache->off + pcache->size) {
            if (off >= pcache->off) {
                // is already in pcache?
                diff = lfs_min(diff, pcache->size - (off-pcache->off));
                memcpy(data, &pcache->buffer[off-pcache->off], diff);

                data += diff;
                off += diff;
                size -= diff;
                continue;
            }

            // pcache takes priority
            diff = lfs_min(diff, pcache->off-off);
        }

        if (rcache->block == LFS_BLOCK_INLINE &&
                off < rcache->off + rcache->size) {
            if (off >= rcache->off) {
                // is already in rcache?
                diff = lfs_min(diff, rcache->size - (off-rcache->off));
                memcpy(data, &rcache->buffer[off-rcache->off], diff);

                data += diff;
                off += diff;
                size -= diff;
                continue;
            }

            // rcache takes priority
            diff = lfs_min(diff, rcache->off-off);
        }

        // load to cache, first condition can no longer fail
        rcache->block = LFS_BLOCK_INLINE;
        rcache->off = lfs_aligndown(off, lfs.cfg->read_size);
        rcache->size = lfs_min(lfs_alignup(off + hint, lfs.cfg->read_size), lfs.cfg->cache_size);
        int err = lfs_dir_getslice(dir, gmask, gtag, rcache->off, rcache->buffer, rcache->size);
        if (err < 0) {
            return err;
        }
    }

    return LFS_ERR_OK;
}

#ifndef LFS_READONLY
static int lfs_dir_traverse_filter(void *p,
        lfs_tag_t tag, const void *buffer) {
    lfs_tag_t *filtertag = p;
    (void)buffer;

    // which mask depends on unique bit in tag structure
    uint32_t mask = (tag & LFS_MKTAG(0x100, 0, 0))
            ? LFS_MKTAG(0x7ff, 0x3ff, 0)
            : LFS_MKTAG(0x700, 0x3ff, 0);

    // check for redundancy
    if ((mask & tag) == (mask & *filtertag) ||
            lfs_tag_isdelete(*filtertag) ||
            (LFS_MKTAG(0x7ff, 0x3ff, 0) & tag) == (
                LFS_MKTAG(LFS_TYPE_DELETE, 0, 0) |
                    (LFS_MKTAG(0, 0x3ff, 0) & *filtertag))) {
        return true;
    }

    // check if we need to adjust for created/deleted tags
    if (lfs_tag_type1(tag) == LFS_TYPE_SPLICE &&
            lfs_tag_id(tag) <= lfs_tag_id(*filtertag)) {
        *filtertag += LFS_MKTAG(0, lfs_tag_splice(tag), 0);
    }

    return false;
}
#endif

#ifndef LFS_READONLY
static int lfs_dir_traverse(const lfs_mdir_t* dir, lfs_off_t off, lfs_tag_t ptag,
                            const struct lfs_mattr* attrs, int attrcount, lfs_tag_t tmask,
                            lfs_tag_t ttag, uint16_t begin, uint16_t end, int16_t diff,
                            int (*cb)(void* data, lfs_tag_t tag, const void* buffer), void* data) {
    // iterate over directory and attrs
    while (true) {
        lfs_tag_t tag;
        const void *buffer;
        struct lfs_diskoff disk;
        if (off+lfs_tag_dsize(ptag) < dir->off) {
            off += lfs_tag_dsize(ptag);
            int err =
                lfs_bd_read(NULL, &lfs.rcache, sizeof(tag), dir->pair[0], off, &tag, sizeof(tag));
            if (err) {
                return err;
            }

            tag = (lfs_frombe32(tag) ^ ptag) | 0x80000000;
            disk.block = dir->pair[0];
            disk.off = off+sizeof(lfs_tag_t);
            buffer = &disk;
            ptag = tag;
        } else if (attrcount > 0) {
            tag = attrs[0].tag;
            buffer = attrs[0].buffer;
            attrs += 1;
            attrcount -= 1;
        } else {
            return LFS_ERR_OK;
        }

        lfs_tag_t mask = LFS_MKTAG(0x7ff, 0, 0);
        if ((mask & tmask & tag) != (mask & tmask & ttag)) {
            continue;
        }

        // do we need to filter? inlining the filtering logic here allows
        // for some minor optimizations
        if (lfs_tag_id(tmask) != 0) {
            // scan for duplicates and update tag based on creates/deletes
            int filter = lfs_dir_traverse(dir, off, ptag, attrs, attrcount, 0, 0, 0, 0, 0,
                                          lfs_dir_traverse_filter, &tag);
            if (filter < 0) {
                return filter;
            }

            if (filter) {
                continue;
            }

            // in filter range?
            if (!(lfs_tag_id(tag) >= begin && lfs_tag_id(tag) < end)) {
                continue;
            }
        }

        // handle special cases for mcu-side operations
        if (lfs_tag_type3(tag) == LFS_FROM_NOOP) {
            // do nothing
        } else if (lfs_tag_type3(tag) == LFS_FROM_MOVE) {
            uint16_t fromid = lfs_tag_size(tag);
            uint16_t toid = lfs_tag_id(tag);
            int err = lfs_dir_traverse(buffer, 0, 0xffffffff, NULL, 0, LFS_MKTAG(0x600, 0x3ff, 0),
                                       LFS_MKTAG(LFS_TYPE_STRUCT, 0, 0), fromid, fromid + 1,
                                       toid - fromid + diff, cb, data);
            if (err) {
                return err;
            }
        } else if (lfs_tag_type3(tag) == LFS_FROM_USERATTRS) {
            for (unsigned i = 0; i < lfs_tag_size(tag); i++) {
                const struct lfs_attr *a = buffer;
                int err = cb(data, LFS_MKTAG(LFS_TYPE_USERATTR + a[i].type,
                        lfs_tag_id(tag) + diff, a[i].size), a[i].buffer);
                if (err) {
                    return err;
                }
            }
        } else {
            int err = cb(data, tag + LFS_MKTAG(0, diff, 0), buffer);
            if (err) {
                return err;
            }
        }
    }
}
#endif

static lfs_stag_t lfs_dir_fetchmatch(lfs_mdir_t* dir, const lfs_block_t pair[2], lfs_tag_t fmask,
                                     lfs_tag_t ftag, uint16_t* id,
                                     int (*cb)(void* data, lfs_tag_t tag, const void* buffer),
                                     void* data) {
    // we can find tag very efficiently during a fetch, since we're already
    // scanning the entire directory
    lfs_stag_t besttag = -1;

    // if either block address is invalid we return LFS_ERR_CORRUPT here,
    // otherwise later writes to the pair could fail
    if (pair[0] >= lfs.cfg->block_count || pair[1] >= lfs.cfg->block_count) {
        return LFS_ERR_CORRUPT;
    }

    // find the block with the most recent revision
    uint32_t revs[2] = {0, 0};
    int r = 0;
    for (int i = 0; i < 2; i++) {
        int err =
            lfs_bd_read(NULL, &lfs.rcache, sizeof(revs[i]), pair[i], 0, &revs[i], sizeof(revs[i]));
        revs[i] = lfs_fromle32(revs[i]);
        if (err && err != LFS_ERR_CORRUPT) {
            return err;
        }

        if (err != LFS_ERR_CORRUPT &&
                lfs_scmp(revs[i], revs[(i+1)%2]) > 0) {
            r = i;
        }
    }

    dir->pair[0] = pair[(r+0)%2];
    dir->pair[1] = pair[(r+1)%2];
    dir->rev = revs[(r+0)%2];
    dir->off = 0; // nonzero = found some commits

    // now scan tags to fetch the actual dir and find possible match
    for (int i = 0; i < 2; i++) {
        lfs_off_t off = 0;
        lfs_tag_t ptag = 0xffffffff;

        uint16_t tempcount = 0;
        lfs_block_t temptail[2] = {LFS_BLOCK_NULL, LFS_BLOCK_NULL};
        bool tempsplit = false;
        lfs_stag_t tempbesttag = besttag;

        dir->rev = lfs_tole32(dir->rev);
        uint32_t crc = lfs_crc(0xffffffff, &dir->rev, sizeof(dir->rev));
        dir->rev = lfs_fromle32(dir->rev);

        while (true) {
            // extract next tag
            lfs_tag_t tag;
            off += lfs_tag_dsize(ptag);
            int err = lfs_bd_read(NULL, &lfs.rcache, lfs.cfg->block_size, dir->pair[0], off, &tag,
                                  sizeof(tag));
            if (err) {
                if (err == LFS_ERR_CORRUPT) {
                    // can't continue?
                    dir->erased = false;
                    break;
                }
                return err;
            }

            crc = lfs_crc(crc, &tag, sizeof(tag));
            tag = lfs_frombe32(tag) ^ ptag;

            // next commit not yet programmed or we're not in valid range
            if (!lfs_tag_isvalid(tag)) {
                dir->erased =
                    (lfs_tag_type1(ptag) == LFS_TYPE_CRC && dir->off % lfs.cfg->prog_size == 0);
                break;
            } else if (off + lfs_tag_dsize(tag) > lfs.cfg->block_size) {
                dir->erased = false;
                break;
            }

            ptag = tag;

            if (lfs_tag_type1(tag) == LFS_TYPE_CRC) {
                // check the crc attr
                uint32_t dcrc;
                err = lfs_bd_read(NULL, &lfs.rcache, lfs.cfg->block_size, dir->pair[0],
                                  off + sizeof(tag), &dcrc, sizeof(dcrc));
                if (err) {
                    if (err == LFS_ERR_CORRUPT) {
                        dir->erased = false;
                        break;
                    }
                    return err;
                }
                dcrc = lfs_fromle32(dcrc);

                if (crc != dcrc) {
                    dir->erased = false;
                    break;
                }

                // reset the next bit if we need to
                ptag ^= (lfs_tag_t)(lfs_tag_chunk(tag) & 1U) << 31;

                // toss our crc into the filesystem seed for
                // pseudorandom numbers, note we use another crc here
                // as a collection function because it is sufficiently
                // random and convenient
                lfs.seed = lfs_crc(lfs.seed, &crc, sizeof(crc));

                // update with what's found so far
                besttag = tempbesttag;
                dir->off = off + lfs_tag_dsize(tag);
                dir->etag = ptag;
                dir->count = tempcount;
                dir->tail[0] = temptail[0];
                dir->tail[1] = temptail[1];
                dir->split = tempsplit;

                // reset crc
                crc = 0xffffffff;
                continue;
            }

            // crc the entry first, hopefully leaving it in the cache
            for (lfs_off_t j = sizeof(tag); j < lfs_tag_dsize(tag); j++) {
                uint8_t dat;
                err = lfs_bd_read(NULL, &lfs.rcache, lfs.cfg->block_size, dir->pair[0], off + j,
                                  &dat, 1);
                if (err) {
                    if (err == LFS_ERR_CORRUPT) {
                        dir->erased = false;
                        break;
                    }
                    return err;
                }

                crc = lfs_crc(crc, &dat, 1);
            }

            // directory modification tags?
            if (lfs_tag_type1(tag) == LFS_TYPE_NAME) {
                // increase count of files if necessary
                if (lfs_tag_id(tag) >= tempcount) {
                    tempcount = lfs_tag_id(tag) + 1;
                }
            } else if (lfs_tag_type1(tag) == LFS_TYPE_SPLICE) {
                tempcount += lfs_tag_splice(tag);

                if (tag == (LFS_MKTAG(LFS_TYPE_DELETE, 0, 0) |
                        (LFS_MKTAG(0, 0x3ff, 0) & tempbesttag))) {
                    tempbesttag |= 0x80000000;
                } else if (tempbesttag != -1 &&
                        lfs_tag_id(tag) <= lfs_tag_id(tempbesttag)) {
                    tempbesttag += LFS_MKTAG(0, lfs_tag_splice(tag), 0);
                }
            } else if (lfs_tag_type1(tag) == LFS_TYPE_TAIL) {
                tempsplit = (lfs_tag_chunk(tag) & 1);

                err = lfs_bd_read(NULL, &lfs.rcache, lfs.cfg->block_size, dir->pair[0],
                                  off + sizeof(tag), &temptail, 8);
                if (err) {
                    if (err == LFS_ERR_CORRUPT) {
                        dir->erased = false;
                        break;
                    }
                }
                lfs_pair_fromle32(temptail);
            }

            // found a match for our fetcher?
            if ((fmask & tag) == (fmask & ftag)) {
                int res = cb(data, tag, &(struct lfs_diskoff){
                        dir->pair[0], off+sizeof(tag)});
                if (res < 0) {
                    if (res == LFS_ERR_CORRUPT) {
                        dir->erased = false;
                        break;
                    }
                    return res;
                }

                if (res == LFS_CMP_EQ) {
                    // found a match
                    tempbesttag = tag;
                } else if ((LFS_MKTAG(0x7ff, 0x3ff, 0) & tag) ==
                        (LFS_MKTAG(0x7ff, 0x3ff, 0) & tempbesttag)) {
                    // found an identical tag, but contents didn't match
                    // this must mean that our besttag has been overwritten
                    tempbesttag = -1;
                } else if (res == LFS_CMP_GT &&
                        lfs_tag_id(tag) <= lfs_tag_id(tempbesttag)) {
                    // found a greater match, keep track to keep things sorted
                    tempbesttag = tag | 0x80000000;
                }
            }
        }

        // consider what we have good enough
        if (dir->off > 0) {
            // synthetic move
            if (lfs_gstate_hasmovehere(&lfs.gdisk, dir->pair)) {
                if (lfs_tag_id(lfs.gdisk.tag) == lfs_tag_id(besttag)) {
                    besttag |= 0x80000000;
                } else if (besttag != -1 && lfs_tag_id(lfs.gdisk.tag) < lfs_tag_id(besttag)) {
                    besttag -= LFS_MKTAG(0, 1, 0);
                }
            }

            // found tag? or found best id?
            if (id) {
                *id = lfs_min(lfs_tag_id(besttag), dir->count);
            }

            if (lfs_tag_isvalid(besttag)) {
                return besttag;
            } else if (lfs_tag_id(besttag) < dir->count) {
                return LFS_ERR_NOENT;
            } else {
                return LFS_ERR_OK;
            }
        }

        // failed, try the other block?
        lfs_pair_swap(dir->pair);
        dir->rev = revs[(r+1)%2];
    }

    LFS_ERROR("Corrupted dir pair at {0x%"PRIx32", 0x%"PRIx32"}",
            dir->pair[0], dir->pair[1]);
    return LFS_ERR_CORRUPT;
}

static int lfs_dir_fetch(lfs_mdir_t* dir, const lfs_block_t pair[2]) {
    // note, mask=-1, tag=-1 can never match a tag since this
    // pattern has the invalid bit set
    return (int)lfs_dir_fetchmatch(dir, pair, (lfs_tag_t)-1, (lfs_tag_t)-1, NULL, NULL, NULL);
}

static int lfs_dir_getgstate(const lfs_mdir_t* dir, lfs_gstate_t* gstate) {
    lfs_gstate_t temp;
    lfs_stag_t res = lfs_dir_get(dir, LFS_MKTAG(0x7ff, 0, 0),
                                 LFS_MKTAG(LFS_TYPE_MOVESTATE, 0, sizeof(temp)), &temp);
    if (res < 0 && res != LFS_ERR_NOENT) {
        return res;
    }

    if (res != LFS_ERR_NOENT) {
        // xor together to find resulting gstate
        lfs_gstate_fromle32(&temp);
        lfs_gstate_xor(gstate, &temp);
    }

    return LFS_ERR_OK;
}

static int lfs_dir_getinfo(lfs_mdir_t* dir, uint16_t id, struct lfs_info* info) {
    if (id == 0x3ff) {
        // special case for root
        strcpy(info->name, "/");
        info->type = LFS_TYPE_DIR;
        return LFS_ERR_OK;
    }

    lfs_stag_t tag = lfs_dir_get(dir, LFS_MKTAG(0x780, 0x3ff, 0),
                                 LFS_MKTAG(LFS_TYPE_NAME, id, lfs.name_max + 1), info->name);
    if (tag < 0) {
        return (int)tag;
    }

    info->type = lfs_tag_type3(tag);

    struct lfs_ctz ctz;
    tag = lfs_dir_get(dir, LFS_MKTAG(0x700, 0x3ff, 0), LFS_MKTAG(LFS_TYPE_STRUCT, id, sizeof(ctz)),
                      &ctz);
    if (tag < 0) {
        return (int)tag;
    }
    lfs_ctz_fromle32(&ctz);

    if (lfs_tag_type3(tag) == LFS_TYPE_CTZSTRUCT) {
        info->size = ctz.size;
    } else if (lfs_tag_type3(tag) == LFS_TYPE_INLINESTRUCT) {
        info->size = lfs_tag_size(tag);
    }

    return LFS_ERR_OK;
}

struct lfs_dir_find_match {
    const void *name;
    lfs_size_t size;
};

static int lfs_dir_find_match(void *data,
        lfs_tag_t tag, const void *buffer) {
    struct lfs_dir_find_match *name = data;
    const struct lfs_diskoff *disk = buffer;

    // compare with disk
    lfs_size_t diff = lfs_min(name->size, lfs_tag_size(tag));
    int res = lfs_bd_cmp(NULL, &lfs.rcache, diff, disk->block, disk->off, name->name, diff);
    if (res != LFS_CMP_EQ) {
        return res;
    }

    // only equal if our size is still the same
    if (name->size != lfs_tag_size(tag)) {
        return (name->size < lfs_tag_size(tag)) ? LFS_CMP_LT : LFS_CMP_GT;
    }

    // found a match!
    return LFS_CMP_EQ;
}

static lfs_stag_t lfs_dir_find(lfs_mdir_t* dir, const char** path, uint16_t* id) {
    // we reduce path to a single name if we can find it
    const char *name = *path;
    if (id) {
        *id = 0x3ff;
    }

    // default to root dir
    lfs_stag_t tag = LFS_MKTAG(LFS_TYPE_DIR, 0x3ff, 0);
    dir->tail[0] = lfs.root[0];
    dir->tail[1] = lfs.root[1];

    while (true) {
nextname:
        // skip slashes
        name += strspn(name, "/");
        lfs_size_t namelen = strcspn(name, "/");

        // skip '.' and root '..'
        if ((namelen == 1 && memcmp(name, ".", 1) == 0) ||
            (namelen == 2 && memcmp(name, "..", 2) == 0)) {
            name += namelen;
            goto nextname;
        }

        // skip if matched by '..' in name
        const char *suffix = name + namelen;
        lfs_size_t sufflen;
        int depth = 1;
        while (true) {
            suffix += strspn(suffix, "/");
            sufflen = strcspn(suffix, "/");
            if (sufflen == 0) {
                break;
            }

            if (sufflen == 2 && memcmp(suffix, "..", 2) == 0) {
                depth -= 1;
                if (depth == 0) {
                    name = suffix + sufflen;
                    goto nextname;
                }
            } else {
                depth += 1;
            }

            suffix += sufflen;
        }

        // found path
        if (name[0] == '\0') {
            return tag;
        }

        // update what we've found so far
        *path = name;

        // only continue if we hit a directory
        if (lfs_tag_type3(tag) != LFS_TYPE_DIR) {
            return LFS_ERR_NOTDIR;
        }

        // grab the entry data
        if (lfs_tag_id(tag) != 0x3ff) {
            lfs_stag_t res = lfs_dir_get(dir, LFS_MKTAG(0x700, 0x3ff, 0),
                                         LFS_MKTAG(LFS_TYPE_STRUCT, lfs_tag_id(tag), 8), dir->tail);
            if (res < 0) {
                return res;
            }
            lfs_pair_fromle32(dir->tail);
        }

        // find entry matching name
        while (true) {
            tag = lfs_dir_fetchmatch(dir, dir->tail, LFS_MKTAG(0x780, 0, 0),
                                     LFS_MKTAG(LFS_TYPE_NAME, 0, namelen),
                                     // are we last name?
                                     (strchr(name, '/') == NULL) ? id : NULL, lfs_dir_find_match,
                                     &(struct lfs_dir_find_match){name, namelen});
            if (tag < 0) {
                return tag;
            }

            if (tag) {
                break;
            }

            if (!dir->split) {
                return LFS_ERR_NOENT;
            }
        }

        // to next name
        name += namelen;
    }
}

// commit logic
struct lfs_commit {
    lfs_block_t block;
    lfs_off_t off;
    lfs_tag_t ptag;
    uint32_t crc;

    lfs_off_t begin;
    lfs_off_t end;
};

#ifndef LFS_READONLY
static int lfs_dir_commitprog(struct lfs_commit* commit, const void* buffer, lfs_size_t size) {
    int err = lfs_bd_prog(&lfs.pcache, &lfs.rcache, false, commit->block, commit->off,
                          (const uint8_t*)buffer, size);
    if (err) {
        return err;
    }

    commit->crc = lfs_crc(commit->crc, buffer, size);
    commit->off += size;
    return LFS_ERR_OK;
}
#endif

#ifndef LFS_READONLY
static int lfs_dir_commitattr(struct lfs_commit* commit, lfs_tag_t tag, const void* buffer) {
    // check if we fit
    lfs_size_t dsize = lfs_tag_dsize(tag);
    if (commit->off + dsize > commit->end) {
        return LFS_ERR_NOSPC;
    }

    // write out tag
    lfs_tag_t ntag = lfs_tobe32((tag & 0x7fffffff) ^ commit->ptag);
    int err = lfs_dir_commitprog(commit, &ntag, sizeof(ntag));
    if (err) {
        return err;
    }

    if (!(tag & 0x80000000)) {
        // from memory
        err = lfs_dir_commitprog(commit, buffer, dsize - sizeof(tag));
        if (err) {
            return err;
        }
    } else {
        // from disk
        const struct lfs_diskoff *disk = buffer;
        for (lfs_off_t i = 0; i < dsize-sizeof(tag); i++) {
            // rely on caching to make this efficient
            uint8_t dat;
            err = lfs_bd_read(NULL, &lfs.rcache, dsize - sizeof(tag) - i, disk->block,
                              disk->off + i, &dat, 1);
            if (err) {
                return err;
            }

            err = lfs_dir_commitprog(commit, &dat, 1);
            if (err) {
                return err;
            }
        }
    }

    commit->ptag = tag & 0x7fffffff;
    return LFS_ERR_OK;
}
#endif

#ifndef LFS_READONLY
static int lfs_dir_commitcrc(struct lfs_commit* commit) {
    // align to program units
    const lfs_off_t end = lfs_alignup(commit->off + 2 * sizeof(uint32_t), lfs.cfg->prog_size);

    lfs_off_t off1 = 0;
    uint32_t crc1 = 0;

    // create crc tags to fill up remainder of commit, note that
    // padding is not crced, which lets fetches skip padding but
    // makes committing a bit more complicated
    while (commit->off < end) {
        lfs_off_t off = commit->off + sizeof(lfs_tag_t);
        lfs_off_t noff = lfs_min(end - off, 0x3fe) + off;
        if (noff < end) {
            noff = lfs_min(noff, end - 2*sizeof(uint32_t));
        }

        // read erased state from next program unit
        lfs_tag_t tag = 0xffffffff;
        int err =
            lfs_bd_read(NULL, &lfs.rcache, sizeof(tag), commit->block, noff, &tag, sizeof(tag));
        if (err && err != LFS_ERR_CORRUPT) {
            return err;
        }

        // build crc tag
        bool reset = ~lfs_frombe32(tag) >> 31;
        tag = LFS_MKTAG(LFS_TYPE_CRC + reset, 0x3ff, noff - off);

        // write out crc
        uint32_t footer[2];
        footer[0] = lfs_tobe32(tag ^ commit->ptag);
        commit->crc = lfs_crc(commit->crc, &footer[0], sizeof(footer[0]));
        footer[1] = lfs_tole32(commit->crc);
        err = lfs_bd_prog(&lfs.pcache, &lfs.rcache, false, commit->block, commit->off, &footer,
                          sizeof(footer));
        if (err) {
            return err;
        }

        // keep track of non-padding checksum to verify
        if (off1 == 0) {
            off1 = commit->off + sizeof(uint32_t);
            crc1 = commit->crc;
        }

        commit->off += sizeof(tag)+lfs_tag_size(tag);
        commit->ptag = tag ^ ((lfs_tag_t)reset << 31);
        commit->crc = 0xffffffff; // reset crc for next "commit"
    }

    // flush buffers
    int err = lfs_bd_sync(&lfs.pcache, &lfs.rcache, false);
    if (err) {
        return err;
    }

    // successful commit, check checksums to make sure
    lfs_off_t off = commit->begin;
    lfs_off_t noff = off1;
    while (off < end) {
        uint32_t crc = 0xffffffff;
        for (lfs_off_t i = off; i < noff+sizeof(uint32_t); i++) {
            // check against written crc, may catch blocks that
            // become readonly and match our commit size exactly
            if (i == off1 && crc != crc1) {
                return LFS_ERR_CORRUPT;
            }

            // leave it up to caching to make this efficient
            uint8_t dat;
            err = lfs_bd_read(NULL, &lfs.rcache, noff + sizeof(uint32_t) - i, commit->block, i,
                              &dat, 1);
            if (err) {
                return err;
            }

            crc = lfs_crc(crc, &dat, 1);
        }

        // detected write error?
        if (crc != 0) {
            return LFS_ERR_CORRUPT;
        }

        // skip padding
        off = lfs_min(end - noff, 0x3fe) + noff;
        if (off < end) {
            off = lfs_min(off, end - 2*sizeof(uint32_t));
        }
        noff = off + sizeof(uint32_t);
    }

    return LFS_ERR_OK;
}
#endif

#ifndef LFS_READONLY
static int lfs_dir_alloc(lfs_mdir_t* dir) {
    // allocate pair of dir blocks (backwards, so we write block 1 first)
    for (int i = 0; i < 2; i++) {
        int err = lfs_alloc(&dir->pair[(i + 1) % 2]);
        if (err) {
            return err;
        }
    }

    // zero for reproducability in case initial block is unreadable
    dir->rev = 0;

    // rather than clobbering one of the blocks we just pretend
    // the revision may be valid
    int err = lfs_bd_read(NULL, &lfs.rcache, sizeof(dir->rev), dir->pair[0], 0, &dir->rev,
                          sizeof(dir->rev));
    dir->rev = lfs_fromle32(dir->rev);
    if (err && err != LFS_ERR_CORRUPT) {
        return err;
    }

    // to make sure we don't immediately evict, align the new revision count
    // to our block_cycles modulus, see lfs_dir_compact for why our modulus
    // is tweaked this way
    if (lfs.cfg->block_cycles > 0) {
        dir->rev = lfs_alignup(dir->rev, ((lfs.cfg->block_cycles + 1) | 1));
    }

    // set defaults
    dir->off = sizeof(dir->rev);
    dir->etag = 0xffffffff;
    dir->count = 0;
    dir->tail[0] = LFS_BLOCK_NULL;
    dir->tail[1] = LFS_BLOCK_NULL;
    dir->erased = false;
    dir->split = false;

    // don't write out yet, let caller take care of that
    return LFS_ERR_OK;
}
#endif

#ifndef LFS_READONLY
static int lfs_dir_drop(lfs_mdir_t* dir, lfs_mdir_t* tail) {
    // steal state
    int err = lfs_dir_getgstate(tail, &lfs.gdelta);
    if (err) {
        return err;
    }

    // steal tail
    lfs_pair_tole32(tail->tail);
    err = lfs_dir_commit(
        dir, LFS_MKATTRS({LFS_MKTAG(LFS_TYPE_TAIL + tail->split, 0x3ff, 8), tail->tail}));
    lfs_pair_fromle32(tail->tail);
    if (err) {
        return err;
    }

    return LFS_ERR_OK;
}
#endif

#ifndef LFS_READONLY
static int lfs_dir_split(lfs_mdir_t* dir, const struct lfs_mattr* attrs, int attrcount,
                         lfs_mdir_t* source, uint16_t split, uint16_t end) {
    // create tail directory
    lfs_alloc_ack();
    lfs_mdir_t tail;
    int err = lfs_dir_alloc(&tail);
    if (err) {
        return err;
    }

    tail.split = dir->split;
    tail.tail[0] = dir->tail[0];
    tail.tail[1] = dir->tail[1];

    err = lfs_dir_compact(&tail, attrs, attrcount, source, split, end);
    if (err) {
        return err;
    }

    dir->tail[0] = tail.pair[0];
    dir->tail[1] = tail.pair[1];
    dir->split = true;

    // update root if needed
    if (lfs_pair_cmp(dir->pair, lfs.root) == 0 && split == 0) {
        lfs.root[0] = tail.pair[0];
        lfs.root[1] = tail.pair[1];
    }

    return LFS_ERR_OK;
}
#endif

#ifndef LFS_READONLY
static int lfs_dir_commit_size(void *p, lfs_tag_t tag, const void *buffer) {
    lfs_size_t *size = p;
    (void)buffer;

    *size += lfs_tag_dsize(tag);
    return LFS_ERR_OK;
}
#endif

#ifndef LFS_READONLY
struct lfs_dir_commit_commit {
    struct lfs_commit *commit;
};
#endif

#ifndef LFS_READONLY
static int lfs_dir_commit_commit(void *p, lfs_tag_t tag, const void *buffer) {
    struct lfs_dir_commit_commit *commit = p;
    return lfs_dir_commitattr(commit->commit, tag, buffer);
}
#endif

#ifndef LFS_READONLY
static int lfs_dir_compact(lfs_mdir_t* dir, const struct lfs_mattr* attrs, int attrcount,
                           lfs_mdir_t* source, uint16_t begin, uint16_t end) {
    // save some state in case block is bad
    const lfs_block_t oldpair[2] = {dir->pair[0], dir->pair[1]};
    bool relocated = false;
    bool tired = false;

    // should we split?
    while (end - begin > 1) {
        // find size
        lfs_size_t size = 0;
        int err = lfs_dir_traverse(source, 0, 0xffffffff, attrs, attrcount,
                                   LFS_MKTAG(0x400, 0x3ff, 0), LFS_MKTAG(LFS_TYPE_NAME, 0, 0),
                                   begin, end, -begin, lfs_dir_commit_size, &size);
        if (err) {
            return err;
        }

        // space is complicated, we need room for tail, crc, gstate,
        // cleanup delete, and we cap at half a block to give room
        // for metadata updates.
        if (end - begin < 0xff &&
            size <= lfs_min(lfs.cfg->block_size - 36,
                            lfs_alignup((lfs.cfg->metadata_max ? lfs.cfg->metadata_max
                                                               : lfs.cfg->block_size) /
                                            2,
                                        lfs.cfg->prog_size))) {
            break;
        }

        // can't fit, need to split, we should really be finding the
        // largest size that fits with a small binary search, but right now
        // it's not worth the code size
        uint16_t split = (end - begin) / 2;
        err = lfs_dir_split(dir, attrs, attrcount, source, begin + split, end);
        if (err) {
            // if we fail to split, we may be able to overcompact, unless
            // we're too big for even the full block, in which case our
            // only option is to error
            if (err == LFS_ERR_NOSPC && size <= lfs.cfg->block_size - 36) {
                break;
            }
            return err;
        }

        end = begin + split;
    }

    // increment revision count
    dir->rev += 1;
    // If our revision count == n * block_cycles, we should force a relocation,
    // this is how littlefs wear-levels at the metadata-pair level. Note that we
    // actually use (block_cycles+1)|1, this is to avoid two corner cases:
    // 1. block_cycles = 1, which would prevent relocations from terminating
    // 2. block_cycles = 2n, which, due to aliasing, would only ever relocate
    //    one metadata block in the pair, effectively making this useless
    if (lfs.cfg->block_cycles > 0 && (dir->rev % ((lfs.cfg->block_cycles + 1) | 1) == 0)) {
        if (lfs_pair_cmp(dir->pair, (const lfs_block_t[2]){0, 1}) == 0) {
            // oh no! we're writing too much to the superblock,
            // should we expand?
            lfs_ssize_t res = lfs_fs_rawsize();
            if (res < 0) {
                return res;
            }

            // do we have extra space? littlefs can't reclaim this space
            // by itself, so expand cautiously
            if ((lfs_size_t)res < lfs.cfg->block_count / 2) {
                LFS_DEBUG("Expanding superblock at rev %"PRIu32, dir->rev);
                int err = lfs_dir_split(dir, attrs, attrcount, source, begin, end);
                if (err && err != LFS_ERR_NOSPC) {
                    return err;
                }

                // welp, we tried, if we ran out of space there's not much
                // we can do, we'll error later if we've become frozen
                if (!err) {
                    end = begin;
                }
            }
        } else {
            // we're writing too much, time to relocate
            tired = true;
            goto relocate;
        }
    }

    // begin loop to commit compaction to blocks until a compact sticks
    while (true) {
        {
            // setup commit state
            struct lfs_commit commit = {
                .block = dir->pair[1],
                .off = 0,
                .ptag = 0xffffffff,
                .crc = 0xffffffff,

                .begin = 0,
                .end = (lfs.cfg->metadata_max ? lfs.cfg->metadata_max : lfs.cfg->block_size) - 8,
            };

            // erase block to write to
            int err = lfs_bd_erase(dir->pair[1]);
            if (err) {
                if (err == LFS_ERR_CORRUPT) {
                    goto relocate;
                }
                return err;
            }

            // write out header
            dir->rev = lfs_tole32(dir->rev);
            err = lfs_dir_commitprog(&commit, &dir->rev, sizeof(dir->rev));
            dir->rev = lfs_fromle32(dir->rev);
            if (err) {
                if (err == LFS_ERR_CORRUPT) {
                    goto relocate;
                }
                return err;
            }

            // traverse the directory, this time writing out all unique tags
            err = lfs_dir_traverse(source, 0, 0xffffffff, attrs, attrcount,
                                   LFS_MKTAG(0x400, 0x3ff, 0), LFS_MKTAG(LFS_TYPE_NAME, 0, 0),
                                   begin, end, -begin, lfs_dir_commit_commit,
                                   &(struct lfs_dir_commit_commit){&commit});
            if (err) {
                if (err == LFS_ERR_CORRUPT) {
                    goto relocate;
                }
                return err;
            }

            // commit tail, which may be new after last size check
            if (!lfs_pair_isnull(dir->tail)) {
                lfs_pair_tole32(dir->tail);
                err = lfs_dir_commitattr(&commit, LFS_MKTAG(LFS_TYPE_TAIL + dir->split, 0x3ff, 8),
                                         dir->tail);
                lfs_pair_fromle32(dir->tail);
                if (err) {
                    if (err == LFS_ERR_CORRUPT) {
                        goto relocate;
                    }
                    return err;
                }
            }

            // bring over gstate?
            lfs_gstate_t delta = {0};
            if (!relocated) {
                lfs_gstate_xor(&delta, &lfs.gdisk);
                lfs_gstate_xor(&delta, &lfs.gstate);
            }
            lfs_gstate_xor(&delta, &lfs.gdelta);
            delta.tag &= ~LFS_MKTAG(0, 0, 0x3ff);

            err = lfs_dir_getgstate(dir, &delta);
            if (err) {
                return err;
            }

            if (!lfs_gstate_iszero(&delta)) {
                lfs_gstate_tole32(&delta);
                err = lfs_dir_commitattr(
                    &commit, LFS_MKTAG(LFS_TYPE_MOVESTATE, 0x3ff, sizeof(delta)), &delta);
                if (err) {
                    if (err == LFS_ERR_CORRUPT) {
                        goto relocate;
                    }
                    return err;
                }
            }

            // complete commit with crc
            err = lfs_dir_commitcrc(&commit);
            if (err) {
                if (err == LFS_ERR_CORRUPT) {
                    goto relocate;
                }
                return err;
            }

            // successful compaction, swap dir pair to indicate most recent
            LFS_ASSERT(commit.off % lfs.cfg->prog_size == 0);
            lfs_pair_swap(dir->pair);
            dir->count = end - begin;
            dir->off = commit.off;
            dir->etag = commit.ptag;
            // update gstate
            lfs.gdelta = (lfs_gstate_t){0};
            if (!relocated) {
                lfs.gdisk = lfs.gstate;
            }
        }
        break;

relocate:
        // commit was corrupted, drop caches and prepare to relocate block
        relocated = true;
        lfs_cache_drop(&lfs.pcache);
        if (!tired) {
            LFS_DEBUG("Bad block at 0x%"PRIx32, dir->pair[1]);
        }

        // can't relocate superblock, filesystem is now frozen
        if (lfs_pair_cmp(dir->pair, (const lfs_block_t[2]){0, 1}) == 0) {
            LFS_WARN("Superblock 0x%"PRIx32" has become unwritable",
                    dir->pair[1]);
            return LFS_ERR_NOSPC;
        }

        // relocate half of pair
        int err = lfs_alloc(&dir->pair[1]);
        if (err && (err != LFS_ERR_NOSPC || !tired)) {
            return err;
        }

        tired = false;
        continue;
    }

    if (relocated) {
        // update references if we relocated
        LFS_DEBUG("Relocating {0x%"PRIx32", 0x%"PRIx32"} "
                    "-> {0x%"PRIx32", 0x%"PRIx32"}",
                oldpair[0], oldpair[1], dir->pair[0], dir->pair[1]);
        int err = lfs_fs_relocate(oldpair, dir->pair);
        if (err) {
            return err;
        }
    }

    return LFS_ERR_OK;
}
#endif

#ifndef LFS_READONLY
static int lfs_dir_commit(lfs_mdir_t* dir, const struct lfs_mattr* attrs, int attrcount) {
    // check for any inline files that aren't RAM backed and
    // forcefully evict them, needed for filesystem consistency
    for (lfs_file_t* f = (lfs_file_t*)lfs.mlist; f; f = f->next) {
        if (dir != &f->m && lfs_pair_cmp(f->m.pair, dir->pair) == 0 && f->type == LFS_TYPE_REG &&
            (f->flags & LFS_F_INLINE) && f->ctz.size > lfs.cfg->cache_size) {
            int err = lfs_file_outline(f);
            if (err) {
                return err;
            }

            err = lfs_file_flush(f);
            if (err) {
                return err;
            }
        }
    }

    // calculate changes to the directory
    lfs_mdir_t olddir = *dir;
    bool hasdelete = false;
    for (int i = 0; i < attrcount; i++) {
        if (lfs_tag_type3(attrs[i].tag) == LFS_TYPE_CREATE) {
            dir->count += 1;
        } else if (lfs_tag_type3(attrs[i].tag) == LFS_TYPE_DELETE) {
            LFS_ASSERT(dir->count > 0);
            dir->count -= 1;
            hasdelete = true;
        } else if (lfs_tag_type1(attrs[i].tag) == LFS_TYPE_TAIL) {
            dir->tail[0] = ((lfs_block_t*)attrs[i].buffer)[0];
            dir->tail[1] = ((lfs_block_t*)attrs[i].buffer)[1];
            dir->split = (lfs_tag_chunk(attrs[i].tag) & 1);
            lfs_pair_fromle32(dir->tail);
        }
    }

    // should we actually drop the directory block?
    if (hasdelete && dir->count == 0) {
        lfs_mdir_t pdir;
        int err = lfs_fs_pred(dir->pair, &pdir);
        if (err && err != LFS_ERR_NOENT) {
            *dir = olddir;
            return err;
        }

        if (err != LFS_ERR_NOENT && pdir.split) {
            err = lfs_dir_drop(&pdir, dir);
            if (err) {
                *dir = olddir;
                return err;
            }
        }
    }

    if (dir->erased || dir->count >= 0xff) {
        // try to commit
        struct lfs_commit commit = {
            .block = dir->pair[0],
            .off = dir->off,
            .ptag = dir->etag,
            .crc = 0xffffffff,

            .begin = dir->off,
            .end = (lfs.cfg->metadata_max ? lfs.cfg->metadata_max : lfs.cfg->block_size) - 8,
        };

        // traverse attrs that need to be written out
        lfs_pair_tole32(dir->tail);
        int err = lfs_dir_traverse(dir, dir->off, dir->etag, attrs, attrcount, 0, 0, 0, 0, 0,
                                   lfs_dir_commit_commit, &(struct lfs_dir_commit_commit){&commit});
        lfs_pair_fromle32(dir->tail);
        if (err) {
            if (err == LFS_ERR_NOSPC || err == LFS_ERR_CORRUPT) {
                goto compact;
            }
            *dir = olddir;
            return err;
        }

        // commit any global diffs if we have any
        lfs_gstate_t delta = {0};
        lfs_gstate_xor(&delta, &lfs.gstate);
        lfs_gstate_xor(&delta, &lfs.gdisk);
        lfs_gstate_xor(&delta, &lfs.gdelta);
        delta.tag &= ~LFS_MKTAG(0, 0, 0x3ff);
        if (!lfs_gstate_iszero(&delta)) {
            err = lfs_dir_getgstate(dir, &delta);
            if (err) {
                *dir = olddir;
                return err;
            }

            lfs_gstate_tole32(&delta);
            err = lfs_dir_commitattr(&commit, LFS_MKTAG(LFS_TYPE_MOVESTATE, 0x3ff, sizeof(delta)),
                                     &delta);
            if (err) {
                if (err == LFS_ERR_NOSPC || err == LFS_ERR_CORRUPT) {
                    goto compact;
                }
                *dir = olddir;
                return err;
            }
        }

        // finalize commit with the crc
        err = lfs_dir_commitcrc(&commit);
        if (err) {
            if (err == LFS_ERR_NOSPC || err == LFS_ERR_CORRUPT) {
                goto compact;
            }
            *dir = olddir;
            return err;
        }

        // successful commit, update dir
        LFS_ASSERT(commit.off % lfs.cfg->prog_size == 0);
        dir->off = commit.off;
        dir->etag = commit.ptag;
        // and update gstate
        lfs.gdisk = lfs.gstate;
        lfs.gdelta = (lfs_gstate_t){0};
    } else {
compact:
        // fall back to compaction
        lfs_cache_drop(&lfs.pcache);

        int err = lfs_dir_compact(dir, attrs, attrcount, dir, 0, dir->count);
        if (err) {
            *dir = olddir;
            return err;
        }
    }

    // this complicated bit of logic is for fixing up any active
    // metadata-pairs that we may have affected
    //
    // note we have to make two passes since the mdir passed to
    // lfs_dir_commit could also be in this list, and even then
    // we need to copy the pair so they don't get clobbered if we refetch
    // our mdir.
    for (struct lfs_mlist* d = lfs.mlist; d; d = d->next) {
        if (&d->m != dir && lfs_pair_cmp(d->m.pair, olddir.pair) == 0) {
            d->m = *dir;
            for (int i = 0; i < attrcount; i++) {
                if (lfs_tag_type3(attrs[i].tag) == LFS_TYPE_DELETE &&
                        d->id == lfs_tag_id(attrs[i].tag)) {
                    d->m.pair[0] = LFS_BLOCK_NULL;
                    d->m.pair[1] = LFS_BLOCK_NULL;
                } else if (lfs_tag_type3(attrs[i].tag) == LFS_TYPE_DELETE &&
                        d->id > lfs_tag_id(attrs[i].tag)) {
                    d->id -= 1;
                    if (d->type == LFS_TYPE_DIR) {
                        ((lfs_dir_t*)d)->pos -= 1;
                    }
                } else if (lfs_tag_type3(attrs[i].tag) == LFS_TYPE_CREATE &&
                        d->id >= lfs_tag_id(attrs[i].tag)) {
                    d->id += 1;
                    if (d->type == LFS_TYPE_DIR) {
                        ((lfs_dir_t*)d)->pos += 1;
                    }
                }
            }
        }
    }

    for (struct lfs_mlist* d = lfs.mlist; d; d = d->next) {
        if (lfs_pair_cmp(d->m.pair, olddir.pair) == 0) {
            while (d->id >= d->m.count && d->m.split) {
                // we split and id is on tail now
                d->id -= d->m.count;
                int err = lfs_dir_fetch(&d->m, d->m.tail);
                if (err) {
                    return err;
                }
            }
        }
    }

    return LFS_ERR_OK;
}
#endif


/// Top level directory operations ///
#ifndef LFS_READONLY
static int lfs_rawmkdir(const char* path) {
    // deorphan if we haven't yet, needed at most once after poweron
    int err = lfs_fs_forceconsistency();
    if (err) {
        return err;
    }

    struct lfs_mlist cwd;
    cwd.next = lfs.mlist;
    uint16_t id;
    err = lfs_dir_find(&cwd.m, &path, &id);
    if (!(err == LFS_ERR_NOENT && id != 0x3ff)) {
        return (err < 0) ? err : LFS_ERR_EXIST;
    }

    // check that name fits
    lfs_size_t nlen = strlen(path);
    if (nlen > lfs.name_max) {
        return LFS_ERR_NAMETOOLONG;
    }

    // build up new directory
    lfs_alloc_ack();
    lfs_mdir_t dir;
    err = lfs_dir_alloc(&dir);
    if (err) {
        return err;
    }

    // find end of list
    lfs_mdir_t pred = cwd.m;
    while (pred.split) {
        err = lfs_dir_fetch(&pred, pred.tail);
        if (err) {
            return err;
        }
    }

    // setup dir
    lfs_pair_tole32(pred.tail);
    err = lfs_dir_commit(&dir, LFS_MKATTRS({LFS_MKTAG(LFS_TYPE_SOFTTAIL, 0x3ff, 8), pred.tail}));
    lfs_pair_fromle32(pred.tail);
    if (err) {
        return err;
    }

    // current block end of list?
    if (cwd.m.split) {
        // update tails, this creates a desync
        err = lfs_fs_preporphans(+1);
        if (err) {
            return err;
        }

        // it's possible our predecessor has to be relocated, and if
        // our parent is our predecessor's predecessor, this could have
        // caused our parent to go out of date, fortunately we can hook
        // ourselves into littlefs to catch this
        cwd.type = 0;
        cwd.id = 0;
        lfs.mlist = &cwd;

        lfs_pair_tole32(dir.pair);
        err =
            lfs_dir_commit(&pred, LFS_MKATTRS({LFS_MKTAG(LFS_TYPE_SOFTTAIL, 0x3ff, 8), dir.pair}));
        lfs_pair_fromle32(dir.pair);
        if (err) {
            lfs.mlist = cwd.next;
            return err;
        }

        lfs.mlist = cwd.next;
        err = lfs_fs_preporphans(-1);
        if (err) {
            return err;
        }
    }

    // now insert into our parent block
    lfs_pair_tole32(dir.pair);
    err = lfs_dir_commit(
        &cwd.m, LFS_MKATTRS({LFS_MKTAG(LFS_TYPE_CREATE, id, 0), NULL},
                            {LFS_MKTAG(LFS_TYPE_DIR, id, nlen), path},
                            {LFS_MKTAG(LFS_TYPE_DIRSTRUCT, id, 8), dir.pair},
                            {LFS_MKTAG_IF(!cwd.m.split, LFS_TYPE_SOFTTAIL, 0x3ff, 8), dir.pair}));
    lfs_pair_fromle32(dir.pair);
    if (err) {
        return err;
    }

    return LFS_ERR_OK;
}
#endif

static int lfs_dir_rawopen(lfs_dir_t* dir, const char* path) {
    lfs_stag_t tag = lfs_dir_find(&dir->m, &path, NULL);
    if (tag < 0) {
        return tag;
    }

    if (lfs_tag_type3(tag) != LFS_TYPE_DIR) {
        return LFS_ERR_NOTDIR;
    }

    lfs_block_t pair[2];
    if (lfs_tag_id(tag) == 0x3ff) {
        // handle root dir separately
        pair[0] = lfs.root[0];
        pair[1] = lfs.root[1];
    } else {
        // get dir pair from parent
        lfs_stag_t res = lfs_dir_get(&dir->m, LFS_MKTAG(0x700, 0x3ff, 0),
                                     LFS_MKTAG(LFS_TYPE_STRUCT, lfs_tag_id(tag), 8), pair);
        if (res < 0) {
            return res;
        }
        lfs_pair_fromle32(pair);
    }

    // fetch first pair
    int err = lfs_dir_fetch(&dir->m, pair);
    if (err) {
        return err;
    }

    // setup entry
    dir->head[0] = dir->m.pair[0];
    dir->head[1] = dir->m.pair[1];
    dir->id = 0;
    dir->pos = 0;

    // add to list of mdirs
    dir->type = LFS_TYPE_DIR;
    lfs_mlist_append((struct lfs_mlist*)dir);

    return LFS_ERR_OK;
}

static int lfs_dir_rawclose(lfs_dir_t* dir) {
    // remove from list of mdirs
    lfs_mlist_remove((struct lfs_mlist*)dir);

    return LFS_ERR_OK;
}

static int lfs_dir_rawread(lfs_dir_t* dir, struct lfs_info* info) {
    memset(info, 0, sizeof(*info));

    // special offset for '.' and '..'
    if (dir->pos == 0) {
        info->type = LFS_TYPE_DIR;
        strcpy(info->name, ".");
        dir->pos += 1;
        return true;
    } else if (dir->pos == 1) {
        info->type = LFS_TYPE_DIR;
        strcpy(info->name, "..");
        dir->pos += 1;
        return true;
    }

    while (true) {
        if (dir->id == dir->m.count) {
            if (!dir->m.split) {
                return false;
            }

            int err = lfs_dir_fetch(&dir->m, dir->m.tail);
            if (err) {
                return err;
            }

            dir->id = 0;
        }

        int err = lfs_dir_getinfo(&dir->m, dir->id, info);
        if (err && err != LFS_ERR_NOENT) {
            return err;
        }

        dir->id += 1;
        if (err != LFS_ERR_NOENT) {
            break;
        }
    }

    dir->pos += 1;
    return true;
}

static int lfs_dir_rawseek(lfs_dir_t* dir, lfs_off_t off) {
    // simply walk from head dir
    int err = lfs_dir_rawrewind(dir);
    if (err) {
        return err;
    }

    // first two for ./..
    dir->pos = lfs_min(2, off);
    off -= dir->pos;

    // skip superblock entry
    dir->id = (off > 0 && lfs_pair_cmp(dir->head, lfs.root) == 0);

    while (off > 0) {
        int diff = lfs_min(dir->m.count - dir->id, off);
        dir->id += diff;
        dir->pos += diff;
        off -= diff;

        if (dir->id == dir->m.count) {
            if (!dir->m.split) {
                return LFS_ERR_INVAL;
            }

            err = lfs_dir_fetch(&dir->m, dir->m.tail);
            if (err) {
                return err;
            }

            dir->id = 0;
        }
    }

    return LFS_ERR_OK;
}

static lfs_soff_t lfs_dir_rawtell(lfs_dir_t* dir) {
    (void)lfs;
    return dir->pos;
}

static int lfs_dir_rawrewind(lfs_dir_t* dir) {
    // reload the head dir
    int err = lfs_dir_fetch(&dir->m, dir->head);
    if (err) {
        return err;
    }

    dir->id = 0;
    dir->pos = 0;
    return LFS_ERR_OK;
}

/// File index list operations ///
static int lfs_ctz_index(lfs_off_t* off) {
    lfs_off_t size = *off;
    lfs_off_t b = lfs.cfg->block_size - 2 * 4;
    lfs_off_t i = size / b;
    if (i == 0) {
        return 0;
    }

    i = (size - 4*(lfs_popc(i-1)+2)) / b;
    *off = size - b*i - 4*lfs_popc(i);
    return i;
}

static int lfs_ctz_find(const lfs_cache_t* pcache, lfs_cache_t* rcache, lfs_block_t head,
                        lfs_size_t size, lfs_size_t pos, lfs_block_t* block, lfs_off_t* off) {
    if (size == 0) {
        *block = LFS_BLOCK_NULL;
        *off = 0;
        return LFS_ERR_OK;
    }

    lfs_off_t current = lfs_ctz_index(&(lfs_off_t){size - 1});
    lfs_off_t target = lfs_ctz_index(&pos);

    while (current > target) {
        lfs_size_t skip = lfs_min(
                lfs_npw2(current-target+1) - 1,
                lfs_ctz(current));

        int err = lfs_bd_read(pcache, rcache, sizeof(head), head, 4 * skip, &head, sizeof(head));
        head = lfs_fromle32(head);
        if (err) {
            return err;
        }

        current -= 1 << skip;
    }

    *block = head;
    *off = pos;
    return LFS_ERR_OK;
}

#ifndef LFS_READONLY
static int lfs_ctz_extend(lfs_cache_t* pcache, lfs_cache_t* rcache, lfs_block_t head,
                          lfs_size_t size, lfs_block_t* block, lfs_off_t* off) {
    while (true) {
        // go ahead and grab a block
        lfs_block_t nblock;
        int err = lfs_alloc(&nblock);
        if (err) {
            return err;
        }

        {
            err = lfs_bd_erase(nblock);
            if (err) {
                if (err == LFS_ERR_CORRUPT) {
                    goto relocate;
                }
                return err;
            }

            if (size == 0) {
                *block = nblock;
                *off = 0;
                return LFS_ERR_OK;
            }

            lfs_size_t noff = size - 1;
            lfs_off_t index = lfs_ctz_index(&noff);
            noff = noff + 1;

            // just copy out the last block if it is incomplete
            if (noff != lfs.cfg->block_size) {
                for (lfs_off_t i = 0; i < noff; i++) {
                    uint8_t data;
                    err = lfs_bd_read(NULL, rcache, noff - i, head, i, &data, 1);
                    if (err) {
                        return err;
                    }

                    err = lfs_bd_prog(pcache, rcache, true, nblock, i, &data, 1);
                    if (err) {
                        if (err == LFS_ERR_CORRUPT) {
                            goto relocate;
                        }
                        return err;
                    }
                }

                *block = nblock;
                *off = noff;
                return LFS_ERR_OK;
            }

            // append block
            index += 1;
            lfs_size_t skips = lfs_ctz(index) + 1;
            lfs_block_t nhead = head;
            for (lfs_off_t i = 0; i < skips; i++) {
                nhead = lfs_tole32(nhead);
                err = lfs_bd_prog(pcache, rcache, true, nblock, 4 * i, &nhead, 4);
                nhead = lfs_fromle32(nhead);
                if (err) {
                    if (err == LFS_ERR_CORRUPT) {
                        goto relocate;
                    }
                    return err;
                }

                if (i != skips-1) {
                    err = lfs_bd_read(NULL, rcache, sizeof(nhead), nhead, 4 * i, &nhead,
                                      sizeof(nhead));
                    nhead = lfs_fromle32(nhead);
                    if (err) {
                        return err;
                    }
                }
            }

            *block = nblock;
            *off = 4*skips;
            return LFS_ERR_OK;
        }

relocate:
        LFS_DEBUG("Bad block at 0x%"PRIx32, nblock);

        // just clear cache and try a new block
        lfs_cache_drop(pcache);
    }
}
#endif

static int lfs_ctz_traverse(const lfs_cache_t* pcache, lfs_cache_t* rcache, lfs_block_t head,
                            lfs_size_t size, int (*cb)(void*, lfs_block_t), void* data) {
    if (size == 0) {
        return LFS_ERR_OK;
    }

    lfs_off_t index = lfs_ctz_index(&(lfs_off_t){size - 1});

    while (true) {
        int err = cb(data, head);
        if (err) {
            return err;
        }

        if (index == 0) {
            return LFS_ERR_OK;
        }

        lfs_block_t heads[2];
        int count = 2 - (index & 1);
        err = lfs_bd_read(pcache, rcache, count * sizeof(head), head, 0, &heads,
                          count * sizeof(head));
        heads[0] = lfs_fromle32(heads[0]);
        heads[1] = lfs_fromle32(heads[1]);
        if (err) {
            return err;
        }

        for (int i = 0; i < count-1; i++) {
            err = cb(data, heads[i]);
            if (err) {
                return err;
            }
        }

        head = heads[count-1];
        index -= count;
    }
}

/// Top level file operations ///
static int lfs_file_rawopencfg(lfs_file_t* file, const char* path, int flags,
                               const struct lfs_file_config* file_cfg) {
#ifndef LFS_READONLY
    // deorphan if we haven't yet, needed at most once after poweron
    if ((flags & LFS_O_WRONLY) == LFS_O_WRONLY) {
        int err = lfs_fs_forceconsistency();
        if (err) {
            return err;
        }
    }
#else
    LFS_ASSERT((flags & LFS_O_RDONLY) == LFS_O_RDONLY);
#endif

    // setup simple file details
    int err;
    file->file_cfg = file_cfg;
    file->flags = flags;
    file->pos = 0;
    file->off = 0;
    file->cache.buffer = NULL;

    // allocate entry for file if it doesn't exist
    lfs_stag_t tag = lfs_dir_find(&file->m, &path, &file->id);
    if (tag < 0 && !(tag == LFS_ERR_NOENT && file->id != 0x3ff)) {
        err = tag;
        goto cleanup;
    }

    // get id, add to list of mdirs to catch update changes
    file->type = LFS_TYPE_REG;
    lfs_mlist_append((struct lfs_mlist*)file);

#ifdef LFS_READONLY
    if (tag == LFS_ERR_NOENT) {
        err = LFS_ERR_NOENT;
        goto cleanup;
#else
    if (tag == LFS_ERR_NOENT) {
        if (!(flags & LFS_O_CREAT)) {
            err = LFS_ERR_NOENT;
            goto cleanup;
        }

        // check that name fits
        lfs_size_t nlen = strlen(path);
        if (nlen > lfs.name_max) {
            err = LFS_ERR_NAMETOOLONG;
            goto cleanup;
        }

        // get next slot and create entry to remember name
        err = lfs_dir_commit(&file->m,
                             LFS_MKATTRS({LFS_MKTAG(LFS_TYPE_CREATE, file->id, 0), NULL},
                                         {LFS_MKTAG(LFS_TYPE_REG, file->id, nlen), path},
                                         {LFS_MKTAG(LFS_TYPE_INLINESTRUCT, file->id, 0), NULL}));
        if (err) {
            err = LFS_ERR_NAMETOOLONG;
            goto cleanup;
        }

        tag = LFS_MKTAG(LFS_TYPE_INLINESTRUCT, 0, 0);
    } else if (flags & LFS_O_EXCL) {
        err = LFS_ERR_EXIST;
        goto cleanup;
#endif
    } else if (lfs_tag_type3(tag) != LFS_TYPE_REG) {
        err = LFS_ERR_ISDIR;
        goto cleanup;
#ifndef LFS_READONLY
    } else if (flags & LFS_O_TRUNC) {
        // truncate if requested
        tag = LFS_MKTAG(LFS_TYPE_INLINESTRUCT, file->id, 0);
        file->flags |= LFS_F_DIRTY;
#endif
    } else {
        // try to load what's on disk, if it's inlined we'll fix it later
        tag = lfs_dir_get(&file->m, LFS_MKTAG(0x700, 0x3ff, 0),
                          LFS_MKTAG(LFS_TYPE_STRUCT, file->id, 8), &file->ctz);
        if (tag < 0) {
            err = tag;
            goto cleanup;
        }
        lfs_ctz_fromle32(&file->ctz);
    }

    // fetch attrs
    for (unsigned i = 0; i < file->file_cfg->attr_count; i++) {
        // if opened for read / read-write operations
        if ((file->flags & LFS_O_RDONLY) == LFS_O_RDONLY) {
            lfs_stag_t res =
                lfs_dir_get(&file->m, LFS_MKTAG(0x7ff, 0x3ff, 0),
                            LFS_MKTAG(LFS_TYPE_USERATTR + file->file_cfg->attrs[i].type, file->id,
                                      file->file_cfg->attrs[i].size),
                            file->file_cfg->attrs[i].buffer);
            if (res < 0 && res != LFS_ERR_NOENT) {
                err = res;
                goto cleanup;
            }
        }

#ifndef LFS_READONLY
        // if opened for write / read-write operations
        if ((file->flags & LFS_O_WRONLY) == LFS_O_WRONLY) {
            if (file->file_cfg->attrs[i].size > lfs.attr_max) {
                err = LFS_ERR_NOSPC;
                goto cleanup;
            }

            file->flags |= LFS_F_DIRTY;
        }
#endif
    }

    // allocate buffer if needed
    if (file->file_cfg->buffer) {
        file->cache.buffer = file->file_cfg->buffer;
    } else {
        file->cache.buffer = lfs_malloc(lfs.cfg->cache_size);
        if (!file->cache.buffer) {
            err = LFS_ERR_NOMEM;
            goto cleanup;
        }
    }

    // zero to avoid information leak
    lfs_cache_zero(&file->cache);

    if (lfs_tag_type3(tag) == LFS_TYPE_INLINESTRUCT) {
        // load inline files
        file->ctz.head = LFS_BLOCK_INLINE;
        file->ctz.size = lfs_tag_size(tag);
        file->flags |= LFS_F_INLINE;
        file->cache.block = file->ctz.head;
        file->cache.off = 0;
        file->cache.size = lfs.cfg->cache_size;

        // don't always read (may be new/trunc file)
        if (file->ctz.size > 0) {
            lfs_stag_t res =
                lfs_dir_get(&file->m, LFS_MKTAG(0x700, 0x3ff, 0),
                            LFS_MKTAG(LFS_TYPE_STRUCT, file->id, lfs_min(file->cache.size, 0x3fe)),
                            file->cache.buffer);
            if (res < 0) {
                err = res;
                goto cleanup;
            }
        }
    }

    return LFS_ERR_OK;

cleanup:
    // clean up lingering resources
#ifndef LFS_READONLY
    file->flags |= LFS_F_ERRED;
#endif
    lfs_file_rawclose(file);
    return err;
}

static int lfs_file_rawopen(lfs_file_t* file, const char* path, int flags) {
    static const struct lfs_file_config defaults = {0};
    int err = lfs_file_rawopencfg(file, path, flags, &defaults);
    return err;
}

static int lfs_file_rawclose(lfs_file_t* file) {
#ifndef LFS_READONLY
    int err = lfs_file_rawsync(file);
#else
    int err = 0;
#endif

    // remove from list of mdirs
    lfs_mlist_remove((struct lfs_mlist*)file);

    // clean up memory
    if (!file->file_cfg->buffer) {
        lfs_free(file->cache.buffer);
    }

    return err;
}

#ifndef LFS_READONLY
static int lfs_file_relocate(lfs_file_t* file) {
    while (true) {
        // just relocate what exists into new block
        lfs_block_t nblock;
        int err = lfs_alloc(&nblock);
        if (err) {
            return err;
        }

        err = lfs_bd_erase(nblock);
        if (err) {
            if (err == LFS_ERR_CORRUPT) {
                goto relocate;
            }
            return err;
        }

        // either read from dirty cache or disk
        for (lfs_off_t i = 0; i < file->off; i++) {
            uint8_t data;
            if (file->flags & LFS_F_INLINE) {
                err = lfs_dir_getread(&file->m,
                                      // note we evict inline files before they can be dirty
                                      NULL, &file->cache, file->off - i, LFS_MKTAG(0xfff, 0x1ff, 0),
                                      LFS_MKTAG(LFS_TYPE_INLINESTRUCT, file->id, 0), i, &data, 1);
                if (err) {
                    return err;
                }
            } else {
                err =
                    lfs_bd_read(&file->cache, &lfs.rcache, file->off - i, file->block, i, &data, 1);
                if (err) {
                    return err;
                }
            }

            err = lfs_bd_prog(&lfs.pcache, &lfs.rcache, true, nblock, i, &data, 1);
            if (err) {
                if (err == LFS_ERR_CORRUPT) {
                    goto relocate;
                }
                return err;
            }
        }

        // copy over new state of file
        memcpy(file->cache.buffer, lfs.pcache.buffer, lfs.cfg->cache_size);
        file->cache.block = lfs.pcache.block;
        file->cache.off = lfs.pcache.off;
        file->cache.size = lfs.pcache.size;
        lfs_cache_zero(&lfs.pcache);

        file->block = nblock;
        file->flags |= LFS_F_WRITING;
        return LFS_ERR_OK;

relocate:
        LFS_DEBUG("Bad block at 0x%"PRIx32, nblock);

        // just clear cache and try a new block
        lfs_cache_drop(&lfs.pcache);
    }
}
#endif

#ifndef LFS_READONLY
static int lfs_file_outline(lfs_file_t* file) {
    file->off = file->pos;
    lfs_alloc_ack();
    int err = lfs_file_relocate(file);
    if (err) {
        return err;
    }

    file->flags &= ~LFS_F_INLINE;
    return LFS_ERR_OK;
}
#endif

#ifndef LFS_READONLY
static int lfs_file_flush(lfs_file_t* file) {
    if (file->flags & LFS_F_READING) {
        if (!(file->flags & LFS_F_INLINE)) {
            lfs_cache_drop(&file->cache);
        }
        file->flags &= ~LFS_F_READING;
    }

    if (file->flags & LFS_F_WRITING) {
        lfs_off_t pos = file->pos;

        if (!(file->flags & LFS_F_INLINE)) {
            // copy over anything after current branch
            lfs_file_t orig = {
                .ctz.head = file->ctz.head,
                .ctz.size = file->ctz.size,
                .flags = LFS_O_RDONLY,
                .pos = file->pos,
                .cache = lfs.rcache,
            };
            lfs_cache_drop(&lfs.rcache);

            while (file->pos < file->ctz.size) {
                // copy over a byte at a time, leave it up to caching
                // to make this efficient
                uint8_t data;
                lfs_ssize_t res = lfs_file_rawread(&orig, &data, 1);
                if (res < 0) {
                    return res;
                }

                res = lfs_file_rawwrite(file, &data, 1);
                if (res < 0) {
                    return res;
                }

                // keep our reference to the rcache in sync
                if (lfs.rcache.block != LFS_BLOCK_NULL) {
                    lfs_cache_drop(&orig.cache);
                    lfs_cache_drop(&lfs.rcache);
                }
            }

            // write out what we have
            while (true) {
                int err = lfs_bd_flush(&file->cache, &lfs.rcache, true);
                if (err) {
                    if (err == LFS_ERR_CORRUPT) {
                        goto relocate;
                    }
                    return err;
                }

                break;

relocate:
                LFS_DEBUG("Bad block at 0x%"PRIx32, file->block);
                err = lfs_file_relocate(file);
                if (err) {
                    return err;
                }
            }
        } else {
            file->pos = lfs_max(file->pos, file->ctz.size);
        }

        // actual file updates
        file->ctz.head = file->block;
        file->ctz.size = file->pos;
        file->flags &= ~LFS_F_WRITING;
        file->flags |= LFS_F_DIRTY;

        file->pos = pos;
    }

    return LFS_ERR_OK;
}
#endif

#ifndef LFS_READONLY
static int lfs_file_rawsync(lfs_file_t* file) {
    if (file->flags & LFS_F_ERRED) {
        // it's not safe to do anything if our file errored
        return LFS_ERR_OK;
    }

    int err = lfs_file_flush(file);
    if (err) {
        file->flags |= LFS_F_ERRED;
        return err;
    }


    if ((file->flags & LFS_F_DIRTY) &&
            !lfs_pair_isnull(file->m.pair)) {
        // update dir entry
        uint16_t type;
        const void *buffer;
        lfs_size_t size;
        struct lfs_ctz ctz;
        if (file->flags & LFS_F_INLINE) {
            // inline the whole file
            type = LFS_TYPE_INLINESTRUCT;
            buffer = file->cache.buffer;
            size = file->ctz.size;
        } else {
            // update the ctz reference
            type = LFS_TYPE_CTZSTRUCT;
            // copy ctz so alloc will work during a relocate
            ctz = file->ctz;
            lfs_ctz_tole32(&ctz);
            buffer = &ctz;
            size = sizeof(ctz);
        }

        // commit file data and attributes
        err = lfs_dir_commit(&file->m, LFS_MKATTRS({LFS_MKTAG(type, file->id, size), buffer},
                                                   {LFS_MKTAG(LFS_FROM_USERATTRS, file->id,
                                                              file->file_cfg->attr_count),
                                                    file->file_cfg->attrs}));
        if (err) {
            file->flags |= LFS_F_ERRED;
            return err;
        }

        file->flags &= ~LFS_F_DIRTY;
    }

    return LFS_ERR_OK;
}
#endif

static lfs_ssize_t lfs_file_rawread(lfs_file_t* file, void* buffer, lfs_size_t size) {
    LFS_ASSERT((file->flags & LFS_O_RDONLY) == LFS_O_RDONLY);

    uint8_t *data = buffer;
    lfs_size_t nsize = size;

#ifndef LFS_READONLY
    if (file->flags & LFS_F_WRITING) {
        // flush out any writes
        int err = lfs_file_flush(file);
        if (err) {
            return err;
        }
    }
#endif

    if (file->pos >= file->ctz.size) {
        // eof if past end
        return LFS_ERR_OK;
    }

    size = lfs_min(size, file->ctz.size - file->pos);
    nsize = size;

    while (nsize > 0) {
        // check if we need a new block
        if (!(file->flags & LFS_F_READING) || file->off == lfs.cfg->block_size) {
            if (!(file->flags & LFS_F_INLINE)) {
                int err = lfs_ctz_find(NULL, &file->cache, file->ctz.head, file->ctz.size,
                                       file->pos, &file->block, &file->off);
                if (err) {
                    return err;
                }
            } else {
                file->block = LFS_BLOCK_INLINE;
                file->off = file->pos;
            }

            file->flags |= LFS_F_READING;
        }

        // read as much as we can in current block
        lfs_size_t diff = lfs_min(nsize, lfs.cfg->block_size - file->off);
        if (file->flags & LFS_F_INLINE) {
            int err = lfs_dir_getread(
                &file->m, NULL, &file->cache, lfs.cfg->block_size, LFS_MKTAG(0xfff, 0x1ff, 0),
                LFS_MKTAG(LFS_TYPE_INLINESTRUCT, file->id, 0), file->off, data, diff);
            if (err) {
                return err;
            }
        } else {
            int err = lfs_bd_read(NULL, &file->cache, lfs.cfg->block_size, file->block, file->off,
                                  data, diff);
            if (err) {
                return err;
            }
        }

        file->pos += diff;
        file->off += diff;
        data += diff;
        nsize -= diff;
    }

    return size;
}

#ifndef LFS_READONLY
static lfs_ssize_t lfs_file_rawwrite(lfs_file_t* file, const void* buffer, lfs_size_t size) {
    LFS_ASSERT((file->flags & LFS_O_WRONLY) == LFS_O_WRONLY);

    const uint8_t *data = buffer;
    lfs_size_t nsize = size;

    if (file->flags & LFS_F_READING) {
        // drop any reads
        int err = lfs_file_flush(file);
        if (err) {
            return err;
        }
    }

    if ((file->flags & LFS_O_APPEND) && file->pos < file->ctz.size) {
        file->pos = file->ctz.size;
    }

    if (file->pos + size > lfs.file_max) {
        // Larger than file limit?
        return LFS_ERR_FBIG;
    }

    if (!(file->flags & LFS_F_WRITING) && file->pos > file->ctz.size) {
        // fill with zeros
        lfs_off_t pos = file->pos;
        file->pos = file->ctz.size;

        while (file->pos < pos) {
            lfs_ssize_t res = lfs_file_rawwrite(file, &(uint8_t){0}, 1);
            if (res < 0) {
                return res;
            }
        }
    }

    if ((file->flags & LFS_F_INLINE) &&
        lfs_max(file->pos + nsize, file->ctz.size) >
            lfs_min(0x3fe,
                    lfs_min(lfs.cfg->cache_size,
                            (lfs.cfg->metadata_max ? lfs.cfg->metadata_max : lfs.cfg->block_size) /
                                8))) {
        // inline file doesn't fit anymore
        int err = lfs_file_outline(file);
        if (err) {
            file->flags |= LFS_F_ERRED;
            return err;
        }
    }

    while (nsize > 0) {
        // check if we need a new block
        if (!(file->flags & LFS_F_WRITING) || file->off == lfs.cfg->block_size) {
            if (!(file->flags & LFS_F_INLINE)) {
                if (!(file->flags & LFS_F_WRITING) && file->pos > 0) {
                    // find out which block we're extending from
                    int err = lfs_ctz_find(NULL, &file->cache, file->ctz.head, file->ctz.size,
                                           file->pos - 1, &file->block, &file->off);
                    if (err) {
                        file->flags |= LFS_F_ERRED;
                        return err;
                    }

                    // mark cache as dirty since we may have read data into it
                    lfs_cache_zero(&file->cache);
                }

                // extend file with new blocks
                lfs_alloc_ack();
                int err = lfs_ctz_extend(&file->cache, &lfs.rcache, file->block, file->pos,
                                         &file->block, &file->off);
                if (err) {
                    file->flags |= LFS_F_ERRED;
                    return err;
                }
            } else {
                file->block = LFS_BLOCK_INLINE;
                file->off = file->pos;
            }

            file->flags |= LFS_F_WRITING;
        }

        // program as much as we can in current block
        lfs_size_t diff = lfs_min(nsize, lfs.cfg->block_size - file->off);
        while (true) {
            int err =
                lfs_bd_prog(&file->cache, &lfs.rcache, true, file->block, file->off, data, diff);
            if (err) {
                if (err == LFS_ERR_CORRUPT) {
                    goto relocate;
                }
                file->flags |= LFS_F_ERRED;
                return err;
            }

            break;
relocate:
    err = lfs_file_relocate(file);
    if (err) {
        file->flags |= LFS_F_ERRED;
        return err;
    }
        }

        file->pos += diff;
        file->off += diff;
        data += diff;
        nsize -= diff;

        lfs_alloc_ack();
    }

    file->flags &= ~LFS_F_ERRED;
    return size;
}
#endif

static lfs_soff_t lfs_file_rawseek(lfs_file_t* file, lfs_soff_t off, int whence) {
    // find new pos
    lfs_off_t npos = file->pos;
    if (whence == LFS_SEEK_SET) {
        npos = off;
    } else if (whence == LFS_SEEK_CUR) {
        npos = file->pos + off;
    } else if (whence == LFS_SEEK_END) {
        npos = lfs_file_rawsize(file) + off;
    }

    if (npos > lfs.file_max) {
        // file position out of range
        return LFS_ERR_INVAL;
    }

    if (file->pos == npos) {
        // noop - position has not changed
        return npos;
    }

#ifndef LFS_READONLY
    // write out everything beforehand, may be noop if rdonly
    int err = lfs_file_flush(file);
    if (err) {
        return err;
    }
#endif

    // update pos
    file->pos = npos;
    return npos;
}

#ifndef LFS_READONLY
static int lfs_file_rawtruncate(lfs_file_t* file, lfs_off_t size) {
    LFS_ASSERT((file->flags & LFS_O_WRONLY) == LFS_O_WRONLY);

    if (size > LFS_FILE_MAX) {
        return LFS_ERR_INVAL;
    }

    lfs_off_t pos = file->pos;
    lfs_off_t oldsize = lfs_file_rawsize(file);
    if (size < oldsize) {
        // need to flush since directly changing metadata
        int err = lfs_file_flush(file);
        if (err) {
            return err;
        }

        // lookup new head in ctz skip list
        err = lfs_ctz_find(NULL, &file->cache, file->ctz.head, file->ctz.size, size, &file->block,
                           &file->off);
        if (err) {
            return err;
        }

        // need to set pos/block/off consistently so seeking back to
        // the old position does not get confused
        file->pos = size;
        file->ctz.head = file->block;
        file->ctz.size = size;
        file->flags |= LFS_F_DIRTY | LFS_F_READING;
    } else if (size > oldsize) {
        // flush+seek if not already at end
        lfs_soff_t res = lfs_file_rawseek(file, 0, LFS_SEEK_END);
        if (res < 0) {
            return (int)res;
        }

        // fill with zeros
        while (file->pos < size) {
            res = lfs_file_rawwrite(file, &(uint8_t){0}, 1);
            if (res < 0) {
                return (int)res;
            }
        }
    }

    // restore pos
    lfs_soff_t res = lfs_file_rawseek(file, pos, LFS_SEEK_SET);
    if (res < 0) {
      return (int)res;
    }

    return LFS_ERR_OK;
}
#endif

static lfs_soff_t lfs_file_rawtell(lfs_file_t* file) {
    (void)lfs;
    return file->pos;
}

static int lfs_file_rawrewind(lfs_file_t* file) {
    lfs_soff_t res = lfs_file_rawseek(file, 0, LFS_SEEK_SET);
    if (res < 0) {
        return (int)res;
    }

    return LFS_ERR_OK;
}

static lfs_soff_t lfs_file_rawsize(lfs_file_t* file) {
    (void)lfs;

#ifndef LFS_READONLY
    if (file->flags & LFS_F_WRITING) {
        return lfs_max(file->pos, file->ctz.size);
    }
#endif

    return file->ctz.size;
}

/// General fs operations ///
static int lfs_rawstat(const char* path, struct lfs_info* info) {
    lfs_mdir_t cwd;
    lfs_stag_t tag = lfs_dir_find(&cwd, &path, NULL);
    if (tag < 0) {
        return (int)tag;
    }

    return lfs_dir_getinfo(&cwd, lfs_tag_id(tag), info);
}

#ifndef LFS_READONLY
static int lfs_rawremove(const char* path) {
    // deorphan if we haven't yet, needed at most once after poweron
    int err = lfs_fs_forceconsistency();
    if (err) {
        return err;
    }

    lfs_mdir_t cwd;
    lfs_stag_t tag = lfs_dir_find(&cwd, &path, NULL);
    if (tag < 0 || lfs_tag_id(tag) == 0x3ff) {
        return (tag < 0) ? (int)tag : LFS_ERR_INVAL;
    }

    struct lfs_mlist dir;
    dir.next = lfs.mlist;
    if (lfs_tag_type3(tag) == LFS_TYPE_DIR) {
        // must be empty before removal
        lfs_block_t pair[2];
        lfs_stag_t res = lfs_dir_get(&cwd, LFS_MKTAG(0x700, 0x3ff, 0),
                                     LFS_MKTAG(LFS_TYPE_STRUCT, lfs_tag_id(tag), 8), pair);
        if (res < 0) {
            return (int)res;
        }
        lfs_pair_fromle32(pair);

        err = lfs_dir_fetch(&dir.m, pair);
        if (err) {
            return err;
        }

        if (dir.m.count > 0 || dir.m.split) {
            return LFS_ERR_NOTEMPTY;
        }

        // mark fs as orphaned
        err = lfs_fs_preporphans(+1);
        if (err) {
            return err;
        }

        // I know it's crazy but yes, dir can be changed by our parent's
        // commit (if predecessor is child)
        dir.type = 0;
        dir.id = 0;
        lfs.mlist = &dir;
    }

    // delete the entry
    err = lfs_dir_commit(&cwd, LFS_MKATTRS({LFS_MKTAG(LFS_TYPE_DELETE, lfs_tag_id(tag), 0), NULL}));
    if (err) {
        lfs.mlist = dir.next;
        return err;
    }

    lfs.mlist = dir.next;
    if (lfs_tag_type3(tag) == LFS_TYPE_DIR) {
        // fix orphan
        err = lfs_fs_preporphans(-1);
        if (err) {
            return err;
        }

        err = lfs_fs_pred(dir.m.pair, &cwd);
        if (err) {
            return err;
        }

        err = lfs_dir_drop(&cwd, &dir.m);
        if (err) {
            return err;
        }
    }

    return LFS_ERR_OK;
}
#endif

#ifndef LFS_READONLY
static int lfs_rawrename(const char* oldpath, const char* newpath) {
    // deorphan if we haven't yet, needed at most once after poweron
    int err = lfs_fs_forceconsistency();
    if (err) {
        return err;
    }

    // find old entry
    lfs_mdir_t oldcwd;
    lfs_stag_t oldtag = lfs_dir_find(&oldcwd, &oldpath, NULL);
    if (oldtag < 0 || lfs_tag_id(oldtag) == 0x3ff) {
        return (oldtag < 0) ? (int)oldtag : LFS_ERR_INVAL;
    }

    // find new entry
    lfs_mdir_t newcwd;
    uint16_t newid;
    lfs_stag_t prevtag = lfs_dir_find(&newcwd, &newpath, &newid);
    if ((prevtag < 0 || lfs_tag_id(prevtag) == 0x3ff) &&
            !(prevtag == LFS_ERR_NOENT && newid != 0x3ff)) {
        return (prevtag < 0) ? (int)prevtag : LFS_ERR_INVAL;
    }

    // if we're in the same pair there's a few special cases...
    bool samepair = (lfs_pair_cmp(oldcwd.pair, newcwd.pair) == 0);
    uint16_t newoldid = lfs_tag_id(oldtag);

    struct lfs_mlist prevdir;
    prevdir.next = lfs.mlist;
    if (prevtag == LFS_ERR_NOENT) {
        // check that name fits
        lfs_size_t nlen = strlen(newpath);
        if (nlen > lfs.name_max) {
            return LFS_ERR_NAMETOOLONG;
        }

        // there is a small chance we are being renamed in the same
        // directory/ to an id less than our old id, the global update
        // to handle this is a bit messy
        if (samepair && newid <= newoldid) {
            newoldid += 1;
        }
    } else if (lfs_tag_type3(prevtag) != lfs_tag_type3(oldtag)) {
        return LFS_ERR_ISDIR;
    } else if (samepair && newid == newoldid) {
        // we're renaming to ourselves??
        return LFS_ERR_OK;
    } else if (lfs_tag_type3(prevtag) == LFS_TYPE_DIR) {
        // must be empty before removal
        lfs_block_t prevpair[2];
        lfs_stag_t res = lfs_dir_get(&newcwd, LFS_MKTAG(0x700, 0x3ff, 0),
                                     LFS_MKTAG(LFS_TYPE_STRUCT, newid, 8), prevpair);
        if (res < 0) {
            return (int)res;
        }
        lfs_pair_fromle32(prevpair);

        // must be empty before removal
        err = lfs_dir_fetch(&prevdir.m, prevpair);
        if (err) {
            return err;
        }

        if (prevdir.m.count > 0 || prevdir.m.split) {
            return LFS_ERR_NOTEMPTY;
        }

        // mark fs as orphaned
        err = lfs_fs_preporphans(+1);
        if (err) {
            return err;
        }

        // I know it's crazy but yes, dir can be changed by our parent's
        // commit (if predecessor is child)
        prevdir.type = 0;
        prevdir.id = 0;
        lfs.mlist = &prevdir;
    }

    if (!samepair) {
        lfs_fs_prepmove(newoldid, oldcwd.pair);
    }

    // move over all attributes
    err = lfs_dir_commit(
        &newcwd,
        LFS_MKATTRS({LFS_MKTAG_IF(prevtag != LFS_ERR_NOENT, LFS_TYPE_DELETE, newid, 0), NULL},
                    {LFS_MKTAG(LFS_TYPE_CREATE, newid, 0), NULL},
                    {LFS_MKTAG(lfs_tag_type3(oldtag), newid, strlen(newpath)), newpath},
                    {LFS_MKTAG(LFS_FROM_MOVE, newid, lfs_tag_id(oldtag)), &oldcwd},
                    {LFS_MKTAG_IF(samepair, LFS_TYPE_DELETE, newoldid, 0), NULL}));
    if (err) {
        lfs.mlist = prevdir.next;
        return err;
    }

    // let commit clean up after move (if we're different! otherwise move
    // logic already fixed it for us)
    if (!samepair && lfs_gstate_hasmove(&lfs.gstate)) {
        // prep gstate and delete move id
        lfs_fs_prepmove(0x3ff, NULL);
        err = lfs_dir_commit(
            &oldcwd, LFS_MKATTRS({LFS_MKTAG(LFS_TYPE_DELETE, lfs_tag_id(oldtag), 0), NULL}));
        if (err) {
            lfs.mlist = prevdir.next;
            return err;
        }
    }

    lfs.mlist = prevdir.next;
    if (prevtag != LFS_ERR_NOENT && lfs_tag_type3(prevtag) == LFS_TYPE_DIR) {
        // fix orphan
        err = lfs_fs_preporphans(-1);
        if (err) {
            return err;
        }

        err = lfs_fs_pred(prevdir.m.pair, &newcwd);
        if (err) {
            return err;
        }

        err = lfs_dir_drop(&newcwd, &prevdir.m);
        if (err) {
            return err;
        }
    }

    return LFS_ERR_OK;
}
#endif

static lfs_ssize_t lfs_rawgetattr(const char* path, uint8_t type, void* buffer, lfs_size_t size) {
    lfs_mdir_t cwd;
    lfs_stag_t tag = lfs_dir_find(&cwd, &path, NULL);
    if (tag < 0) {
        return tag;
    }

    uint16_t id = lfs_tag_id(tag);
    if (id == 0x3ff) {
        // special case for root
        id = 0;
        int err = lfs_dir_fetch(&cwd, lfs.root);
        if (err) {
            return err;
        }
    }

    tag = lfs_dir_get(&cwd, LFS_MKTAG(0x7ff, 0x3ff, 0),
                      LFS_MKTAG(LFS_TYPE_USERATTR + type, id, lfs_min(size, lfs.attr_max)), buffer);
    if (tag < 0) {
        if (tag == LFS_ERR_NOENT) {
            return LFS_ERR_NOATTR;
        }

        return tag;
    }

    return lfs_tag_size(tag);
}

#ifndef LFS_READONLY
static int lfs_commitattr(const char* path, uint8_t type, const void* buffer, lfs_size_t size) {
    lfs_mdir_t cwd;
    lfs_stag_t tag = lfs_dir_find(&cwd, &path, NULL);
    if (tag < 0) {
        return tag;
    }

    uint16_t id = lfs_tag_id(tag);
    if (id == 0x3ff) {
        // special case for root
        id = 0;
        int err = lfs_dir_fetch(&cwd, lfs.root);
        if (err) {
            return err;
        }
    }

    return lfs_dir_commit(&cwd,
                          LFS_MKATTRS({LFS_MKTAG(LFS_TYPE_USERATTR + type, id, size), buffer}));
}
#endif

#ifndef LFS_READONLY
static int lfs_rawsetattr(const char* path, uint8_t type, const void* buffer, lfs_size_t size) {
    if (size > lfs.attr_max) {
        return LFS_ERR_NOSPC;
    }

    return lfs_commitattr(path, type, buffer, size);
}
#endif

#ifndef LFS_READONLY
static int lfs_rawremoveattr(const char* path, uint8_t type) {
    return lfs_commitattr(path, type, NULL, 0x3ff);
}
#endif


/// Filesystem operations ///
static int lfs_init(const struct lfs_config* cfg) {
    lfs.cfg = cfg;
    int err = 0;

    // validate that the lfs-cfg sizes were initiated properly before
    // performing any arithmetic logics with them
    LFS_ASSERT(lfs.cfg->read_size != 0);
    LFS_ASSERT(lfs.cfg->prog_size != 0);
    LFS_ASSERT(lfs.cfg->cache_size != 0);

    // check that block size is a multiple of cache size is a multiple
    // of prog and read sizes
    LFS_ASSERT(lfs.cfg->cache_size % lfs.cfg->read_size == 0);
    LFS_ASSERT(lfs.cfg->cache_size % lfs.cfg->prog_size == 0);
    LFS_ASSERT(lfs.cfg->block_size % lfs.cfg->cache_size == 0);

    // check that the block size is large enough to fit ctz pointers
    LFS_ASSERT(4 * lfs_npw2(0xffffffff / (lfs.cfg->block_size - 2 * 4)) <= lfs.cfg->block_size);

    // block_cycles = 0 is no longer supported.
    //
    // block_cycles is the number of erase cycles before littlefs evicts
    // metadata logs as a part of wear leveling. Suggested values are in the
    // range of 100-1000, or set block_cycles to -1 to disable block-level
    // wear-leveling.
    LFS_ASSERT(lfs.cfg->block_cycles != 0);

    // setup read cache
    if (lfs.cfg->read_buffer) {
        lfs.rcache.buffer = lfs.cfg->read_buffer;
    } else {
        lfs.rcache.buffer = lfs_malloc(lfs.cfg->cache_size);
        if (!lfs.rcache.buffer) {
            err = LFS_ERR_NOMEM;
            goto cleanup;
        }
    }

    // setup program cache
    if (lfs.cfg->prog_buffer) {
        lfs.pcache.buffer = lfs.cfg->prog_buffer;
    } else {
        lfs.pcache.buffer = lfs_malloc(lfs.cfg->cache_size);
        if (!lfs.pcache.buffer) {
            err = LFS_ERR_NOMEM;
            goto cleanup;
        }
    }

    // zero to avoid information leaks
    lfs_cache_zero(&lfs.rcache);
    lfs_cache_zero(&lfs.pcache);

    // setup lookahead, must be multiple of 64-bits, 32-bit aligned
    LFS_ASSERT(lfs.cfg->lookahead_size > 0);
    LFS_ASSERT(lfs.cfg->lookahead_size % 8 == 0 && (uintptr_t)lfs.cfg->lookahead_buffer % 4 == 0);
    if (lfs.cfg->lookahead_buffer) {
        lfs.free.buffer = lfs.cfg->lookahead_buffer;
    } else {
        lfs.free.buffer = lfs_malloc(lfs.cfg->lookahead_size);
        if (!lfs.free.buffer) {
            err = LFS_ERR_NOMEM;
            goto cleanup;
        }
    }

    // check that the size limits are sane
    LFS_ASSERT(lfs.cfg->name_max <= LFS_NAME_MAX);
    lfs.name_max = lfs.cfg->name_max;
    if (!lfs.name_max) {
        lfs.name_max = LFS_NAME_MAX;
    }

    LFS_ASSERT(lfs.cfg->file_max <= LFS_FILE_MAX);
    lfs.file_max = lfs.cfg->file_max;
    if (!lfs.file_max) {
        lfs.file_max = LFS_FILE_MAX;
    }

    LFS_ASSERT(lfs.cfg->attr_max <= LFS_ATTR_MAX);
    lfs.attr_max = lfs.cfg->attr_max;
    if (!lfs.attr_max) {
        lfs.attr_max = LFS_ATTR_MAX;
    }

    LFS_ASSERT(lfs.cfg->metadata_max <= lfs.cfg->block_size);

    // setup default state
    lfs.root[0] = LFS_BLOCK_NULL;
    lfs.root[1] = LFS_BLOCK_NULL;
    lfs.mlist = NULL;
    lfs.seed = 0;
    lfs.gdisk = (lfs_gstate_t){0};
    lfs.gstate = (lfs_gstate_t){0};
    lfs.gdelta = (lfs_gstate_t){0};

    return LFS_ERR_OK;

cleanup:
    lfs_deinit();
    return err;
}

static int lfs_deinit(void) {
    // free allocated memory
    if (!lfs.cfg->read_buffer) {
        lfs_free(lfs.rcache.buffer);
    }

    if (!lfs.cfg->prog_buffer) {
        lfs_free(lfs.pcache.buffer);
    }

    if (!lfs.cfg->lookahead_buffer) {
        lfs_free(lfs.free.buffer);
    }

    return LFS_ERR_OK;
}

// Thread-safe wrappers if enabled
#if LIB_PICO_MULTICORE
#define LFS_LOCK lfs.cfg->lock()
#define LFS_UNLOCK lfs.cfg->unlock()
#else
#define LFS_LOCK LFS_ERR_OK
#define LFS_UNLOCK LFS_ERR_OK
#endif

#ifndef LFS_READONLY
static int lfs_rawformat(const struct lfs_config* cfg) {
    int err = lfs_init(cfg);
    if (err) {
        return err;
    }
    LFS_LOCK;

    // create free lookahead
    memset(lfs.free.buffer, 0, lfs.cfg->lookahead_size);
    lfs.free.off = 0;
    lfs.free.size = lfs_min(8 * lfs.cfg->lookahead_size, lfs.cfg->block_count);
    lfs.free.i = 0;
    lfs_alloc_ack();

    // create root dir
    lfs_mdir_t root;
    err = lfs_dir_alloc(&root);
    if (err) {
        goto cleanup;
    }

    // write one superblock
    lfs_superblock_t superblock = {
        .version = LFS_DISK_VERSION,
        .block_size = lfs.cfg->block_size,
        .block_count = lfs.cfg->block_count,
        .name_max = lfs.name_max,
        .file_max = lfs.file_max,
        .attr_max = lfs.attr_max,
    };

    lfs_superblock_tole32(&superblock);
    err = lfs_dir_commit(
        &root, LFS_MKATTRS({LFS_MKTAG(LFS_TYPE_CREATE, 0, 0), NULL},
                           {LFS_MKTAG(LFS_TYPE_SUPERBLOCK, 0, 8), "littlefs"},
                           {LFS_MKTAG(LFS_TYPE_INLINESTRUCT, 0, sizeof(superblock)), &superblock}));
    if (err) {
        goto cleanup;
    }

    // force compaction to prevent accidentally mounting any
    // older version of littlefs that may live on disk
    root.erased = false;
    err = lfs_dir_commit(&root, NULL, 0);
    if (err) {
        goto cleanup;
    }

    // sanity check that fetch works
    err = lfs_dir_fetch(&root, (const lfs_block_t[2]){0, 1});

cleanup:
    LFS_UNLOCK;
    lfs_deinit();
    return err;
}
#endif

static int lfs_rawmount(const struct lfs_config* cfg) {
    int err = lfs_init(cfg);
    if (err) {
        return err;
    }
    LFS_LOCK;
    // scan directory blocks for superblock and any global updates
    lfs_mdir_t dir = {.tail = {0, 1}};
    lfs_block_t cycle = 0;
    while (!lfs_pair_isnull(dir.tail)) {
        if (cycle >= lfs.cfg->block_count / 2) {
            // loop detected
            err = LFS_ERR_CORRUPT;
            goto cleanup;
        }
        cycle += 1;

        // fetch next block in tail list
        lfs_stag_t tag = lfs_dir_fetchmatch(
            &dir, dir.tail, LFS_MKTAG(0x7ff, 0x3ff, 0), LFS_MKTAG(LFS_TYPE_SUPERBLOCK, 0, 8), NULL,
            lfs_dir_find_match, &(struct lfs_dir_find_match){"littlefs", 8});
        if (tag < 0) {
            err = tag;
            goto cleanup;
        }

        // has superblock?
        if (tag && !lfs_tag_isdelete(tag)) {
            // update root
            lfs.root[0] = dir.pair[0];
            lfs.root[1] = dir.pair[1];

            // grab superblock
            lfs_superblock_t superblock;
            tag = lfs_dir_get(&dir, LFS_MKTAG(0x7ff, 0x3ff, 0),
                              LFS_MKTAG(LFS_TYPE_INLINESTRUCT, 0, sizeof(superblock)), &superblock);
            if (tag < 0) {
                err = tag;
                goto cleanup;
            }
            lfs_superblock_fromle32(&superblock);

            // check version
            uint16_t major_version = (0xffff & (superblock.version >> 16));
            uint16_t minor_version = (0xffff & (superblock.version >>  0));
            if ((major_version != LFS_DISK_VERSION_MAJOR ||
                 minor_version > LFS_DISK_VERSION_MINOR)) {
                LFS_ERROR("Invalid version v%"PRIu16".%"PRIu16,
                        major_version, minor_version);
                err = LFS_ERR_INVAL;
                goto cleanup;
            }

            // check superblock configuration
            if (superblock.name_max) {
                if (superblock.name_max > lfs.name_max) {
                    LFS_ERROR("Unsupported name_max (%" PRIu32 " > %" PRIu32 ")",
                              superblock.name_max, lfs.name_max);
                    err = LFS_ERR_INVAL;
                    goto cleanup;
                }

                lfs.name_max = superblock.name_max;
            }

            if (superblock.file_max) {
                if (superblock.file_max > lfs.file_max) {
                    LFS_ERROR("Unsupported file_max (%" PRIu32 " > %" PRIu32 ")",
                              superblock.file_max, lfs.file_max);
                    err = LFS_ERR_INVAL;
                    goto cleanup;
                }

                lfs.file_max = superblock.file_max;
            }

            if (superblock.attr_max) {
                if (superblock.attr_max > lfs.attr_max) {
                    LFS_ERROR("Unsupported attr_max (%" PRIu32 " > %" PRIu32 ")",
                              superblock.attr_max, lfs.attr_max);
                    err = LFS_ERR_INVAL;
                    goto cleanup;
                }

                lfs.attr_max = superblock.attr_max;
            }
        }

        // has gstate?
        err = lfs_dir_getgstate(&dir, &lfs.gstate);
        if (err) {
            goto cleanup;
        }
    }

    // found superblock?
    if (lfs_pair_isnull(lfs.root)) {
        err = LFS_ERR_INVAL;
        goto cleanup;
    }

    // update littlefs with gstate
    if (!lfs_gstate_iszero(&lfs.gstate)) {
        LFS_DEBUG("Found pending gstate 0x%08" PRIx32 "%08" PRIx32 "%08" PRIx32, lfs.gstate.tag,
                  lfs.gstate.pair[0], lfs.gstate.pair[1]);
    }
    lfs.gstate.tag += !lfs_tag_isvalid(lfs.gstate.tag);
    lfs.gdisk = lfs.gstate;

    // setup free lookahead, to distribute allocations uniformly across
    // boots, we start the allocator at a random location
    lfs.free.off = lfs.seed % lfs.cfg->block_count;
    lfs_alloc_drop();
    LFS_UNLOCK;
    return LFS_ERR_OK;

cleanup:
    lfs_rawunmount();
    LFS_UNLOCK;
    return err;
}

static int lfs_rawunmount(void) { return lfs_deinit(); }

/// Filesystem filesystem operations ///
int lfs_fs_rawtraverse(int (*cb)(void* data, lfs_block_t block), void* data, bool includeorphans) {
    // iterate over metadata pairs
    lfs_mdir_t dir = {.tail = {0, 1}};

    lfs_block_t cycle = 0;
    while (!lfs_pair_isnull(dir.tail)) {
        if (cycle >= lfs.cfg->block_count / 2) {
            // loop detected
            return LFS_ERR_CORRUPT;
        }
        cycle += 1;

        for (int i = 0; i < 2; i++) {
            int err = cb(data, dir.tail[i]);
            if (err) {
                return err;
            }
        }

        // iterate through ids in directory
        int err = lfs_dir_fetch(&dir, dir.tail);
        if (err) {
            return err;
        }

        for (uint16_t id = 0; id < dir.count; id++) {
            struct lfs_ctz ctz;
            lfs_stag_t tag = lfs_dir_get(&dir, LFS_MKTAG(0x700, 0x3ff, 0),
                                         LFS_MKTAG(LFS_TYPE_STRUCT, id, sizeof(ctz)), &ctz);
            if (tag < 0) {
                if (tag == LFS_ERR_NOENT) {
                    continue;
                }
                return tag;
            }
            lfs_ctz_fromle32(&ctz);

            if (lfs_tag_type3(tag) == LFS_TYPE_CTZSTRUCT) {
                err = lfs_ctz_traverse(NULL, &lfs.rcache, ctz.head, ctz.size, cb, data);
                if (err) {
                    return err;
                }
            } else if (includeorphans &&
                    lfs_tag_type3(tag) == LFS_TYPE_DIRSTRUCT) {
                for (int i = 0; i < 2; i++) {
                    err = cb(data, (&ctz.head)[i]);
                    if (err) {
                        return err;
                    }
                }
            }
        }
    }

#ifndef LFS_READONLY
    // iterate over any open files
    for (lfs_file_t* f = (lfs_file_t*)lfs.mlist; f; f = f->next) {
        if (f->type != LFS_TYPE_REG) {
            continue;
        }

        if ((f->flags & LFS_F_DIRTY) && !(f->flags & LFS_F_INLINE)) {
            int err = lfs_ctz_traverse(&f->cache, &lfs.rcache, f->ctz.head, f->ctz.size, cb, data);
            if (err) {
                return err;
            }
        }

        if ((f->flags & LFS_F_WRITING) && !(f->flags & LFS_F_INLINE)) {
            int err = lfs_ctz_traverse(&f->cache, &lfs.rcache, f->block, f->pos, cb, data);
            if (err) {
                return err;
            }
        }
    }
#endif

    return LFS_ERR_OK;
}

#ifndef LFS_READONLY
static int lfs_fs_pred(const lfs_block_t pair[2], lfs_mdir_t* pdir) {
    // iterate over all directory directory entries
    pdir->tail[0] = 0;
    pdir->tail[1] = 1;
    lfs_block_t cycle = 0;
    while (!lfs_pair_isnull(pdir->tail)) {
        if (cycle >= lfs.cfg->block_count / 2) {
            // loop detected
            return LFS_ERR_CORRUPT;
        }
        cycle += 1;

        if (lfs_pair_cmp(pdir->tail, pair) == 0) {
            return LFS_ERR_OK;
        }

        int err = lfs_dir_fetch(pdir, pdir->tail);
        if (err) {
            return err;
        }
    }

    return LFS_ERR_NOENT;
}
#endif

#ifndef LFS_READONLY
struct lfs_fs_parent_match {
    const lfs_block_t pair[2];
};
#endif

#ifndef LFS_READONLY
static int lfs_fs_parent_match(void *data,
        lfs_tag_t tag, const void *buffer) {
    struct lfs_fs_parent_match *find = data;
    const struct lfs_diskoff *disk = buffer;
    (void)tag;

    lfs_block_t child[2];
    int err = lfs_bd_read(&lfs.pcache, &lfs.rcache, lfs.cfg->block_size, disk->block, disk->off,
                          &child, sizeof(child));
    if (err) {
        return err;
    }

    lfs_pair_fromle32(child);
    return (lfs_pair_cmp(child, find->pair) == 0) ? LFS_CMP_EQ : LFS_CMP_LT;
}
#endif

#ifndef LFS_READONLY
static lfs_stag_t lfs_fs_parent(const lfs_block_t pair[2], lfs_mdir_t* parent) {
    // use fetchmatch with callback to find pairs
    parent->tail[0] = 0;
    parent->tail[1] = 1;
    lfs_block_t cycle = 0;
    while (!lfs_pair_isnull(parent->tail)) {
        if (cycle >= lfs.cfg->block_count / 2) {
            // loop detected
            return LFS_ERR_CORRUPT;
        }
        cycle += 1;

        lfs_stag_t tag = lfs_dir_fetchmatch(
            parent, parent->tail, LFS_MKTAG(0x7ff, 0, 0x3ff), LFS_MKTAG(LFS_TYPE_DIRSTRUCT, 0, 8),
            NULL, lfs_fs_parent_match, &(struct lfs_fs_parent_match){{pair[0], pair[1]}});
        if (tag && tag != LFS_ERR_NOENT) {
            return tag;
        }
    }

    return LFS_ERR_NOENT;
}
#endif

#ifndef LFS_READONLY
static int lfs_fs_relocate(const lfs_block_t oldpair[2], lfs_block_t newpair[2]) {
    // update internal root
    if (lfs_pair_cmp(oldpair, lfs.root) == 0) {
        lfs.root[0] = newpair[0];
        lfs.root[1] = newpair[1];
    }

    // update internally tracked dirs
    for (struct lfs_mlist* d = lfs.mlist; d; d = d->next) {
        if (lfs_pair_cmp(oldpair, d->m.pair) == 0) {
            d->m.pair[0] = newpair[0];
            d->m.pair[1] = newpair[1];
        }

        if (d->type == LFS_TYPE_DIR &&
                lfs_pair_cmp(oldpair, ((lfs_dir_t*)d)->head) == 0) {
            ((lfs_dir_t*)d)->head[0] = newpair[0];
            ((lfs_dir_t*)d)->head[1] = newpair[1];
        }
    }

    // find parent
    lfs_mdir_t parent;
    lfs_stag_t tag = lfs_fs_parent(oldpair, &parent);
    if (tag < 0 && tag != LFS_ERR_NOENT) {
        return tag;
    }

    if (tag != LFS_ERR_NOENT) {
        // update disk, this creates a desync
        int err = lfs_fs_preporphans(+1);
        if (err) {
            return err;
        }

        // fix pending move in this pair? this looks like an optimization but
        // is in fact _required_ since relocating may outdate the move.
        uint16_t moveid = 0x3ff;
        if (lfs_gstate_hasmovehere(&lfs.gstate, parent.pair)) {
            moveid = lfs_tag_id(lfs.gstate.tag);
            LFS_DEBUG("Fixing move while relocating "
                    "{0x%"PRIx32", 0x%"PRIx32"} 0x%"PRIx16"\n",
                    parent.pair[0], parent.pair[1], moveid);
            lfs_fs_prepmove(0x3ff, NULL);
            if (moveid < lfs_tag_id(tag)) {
                tag -= LFS_MKTAG(0, 1, 0);
            }
        }

        lfs_pair_tole32(newpair);
        err = lfs_dir_commit(
            &parent, LFS_MKATTRS({LFS_MKTAG_IF(moveid != 0x3ff, LFS_TYPE_DELETE, moveid, 0), NULL},
                                 {tag, newpair}));
        lfs_pair_fromle32(newpair);
        if (err) {
            return err;
        }

        // next step, clean up orphans
        err = lfs_fs_preporphans(-1);
        if (err) {
            return err;
        }
    }

    // find pred
    int err = lfs_fs_pred(oldpair, &parent);
    if (err && err != LFS_ERR_NOENT) {
        return err;
    }

    // if we can't find dir, it must be new
    if (err != LFS_ERR_NOENT) {
        // fix pending move in this pair? this looks like an optimization but
        // is in fact _required_ since relocating may outdate the move.
        uint16_t moveid = 0x3ff;
        if (lfs_gstate_hasmovehere(&lfs.gstate, parent.pair)) {
            moveid = lfs_tag_id(lfs.gstate.tag);
            LFS_DEBUG("Fixing move while relocating "
                    "{0x%"PRIx32", 0x%"PRIx32"} 0x%"PRIx16"\n",
                    parent.pair[0], parent.pair[1], moveid);
            lfs_fs_prepmove(0x3ff, NULL);
        }

        // replace bad pair, either we clean up desync, or no desync occured
        lfs_pair_tole32(newpair);
        err = lfs_dir_commit(
            &parent, LFS_MKATTRS({LFS_MKTAG_IF(moveid != 0x3ff, LFS_TYPE_DELETE, moveid, 0), NULL},
                                 {LFS_MKTAG(LFS_TYPE_TAIL + parent.split, 0x3ff, 8), newpair}));
        lfs_pair_fromle32(newpair);
        if (err) {
            return err;
        }
    }

    return LFS_ERR_OK;
}
#endif

#ifndef LFS_READONLY
static int lfs_fs_preporphans(int8_t orphans) {
    LFS_ASSERT(lfs_tag_size(lfs.gstate.tag) > 0 || orphans >= 0);
    lfs.gstate.tag += orphans;
    lfs.gstate.tag = ((lfs.gstate.tag & ~LFS_MKTAG(0x800, 0, 0)) |
                      ((uint32_t)lfs_gstate_hasorphans(&lfs.gstate) << 31));

    return LFS_ERR_OK;
}
#endif

#ifndef LFS_READONLY
static void lfs_fs_prepmove(uint16_t id, const lfs_block_t pair[2]) {
    lfs.gstate.tag = ((lfs.gstate.tag & ~LFS_MKTAG(0x7ff, 0x3ff, 0)) |
                      ((id != 0x3ff) ? LFS_MKTAG(LFS_TYPE_DELETE, id, 0) : 0));
    lfs.gstate.pair[0] = (id != 0x3ff) ? pair[0] : 0;
    lfs.gstate.pair[1] = (id != 0x3ff) ? pair[1] : 0;
}
#endif

#ifndef LFS_READONLY
static int lfs_fs_demove(void) {
    if (!lfs_gstate_hasmove(&lfs.gdisk)) {
        return LFS_ERR_OK;
    }

    // Fix bad moves
    LFS_DEBUG("Fixing move {0x%" PRIx32 ", 0x%" PRIx32 "} 0x%" PRIx16, lfs.gdisk.pair[0],
              lfs.gdisk.pair[1], lfs_tag_id(lfs.gdisk.tag));

    // fetch and delete the moved entry
    lfs_mdir_t movedir;
    int err = lfs_dir_fetch(&movedir, lfs.gdisk.pair);
    if (err) {
        return err;
    }

    // prep gstate and delete move id
    uint16_t moveid = lfs_tag_id(lfs.gdisk.tag);
    lfs_fs_prepmove(0x3ff, NULL);
    err = lfs_dir_commit(&movedir, LFS_MKATTRS({LFS_MKTAG(LFS_TYPE_DELETE, moveid, 0), NULL}));
    if (err) {
        return err;
    }

    return LFS_ERR_OK;
}
#endif

#ifndef LFS_READONLY
static int lfs_fs_deorphan(void) {
    if (!lfs_gstate_hasorphans(&lfs.gstate)) {
        return LFS_ERR_OK;
    }

    // Fix any orphans
    lfs_mdir_t pdir = {.split = true, .tail = {0, 1}};
    lfs_mdir_t dir;

    // iterate over all directory directory entries
    while (!lfs_pair_isnull(pdir.tail)) {
        int err = lfs_dir_fetch(&dir, pdir.tail);
        if (err) {
            return err;
        }

        // check head blocks for orphans
        if (!pdir.split) {
            // check if we have a parent
            lfs_mdir_t parent;
            lfs_stag_t tag = lfs_fs_parent(pdir.tail, &parent);
            if (tag < 0 && tag != LFS_ERR_NOENT) {
                return tag;
            }

            if (tag == LFS_ERR_NOENT) {
                // we are an orphan
                LFS_DEBUG("Fixing orphan {0x%"PRIx32", 0x%"PRIx32"}",
                        pdir.tail[0], pdir.tail[1]);

                err = lfs_dir_drop(&pdir, &dir);
                if (err) {
                    return err;
                }

                // refetch tail
                continue;
            }

            lfs_block_t pair[2];
            lfs_stag_t res = lfs_dir_get(&parent, LFS_MKTAG(0x7ff, 0x3ff, 0), tag, pair);
            if (res < 0) {
                return res;
            }
            lfs_pair_fromle32(pair);

            if (!lfs_pair_sync(pair, pdir.tail)) {
                // we have desynced
                LFS_DEBUG("Fixing half-orphan {0x%"PRIx32", 0x%"PRIx32"} "
                            "-> {0x%"PRIx32", 0x%"PRIx32"}",
                        pdir.tail[0], pdir.tail[1], pair[0], pair[1]);

                lfs_pair_tole32(pair);
                err = lfs_dir_commit(&pdir,
                                     LFS_MKATTRS({LFS_MKTAG(LFS_TYPE_SOFTTAIL, 0x3ff, 8), pair}));
                lfs_pair_fromle32(pair);
                if (err) {
                    return err;
                }

                // refetch tail
                continue;
            }
        }

        pdir = dir;
    }

    // mark orphans as fixed
    return lfs_fs_preporphans(-lfs_gstate_getorphans(&lfs.gstate));
}
#endif

#ifndef LFS_READONLY
static int lfs_fs_forceconsistency(void) {
    int err = lfs_fs_demove();
    if (err) {
        return err;
    }

    err = lfs_fs_deorphan();
    if (err) {
        return err;
    }

    return LFS_ERR_OK;
}
#endif

static int lfs_fs_size_count(void *p, lfs_block_t block) {
    (void)block;
    lfs_size_t *size = p;
    *size += 1;
    return LFS_ERR_OK;
}

static lfs_ssize_t lfs_fs_rawsize(void) {
    lfs_size_t size = 0;
    int err = lfs_fs_rawtraverse(lfs_fs_size_count, &size, false);
    if (err) {
        return err;
    }

    return size;
}

/// Public API wrappers ///

// Here we can add tracing/thread safety easily

// Public API
#ifndef LFS_READONLY
int lfs_format(const struct lfs_config* cfg) {
    LFS_TRACE("lfs_format(%p, %p {.context=%p, "
                ".read=%p, .prog=%p, .erase=%p, .sync=%p, "
                ".read_size=%"PRIu32", .prog_size=%"PRIu32", "
                ".block_size=%"PRIu32", .block_count=%"PRIu32", "
                ".block_cycles=%"PRIu32", .cache_size=%"PRIu32", "
                ".lookahead_size=%"PRIu32", .read_buffer=%p, "
                ".prog_buffer=%p, .lookahead_buffer=%p, "
                ".name_max=%"PRIu32", .file_max=%"PRIu32", "
                ".attr_max=%"PRIu32"})",
            (void*)lfs, (void*)cfg, cfg->context,
            (void*)(uintptr_t)cfg->read, (void*)(uintptr_t)cfg->prog,
            (void*)(uintptr_t)cfg->erase, (void*)(uintptr_t)cfg->sync,
            cfg->read_size, cfg->prog_size, cfg->block_size, cfg->block_count,
            cfg->block_cycles, cfg->cache_size, cfg->lookahead_size,
            cfg->read_buffer, cfg->prog_buffer, cfg->lookahead_buffer,
            cfg->name_max, cfg->file_max, cfg->attr_max);

    int err = lfs_rawformat(cfg);

    LFS_TRACE("lfs_format -> %d", err);
    return err;
}
#endif

int lfs_mount(const struct lfs_config* cfg) {
    LFS_TRACE("lfs_mount(%p, %p {.context=%p, "
                ".read=%p, .prog=%p, .erase=%p, .sync=%p, "
                ".read_size=%"PRIu32", .prog_size=%"PRIu32", "
                ".block_size=%"PRIu32", .block_count=%"PRIu32", "
                ".block_cycles=%"PRIu32", .cache_size=%"PRIu32", "
                ".lookahead_size=%"PRIu32", .read_buffer=%p, "
                ".prog_buffer=%p, .lookahead_buffer=%p, "
                ".name_max=%"PRIu32", .file_max=%"PRIu32", "
                ".attr_max=%"PRIu32"})",
            (void*)lfs, (void*)cfg, cfg->context,
            (void*)(uintptr_t)cfg->read, (void*)(uintptr_t)cfg->prog,
            (void*)(uintptr_t)cfg->erase, (void*)(uintptr_t)cfg->sync,
            cfg->read_size, cfg->prog_size, cfg->block_size, cfg->block_count,
            cfg->block_cycles, cfg->cache_size, cfg->lookahead_size,
            cfg->read_buffer, cfg->prog_buffer, cfg->lookahead_buffer,
            cfg->name_max, cfg->file_max, cfg->attr_max);

    int err = lfs_rawmount(cfg);

    LFS_TRACE("lfs_mount -> %d", err);
    return err;
}

int lfs_unmount(void) {
    LFS_TRACE("lfs_unmount(%p)", (void*)lfs);
    int err = lfs_rawunmount();
    LFS_TRACE("lfs_unmount -> %d", err);
    return err;
}

#ifndef LFS_READONLY
int lfs_remove(const char* path) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_remove(%p, \"%s\")", (void*)lfs, path);

    err = lfs_rawremove(path);

    LFS_TRACE("lfs_remove -> %d", err);
    LFS_UNLOCK;
    return err;
}
#endif

#ifndef LFS_READONLY
int lfs_rename(const char* oldpath, const char* newpath) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_rename(%p, \"%s\", \"%s\")", (void*)lfs, oldpath, newpath);

    err = lfs_rawrename(oldpath, newpath);

    LFS_TRACE("lfs_rename -> %d", err);
    LFS_UNLOCK;
    return err;
}
#endif

int lfs_stat(const char* path, struct lfs_info* info) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_stat(%p, \"%s\", %p)", (void*)lfs, path, (void*)info);

    err = lfs_rawstat(path, info);

    LFS_TRACE("lfs_stat -> %d", err);
    LFS_UNLOCK;
    return err;
}

lfs_ssize_t lfs_getattr(const char* path, uint8_t type, void* buffer, lfs_size_t size) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_getattr(%p, \"%s\", %"PRIu8", %p, %"PRIu32")",
            (void*)lfs, path, type, buffer, size);

    lfs_ssize_t res = lfs_rawgetattr(path, type, buffer, size);

    LFS_TRACE("lfs_getattr -> %"PRId32, res);
    LFS_UNLOCK;
    return res;
}

#ifndef LFS_READONLY
int lfs_setattr(const char* path, uint8_t type, const void* buffer, lfs_size_t size) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_setattr(%p, \"%s\", %"PRIu8", %p, %"PRIu32")",
            (void*)lfs, path, type, buffer, size);

    err = lfs_rawsetattr(path, type, buffer, size);

    LFS_TRACE("lfs_setattr -> %d", err);
    LFS_UNLOCK;
    return err;
}
#endif

#ifndef LFS_READONLY
int lfs_removeattr(const char* path, uint8_t type) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_removeattr(%p, \"%s\", %"PRIu8")", (void*)lfs, path, type);

    err = lfs_rawremoveattr(path, type);

    LFS_TRACE("lfs_removeattr -> %d", err);
    LFS_UNLOCK;
    return err;
}
#endif

int lfs_file_open(lfs_file_t* file, const char* path, int flags) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_file_open(%p, %p, \"%s\", %x)",
            (void*)lfs, (void*)file, path, flags);
    LFS_ASSERT(!lfs_mlist_isopen(lfs.mlist, (struct lfs_mlist*)file));

    err = lfs_file_rawopen(file, path, flags);

    LFS_TRACE("lfs_file_open -> %d", err);
    LFS_UNLOCK;
    return err;
}

int lfs_file_opencfg(lfs_file_t* file, const char* path, int flags,
                     const struct lfs_file_config* cfg) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_file_opencfg(%p, %p, \"%s\", %x, %p {"
                 ".buffer=%p, .attrs=%p, .attr_count=%"PRIu32"})",
            (void*)lfs, (void*)file, path, flags,
            (void*)cfg, cfg->buffer, (void*)cfg->attrs, cfg->attr_count);
    LFS_ASSERT(!lfs_mlist_isopen(lfs.mlist, (struct lfs_mlist*)file));

    err = lfs_file_rawopencfg(file, path, flags, cfg);

    LFS_TRACE("lfs_file_opencfg -> %d", err);
    LFS_UNLOCK;
    return err;
}

int lfs_file_close(lfs_file_t* file) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_file_close(%p, %p)", (void*)lfs, (void*)file);
    LFS_ASSERT(lfs_mlist_isopen(lfs.mlist, (struct lfs_mlist*)file));

    err = lfs_file_rawclose(file);

    LFS_TRACE("lfs_file_close -> %d", err);
    LFS_UNLOCK;
    return err;
}

#ifndef LFS_READONLY
int lfs_file_sync(lfs_file_t* file) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_file_sync(%p, %p)", (void*)lfs, (void*)file);
    LFS_ASSERT(lfs_mlist_isopen(lfs.mlist, (struct lfs_mlist*)file));

    err = lfs_file_rawsync(file);

    LFS_TRACE("lfs_file_sync -> %d", err);
    LFS_UNLOCK;
    return err;
}
#endif

lfs_ssize_t lfs_file_read(lfs_file_t* file, void* buffer, lfs_size_t size) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_file_read(%p, %p, %p, %"PRIu32")",
            (void*)lfs, (void*)file, buffer, size);
    LFS_ASSERT(lfs_mlist_isopen(lfs.mlist, (struct lfs_mlist*)file));

    lfs_ssize_t res = lfs_file_rawread(file, buffer, size);

    LFS_TRACE("lfs_file_read -> %"PRId32, res);
    LFS_UNLOCK;
    return res;
}

#ifndef LFS_READONLY
lfs_ssize_t lfs_file_write(lfs_file_t* file, const void* buffer, lfs_size_t size) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_file_write(%p, %p, %p, %"PRIu32")",
            (void*)lfs, (void*)file, buffer, size);
    LFS_ASSERT(lfs_mlist_isopen(lfs.mlist, (struct lfs_mlist*)file));

    lfs_ssize_t res = lfs_file_rawwrite(file, buffer, size);

    LFS_TRACE("lfs_file_write -> %"PRId32, res);
    LFS_UNLOCK;
    return res;
}
#endif

lfs_soff_t lfs_file_seek(lfs_file_t* file, lfs_soff_t off, int whence) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_file_seek(%p, %p, %"PRId32", %d)",
            (void*)lfs, (void*)file, off, whence);
    LFS_ASSERT(lfs_mlist_isopen(lfs.mlist, (struct lfs_mlist*)file));

    lfs_soff_t res = lfs_file_rawseek(file, off, whence);

    LFS_TRACE("lfs_file_seek -> %"PRId32, res);
    LFS_UNLOCK;
    return res;
}

#ifndef LFS_READONLY
int lfs_file_truncate(lfs_file_t* file, lfs_off_t size) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_file_truncate(%p, %p, %"PRIu32")",
            (void*)lfs, (void*)file, size);
    LFS_ASSERT(lfs_mlist_isopen(lfs.mlist, (struct lfs_mlist*)file));

    err = lfs_file_rawtruncate(file, size);

    LFS_TRACE("lfs_file_truncate -> %d", err);
    LFS_UNLOCK;
    return err;
}
#endif

lfs_soff_t lfs_file_tell(lfs_file_t* file) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_file_tell(%p, %p)", (void*)lfs, (void*)file);
    LFS_ASSERT(lfs_mlist_isopen(lfs.mlist, (struct lfs_mlist*)file));

    lfs_soff_t res = lfs_file_rawtell(file);

    LFS_TRACE("lfs_file_tell -> %"PRId32, res);
    LFS_UNLOCK;
    return res;
}

int lfs_file_rewind(lfs_file_t* file) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_file_rewind(%p, %p)", (void*)lfs, (void*)file);

    err = lfs_file_rawrewind(file);

    LFS_TRACE("lfs_file_rewind -> %d", err);
    LFS_UNLOCK;
    return err;
}

lfs_soff_t lfs_file_size(lfs_file_t* file) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_file_size(%p, %p)", (void*)lfs, (void*)file);
    LFS_ASSERT(lfs_mlist_isopen(lfs.mlist, (struct lfs_mlist*)file));

    lfs_soff_t res = lfs_file_rawsize(file);

    LFS_TRACE("lfs_file_size -> %"PRId32, res);
    LFS_UNLOCK;
    return res;
}

#ifndef LFS_READONLY
int lfs_mkdir(const char* path) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_mkdir(%p, \"%s\")", (void*)lfs, path);

    err = lfs_rawmkdir(path);

    LFS_TRACE("lfs_mkdir -> %d", err);
    LFS_UNLOCK;
    return err;
}
#endif

int lfs_dir_open(lfs_dir_t* dir, const char* path) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_dir_open(%p, %p, \"%s\")", (void*)lfs, (void*)dir, path);
    LFS_ASSERT(!lfs_mlist_isopen(lfs.mlist, (struct lfs_mlist*)dir));

    err = lfs_dir_rawopen(dir, path);

    LFS_TRACE("lfs_dir_open -> %d", err);
    LFS_UNLOCK;
    return err;
}

int lfs_dir_close(lfs_dir_t* dir) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_dir_close(%p, %p)", (void*)lfs, (void*)dir);

    err = lfs_dir_rawclose(dir);

    LFS_TRACE("lfs_dir_close -> %d", err);
    LFS_UNLOCK;
    return err;
}

int lfs_dir_read(lfs_dir_t* dir, struct lfs_info* info) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_dir_read(%p, %p, %p)",
            (void*)lfs, (void*)dir, (void*)info);

    err = lfs_dir_rawread(dir, info);

    LFS_TRACE("lfs_dir_read -> %d", err);
    LFS_UNLOCK;
    return err;
}

int lfs_dir_seek(lfs_dir_t* dir, lfs_off_t off) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_dir_seek(%p, %p, %"PRIu32")",
            (void*)lfs, (void*)dir, off);

    err = lfs_dir_rawseek(dir, off);

    LFS_TRACE("lfs_dir_seek -> %d", err);
    LFS_UNLOCK;
    return err;
}

lfs_soff_t lfs_dir_tell(lfs_dir_t* dir) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_dir_tell(%p, %p)", (void*)lfs, (void*)dir);

    lfs_soff_t res = lfs_dir_rawtell(dir);

    LFS_TRACE("lfs_dir_tell -> %"PRId32, res);
    LFS_UNLOCK;
    return res;
}

int lfs_dir_rewind(lfs_dir_t* dir) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_dir_rewind(%p, %p)", (void*)lfs, (void*)dir);

    err = lfs_dir_rawrewind(dir);

    LFS_TRACE("lfs_dir_rewind -> %d", err);
    LFS_UNLOCK;
    return err;
}

lfs_ssize_t lfs_fs_size(void) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_fs_size(%p)", (void*)lfs);

    lfs_ssize_t res = lfs_fs_rawsize();

    LFS_TRACE("lfs_fs_size -> %"PRId32, res);
    LFS_UNLOCK;
    return res;
}

int lfs_fs_traverse(int (*cb)(void*, lfs_block_t), void* data) {
    int err = LFS_LOCK;
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_fs_traverse(%p, %p, %p)",
            (void*)lfs, (void*)(uintptr_t)cb, data);

    err = lfs_fs_rawtraverse(cb, data, true);

    LFS_TRACE("lfs_fs_traverse -> %d", err);
    LFS_UNLOCK;
    return err;
}

// Software CRC implementation with small lookup table
uint32_t lfs_crc(uint32_t crc, const void* buffer, size_t size) {
    static const uint32_t rtable[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4,
        0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
    };

    const uint8_t* data = buffer;

    for (size_t i = 0; i < size; i++) {
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 0)) & 0xf];
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 4)) & 0xf];
    }

    return crc;
}
