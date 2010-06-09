/*
 * Copyright (c) 1996, 1998-2005, 2007-2009
 *	Todd C. Miller <Todd.Miller@courtesan.com>
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
 *
 * Sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F39502-99-1-0512.
 */

#ifdef __TANDEM
# include <floss.h>
#endif

#include <config.h>

#include <sys/types.h>
#include <sys/param.h>
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
#include <pwd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>

#include "sudo.h"

#if !defined(NSIG)
# if defined(_NSIG)
#  define NSIG _NSIG
# elif defined(__NSIG)
#  define NSIG __NSIG
# else
#  define NSIG 64
# endif
#endif

static volatile sig_atomic_t signo[NSIG];

static void handler(int);
static char *getln(int, char *, size_t, int);
static char *sudo_askpass(const char *, const char *);

extern struct user_details user_details; /* XXX */

/*
 * Like getpass(3) but with timeout and echo flags.
 */
char *
tgetpass(const char *prompt, int timeout, int flags)
{
    sigaction_t sa, savealrm, saveint, savehup, savequit, saveterm;
    sigaction_t savetstp, savettin, savettou, savepipe;
    char *pass;
    static char *askpass;
    static char buf[SUDO_PASS_MAX + 1];
    int i, input, output, save_errno, neednl = 0, need_restart;

    (void) fflush(stdout);

    /* If using a helper program to get the password, run it instead. */
    /* XXX - askpass may be set by policy */
    if (ISSET(flags, TGP_ASKPASS)) {
	if (!askpass) {
	    askpass = getenv("SUDO_ASKPASS");
#ifdef _PATH_SUDO_ASKPASS
	    if (!askpass)
		askpass = _PATH_SUDO_ASKPASS;
#endif
	}
	if (askpass && *askpass)
	    return(sudo_askpass(askpass, prompt));
    }

restart:
    for (i = 0; i < NSIG; i++)
	signo[i] = 0;
    pass = NULL;
    save_errno = 0;
    need_restart = 0;
    /* Open /dev/tty for reading/writing if possible else use stdin/stderr. */
    if (ISSET(flags, TGP_STDIN) ||
	(input = output = open(_PATH_TTY, O_RDWR|O_NOCTTY)) == -1) {
	input = STDIN_FILENO;
	output = STDERR_FILENO;
    }

    /*
     * If we are using a tty but are not the foreground pgrp this will
     * generate SIGTTOU, so do it *before* installing the signal handlers.
     */
    if (!ISSET(flags, TGP_ECHO)) {
	if (ISSET(flags, TGP_MASK))
	    neednl = term_cbreak(input);
	else
	    neednl = term_noecho(input);
    }

    /*
     * Catch signals that would otherwise cause the user to end
     * up with echo turned off in the shell.
     */
    zero_bytes(&sa, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_INTERRUPT;	/* don't restart system calls */
    sa.sa_handler = handler;
    (void) sigaction(SIGALRM, &sa, &savealrm);
    (void) sigaction(SIGINT, &sa, &saveint);
    (void) sigaction(SIGHUP, &sa, &savehup);
    (void) sigaction(SIGQUIT, &sa, &savequit);
    (void) sigaction(SIGTERM, &sa, &saveterm);
    (void) sigaction(SIGTSTP, &sa, &savetstp);
    (void) sigaction(SIGTTIN, &sa, &savettin);
    (void) sigaction(SIGTTOU, &sa, &savettou);

    /* Ignore SIGPIPE in case stdin is a pipe and TGP_STDIN is set */
    sa.sa_handler = SIG_IGN;
    (void) sigaction(SIGPIPE, &sa, &savepipe);

    if (prompt)
	(void) write(output, prompt, strlen(prompt));

    if (timeout > 0)
	alarm(timeout);
    pass = getln(input, buf, sizeof(buf), ISSET(flags, TGP_MASK));
    alarm(0);
    save_errno = errno;

    if (neednl || pass == NULL)
	(void) write(output, "\n", 1);

    /* Restore old tty settings and signals. */
    if (!ISSET(flags, TGP_ECHO))
	term_restore(input, 1);
    (void) sigaction(SIGALRM, &savealrm, NULL);
    (void) sigaction(SIGINT, &saveint, NULL);
    (void) sigaction(SIGHUP, &savehup, NULL);
    (void) sigaction(SIGQUIT, &savequit, NULL);
    (void) sigaction(SIGTERM, &saveterm, NULL);
    (void) sigaction(SIGTSTP, &savetstp, NULL);
    (void) sigaction(SIGTTIN, &savettin, NULL);
    (void) sigaction(SIGTTOU, &savettou, NULL);
    (void) sigaction(SIGTTOU, &savepipe, NULL);
    if (input != STDIN_FILENO)
	(void) close(input);

    /*
     * If we were interrupted by a signal, resend it to ourselves
     * now that we have restored the signal handlers.
     */
    for (i = 0; i < NSIG; i++) {
	if (signo[i]) {
	    kill(getpid(), i);
	    switch (i) {
		case SIGTSTP:
		case SIGTTIN:
		case SIGTTOU:
		    need_restart = 1;
		    break;
	    }
	}
    }
    if (need_restart)
	goto restart;

    if (save_errno)
	errno = save_errno;
    return(pass);
}

/*
 * Fork a child and exec sudo-askpass to get the password from the user.
 */
static char *
sudo_askpass(const char *askpass, const char *prompt)
{
    static char buf[SUDO_PASS_MAX + 1], *pass;
    sigaction_t sa, saved_sa_pipe;
    int pfd[2];
    pid_t pid;

    if (pipe(pfd) == -1)
	error(1, "unable to create pipe");

    if ((pid = fork()) == -1)
	error(1, "unable to fork");

    if (pid == 0) {
	/* child, point stdout to output side of the pipe and exec askpass */
	(void) dup2(pfd[1], STDOUT_FILENO);
	(void) setuid(ROOT_UID);
	if (setgid(user_details.gid)) {
	    warning("unable to set gid to %u", (unsigned int)user_details.gid);
	    _exit(255);
	}
	if (setuid(user_details.uid)) {
	    warning("unable to set uid to %u", (unsigned int)user_details.uid);
	    _exit(255);
	}
	closefrom(STDERR_FILENO + 1);
	execl(askpass, askpass, prompt, (char *)NULL);
	warning("unable to run %s", askpass);
	_exit(255);
    }

    /* Ignore SIGPIPE in case child exits prematurely */
    zero_bytes(&sa, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_IGN;
    (void) sigaction(SIGPIPE, &sa, &saved_sa_pipe);

    /* Get response from child (askpass) and restore SIGPIPE handler */
    (void) close(pfd[1]);
    pass = getln(pfd[0], buf, sizeof(buf), 0);
    (void) close(pfd[0]);
    (void) sigaction(SIGPIPE, &saved_sa_pipe, NULL);

    return(pass);
}

extern int term_erase, term_kill;

static char *
getln(int fd, char *buf, size_t bufsiz, int feedback)
{
    size_t left = bufsiz;
    ssize_t nr = -1;
    char *cp = buf;
    char c = '\0';

    if (left == 0) {
	errno = EINVAL;
	return(NULL);			/* sanity */
    }

    while (--left) {
	nr = read(fd, &c, 1);
	if (nr != 1 || c == '\n' || c == '\r')
	    break;
	if (feedback) {
	    if (c == term_kill) {
		while (cp > buf) {
		    (void) write(fd, "\b \b", 3);
		    --cp;
		}
		left = bufsiz;
		continue;
	    } else if (c == term_erase) {
		if (cp > buf) {
		    (void) write(fd, "\b \b", 3);
		    --cp;
		    left++;
		}
		continue;
	    }
	    (void) write(fd, "*", 1);
	}
	*cp++ = c;
    }
    *cp = '\0';
    if (feedback) {
	/* erase stars */
	while (cp > buf) {
	    (void) write(fd, "\b \b", 3);
	    --cp;
	}
    }

    return(nr == 1 ? buf : NULL);
}

static void
handler(int s)
{
    if (s != SIGALRM)
	signo[s] = 1;
}

int
tty_present(void)
{
    int fd;

    if ((fd = open(_PATH_TTY, O_RDWR|O_NOCTTY)) != -1)
	close(fd);
    return(fd != -1);
}
