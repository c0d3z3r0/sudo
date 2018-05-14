/*
 * Copyright (c) 2003-2018 Todd C. Miller <Todd.Miller@sudo.ws>
 * Copyright (c) 2011 Daniel Kopecek <dkopecek@redhat.com>
 *
 * This code is derived from software contributed by Aaron Spangler.
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

#ifdef HAVE_SSSD

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STRING_H
# include <string.h>
#endif /* HAVE_STRING_H */
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif /* HAVE_STRINGS_H */
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>

#include <errno.h>
#include <stdint.h>

#include "sudoers.h"
#include "gram.h"
#include "sudo_lbuf.h"
#include "sudo_ldap.h"
#include "sudo_dso.h"

/* SSSD <--> SUDO interface - do not change */
struct sss_sudo_attr {
    char *name;
    char **values;
    unsigned int num_values;
};

struct sss_sudo_rule {
    unsigned int num_attrs;
    struct sss_sudo_attr *attrs;
};

struct sss_sudo_result {
    unsigned int num_rules;
    struct sss_sudo_rule *rules;
};

typedef int  (*sss_sudo_send_recv_t)(uid_t, const char*, const char*,
                                     uint32_t*, struct sss_sudo_result**);

typedef int  (*sss_sudo_send_recv_defaults_t)(uid_t, const char*, uint32_t*,
                                              char**, struct sss_sudo_result**);

typedef void (*sss_sudo_free_result_t)(struct sss_sudo_result*);

typedef int  (*sss_sudo_get_values_t)(struct sss_sudo_rule*, const char*,
                                      char***);

typedef void (*sss_sudo_free_values_t)(char**);

/* sudo_nss implementation */

struct sudo_sss_handle {
    char *domainname;
    char *ipa_host;
    char *ipa_shost;
    struct passwd *pw;
    void *ssslib;
    sss_sudo_send_recv_t fn_send_recv;
    sss_sudo_send_recv_defaults_t fn_send_recv_defaults;
    sss_sudo_free_result_t fn_free_result;
    sss_sudo_get_values_t fn_get_values;
    sss_sudo_free_values_t fn_free_values;
};

static int sudo_sss_open(struct sudo_nss *nss);
static int sudo_sss_close(struct sudo_nss *nss);
static int sudo_sss_parse(struct sudo_nss *nss);
static int sudo_sss_query(struct sudo_nss *nss, struct passwd *pw);
static bool sudo_sss_parse_options(struct sudo_sss_handle *handle,
				   struct sss_sudo_rule *rule,
				   struct defaults_list *defs);

static int sudo_sss_getdefs(struct sudo_nss *nss);

static struct sss_sudo_result *sudo_sss_result_get(struct sudo_nss *nss,
						   struct passwd *pw);

static bool sss_to_sudoers(struct sudo_sss_handle *handle,
			   struct sss_sudo_result *sss_result,
			   struct userspec_list *sss_userspecs);

static void
sudo_sss_attrfree(struct sss_sudo_attr *attr)
{
    unsigned int i;
    debug_decl(sudo_sss_attrfree, SUDOERS_DEBUG_SSSD)

    free(attr->name);
    attr->name = NULL;
    if (attr->values != NULL) {
	for (i = 0; i < attr->num_values; ++i)
	    free(attr->values[i]);
	free(attr->values);
	attr->values = NULL;
    }
    attr->num_values = 0;

    debug_return;
}

static bool
sudo_sss_attrcpy(struct sss_sudo_attr *dst, const struct sss_sudo_attr *src)
{
    unsigned int i = 0;
    debug_decl(sudo_sss_attrcpy, SUDOERS_DEBUG_SSSD)

    sudo_debug_printf(SUDO_DEBUG_DEBUG, "dst=%p, src=%p", dst, src);
    sudo_debug_printf(SUDO_DEBUG_INFO, "malloc: cnt=%d", src->num_values);

    dst->name = strdup(src->name);
    dst->values = reallocarray(NULL, src->num_values, sizeof(char *));
    if (dst->name == NULL || dst->values == NULL)
	goto oom;
    dst->num_values = src->num_values;

    for (i = 0; i < dst->num_values; ++i) {
	dst->values[i] = strdup(src->values[i]);
	if (dst->values[i] == NULL) {
	    dst->num_values = i;
	    goto oom;
	}
    }

    debug_return_bool(true);
oom:
    sudo_warnx(U_("%s: %s"), __func__, U_("unable to allocate memory"));
    sudo_sss_attrfree(dst);
    debug_return_bool(false);
}

static void
sudo_sss_rulefree(struct sss_sudo_rule *rule)
{
    unsigned int i;
    debug_decl(sudo_sss_rulefree, SUDOERS_DEBUG_SSSD)

    for (i = 0; i < rule->num_attrs; ++i)
	sudo_sss_attrfree(rule->attrs + i);
    free(rule->attrs);
    rule->attrs = NULL;
    rule->num_attrs = 0;

    debug_return;
}

static bool
sudo_sss_rulecpy(struct sss_sudo_rule *dst, const struct sss_sudo_rule *src)
{
    unsigned int i;
    debug_decl(sudo_sss_rulecpy, SUDOERS_DEBUG_SSSD)

    sudo_debug_printf(SUDO_DEBUG_DEBUG, "dst=%p, src=%p", dst, src);
    sudo_debug_printf(SUDO_DEBUG_INFO, "malloc: cnt=%d", src->num_attrs);

    dst->num_attrs = 0;
    dst->attrs = reallocarray(NULL, src->num_attrs, sizeof(struct sss_sudo_attr));
    if (dst->attrs == NULL) {
	sudo_warnx(U_("%s: %s"), __func__, U_("unable to allocate memory"));
	debug_return_bool(false);
    }

    for (i = 0; i < src->num_attrs; ++i) {
	if (!sudo_sss_attrcpy(dst->attrs + i, src->attrs + i)) {
	    dst->num_attrs = i;
	    sudo_sss_rulefree(dst);
	    debug_return_bool(false);
	}
    }
    dst->num_attrs = i;

    debug_return_bool(true);
}

#define SUDO_SSS_FILTER_INCLUDE 0
#define SUDO_SSS_FILTER_EXCLUDE 1

/* XXX - insted of filtering result, include user and host in sudoers parse tree */
static struct sss_sudo_result *
sudo_sss_filter_result(struct sudo_sss_handle *handle,
    struct sss_sudo_result *in_res,
    int (*filterp)(struct sudo_sss_handle *, struct sss_sudo_rule *, void *),
    int act, void *filterp_arg)
{
    struct sss_sudo_result *out_res;
    unsigned int i, l;
    int r;
    debug_decl(sudo_sss_filter_result, SUDOERS_DEBUG_SSSD)

    sudo_debug_printf(SUDO_DEBUG_DEBUG, "in_res=%p, count=%u, act=%s",
	in_res, in_res ? in_res->num_rules : 0,
	act == SUDO_SSS_FILTER_EXCLUDE ? "EXCLUDE" : "INCLUDE");

    if (in_res == NULL)
	debug_return_ptr(NULL);

    sudo_debug_printf(SUDO_DEBUG_DEBUG, "malloc: cnt=%d", in_res->num_rules);

    if ((out_res = calloc(1, sizeof(struct sss_sudo_result))) == NULL) {
	sudo_warnx(U_("%s: %s"), __func__, U_("unable to allocate memory"));
	debug_return_ptr(NULL);
    }
    if (in_res->num_rules > 0) {
	out_res->rules =
	    reallocarray(NULL, in_res->num_rules, sizeof(struct sss_sudo_rule));
	if (out_res->rules == NULL) {
	    sudo_warnx(U_("%s: %s"), __func__, U_("unable to allocate memory"));
	    free(out_res);
	    debug_return_ptr(NULL);
	}
    }

    for (i = l = 0; i < in_res->num_rules; ++i) {
	 r = filterp(handle, in_res->rules + i, filterp_arg);

	 if (( r && act == SUDO_SSS_FILTER_INCLUDE) ||
	     (!r && act == SUDO_SSS_FILTER_EXCLUDE)) {
	    sudo_debug_printf(SUDO_DEBUG_DEBUG,
		"COPY (%s): %p[%u] => %p[%u] (= %p)",
		act == SUDO_SSS_FILTER_EXCLUDE ? "not excluded" : "included",
		in_res->rules, i, out_res->rules, l, in_res->rules + i);

	    if (!sudo_sss_rulecpy(out_res->rules + l, in_res->rules + i)) {
		while (l--) {
		    sudo_sss_rulefree(out_res->rules + l);
		}
		free(out_res->rules);
		free(out_res);
		debug_return_ptr(NULL);
	    }
	    ++l;
	}
    }

    if (l < in_res->num_rules) {
	sudo_debug_printf(SUDO_DEBUG_DEBUG,
	    "reallocating result: %p (count: %u -> %u)", out_res->rules,
	    in_res->num_rules, l);
	if (l > 0) {
	    struct sss_sudo_rule *rules =
		reallocarray(out_res->rules, l, sizeof(struct sss_sudo_rule));
	    if (out_res->rules == NULL) {
		sudo_warnx(U_("%s: %s"), __func__, U_("unable to allocate memory"));
		while (l--) {
		    sudo_sss_rulefree(out_res->rules + l);
		}
		free(out_res->rules);
		free(out_res);
		debug_return_ptr(NULL);
	    }
	    out_res->rules = rules;
	} else {
	    free(out_res->rules);
	    out_res->rules = NULL;
	}
    }

    out_res->num_rules = l;

    debug_return_ptr(out_res);
}

static int
get_ipa_hostname(char **shostp, char **lhostp)
{
    size_t linesize = 0;
    char *lhost = NULL;
    char *shost = NULL;
    char *line = NULL;
    int ret = false;
    ssize_t len;
    FILE *fp;
    debug_decl(get_ipa_hostname, SUDOERS_DEBUG_SSSD)

    fp = fopen(_PATH_SSSD_CONF, "r");
    if (fp != NULL) {
	while ((len = getline(&line, &linesize, fp)) != -1) {
	    char *cp = line;

	    /* Trim trailing and leading spaces. */
	    while (len > 0 && isspace((unsigned char)line[len - 1]))
		line[--len] = '\0';
	    while (isspace((unsigned char)*cp))
		cp++;

	    /*
	     * Match ipa_hostname = foo
	     * Note: currently ignores the domain (XXX)
	     */
	    if (strncmp(cp, "ipa_hostname", 12) == 0) {
		cp += 12;
		/* Trim " = " after "ipa_hostname" */
		while (isblank((unsigned char)*cp))
		    cp++;
		if (*cp++ != '=')
		    continue;
		while (isblank((unsigned char)*cp))
		    cp++;
		/* Ignore empty value */
		if (*cp == '\0')
		    continue;
		lhost = strdup(cp);
		if (lhost != NULL && (cp = strchr(lhost, '.')) != NULL) {
		    shost = strndup(lhost, (size_t)(cp - lhost));
		} else {
		    shost = lhost;
		}
		if (shost != NULL && lhost != NULL) {
		    sudo_debug_printf(SUDO_DEBUG_INFO,
			"ipa_hostname %s overrides %s", lhost, user_host);
		    *shostp = shost;
		    *lhostp = lhost;
		    ret = true;
		} else {
		    sudo_warnx(U_("%s: %s"), __func__,
			U_("unable to allocate memory"));
		    free(shost);
		    free(lhost);
		    ret = -1;
		}
		break;
	    }
	}
	fclose(fp);
	free(line);
    }
    debug_return_int(ret);
}

struct sudo_nss sudo_nss_sss = {
    { NULL, NULL },
    sudo_sss_open,
    sudo_sss_close,
    sudo_sss_parse,
    sudo_sss_query,
    sudo_sss_getdefs
};

/* sudo_nss implementation */
// ok
static int
sudo_sss_open(struct sudo_nss *nss)
{
    struct sudo_sss_handle *handle;
    static const char path[] = _PATH_SSSD_LIB"/libsss_sudo.so";
    debug_decl(sudo_sss_open, SUDOERS_DEBUG_SSSD);

    /* Create a handle container. */
    handle = calloc(1, sizeof(struct sudo_sss_handle));
    if (handle == NULL) {
	sudo_warnx(U_("%s: %s"), __func__, U_("unable to allocate memory"));
	debug_return_int(ENOMEM);
    }

    /* Load symbols */
    handle->ssslib = sudo_dso_load(path, SUDO_DSO_LAZY);
    if (handle->ssslib == NULL) {
	const char *errstr = sudo_dso_strerror();
	sudo_warnx(U_("unable to load %s: %s"), path,
	    errstr ? errstr : "unknown error");
	sudo_warnx(U_("unable to initialize SSS source. Is SSSD installed on your machine?"));
	free(handle);
	debug_return_int(EFAULT);
    }

    handle->fn_send_recv =
	sudo_dso_findsym(handle->ssslib, "sss_sudo_send_recv");
    if (handle->fn_send_recv == NULL) {
	sudo_warnx(U_("unable to find symbol \"%s\" in %s"), path,
	   "sss_sudo_send_recv");
	free(handle);
	debug_return_int(EFAULT);
    }

    handle->fn_send_recv_defaults =
	sudo_dso_findsym(handle->ssslib, "sss_sudo_send_recv_defaults");
    if (handle->fn_send_recv_defaults == NULL) {
	sudo_warnx(U_("unable to find symbol \"%s\" in %s"), path,
	   "sss_sudo_send_recv_defaults");
	free(handle);
	debug_return_int(EFAULT);
    }

    handle->fn_free_result =
	sudo_dso_findsym(handle->ssslib, "sss_sudo_free_result");
    if (handle->fn_free_result == NULL) {
	sudo_warnx(U_("unable to find symbol \"%s\" in %s"), path,
	   "sss_sudo_free_result");
	free(handle);
	debug_return_int(EFAULT);
    }

    handle->fn_get_values =
	sudo_dso_findsym(handle->ssslib, "sss_sudo_get_values");
    if (handle->fn_get_values == NULL) {
	sudo_warnx(U_("unable to find symbol \"%s\" in %s"), path,
	   "sss_sudo_get_values");
	free(handle);
	debug_return_int(EFAULT);
    }

    handle->fn_free_values =
	sudo_dso_findsym(handle->ssslib, "sss_sudo_free_values");
    if (handle->fn_free_values == NULL) {
	sudo_warnx(U_("unable to find symbol \"%s\" in %s"), path,
	   "sss_sudo_free_values");
	free(handle);
	debug_return_int(EFAULT);
    }

    nss->handle = handle;

    /*
     * If runhost is the same as the local host, check for ipa_hostname
     * in sssd.conf and use it in preference to user_runhost.
     */
    if (strcasecmp(user_runhost, user_host) == 0) {
	if (get_ipa_hostname(&handle->ipa_shost, &handle->ipa_host) == -1) {
	    free(handle);
	    debug_return_int(ENOMEM);
	}
    }

    sudo_debug_printf(SUDO_DEBUG_DEBUG, "handle=%p", handle);

    debug_return_int(0);
}

// ok
static int
sudo_sss_close(struct sudo_nss *nss)
{
    struct member_list *prev_binding = NULL;
    struct defaults *def;
    struct userspec *us;
    struct sudo_sss_handle *handle;
    debug_decl(sudo_sss_close, SUDOERS_DEBUG_SSSD);

    if (nss && nss->handle) {
	handle = nss->handle;
	sudo_dso_unload(handle->ssslib);
	if (handle->pw != NULL)
	    sudo_pw_delref(handle->pw);
	free(handle->ipa_host);
	if (handle->ipa_host != handle->ipa_shost)
	    free(handle->ipa_shost);
	free(handle);
	nss->handle = NULL;

	/* XXX - do in main module? */
	while ((us = TAILQ_FIRST(&nss->userspecs)) != NULL) {
	    TAILQ_REMOVE(&nss->userspecs, us, entries);
	    free_userspec(us);
	}
	while ((def = TAILQ_FIRST(&nss->defaults)) != NULL) {
	    TAILQ_REMOVE(&nss->defaults, def, entries);
	    free_default(def, &prev_binding);
	}
    }
    debug_return_int(0);
}

/*
 * Perform query for user and host and convert to sudoers parse tree.
 */
static int
sudo_sss_query(struct sudo_nss *nss, struct passwd *pw)
{
    struct sudo_sss_handle *handle = nss->handle;
    struct sss_sudo_result *sss_result = NULL;
    struct userspec *us;
    int ret = 0;
    debug_decl(sudo_sss_query, SUDOERS_DEBUG_SSSD);

    /* Use cached result if it matches pw. */
    if (handle->pw != NULL) {
	if (pw == handle->pw)
	    goto done;
	sudo_pw_delref(handle->pw);
	handle->pw = NULL;
    }

    /* Free old userspecs, if any. */
    while ((us = TAILQ_FIRST(&nss->userspecs)) != NULL) {
	TAILQ_REMOVE(&nss->userspecs, us, entries);
	free_userspec(us);
    }

    /* Fetch list of sudoRole entries that match user and host. */
    sss_result = sudo_sss_result_get(nss, pw);

    sudo_debug_printf(SUDO_DEBUG_DIAG,
	"searching SSSD/LDAP for sudoers entries for user %s, host %s",
	 pw->pw_name, user_runhost);

    if (sss_result == NULL)
	goto done;

    /* Convert to sudoers parse tree. */
    if (!sss_to_sudoers(handle, sss_result, &nss->userspecs)) {
	ret = -1;
	goto done;
    }

    /* Stash a ref to the passwd struct in the handle. */
    sudo_pw_addref(pw);
    handle->pw = pw;

done:
    /* Cleanup */
    handle->fn_free_result(sss_result);
    if (ret == -1) {
	while ((us = TAILQ_FIRST(&nss->userspecs)) != NULL) {
	    TAILQ_REMOVE(&nss->userspecs, us, entries);
	    free_userspec(us);
	}
    }

    sudo_debug_printf(SUDO_DEBUG_DIAG, "Done with LDAP searches");

    debug_return_int(ret);
}

// ok
static int
sudo_sss_parse(struct sudo_nss *nss)
{
    debug_decl(sudo_sss_parse, SUDOERS_DEBUG_SSSD);
    debug_return_int(0);
}

static int
sudo_sss_getdefs(struct sudo_nss *nss)
{
    struct sudo_sss_handle *handle = nss->handle;
    struct sss_sudo_result *sss_result = NULL;
    struct sss_sudo_rule   *sss_rule;
    struct member_list *prev_binding = NULL;
    struct defaults *def;
    uint32_t sss_error;
    unsigned int i;
    debug_decl(sudo_sss_getdefs, SUDOERS_DEBUG_SSSD);

    if (handle == NULL)
	debug_return_int(-1);

    /* Free old defaults, if any. */
    while ((def = TAILQ_FIRST(&nss->defaults)) != NULL) {
	TAILQ_REMOVE(&nss->defaults, def, entries);
	free_default(def, &prev_binding);
    }

    sudo_debug_printf(SUDO_DEBUG_DIAG, "Looking for cn=defaults");

    /* NOTE: these are global defaults, user ID and name are not used. */
    if (handle->fn_send_recv_defaults(sudo_user.pw->pw_uid,
				      sudo_user.pw->pw_name, &sss_error,
				      &handle->domainname, &sss_result) != 0) {
	sudo_debug_printf(SUDO_DEBUG_INFO,
	    "handle->fn_send_recv_defaults: != 0, sss_error=%u", sss_error);
	debug_return_int(-1);
    }
    if (sss_error != 0) {
	if (sss_error == ENOENT) {
	    sudo_debug_printf(SUDO_DEBUG_INFO,
		"The user was not found in SSSD.");
	    goto done;
	}
	sudo_debug_printf(SUDO_DEBUG_INFO, "sss_error=%u\n", sss_error);
	goto bad;
    }

    for (i = 0; i < sss_result->num_rules; ++i) {
	 sudo_debug_printf(SUDO_DEBUG_DIAG,
	    "Parsing cn=defaults, %d/%d", i, sss_result->num_rules);
	 sss_rule = sss_result->rules + i;
	 if (!sudo_sss_parse_options(handle, sss_rule, &nss->defaults))
	    goto bad;
    }

done:
    handle->fn_free_result(sss_result);
    debug_return_int(0);
bad:
    handle->fn_free_result(sss_result);
    debug_return_int(-1);
}

static bool
sudo_sss_check_host(struct sudo_sss_handle *handle, struct sss_sudo_rule *rule)
{
    const char *host = handle->ipa_host ? handle->ipa_host : user_runhost;
    const char *shost = handle->ipa_shost ? handle->ipa_shost : user_srunhost;
    char *val, **val_array;
    int matched = UNSPEC;
    bool negated;
    int i;
    debug_decl(sudo_sss_check_host, SUDOERS_DEBUG_SSSD);

    if (rule == NULL)
	debug_return_bool(false);

    /* get the values from the rule */
    switch (handle->fn_get_values(rule, "sudoHost", &val_array)) {
    case 0:
	break;
    case ENOENT:
	sudo_debug_printf(SUDO_DEBUG_INFO, "No result.");
	debug_return_bool(false);
    default:
	sudo_debug_printf(SUDO_DEBUG_INFO, "handle->fn_get_values(sudoHost): != 0");
	debug_return_bool(false);
    }

    /* walk through values */
    for (i = 0; val_array[i] != NULL && matched != false; ++i) {
	val = val_array[i];
	sudo_debug_printf(SUDO_DEBUG_DEBUG, "val[%d]=%s", i, val);

	negated = sudo_ldap_is_negated(&val);

	/* match any or address or netgroup or hostname */
	if (strcmp(val, "ALL") == 0 || addr_matches(val) ||
	    netgr_matches(val, host, shost,
	    def_netgroup_tuple ? handle->pw->pw_name : NULL) ||
	    hostname_matches(shost, host, val)) {

	    matched = negated ? false : true;
	}

	sudo_debug_printf(SUDO_DEBUG_INFO, "sssd/ldap sudoHost '%s' ... %s",
	    val, matched == true ? "MATCH!" : "not");
    }

    handle->fn_free_values(val_array);

    debug_return_bool(matched == true);
}

/*
 * SSSD doesn't handle netgroups, we have to ensure they are correctly filtered
 * in sudo. The rules may contain mixed sudoUser specification so we have to
 * check not only for netgroup membership but also for user and group matches.
 */
static bool
sudo_sss_check_user(struct sudo_sss_handle *handle, struct sss_sudo_rule *rule)
{
    const char *host = handle->ipa_host ? handle->ipa_host : user_runhost;
    const char *shost = handle->ipa_shost ? handle->ipa_shost : user_srunhost;
    char **val_array;
    int i, ret = false;
    debug_decl(sudo_sss_check_user, SUDOERS_DEBUG_SSSD);

    if (rule == NULL)
	debug_return_bool(false);

    switch (handle->fn_get_values(rule, "sudoUser", &val_array)) {
    case 0:
	break;
    case ENOENT:
	sudo_debug_printf(SUDO_DEBUG_INFO, "No result.");
	debug_return_bool(false);
    default:
	sudo_debug_printf(SUDO_DEBUG_INFO,
	    "handle->fn_get_values(sudoUser): != 0");
	debug_return_bool(false);
    }

    /* Walk through sudoUser values.  */
    for (i = 0; val_array[i] != NULL && !ret; ++i) {
	const char *val = val_array[i];

	sudo_debug_printf(SUDO_DEBUG_DEBUG, "val[%d]=%s", i, val);
	switch (*val) {
	case '+':
	    /* Netgroup spec found, check membership. */
	    if (netgr_matches(val, def_netgroup_tuple ? host : NULL,
		def_netgroup_tuple ? shost : NULL, handle->pw->pw_name)) {
		ret = true;
	    }
	    break;
	case '%':
	    /* User group found, check membership. */
	    if (usergr_matches(val, handle->pw->pw_name, handle->pw)) {
		ret = true;
	    }
	    break;
	default:
	    /* Not a netgroup or user group. */
	    if (strcmp(val, "ALL") == 0 ||
		userpw_matches(val, handle->pw->pw_name, handle->pw)) {
		ret = true;
	    }
	    break;
	}
	sudo_debug_printf(SUDO_DEBUG_DIAG,
	    "sssd/ldap sudoUser '%s' ... %s (%s)", val,
	    ret ? "MATCH!" : "not", handle->pw->pw_name);
    }
    handle->fn_free_values(val_array);
    debug_return_bool(ret);
}

/* XXX - insted of filtering result, include user and host in sudoers parse tree */
static int
sudo_sss_result_filterp(struct sudo_sss_handle *handle,
    struct sss_sudo_rule *rule, void *unused)
{
    (void)unused;
    debug_decl(sudo_sss_result_filterp, SUDOERS_DEBUG_SSSD);

    if (sudo_sss_check_host(handle, rule) &&
        sudo_sss_check_user(handle, rule))
	debug_return_int(1);
    else
	debug_return_int(0);
}

static struct sss_sudo_result *
sudo_sss_result_get(struct sudo_nss *nss, struct passwd *pw)
{
    struct sudo_sss_handle *handle = nss->handle;
    struct sss_sudo_result *u_sss_result, *f_sss_result;
    uint32_t sss_error = 0, ret;
    debug_decl(sudo_sss_result_get, SUDOERS_DEBUG_SSSD);

    sudo_debug_printf(SUDO_DEBUG_DIAG, "  username=%s", pw->pw_name);
    sudo_debug_printf(SUDO_DEBUG_DIAG, "domainname=%s",
	handle->domainname ? handle->domainname : "NULL");

    u_sss_result = f_sss_result = NULL;

    ret = handle->fn_send_recv(pw->pw_uid, pw->pw_name,
	handle->domainname, &sss_error, &u_sss_result);

    switch (ret) {
    case 0:
	switch (sss_error) {
	case 0:
	    if (u_sss_result != NULL) {
		sudo_debug_printf(SUDO_DEBUG_INFO, "Received %u rule(s)",
		    u_sss_result->num_rules);
	    } else {
		sudo_debug_printf(SUDO_DEBUG_INFO,
		    "Internal error: u_sss_result == NULL && sss_error == 0");
		debug_return_ptr(NULL);
	    }
	    break;
	case ENOENT:
	    sudo_debug_printf(SUDO_DEBUG_INFO, "The user was not found in SSSD.");
	    debug_return_ptr(NULL);
	default:
	    sudo_debug_printf(SUDO_DEBUG_INFO, "sss_error=%u\n", sss_error);
	    debug_return_ptr(NULL);
	}
	break;
    default:
	sudo_debug_printf(SUDO_DEBUG_INFO,
	    "handle->fn_send_recv: != 0: ret=%d", ret);
	debug_return_ptr(NULL);
    }

    /* XXX - insted of filtering result, include user and host in sudoers parse tree */
    f_sss_result = sudo_sss_filter_result(handle, u_sss_result,
	sudo_sss_result_filterp, SUDO_SSS_FILTER_INCLUDE, NULL);

    if (f_sss_result != NULL) {
	sudo_debug_printf(SUDO_DEBUG_DEBUG,
	    "u_sss_result=(%p, %u) => f_sss_result=(%p, %u)", u_sss_result,
	    u_sss_result->num_rules, f_sss_result, f_sss_result->num_rules);
    } else {
	sudo_debug_printf(SUDO_DEBUG_DEBUG,
	    "u_sss_result=(%p, %u) => f_sss_result=NULL", u_sss_result,
	    u_sss_result->num_rules);
    }

    handle->fn_free_result(u_sss_result);

    debug_return_ptr(f_sss_result);
}

static bool
sudo_sss_parse_options(struct sudo_sss_handle *handle, struct sss_sudo_rule *rule, struct defaults_list *defs)
{
    int i;
    char *source = NULL;
    bool ret = false;
    char **val_array = NULL;
    char **cn_array = NULL;
    debug_decl(sudo_sss_parse_options, SUDOERS_DEBUG_SSSD);

    if (rule == NULL)
	debug_return_bool(true);

    switch (handle->fn_get_values(rule, "sudoOption", &val_array)) {
    case 0:
	break;
    case ENOENT:
	sudo_debug_printf(SUDO_DEBUG_INFO, "No result.");
	debug_return_bool(true);
    default:
	sudo_debug_printf(SUDO_DEBUG_INFO, "handle->fn_get_values(sudoOption): != 0");
	debug_return_bool(false);
    }

    /* Use sudoRole in place of file name in defaults. */
    if (handle->fn_get_values(rule, "cn", &cn_array) == 0) {
	if (cn_array[0] != NULL) {
	    char *cp;
	    if (asprintf(&cp, "sudoRole %s", cn_array[0]) == -1)
		goto oom;
	    source = rcstr_dup(cp);
	    free(cp);
	    if (source == NULL)
		goto oom;
	}
	handle->fn_free_values(cn_array);
	cn_array = NULL;
    }
    if (source == NULL) {
	if ((source = rcstr_dup("sudoRole UNKNOWN")) == NULL)
	    goto oom;
    }

    /* Walk through options, appending to defs. */
    for (i = 0; val_array[i] != NULL; i++) {
	char *copy, *var, *val;
	int op;

	/* XXX - should not need to copy */
	if ((copy = strdup(val_array[i])) == NULL)
	    goto oom;
	op = sudo_ldap_parse_option(copy, &var, &val);
	if (!sudo_ldap_add_default(var, val, op, source, defs)) {
	    free(copy);
	    goto oom;
	}
	free(copy);
    }
    ret = true;
    goto done;

oom:
    sudo_warnx(U_("%s: %s"), __func__, U_("unable to allocate memory"));

done:
    rcstr_delref(source);
    handle->fn_free_values(val_array);
    debug_return_bool(ret);
}

static char *
val_array_iter(void **vp)
{
    char **val_array = *vp;

    *vp = val_array + 1;

    return *val_array;
}

static bool
sss_to_sudoers(struct sudo_sss_handle *handle, struct sss_sudo_result *sss_result, struct userspec_list *sss_userspecs)
{
    struct userspec *us;
    struct member *m;
    unsigned int i;
    debug_decl(sss_to_sudoers, SUDOERS_DEBUG_SSSD)

    /* We only have a single userspec */
    if ((us = calloc(1, sizeof(*us))) == NULL)
	goto oom;
    TAILQ_INIT(&us->users);
    TAILQ_INIT(&us->privileges);
    STAILQ_INIT(&us->comments);
    TAILQ_INSERT_TAIL(sss_userspecs, us, entries);

    /* The user has already matched, use ALL as wildcard. */
    /* XXX - remove filtering and include sudoUser and host in userspec */
    if ((m = calloc(1, sizeof(*m))) == NULL)
	goto oom;
    m->type = ALL;
    TAILQ_INSERT_TAIL(&us->users, m, entries);

    /* Treat each sudoRole as a separate privilege. */
    for (i = 0; i < sss_result->num_rules; i++) {
	struct sss_sudo_rule *rule = sss_result->rules + i;
	char **cmnds, **runasusers = NULL, **runasgroups = NULL;
	char **opts = NULL, **notbefore = NULL, **notafter = NULL;
	char **cn_array = NULL;
	char *cn = NULL;
	struct privilege *priv;

	/* XXX - check for error vs. ENOENT */

	/* Ignore sudoRole without sudoCommand. */
	if (handle->fn_get_values(rule, "sudoCommand", &cmnds) != 0)
	    continue;

	/* Get the entry's dn for long format printing. */
	if (handle->fn_get_values(rule, "cn", &cn_array) == 0)
	    cn = cn_array[0];

	/* Get sudoRunAsUser / sudoRunAsGroup */
	if (handle->fn_get_values(rule, "sudoRunAsUser", &runasusers) != 0) {
	    handle->fn_get_values(rule, "sudoRunAs", &runasusers);
	}
	handle->fn_get_values(rule, "sudoRunAsGroup", &runasgroups);

	/* Get sudoNotBefore / sudoNotAfter */
	handle->fn_get_values(rule, "sudoNotBefore", &notbefore);
	handle->fn_get_values(rule, "sudoNotAfter", &notafter);

	/* Parse sudoOptions. */
	handle->fn_get_values(rule, "sudoOption", &opts);

	priv = sudo_ldap_role_to_priv(cn, NULL, runasusers, runasgroups,
	    cmnds, opts, notbefore ? notbefore[0] : NULL,
	    notafter ? notafter[0] : NULL, false, long_list, val_array_iter);

	/* Cleanup */
	if (cn_array != NULL)
	    handle->fn_free_values(cn_array);
	if (cmnds != NULL)
	    handle->fn_free_values(cmnds);
	if (runasusers != NULL)
	    handle->fn_free_values(runasusers);
	if (runasgroups != NULL)
	    handle->fn_free_values(runasgroups);
	if (opts != NULL)
	    handle->fn_free_values(opts);
	if (notbefore != NULL)
	    handle->fn_free_values(notbefore);
	if (notafter != NULL)
	    handle->fn_free_values(notafter);

	if (priv == NULL)
	    goto oom;
	TAILQ_INSERT_TAIL(&us->privileges, priv, entries);
    }

    debug_return_bool(true);

oom:
    sudo_warnx(U_("%s: %s"), __func__, U_("unable to allocate memory"));
    while ((us = TAILQ_FIRST(sss_userspecs)) != NULL) {
	TAILQ_REMOVE(sss_userspecs, us, entries);
	free_userspec(us);
    }
    debug_return_bool(false);
}
#endif /* HAVE_SSSD */
