/*
 * Copyright (c) 2018 Todd C. Miller <Todd.Miller@sudo.ws>
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
#include <stdlib.h>
#ifdef HAVE_STRING_H
# include <string.h>
#endif /* HAVE_STRING_H */
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif /* HAVE_STRINGS_H */

#include "sudoers.h"
#include "sudo_ldap.h"
#include "redblack.h"
#include "strlist.h"
#include <gram.h>

struct sudo_role {
    STAILQ_ENTRY(sudo_role) entries;
    char *cn;
    char *notbefore;
    char *notafter;
    double order;
    struct sudoers_str_list *cmnds;
    struct sudoers_str_list *hosts;
    struct sudoers_str_list *users;
    struct sudoers_str_list *runasusers;
    struct sudoers_str_list *runasgroups;
    struct sudoers_str_list *options;
};
STAILQ_HEAD(sudo_role_list, sudo_role);

static void
sudo_role_free(struct sudo_role *role)
{
    debug_decl(sudo_role_free, SUDOERS_DEBUG_UTIL)

    if (role != NULL) {
	free(role->cn);
	free(role->notbefore);
	free(role->notafter);
	str_list_free(role->cmnds);
	str_list_free(role->hosts);
	str_list_free(role->users);
	str_list_free(role->runasusers);
	str_list_free(role->runasgroups);
	str_list_free(role->options);
	free(role);
    }

    debug_return;
}

static struct sudo_role *
sudo_role_alloc(void)
{
    struct sudo_role *role;
    debug_decl(sudo_role_alloc, SUDOERS_DEBUG_UTIL)

    role = calloc(1, sizeof(*role));
    if (role != NULL) {
	role->cmnds = str_list_alloc();
	role->hosts = str_list_alloc();
	role->users = str_list_alloc();
	role->runasusers = str_list_alloc();
	role->runasgroups = str_list_alloc();
	role->options = str_list_alloc();
	if (role->cmnds == NULL || role->hosts == NULL ||
	    role->users == NULL || role->runasusers == NULL ||
	    role->runasgroups == NULL || role->options == NULL) {
	    sudo_role_free(role);
	    role = NULL;
	}
    }

    debug_return_ptr(role);
}

/*
 * Parse an LDIF attribute, including base64 support.
 * See http://www.faqs.org/rfcs/rfc2849.html
 */
static char *
ldif_parse_attribute(char *str)
{
    bool encoded = false;
    char *attr, *ep;
    size_t len;
    debug_decl(ldif_parse_attribute, SUDOERS_DEBUG_UTIL)

    /* Check for foo:: base64str. */
    if (*str == ':') {
	encoded = true;
	str++;
    }

    /* Trim leading and trailing space. */
    while (*str == ' ')
	str++;

    ep = str + strlen(str);
    while (ep > str && ep[-1] == ' ') {
	ep--;
	/* Don't trim escaped trailing space if not base64. */
	if (!encoded && ep != str && ep[-1] == '\\')
	    break;
	*ep = '\0';
    }

    attr = str;
    if (encoded) {
	/*
	 * Decode base64 inline and add NUL-terminator.
	 * The copy allows us to provide a useful message on error.
	 */
	char *copy = strdup(str);
	if (copy == NULL) {
	    sudo_fatalx(U_("%s: %s"), __func__,
		U_("unable to allocate memory"));
	}
	len = base64_decode(copy, (unsigned char *)attr, strlen(attr));
	if (len == (size_t)-1) {
	    sudo_warnx(U_("ignoring invalid attribute value: %s"), copy);
	    free(copy);
	    debug_return_str(NULL);
	}
	attr[len] = '\0';
	free(copy);
    }

    debug_return_str(attr);
}

/*
 * Allocate a struct sudoers_string, store str in it and
 * insert into the specified strlist.
 */
static void
ldif_store_string(const char *str, struct sudoers_str_list *strlist, bool sorted)
{
    struct sudoers_string *ls;
    debug_decl(ldif_store_string, SUDOERS_DEBUG_UTIL)

    if ((ls = sudoers_string_alloc(str)) == NULL) {
	sudo_fatalx(U_("%s: %s"), __func__,
	    U_("unable to allocate memory"));
    }
    if (!sorted) {
	STAILQ_INSERT_TAIL(strlist, ls, entries);
    } else {
	struct sudoers_string *prev, *next;

	/* Insertion sort, list is small. */
	prev = STAILQ_FIRST(strlist);
	if (prev == NULL || strcasecmp(str, prev->str) <= 0) {
	    STAILQ_INSERT_HEAD(strlist, ls, entries);
	} else {
	    while ((next = STAILQ_NEXT(prev, entries)) != NULL) {
		if (strcasecmp(str, next->str) <= 0)
		    break;
		prev = next;
	    }
	    STAILQ_INSERT_AFTER(strlist, prev, ls, entries);
	}
    }

    debug_return;
}

/*
 * Iterator for sudo_ldap_role_to_priv().
 * Takes a pointer to a struct sudoers_string *.
 * Returns the string or NULL if we've reached the end.
 */
static char *
sudoers_string_iter(void **vp)
{
    struct sudoers_string *ls = *vp;

    if (ls == NULL)
	return NULL;

    *vp = STAILQ_NEXT(ls, entries);

    return ls->str;
}

static int
role_order_cmp(const void *va, const void *vb)
{
    const struct sudo_role *a = *(const struct sudo_role **)va;
    const struct sudo_role *b = *(const struct sudo_role **)vb;
    debug_decl(role_order_cmp, SUDOERS_DEBUG_LDAP)

    debug_return_int(a->order < b->order ? -1 :
        (a->order > b->order ? 1 : 0));
}

/*
 * Parse list of sudoOption and store in the parse tree's defaults list.
 */
static void
ldif_store_options(struct sudoers_parse_tree *parse_tree,
    struct sudoers_str_list *options)
{
    struct defaults *d;
    struct sudoers_string *ls;
    char *var, *val;
    debug_decl(ldif_store_options, SUDOERS_DEBUG_UTIL)

    STAILQ_FOREACH(ls, options, entries) {
	if ((d = calloc(1, sizeof(*d))) == NULL ||
	    (d->binding = malloc(sizeof(*d->binding))) == NULL) {
	    sudo_fatalx(U_("%s: %s"), __func__,
		U_("unable to allocate memory"));
	}
	TAILQ_INIT(d->binding);
	d->type = DEFAULTS;
	d->op = sudo_ldap_parse_option(ls->str, &var, &val);
	if ((d->var = strdup(var)) == NULL) {
	    sudo_fatalx(U_("%s: %s"), __func__,
		U_("unable to allocate memory"));
	}
	if (val != NULL) {
	    if ((d->val = strdup(val)) == NULL) {
		sudo_fatalx(U_("%s: %s"), __func__,
		    U_("unable to allocate memory"));
	    }
	}
	TAILQ_INSERT_TAIL(&parse_tree->defaults, d, entries);
    }
    debug_return;
}

static int
str_list_cmp(const void *aa, const void *bb)
{
    const struct sudoers_str_list *a = aa;
    const struct sudoers_str_list *b = bb;
    const struct sudoers_string *lsa = STAILQ_FIRST(a);
    const struct sudoers_string *lsb = STAILQ_FIRST(b);
    int ret;

    while (lsa != NULL && lsb != NULL) {
	if ((ret = strcmp(lsa->str, lsb->str)) != 0)
	    return ret;
	lsa = STAILQ_NEXT(lsa, entries);
	lsb = STAILQ_NEXT(lsb, entries);
    }
    return lsa == lsb ? 0 : (lsa == NULL ? -1 : 1);
}

static int
str_list_cache(struct rbtree *cache, struct sudoers_str_list **strlistp)
{
    struct sudoers_str_list *strlist = *strlistp;
    struct rbnode *node;
    int ret;
    debug_decl(str_list_cache, SUDOERS_DEBUG_UTIL)

    ret = rbinsert(cache, strlist, &node);
    switch (ret) {
    case 0:
	/* new entry, take a ref for the cache */
	strlist->refcnt++;
	break;
    case 1:
	/* already exists, use existing and take a ref. */
	str_list_free(strlist);
	strlist = node->data;
	strlist->refcnt++;
	*strlistp = strlist;
	break;
    }
    debug_return_int(ret);
}

/*
 * Convert a sudoRole to sudoers format and store in the parse tree.
 */
static void
role_to_sudoers(struct sudoers_parse_tree *parse_tree, struct sudo_role *role,
    bool store_options, bool reuse_userspec, bool reuse_privilege,
    bool reuse_runas)
{
    struct privilege *priv;
    struct sudoers_string *ls;
    struct userspec *us;
    struct member *m;
    debug_decl(role_to_sudoers, SUDOERS_DEBUG_UTIL)

    /*
     * TODO: use cn to create a UserAlias if multiple users in it?
     */

    if (reuse_userspec) {
	/* Re-use the previous userspec */
	us = TAILQ_LAST(&parse_tree->userspecs, userspec_list);
    } else {
	/* Allocate a new userspec and fill in the user list. */
	if ((us = calloc(1, sizeof(*us))) == NULL) {
	    sudo_fatalx(U_("%s: %s"), __func__,
		U_("unable to allocate memory"));
	}
	TAILQ_INIT(&us->privileges);
	TAILQ_INIT(&us->users);
	STAILQ_INIT(&us->comments);

	STAILQ_FOREACH(ls, role->users, entries) {
	    char *user = ls->str;

	    if ((m = calloc(1, sizeof(*m))) == NULL) {
		sudo_fatalx(U_("%s: %s"), __func__,
		    U_("unable to allocate memory"));
	    }
	    m->negated = sudo_ldap_is_negated(&user);
	    m->name = strdup(user);
	    if (m->name == NULL) {
		sudo_fatalx(U_("%s: %s"), __func__,
		    U_("unable to allocate memory"));
	    }
	    if (strcmp(user, "ALL") == 0) {
		m->type = ALL;
	    } else if (*user == '+') {
		m->type = NETGROUP;
	    } else if (*user == '%') {
		m->type = USERGROUP;
	    } else {
		m->type = WORD;
	    }
	    TAILQ_INSERT_TAIL(&us->users, m, entries);
	}
    }

    /* Add source role as a comment. */
    if (role->cn != NULL) {
	struct sudoers_comment *comment = NULL;
	if (reuse_userspec) {
	    /* Try to re-use comment too. */
	    STAILQ_FOREACH(comment, &us->comments, entries) {
		if (strncmp(comment->str, "sudoRole ", 9) == 0) {
		    char *tmpstr;
		    if (asprintf(&tmpstr, "%s, %s", comment->str, role->cn) == -1) {
			sudo_fatalx(U_("%s: %s"), __func__,
			    U_("unable to allocate memory"));
		    }
		    free(comment->str);
		    comment->str = tmpstr;
		    break;
		}
	    }
	}
	if (comment == NULL) {
	    /* Create a new comment. */
	    if ((comment = malloc(sizeof(*comment))) == NULL) {
		sudo_fatalx(U_("%s: %s"), __func__,
		    U_("unable to allocate memory"));
	    }
	    if (asprintf(&comment->str, "sudoRole %s", role->cn) == -1) {
		sudo_fatalx(U_("%s: %s"), __func__,
		    U_("unable to allocate memory"));
	    }
	    STAILQ_INSERT_TAIL(&us->comments, comment, entries);
	}
    }

    /* Convert role to sudoers privilege. */
    priv = sudo_ldap_role_to_priv(role->cn, STAILQ_FIRST(role->hosts),
	STAILQ_FIRST(role->runasusers), STAILQ_FIRST(role->runasgroups),
	STAILQ_FIRST(role->cmnds), STAILQ_FIRST(role->options),
	role->notbefore, role->notafter, true, store_options,
	sudoers_string_iter);
    if (priv == NULL) {
	sudo_fatalx(U_("%s: %s"), __func__,
	    U_("unable to allocate memory"));
    }

    if (reuse_privilege) {
	/* Hostspec unchanged, append cmndlist to previous privilege. */
	struct privilege *prev_priv = TAILQ_LAST(&us->privileges, privilege_list);
	if (reuse_runas) {
	    /* Runas users and groups same if as in previous privilege. */
	    struct member_list *runasuserlist =
		TAILQ_FIRST(&prev_priv->cmndlist)->runasuserlist;
	    struct member_list *runasgrouplist =
		TAILQ_FIRST(&prev_priv->cmndlist)->runasgrouplist;
	    struct cmndspec *cmndspec = TAILQ_FIRST(&priv->cmndlist);

	    /* Free duplicate runas lists. */
	    if (cmndspec->runasuserlist != NULL) {
		free_members(cmndspec->runasuserlist);
		free(cmndspec->runasuserlist);
	    }
	    if (cmndspec->runasgrouplist != NULL) {
		free_members(cmndspec->runasgrouplist);
		free(cmndspec->runasgrouplist);
	    }

	    /* Update cmndspec with previous runas lists. */
	    TAILQ_FOREACH(cmndspec, &priv->cmndlist, entries) {
		cmndspec->runasuserlist = runasuserlist;
		cmndspec->runasgrouplist = runasgrouplist;
	    }
	}
	TAILQ_CONCAT(&prev_priv->cmndlist, &priv->cmndlist, entries);
	free_privilege(priv);
    } else {
	TAILQ_INSERT_TAIL(&us->privileges, priv, entries);
    }

    /* Add finished userspec to the list if new. */
    if (!reuse_userspec)
	TAILQ_INSERT_TAIL(&parse_tree->userspecs, us, entries);

    debug_return;
}

/*
 * Convert the list of sudoRoles to sudoers format and store in the parse tree.
 */
static void
ldif_to_sudoers(struct sudoers_parse_tree *parse_tree,
    struct sudo_role_list *roles, unsigned int numroles, bool store_options)
{
    struct sudo_role **role_array, *role = NULL;
    unsigned int n;
    debug_decl(ldif_to_sudoers, SUDOERS_DEBUG_UTIL)

    /* Convert from list of roles to array and sort by order. */
    role_array = reallocarray(NULL, numroles + 1, sizeof(*role_array));
    for (n = 0; n < numroles; n++) {
	if ((role = STAILQ_FIRST(roles)) == NULL)
	    break;	/* cannot happen */
	STAILQ_REMOVE_HEAD(roles, entries);
	role_array[n] = role;
    }
    role_array[n] = NULL;
    qsort(role_array, numroles, sizeof(*role_array), role_order_cmp);

    /*
     * Iterate over roles in sorted order, converting to sudoers.
     */
    for (n = 0; n < numroles; n++) {
	bool reuse_userspec = false;
	bool reuse_privilege = false;
	bool reuse_runas = false;

	role = role_array[n];

	/* Check whether we can reuse the previous user and host specs */
	if (n > 0 && role->users == role_array[n - 1]->users) {
	    reuse_userspec = true;

	    /*
	     * Since options are stored per-privilege we can't
	     * append to the previous privilege's cmndlist if
	     * we are storing options.
	     */
	    if (!store_options) {
		if (role->hosts == role_array[n - 1]->hosts) {
		    reuse_privilege = true;

		    /* Reuse runasusers and runasgroups if possible. */
		    if (role->runasusers == role_array[n - 1]->runasusers &&
			role->runasgroups == role_array[n - 1]->runasgroups)
			reuse_runas = true;
		}
	    }
	}

	role_to_sudoers(parse_tree, role, store_options, reuse_userspec,
	    reuse_privilege, reuse_runas);
    }

    /* Clean up. */
    for (n = 0; n < numroles; n++)
	sudo_role_free(role_array[n]);
    free(role_array);

    debug_return;
}

/*
 * Given a cn with possible quoted characters, return a copy of
 * the cn with quote characters ('\\') removed.
 * The caller is responsible for freeing the returned string.
 */
static
char *unquote_cn(const char *src)
{
    char *dst, *new_cn;
    size_t len;
    debug_decl(unquote_cn, SUDOERS_DEBUG_UTIL)

    len = strlen(src);
    if ((new_cn = malloc(len + 1)) == NULL)
	debug_return_str(NULL);

    for (dst = new_cn; *src != '\0';) {
	if (src[0] == '\\' && src[1] != '\0')
	    src++;
	*dst++ = *src++;
    }
    *dst = '\0';

    debug_return_str(new_cn);
}

/*
 * Parse a sudoers file in LDIF format, https://tools.ietf.org/html/rfc2849
 * Parsed sudoRole objects are stored in the specified parse_tree which
 * must already be initialized.
 */
bool
sudoers_parse_ldif(struct sudoers_parse_tree *parse_tree,
    FILE *fp, const char *sudoers_base, bool store_options)
{
    struct sudo_role_list roles = STAILQ_HEAD_INITIALIZER(roles);
    struct sudo_role *role = NULL;
    struct rbtree *usercache, *groupcache, *hostcache;
    unsigned numroles = 0;
    bool in_role = false;
    size_t linesize = 0;
    char *attr, *line = NULL, *savedline = NULL;
    ssize_t savedlen = 0;
    bool mismatch = false;
    debug_decl(sudoers_parse_ldif, SUDOERS_DEBUG_UTIL)

    /* Free old contents of the parse tree (if any). */
    free_parse_tree(parse_tree);

    /*
     * We cache user, group and host lists to make it eay to detect when there
     * are identical lists (simple pointer compare).  This makes it possible
     * to merge multiplpe sudoRole objects into a single UserSpec and/or
     * Privilege.  The lists are sorted since LDAP order is arbitrary.
     */
    usercache = rbcreate(str_list_cmp);
    groupcache = rbcreate(str_list_cmp);
    hostcache = rbcreate(str_list_cmp);
    if (usercache == NULL || groupcache == NULL || hostcache == NULL)
	sudo_fatalx(U_("%s: %s"), __func__, U_("unable to allocate memory"));

    /* Read through input, parsing into sudo_roles and global defaults. */
    for (;;) {
	int ch;
	ssize_t len = getline(&line, &linesize, fp);

	/* Trim trailing return or newline. */
	while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n'))
	    line[--len] = '\0';

	/* Blank line or EOF terminates an entry. */
	if (len <= 0) {
	    if (in_role) {
		if (role->cn != NULL && strcmp(role->cn, "defaults") == 0) {
		    ldif_store_options(parse_tree, role->options);
		    sudo_role_free(role);
		    role = NULL;
		} else if (STAILQ_EMPTY(role->users) ||
		    STAILQ_EMPTY(role->hosts) || STAILQ_EMPTY(role->cmnds)) {
		    /* Incomplete role. */
		    sudo_warnx(U_("ignoring incomplete sudoRole: cn: %s"),
			role->cn ? role->cn : "UNKNOWN");
		    sudo_role_free(role);
		    role = NULL;
		} else {
		    /* Cache users, hosts, runasusers and runasgroups. */
		    if (str_list_cache(usercache, &role->users) == -1 ||
			str_list_cache(hostcache, &role->hosts) == -1 ||
			str_list_cache(usercache, &role->runasusers) == -1 ||
			str_list_cache(groupcache, &role->runasgroups) == -1) {
			sudo_fatalx(U_("%s: %s"), __func__,
			    U_("unable to allocate memory"));
		    }

		    /* Store finished role. */
		    STAILQ_INSERT_TAIL(&roles, role, entries);
		    numroles++;
		}
		role = NULL;
		in_role = false;
	    }
	    if (len == -1) {
		/* EOF */
		break;
	    }
	    mismatch = false;
	    continue;
	}

	if (savedline != NULL) {
	    char *tmp;

	    /* Append to saved line. */
	    linesize = savedlen + len + 1;
	    if ((tmp = realloc(savedline, linesize)) == NULL) {
		sudo_fatalx(U_("%s: %s"), __func__,
		    U_("unable to allocate memory"));
	    }
	    memcpy(tmp + savedlen, line, len + 1);
	    free(line);
	    line = tmp;
	    savedline = NULL;
	} else {
	    /* Skip comment lines or records that don't match the base. */
	    if (*line == '#' || mismatch)
		continue;
	}

	/* Check for folded line */
	if ((ch = getc(fp)) == ' ') {
	    /* folded line, append to the saved portion. */
	    savedlen = len;
	    savedline = line;
	    line = NULL;
	    linesize = 0;
	    continue;
	} else {
	    /* not folded, push back ch */
	    ungetc(ch, fp);
	}

	/* Allocate new role as needed. */
	if (role == NULL) {
	    if ((role = sudo_role_alloc()) == NULL) {
		sudo_fatalx(U_("%s: %s"), __func__,
		    U_("unable to allocate memory"));
	    }
	}

	/* Parse dn and objectClass. */
	if (strncasecmp(line, "dn:", 3) == 0) {
	    /* Compare dn to base, if specified. */
	    if (sudoers_base != NULL) {
		attr = ldif_parse_attribute(line + 3);
		if (attr == NULL) {
		    /* invalid attribute */
		    mismatch = true;
		    continue;
		}
		/* Skip over cn if present. */
		if (strncasecmp(attr, "cn=", 3) == 0) {
		    for (attr += 3; *attr != '\0'; attr++) {
			/* Handle escaped ',' chars. */
			if (*attr == '\\')
			    attr++;
			if (*attr == ',') {
			    attr++;
			    break;
			}
		    }
		}
		if (strcasecmp(attr, sudoers_base) != 0) {
		    /* Doesn't match base, skip the rest of it. */
		    mismatch = true;
		    continue;
		}
	    }
	} else if (strncmp(line, "objectClass:", 12) == 0) {
	    attr = ldif_parse_attribute(line + 12);
	    if (attr != NULL && strcmp(attr, "sudoRole") == 0)
		in_role = true;
	}

	/* Not in a sudoRole, keep reading. */
	if (!in_role)
	    continue;

	/* Part of a sudoRole, parse it. */
	if (strncmp(line, "cn:", 3) == 0) {
	    attr = ldif_parse_attribute(line + 3);
	    if (attr != NULL) {
		free(role->cn);
		role->cn = unquote_cn(attr);
		if (role->cn == NULL) {
		    sudo_fatalx(U_("%s: %s"), __func__,
			U_("unable to allocate memory"));
		}
	    }
	} else if (strncmp(line, "sudoUser:", 9) == 0) {
	    attr = ldif_parse_attribute(line + 9);
	    if (attr != NULL)
		ldif_store_string(attr, role->users, true);
	} else if (strncmp(line, "sudoHost:", 9) == 0) {
	    attr = ldif_parse_attribute(line + 9);
	    if (attr != NULL)
		ldif_store_string(attr, role->hosts, true);
	} else if (strncmp(line, "sudoRunAs:", 10) == 0) {
	    attr = ldif_parse_attribute(line + 10);
	    if (attr != NULL)
		ldif_store_string(attr, role->runasusers, true);
	} else if (strncmp(line, "sudoRunAsUser:", 14) == 0) {
	    attr = ldif_parse_attribute(line + 14);
	    if (attr != NULL)
		ldif_store_string(attr, role->runasusers, true);
	} else if (strncmp(line, "sudoRunAsGroup:", 15) == 0) {
	    attr = ldif_parse_attribute(line + 15);
	    if (attr != NULL)
		ldif_store_string(attr, role->runasgroups, true);
	} else if (strncmp(line, "sudoCommand:", 12) == 0) {
	    attr = ldif_parse_attribute(line + 12);
	    if (attr != NULL)
		ldif_store_string(attr, role->cmnds, false);
	} else if (strncmp(line, "sudoOption:", 11) == 0) {
	    attr = ldif_parse_attribute(line + 11);
	    if (attr != NULL)
		ldif_store_string(attr, role->options, false);
	} else if (strncmp(line, "sudoOrder:", 10) == 0) {
	    char *ep;
	    attr = ldif_parse_attribute(line + 10);
	    if (attr != NULL) {
		role->order = strtod(attr, &ep);
		if (ep == attr || *ep != '\0')
		    sudo_warnx(U_("invalid sudoOrder attribute: %s"), attr);
	    }
	} else if (strncmp(line, "sudoNotBefore:", 14) == 0) {
	    attr = ldif_parse_attribute(line + 14);
	    if (attr != NULL) {
		free(role->notbefore);
		role->notbefore = strdup(attr);
		if (role->notbefore == NULL) {
		    sudo_fatalx(U_("%s: %s"), __func__,
			U_("unable to allocate memory"));
		}
	    }
	} else if (strncmp(line, "sudoNotAfter:", 13) == 0) {
	    attr = ldif_parse_attribute(line + 13);
	    if (attr != NULL) {
		free(role->notafter);
		role->notafter = strdup(attr);
		if (role->notafter == NULL) {
		    sudo_fatalx(U_("%s: %s"), __func__,
			U_("unable to allocate memory"));
		}
	    }
	}
    }
    sudo_role_free(role);
    free(line);

    /* Convert from roles to sudoers data structures. */
    ldif_to_sudoers(parse_tree, &roles, numroles, store_options);

    /* Clean up. */
    rbdestroy(usercache, str_list_free);
    rbdestroy(groupcache, str_list_free);
    rbdestroy(hostcache, str_list_free);

    if (fp != stdin)
	fclose(fp);

    debug_return_bool(true);
}
