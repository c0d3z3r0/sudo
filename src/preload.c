/*
 * Copyright (c) 2010, 2011 Todd C. Miller <Todd.Miller@courtesan.com>
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

#if HAVE_GSS_KRB5_CCACHE_NAME
# if defined(HAVE_GSSAPI_GSSAPI_KRB5_H)
#  include <gssapi/gssapi.h>
#  include <gssapi/gssapi_krb5.h>
# elif defined(HAVE_GSSAPI_GSSAPI_H)
#  include <gssapi/gssapi.h>
# else
#  include <gssapi.h>
# endif
#endif

#include "sudo_plugin.h"

extern struct policy_plugin sudoers_policy;
extern struct io_plugin sudoers_io;

struct sudo_preload_table {
    const char *name;
    void *address;
} sudo_preload_table[] = {
    { "sudoers_policy", (void *) &sudoers_policy},
    { "sudoers_io", (void *) &sudoers_io},
#ifdef HAVE_GSS_KRB5_CCACHE_NAME
    { "gss_krb5_ccache_name", (void *) &gss_krb5_ccache_name},
#endif
    { (const char *)0, (void *)0 }
};
