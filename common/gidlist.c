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
#include <grp.h>

#define DEFAULT_TEXT_DOMAIN	"sudo"
#include "gettext.h"		/* must be included before missing.h */

#include "missing.h"
#include "alloc.h"
#include "fatal.h"
#include "sudo_debug.h"
#include "sudo_util.h"

/*
 * Parse a comma-separated list of gids into an allocated array of GETGROUPS_T.
 * If a pointer to the base gid is specified, it is stored as the first element
 * in the array.
 * Returns the number of gids in the allocated array.
 */
int
parse_gid_list(const char *gidstr, const gid_t *basegid, GETGROUPS_T **gidsp)
{
    int ngids = 0;
    GETGROUPS_T *gids;
    const char *cp = gidstr;
    const char *errstr;
    char *ep;
    debug_decl(atoid, SUDO_DEBUG_UTIL)

    /* Count groups. */
    if (*cp != '\0') {
	ngids++;
	do {
	    if (*cp++ == ',')
		ngids++;
	} while (*cp != '\0');
    }
    /* Base gid is optional. */
    if (basegid != NULL)
	ngids++;
    /* Allocate and fill in array. */
    if (ngids != 0) {
	gids = emalloc2(ngids, sizeof(GETGROUPS_T));
	ngids = 0;
	if (basegid != NULL)
	    gids[ngids++] = *basegid;
	cp = gidstr;
	do {
	    gids[ngids] = (GETGROUPS_T) atoid(cp, ",", &ep, &errstr);
	    if (errstr != NULL) {
		warningx(U_("%s: %s"), cp, U_(errstr));
		free(gids);
		debug_return_int(-1);
	    }
	    if (basegid == NULL || gids[ngids] != *basegid)
		ngids++;
	    cp = ep + 1;
	} while (*ep != '\0');
	*gidsp = gids;
    }
    debug_return_int(ngids);
}
