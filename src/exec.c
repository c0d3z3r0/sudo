/*
 * Copyright (c) 2009-2010 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <config.h>

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#ifdef HAVE_TERMIOS_H
# include <termios.h>
#else
# include <termio.h>
#endif /* HAVE_TERMIOS_H */
#include <sys/ioctl.h>
#ifdef HAVE_SYS_SELECT_H
# include <sys/select.h>
#endif /* HAVE_SYS_SELECT_H */
#include <stdio.h>
#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif /* STDC_HEADERS */
#ifdef HAVE_STRING_H
# if defined(HAVE_MEMORY_H) && !defined(STDC_HEADERS)
#  include <memory.h>
# endif
# include <string.h>
#else
# ifdef HAVE_STRINGS_H
#  include <strings.h>
# endif
#endif /* HAVE_STRING_H */
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif /* HAVE_UNISTD_H */
#if TIME_WITH_SYS_TIME
# include <time.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#ifdef HAVE_SELINUX
# include <selinux/selinux.h>
#endif

/* XXX - move to compat */
#if !defined(NSIG)
# if defined(_NSIG)
#  define NSIG _NSIG
# elif defined(__NSIG)
#  define NSIG __NSIG
# else
#  define NSIG 64
# endif
#endif

#include "sudo.h" /* XXX? */
#include "sudo_plugin.h"
#include "sudo_plugin_int.h"

extern struct user_details user_details; /* XXX need tty name for SELinux */

/* shared with exec_pty.c */
sig_atomic_t recvsig[NSIG];
void handler(int s);

/*
 * Like execve(2) but falls back to running through /bin/sh
 * ala execvp(3) if we get ENOEXEC.
 */
int
my_execve(const char *path, char *const argv[], char *const envp[])
{
    execve(path, argv, envp);
    if (errno == ENOEXEC) {
	int argc;
	char **nargv;

	for (argc = 0; argv[argc] != NULL; argc++)
	    continue;
	nargv = emalloc2(argc + 2, sizeof(char *));
	nargv[0] = "sh";
	nargv[1] = (char *)path;
	memcpy(nargv + 2, argv + 1, argc * sizeof(char *));
	execve(_PATH_BSHELL, nargv, envp);
	efree(nargv);
    }
    return -1;
}

/*
 * Fork and execute a command, returns the child's pid.
 * Sends errno back on sv[1] if execve() fails.
 */
static int fork_cmnd(struct command_details *details, char *argv[],
    char *envp[], int sv[2], int rbac_enabled)
{
    struct command_status cstat;
    int pid;

    pid = fork();
    switch (pid) {
    case -1:
	error(1, "fork");
	break;
    case 0:
	/* child */
	close(sv[0]);
	fcntl(sv[1], F_SETFD, FD_CLOEXEC);
#ifdef HAVE_SELINUX
	if (rbac_enabled) {
	    selinux_setup(details->selinux_role, details->selinux_type,
		user_details.tty, -1);
	}
#endif
	if (exec_setup(details) == TRUE) {
	    /* headed for execve() */
	    if (details->closefrom >= 0)
		closefrom(details->closefrom);
#ifdef HAVE_SELINUX
	    if (rbac_enabled)
		selinux_execve(details->command, argv, envp);
	    else
#endif
		my_execve(details->command, argv, envp);
	}
	cstat.type = CMD_ERRNO;
	cstat.val = errno;
	send(sv[1], &cstat, sizeof(cstat), 0);
	_exit(1);
    }
    return pid;
}

/*
 * Execute a command, potentially in a pty with I/O loggging.
 * This is a little bit tricky due to how POSIX job control works and
 * we fact that we have two different controlling terminals to deal with.
 */
int
sudo_execve(struct command_details *details, char *argv[], char *envp[],
    struct command_status *cstat)
{
    sigaction_t sa;
    fd_set *fdsr, *fdsw;
    int maxfd, n, nready, status, sv[2];
    int rbac_enabled = 0;
    int log_io = 0;
    pid_t child;

    cstat->type = CMD_INVALID;

    log_io = !tq_empty(&io_plugins);
    if (log_io) {
	sudo_debug(8, "allocate pty for I/O logging");
	pty_setup(details->euid);
    }

#ifdef HAVE_SELINUX
    rbac_enabled = is_selinux_enabled() > 0 && details->selinux_role != NULL;
#endif

    /*
     * We communicate with the child over a bi-directional pair of sockets.
     * Parent sends signal info to child and child sends back wait status.
     */
    if (socketpair(PF_UNIX, SOCK_DGRAM, 0, sv) != 0)
	error(1, "cannot create sockets");

    zero_bytes(&sa, sizeof(sa));
    sigemptyset(&sa.sa_mask);

    /* Note: HP-UX select() will not be interrupted if SA_RESTART set */
    sa.sa_flags = 0; /* do not restart syscalls */
    sa.sa_handler = handler;
    sigaction(SIGALRM, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Max fd we will be selecting on. */
    maxfd = sv[0];

    /*
     * Child will run the command in the pty, parent will pass data
     * to and from pty.  Adjusts maxfd as needed.
     */
    if (log_io)
	child = fork_pty(details, argv, envp, sv, rbac_enabled, &maxfd);
    else
	child = fork_cmnd(details, argv, envp, sv, rbac_enabled);
    close(sv[1]);

    /* Set command timeout if specified. */
    if (ISSET(details->flags, CD_SET_TIMEOUT))
	alarm(details->timeout);

    /*
     * In the event loop we pass input from user tty to master
     * and pass output from master to stdout and IO plugin.
     */
    fdsr = (fd_set *)emalloc2(howmany(maxfd + 1, NFDBITS), sizeof(fd_mask));
    fdsw = (fd_set *)emalloc2(howmany(maxfd + 1, NFDBITS), sizeof(fd_mask));
    for (;;) {
	if (recvsig[SIGCHLD]) {
	    pid_t pid;

	    /*
	     * If logging I/O, child is the intermediate process,
	     * otherwise it is the command itself.
	     */
	    recvsig[SIGCHLD] = FALSE;
	    do {
		pid = waitpid(child, &status, WUNTRACED|WNOHANG);
	    } while (pid == -1 && errno == EINTR);
	    if (pid == child) {
		/* If not logging I/O and child has exited we are done. */
		if (!log_io) {
		    if (WIFSTOPPED(status)) {
			/* Child may not have privs to suspend us itself. */
			kill(getpid(), WSTOPSIG(status));
		    } else {
			/* Child has exited, we are done. */
			cstat->type = CMD_WSTATUS;
			cstat->val = status;
			return 0;
		    }
		}
		/* Else we get ECONNRESET on sv[0] if child dies. */
	    }
	}

	zero_bytes(fdsw, howmany(maxfd + 1, NFDBITS) * sizeof(fd_mask));
	zero_bytes(fdsr, howmany(maxfd + 1, NFDBITS) * sizeof(fd_mask));

	FD_SET(sv[0], fdsr);
	if (log_io)
	    fd_set_iobs(fdsr, fdsw); /* XXX - better name */
	for (n = 0; n < NSIG; n++) {
	    if (recvsig[n] && n != SIGCHLD) {
		if (log_io) {
		    FD_SET(sv[0], fdsw);
		    break;
		} else {
		    /* nothing listening on sv[0], send directly */
		    if (n == SIGALRM) {
			terminate_child(child, FALSE);
		    } else {
			kill(child, n);
		    }
		}
	    }
	}

	if (recvsig[SIGCHLD])
	    continue;
	nready = select(maxfd + 1, fdsr, fdsw, NULL, NULL);
	if (nready == -1) {
	    if (errno == EINTR)
		continue;
	    error(1, "select failed");
	}
	if (FD_ISSET(sv[0], fdsr)) {
	    /* read child status */
	    n = recv(sv[0], cstat, sizeof(*cstat), 0);
	    if (n == -1) {
		if (errno == EINTR)
		    continue;
		/*
		 * If not logging I/O we will receive ECONNRESET when
		 * the command is executed.  It is safe to ignore this.
		 */
		if (log_io && errno != EAGAIN) {
		    cstat->type = CMD_ERRNO;
		    cstat->val = errno;
		    break;
		}
	    }
	    if (cstat->type == CMD_WSTATUS) {
		if (WIFSTOPPED(cstat->val)) {
		    /* Suspend parent and tell child how to resume on return. */
		    sudo_debug(8, "child stopped, suspending parent");
		    n = suspend_parent(WSTOPSIG(cstat->val));
		    recvsig[n] = TRUE;
		    continue;
		} else {
		    /* Child exited or was killed, either way we are done. */
		    break;
		}
	    } else if (cstat->type == CMD_ERRNO) {
		/* Child was unable to execute command or broken pipe. */
		break;
	    }
	}

	if (FD_ISSET(sv[0], fdsw)) {
	    for (n = 0; n < NSIG; n++) {
		if (!recvsig[n])
		    continue;
		recvsig[n] = FALSE;
		sudo_debug(9, "sending signal %d to child over backchannel", n);
		cstat->type = CMD_SIGNO;
		cstat->val = n;
		do {
		    n = send(sv[0], cstat, sizeof(*cstat), 0);
		} while (n == -1 && errno == EINTR);
		if (n != sizeof(*cstat)) {
		    recvsig[n] = TRUE;
		    break;
		}
	    }
	}
	if (perform_io(fdsr, fdsw, cstat) != 0)
	    break;
    }

    if (log_io) {
	/* Flush any remaining output and free pty-related memory. */
	pty_close(cstat);
   }

#ifdef HAVE_SELINUX
    if (rbac_enabled) {
	/* This is probably not needed in log_io mode. */
	if (selinux_restore_tty() != 0)
	    warningx("unable to restore tty label");
    }
#endif

    efree(fdsr);
    efree(fdsw);

    return cstat->type == CMD_ERRNO ? -1 : 0;
}

/*
 * Generic handler for signals passed from parent -> child.
 * The recvsig[] array is checked in the main event loop.
 */
void
handler(int s)
{
    recvsig[s] = TRUE;
}
