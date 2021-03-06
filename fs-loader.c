/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * zus-vfs.c - Abstract FS interface that calls into the um-FS
 *
 * Copyright (c) 2018 NetApp, Inc. All rights reserved.
 *
 * See module.c for LICENSE details.
 *
 * Authors:
 *	Yigal Korman <yigalk@netapp.com>
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>

#include "zus.h"
#include "zuf_call.h"

/* ~~~ called by FS code to add an FS-type ~~~ */
int zus_register_one(int fd, struct zus_fs_info *zfi)
{
	int err;

	err = zuf_register_fs(fd, zfi);
	if (err)
		return err;

	return 0;
}

/* ~~~ dynamic loading of FS plugins ~~~ */
static void *g_dl_list[ZUS_LIBFS_MAX_NR] = {};

static int _try_load_from(void **handle, const char namefmt[], ...)
{
	char libfs_path[ZUS_LIBFS_MAX_PATH];
	void *dl_lib;
	va_list args;
	int err;

	va_start (args, namefmt);
	err = vsnprintf(libfs_path, sizeof(libfs_path), namefmt, args);
	va_end (args);

	if (err < 0) {
		ERROR("Path reconstruction too long [%s]\n", namefmt);
		return -EINVAL;
	}

	dl_lib = dlopen(libfs_path, RTLD_NOW);
	DBG("dlopen(%s) = %p, dlerror=%s\n", libfs_path, dl_lib, dlerror());
	if (!dl_lib)
		return -ENOENT;

	*handle = dl_lib;
	return 0;
}

static int _load_one_fs(int fd, const char *fs_name, void **handle)
{
	void *dl_lib;
	int (*register_fn)(int fd);
	char *dl_err;
	int err;

	DBG("p=%s\n", fs_name);
	/* try to load production path */
	err = _try_load_from(&dl_lib, "%s/lib%s.so", ZUS_LIBFS_DIR, fs_name);
	if (!err)
		goto found;
	if (err != -ENOENT)
		return err;

	/*  try to load from current dir or LD_LIBRARY_PATH */
	err = _try_load_from(&dl_lib, "lib%s.so", fs_name);
	if (!err)
		goto found;
	if (err != -ENOENT)
		return err;

	/*  try to load from full path name */
	err = _try_load_from(&dl_lib, "%s", fs_name);
	if (!err)
		goto found;
	if (err)
		return err;

found:
	/* clear existing errors (in the case DBG not compiled) */
	dlerror();

	register_fn = dlsym(dl_lib, REGISTER_FS_NAME);
	dl_err = dlerror();
	if (dl_err) {
		ERROR("register_fs retrieval failed => %s\n", dl_err);
		dlclose(dl_lib);
		return -EBADF;
	}

	err = register_fn(fd);
	if (err) {
		ERROR("%s::register_fs failed => %d\n", fs_name, err);
		dlclose(dl_lib);
		return err;
	}
	err = zus_add_module_ddbg(fs_name, dl_lib);
	if (err)
		ERROR("%s:: dynamic debug load failed=> %d\n", fs_name, err);

	*handle = dl_lib;
	return 0;
}

static int _load_libfs(int fd)
{
	const char *libfs_env = getenv(ZUFS_LIBFS_LIST);
	char *orig_libfs_str, *libfs_str, *p;
	int lib_no = 0;
	int err = 0;

	DBG("%s: %s\n", ZUFS_LIBFS_LIST, libfs_env);
	if (!libfs_env || !*libfs_env)
		return 0;

	libfs_str = orig_libfs_str = strdup(libfs_env);
	if (!orig_libfs_str)
		return -ENOMEM;

	while ((p = strsep(&libfs_str, ",")) != NULL) {
		if (!*p)
			continue;
		err = _load_one_fs(fd, p, &g_dl_list[lib_no]);
		if (unlikely(err))
			break;
		++lib_no;
	}
	free(orig_libfs_str);

	return err;
}

static void _unload_libfs(void *handle)
{
	int err;

	if (handle) {
		err = dlclose(handle);
		if (err)
			ERROR("dlclose failed => %d\n", err);
	}
}

/* ~~~ called by zus thread ~~~ */
int zus_register_all(int fd)
{
	int err;

	err = _load_libfs(fd);
	if (err) {
		ERROR("failed to load dynamic libfs modules => %d\n", err);
		return err;
	}

	return 0;
}

void zus_unregister_all(void)
{
	int i;

	zus_free_ddbg_db();
	for (i = 0; i < ZUS_LIBFS_MAX_NR; ++i) {
		if (g_dl_list[i])
			_unload_libfs(g_dl_list[i]);
	}
}
