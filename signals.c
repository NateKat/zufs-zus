/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * signals.c -- Zusd signal handling
 *
 * Copyright (c) 2018 NetApp, Inc. All rights reserved.
 *
 * See module.c for LICENSE details.
 *
 * Authors:
 *	Shachar Sharon <sshachar@netapp.com>
 */
#define _GNU_SOURCE

#include <unistd.h>
#include <signal.h>

#include "zus.h"
#include "zusd.h"


static void _sigaction_info_handler(int signum, siginfo_t *si, void *p)
{
	INFO("SIGNAL: signum=%d si_errno=%d\n", signum, si->si_errno);
}

static void _sigaction_exit_handler(int signum, siginfo_t *si, void *p)
{
	_sigaction_info_handler(signum, si, p);
	zus_mount_thread_stop();
	exit(1);
}

static void _sigaction_abort_handler(int signum, siginfo_t *si, void *p)
{
	ZUS_BUG_ON(1);
	_sigaction_info_handler(signum, si, p);
	abort();
}

static void _sigaction_info(int signum)
{
	static struct sigaction sa_info = {
		.sa_sigaction   = _sigaction_info_handler,
		.sa_flags       = SA_SIGINFO
	};

	sigaction(signum, &sa_info, NULL);
}

static void _sigaction_exit(int signum)
{
	static struct sigaction sa_exit = {
		.sa_sigaction   = _sigaction_exit_handler,
		.sa_flags       = SA_SIGINFO
	};

	sigaction(signum, &sa_exit, NULL);
}

static void _sigaction_abort(int signum)
{
	static struct sigaction sa_abort = {
		.sa_sigaction   = _sigaction_abort_handler,
		.sa_flags       = SA_SIGINFO
	};

	sigaction(signum, &sa_abort, NULL);
}

void zus_register_sigactions(void)
{
	/*
	 * IMPORTANT: do not catch SIGABRT -- let abort work as expected from
	 * within _sigaction_abort_handler
	 */
	_sigaction_info(SIGHUP);
	_sigaction_exit(SIGINT);
	_sigaction_exit(SIGQUIT);
	_sigaction_abort(SIGILL);
	_sigaction_info(SIGTRAP);
	_sigaction_abort(SIGBUS);
	_sigaction_abort(SIGFPE);
	_sigaction_abort(SIGKILL);
	_sigaction_exit(SIGUSR1);
	_sigaction_abort(SIGSEGV);
	_sigaction_info(SIGUSR2);
	_sigaction_info(SIGPIPE);
	_sigaction_info(SIGALRM);
	_sigaction_exit(SIGTERM);
	_sigaction_abort(SIGSTKFLT);
	_sigaction_info(SIGCHLD); /* TODO: Maybe exit? */
	_sigaction_info(SIGCONT);
	_sigaction_exit(SIGSTOP);
	_sigaction_exit(SIGTSTP);
	_sigaction_exit(SIGTTIN);
	_sigaction_exit(SIGTTOU);
	_sigaction_info(SIGURG);
	_sigaction_exit(SIGXCPU);
	_sigaction_exit(SIGXFSZ);
	_sigaction_exit(SIGVTALRM);
	_sigaction_info(SIGPROF);
	_sigaction_info(SIGWINCH);
	_sigaction_info(SIGIO);
	_sigaction_exit(SIGPWR);
	_sigaction_exit(SIGSYS);
}