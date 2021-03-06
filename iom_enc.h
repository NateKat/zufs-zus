/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * iom_enc.h - Encodes an iom_elemets array to send to Kernel
 *
 * Encoding is only done in user-mode. And decoding only in Kernel.
 * This is the encoding side. Common stuff come from zus_api.h
 *
 * Copyright (c) 2018 NetApp, Inc. All rights reserved.
 *
 * See module.c for LICENSE details.
 *
 * Authors:
 *	Boaz Harrosh <boazh@netapp.com>
 */
#ifndef __ZUS_IOM_H__
#define __ZUS_IOM_H__

#include <errno.h>

#include "zus.h"


typedef void (*iomd_done_fn)(struct zus_iomap_done *iomd, int err);
struct zus_iomap_done {
	iomd_done_fn done;
};

struct zus_iomap_build;
typedef void (*iomb_submit_fn)(struct zus_iomap_build *iomb, bool sync);
struct zus_iomap_build {
	iomb_submit_fn		submit;
	struct zus_iomap_done	*iomd;
	struct zus_sb_info	*sbi;		/* needed if for ioc_exec */
	int			fd;
	int			err;

	void 			*cur_iom_e;
	void 			*end_iom_e;

	struct zufs_iomap	*ziom;
	union {
		struct zufs_ioc_iomap_exec *ioc_exec;
		struct zufs_ioc_IO *ioc_io;
	};
};

void _zus_iom_ioc_exec_submit(struct zus_iomap_build *iomb, bool sync);

static inline ulong _zus_iom_len(struct zus_iomap_build *iomb)
{
	return (__u64 *)iomb->cur_iom_e - iomb->ziom->iom_e;
}

static inline bool _zus_iom_empty(struct zus_iomap_build *iomb)
{
	return !_zus_iom_len(iomb);
}

static inline void _zus_iom_enc_type_val(__u64 *ptr, enum ZUFS_IOM_TYPE type,
					 ulong val)
{
	ZUS_WARN_ON(val & ~ZUFS_IOM_FIRST_VAL_MASK);
	_zufs_iom_enc_type_val(ptr, type, val);
}

/* iomb comes ZEROed! */
static inline void _zus_iom_common_init(struct zus_iomap_build *iomb,
					struct zus_sb_info *sbi,
					struct zufs_iomap *ziom, void *end_ptr)
{
	memset(ziom, 0, sizeof(*ziom));
	ziom->iom_max = (end_ptr - (void *)ziom->iom_e) / sizeof(__u64);

	iomb->sbi = sbi;
	iomb->ziom = ziom;
	iomb->end_iom_e = end_ptr;
}

static inline void _zus_iom_init_4_ioc_exec(struct zus_iomap_build *iomb,
					struct zus_sb_info *sbi, int fd,
					struct zufs_ioc_iomap_exec *ioc_exec,
					uint max_bytes)
{
	_zus_iom_common_init(iomb, sbi, &ioc_exec->ziom,
			     (void *)ioc_exec + max_bytes);
	iomb->fd = fd;
	iomb->submit = _zus_iom_ioc_exec_submit;
	iomb->ioc_exec = ioc_exec;
}

static inline void _zus_iom_init_4_ioc_io(struct zus_iomap_build *iomb,
					    struct zus_sb_info *sbi,
					    struct zufs_ioc_IO *ioc_io,
					    uint max_bytes)
{
	_zus_iom_common_init(iomb, sbi, &ioc_io->ziom,
			     (void *)ioc_io + max_bytes);
	iomb->ioc_io = ioc_io;
}

static inline void _zus_iom_start(struct zus_iomap_build *iomb,
				  struct zus_iomap_done *iomd)
{
	iomb->cur_iom_e = iomb->ziom->iom_e;
	iomb->ziom->iom_n = 0;
	iomb->ziom->iom_e[0] = 0;
	iomb->iomd = iomd;
}
static inline void _zus_iom_end(struct zus_iomap_build *iomb)
{
	/* NULL terminated list */
	if (iomb->cur_iom_e < iomb->end_iom_e)
		_zus_iom_enc_type_val(iomb->cur_iom_e, 0, 0);

	if (iomb->ziom)
		iomb->ziom->iom_n = _zus_iom_len(iomb);
}

static inline int _zus_iom_enc_wbinv(struct zus_iomap_build *iomb)
{
	__u64 *iom_e = iomb->cur_iom_e;

	if (unlikely(iomb->end_iom_e < (void*)(iom_e + 1)))
		return -ENOSPC;

	iomb->cur_iom_e = iom_e + 1;
	_zus_iom_enc_type_val(iom_e, IOM_WBINV, 0);
	return 0;
}

static inline int _zus_iom_enc_discard(struct zus_iomap_build *iomb,
				       __u64 t2_bn, __u64 num_pages)
{
	struct zufs_iom_t2_io_len *iom_io_range = iomb->cur_iom_e;
	void *next_iom_e = iom_io_range + 1;

	if (unlikely(iomb->end_iom_e < next_iom_e))
		return -ENOSPC;

	_zus_iom_enc_type_val((__u64*)iom_io_range, IOM_DISCARD, t2_bn);
	iom_io_range->num_pages = num_pages;

	iomb->cur_iom_e = next_iom_e;

	return 0;
}

static inline int _zus_iom_enc_unmap(struct zus_iomap_build *iomb, ulong index,
				     ulong n, ulong ino)
{
	struct zufs_iom_unmap *iom_unmap = iomb->cur_iom_e;
	void *next_iom_e = iom_unmap + 1;

	if (unlikely(iomb->end_iom_e < next_iom_e))
		return -ENOSPC;

	_zus_iom_enc_type_val(&iom_unmap->unmap_index, IOM_UNMAP, index);
	iom_unmap->unmap_n = n;
	iom_unmap->ino = ino;
	iomb->cur_iom_e = next_iom_e;
	return 0;
}

static inline int _zus_iom_enc_t2_io(struct zus_iomap_build *iomb, ulong t2_bn,
				     zu_dpp_t t1_val, enum ZUFS_IOM_TYPE type)
{
	struct zufs_iom_t2_io *iom_io = iomb->cur_iom_e;
	void *next_iom_e = iom_io + 1;

	if (unlikely(iomb->end_iom_e < next_iom_e))
		return -ENOSPC;

	_zus_iom_enc_type_val(&iom_io->t2_val, type, t2_bn);
	iom_io->t1_val = t1_val;

	iomb->cur_iom_e = next_iom_e;
	return 0;
}

static inline int _zus_iom_enc_t2_write(struct zus_iomap_build *iomb, ulong t2_bn,
				     zu_dpp_t t1_val)
{
	return _zus_iom_enc_t2_io(iomb, t2_bn, t1_val, IOM_T2_WRITE);
}

static inline int _zus_iom_enc_t2_read(struct zus_iomap_build *iomb, ulong t2_bn,
				     zu_dpp_t t1_val)
{
	return _zus_iom_enc_t2_io(iomb, t2_bn, t1_val, IOM_T2_READ);
}

static inline int _zus_iom_enc_t2_zusmem_io(struct zus_iomap_build *iomb,
					    ulong t2_bn, void *ptr, ulong len,
					    enum ZUFS_IOM_TYPE type)
{
	struct zufs_iom_t2_zusmem_io *iom_io = iomb->cur_iom_e;
	void *next_iom_e = iom_io + 1;

	if (unlikely(iomb->end_iom_e < next_iom_e))
		return -ENOSPC;

	_zus_iom_enc_type_val(&iom_io->t2_val, type, t2_bn);
	iom_io->zus_mem_ptr = (__u64)ptr;
	iom_io->len = len;

	iomb->cur_iom_e = next_iom_e;
	return 0;
}

static inline int _zus_iom_enc_t2_zusmem_write(struct zus_iomap_build *iomb,
						ulong t2_bn, void *ptr,
						ulong len)
{
	return _zus_iom_enc_t2_zusmem_io(iomb, t2_bn, ptr, len,
					 IOM_T2_ZUSMEM_WRITE);
}

static inline int _zus_iom_enc_t2_zusmem_read(struct zus_iomap_build *iomb,
						ulong t2_bn, void *ptr,
						ulong len)
{
	return _zus_iom_enc_t2_zusmem_io(iomb, t2_bn, ptr, len,
					 IOM_T2_ZUSMEM_READ);
}

static inline bool _ziom_enc_t1_bn(struct zus_iomap_build *iomb, ulong bn,
				     uint pool)
{
	__u64 *iom_e = iomb->cur_iom_e;

	iomb->cur_iom_e = iom_e + 1;
	_zufs_iom_enc_bn(iom_e, bn, pool);
	++iomb->ziom->iom_n;

	/* Special no need to call _zus_iom_end, Returns true if more space */
	return  iomb->cur_iom_e < iomb->end_iom_e;
}
#endif /* define __ZUS_IOM_H__ */
