/*
 * Copyright (c) 2007-2011, 2013-2015 Todd C. Miller <Todd.Miller@sudo.ws>
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

#ifndef SUDOERS_NSS_H
#define SUDOERS_NSS_H

struct sudo_lbuf;
struct passwd;

struct sudo_nss {
    TAILQ_ENTRY(sudo_nss) entries;
    int (*open)(struct sudo_nss *nss);
    int (*close)(struct sudo_nss *nss);
    int (*parse)(struct sudo_nss *nss);
    int (*query)(struct sudo_nss *nss, struct passwd *pw);
    int (*getdefs)(struct sudo_nss *nss);
    void *handle;
    bool ret_if_found;
    bool ret_if_notfound;
    struct defaults_list defaults;
    struct userspec_list userspecs;
};

TAILQ_HEAD(sudo_nss_list, sudo_nss);

struct sudo_nss_list *sudo_read_nss(void);
bool sudo_nss_can_continue(struct sudo_nss *nss, int match);

#endif /* SUDOERS_NSS_H */
