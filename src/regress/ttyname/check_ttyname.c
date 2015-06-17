/*
 * Copyright (c) 2013-2014 Todd C. Miller <Todd.Miller@courtesan.com>
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
#endif /* HAVE_STRING_H */
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif /* HAVE_STRINGS_H */
#include <unistd.h>
#include <errno.h>

#include "sudo_compat.h"
#include "sudo_fatal.h"
#include "sudo_util.h"
#include "sudo_debug.h"

__dso_public int main(int argc, char *argv[]);

int sudo_debug_instance = SUDO_DEBUG_INSTANCE_INITIALIZER;
extern char *get_process_ttyname(void);

int
main(int argc, char *argv[])
{
    char *tty_libc = NULL, *tty_sudo;
    int rval = 1;

    initprogname(argc > 0 ? argv[0] : "check_ttyname");

    /* Lookup tty name using kernel info if possible. */
    tty_sudo = get_process_ttyname();

#if defined(HAVE_STRUCT_KINFO_PROC2_P_TDEV) || \
    defined(HAVE_STRUCT_KINFO_PROC_P_TDEV) || \
    defined(HAVE_STRUCT_KINFO_PROC_KI_TDEV) || \
    defined(HAVE_STRUCT_KINFO_PROC_KP_EPROC_E_TDEV) || \
    defined(HAVE__TTYNAME_DEV) || defined(HAVE_STRUCT_PSINFO_PR_TTYDEV) || \
    defined(HAVE_PSTAT_GETPROC) || defined(__linux__)

    /* Lookup tty name attached to stdin via libc. */
    tty_libc = ttyname(STDIN_FILENO);
#endif

    /* Compare libc and kernel ttys. */
    if (tty_libc != NULL && tty_sudo != NULL) {
	if (strcmp(tty_libc, tty_sudo) == 0)
	    rval = 0;
    } else if (tty_libc == NULL && tty_sudo == NULL) {
	rval = 0;
    }

    if (rval == 0) {
	printf("%s: OK (%s)\n", getprogname(), tty_sudo ? tty_sudo : "none");
    } else if (tty_libc == NULL) {
	printf("%s: SKIP (%s)\n", getprogname(), tty_sudo ? tty_sudo : "none");
	rval = 0;
    } else {
	printf("%s: FAIL %s (sudo) vs. %s (libc)\n", getprogname(),
	    tty_sudo ? tty_sudo : "none", tty_libc ? tty_libc : "none");
    }

    free(tty_sudo);
    exit(rval);
}
