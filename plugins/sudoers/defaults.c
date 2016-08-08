/*
 * Copyright (c) 1999-2005, 2007-2016
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
#include <unistd.h>
#include <pwd.h>
#include <ctype.h>

#include "sudoers.h"
#include "parse.h"
#include <gram.h>

/*
 * For converting between syslog numbers and strings.
 */
struct strmap {
    char *name;
    int num;
};

static struct strmap facilities[] = {
#ifdef LOG_AUTHPRIV
	{ "authpriv",	LOG_AUTHPRIV },
#endif
	{ "auth",	LOG_AUTH },
	{ "daemon",	LOG_DAEMON },
	{ "user",	LOG_USER },
	{ "local0",	LOG_LOCAL0 },
	{ "local1",	LOG_LOCAL1 },
	{ "local2",	LOG_LOCAL2 },
	{ "local3",	LOG_LOCAL3 },
	{ "local4",	LOG_LOCAL4 },
	{ "local5",	LOG_LOCAL5 },
	{ "local6",	LOG_LOCAL6 },
	{ "local7",	LOG_LOCAL7 },
	{ NULL,		-1 }
};

static struct strmap priorities[] = {
	{ "alert",	LOG_ALERT },
	{ "crit",	LOG_CRIT },
	{ "debug",	LOG_DEBUG },
	{ "emerg",	LOG_EMERG },
	{ "err",	LOG_ERR },
	{ "info",	LOG_INFO },
	{ "notice",	LOG_NOTICE },
	{ "warning",	LOG_WARNING },
	{ NULL,		-1 }
};

/*
 * Defaults values to apply before others.
 */
struct early_default {
    const char *var;
    const char *val;
    short op;
    short set;
};

static struct early_default early_defaults[] = {
#ifdef FQDN
    { "fqdn", NULL, true },
#else
    { "fqdn" },
#endif
    { "runas_default" },
    { "sudoers_locale" },
    { NULL }
};

/*
 * Local prototypes.
 */
static bool store_int(const char *, struct sudo_defs_types *, int);
static bool store_list(const char *, struct sudo_defs_types *, int);
static bool store_mode(const char *, struct sudo_defs_types *, int);
static int  store_str(const char *, struct sudo_defs_types *, int);
static bool store_syslogfac(const char *, struct sudo_defs_types *, int);
static bool store_syslogpri(const char *, struct sudo_defs_types *, int);
static bool store_tuple(const char *, struct sudo_defs_types *, int);
static bool store_uint(const char *, struct sudo_defs_types *, int);
static bool store_float(const char *, struct sudo_defs_types *, int);
static bool list_op(const char *, size_t, struct sudo_defs_types *, enum list_ops);
static const char *logfac2str(int);
static const char *logpri2str(int);

/*
 * Table describing compile-time and run-time options.
 */
#include <def_data.c>

/*
 * Print version and configure info.
 */
void
dump_defaults(void)
{
    struct sudo_defs_types *cur;
    struct list_member *item;
    struct def_values *def;
    char *desc;
    debug_decl(dump_defaults, SUDOERS_DEBUG_DEFAULTS)

    for (cur = sudo_defs_table; cur->name; cur++) {
	if (cur->desc) {
	    desc = _(cur->desc);
	    switch (cur->type & T_MASK) {
		case T_FLAG:
		    if (cur->sd_un.flag)
			sudo_printf(SUDO_CONV_INFO_MSG, "%s\n", desc);
		    break;
		case T_STR:
		    if (cur->sd_un.str) {
			sudo_printf(SUDO_CONV_INFO_MSG, desc, cur->sd_un.str);
			sudo_printf(SUDO_CONV_INFO_MSG, "\n");
		    }
		    break;
		case T_LOGFAC:
		    if (cur->sd_un.ival) {
			sudo_printf(SUDO_CONV_INFO_MSG, desc,
			    logfac2str(cur->sd_un.ival));
			sudo_printf(SUDO_CONV_INFO_MSG, "\n");
		    }
		    break;
		case T_LOGPRI:
		    if (cur->sd_un.ival) {
			sudo_printf(SUDO_CONV_INFO_MSG, desc,
			    logpri2str(cur->sd_un.ival));
			sudo_printf(SUDO_CONV_INFO_MSG, "\n");
		    }
		    break;
		case T_INT:
		    sudo_printf(SUDO_CONV_INFO_MSG, desc, cur->sd_un.ival);
		    sudo_printf(SUDO_CONV_INFO_MSG, "\n");
		    break;
		case T_UINT:
		    sudo_printf(SUDO_CONV_INFO_MSG, desc, cur->sd_un.uival);
		    sudo_printf(SUDO_CONV_INFO_MSG, "\n");
		    break;
		case T_FLOAT:
		    sudo_printf(SUDO_CONV_INFO_MSG, desc, cur->sd_un.fval);
		    sudo_printf(SUDO_CONV_INFO_MSG, "\n");
		    break;
		case T_MODE:
		    sudo_printf(SUDO_CONV_INFO_MSG, desc, cur->sd_un.mode);
		    sudo_printf(SUDO_CONV_INFO_MSG, "\n");
		    break;
		case T_LIST:
		    if (!SLIST_EMPTY(&cur->sd_un.list)) {
			sudo_printf(SUDO_CONV_INFO_MSG, "%s\n", desc);
			SLIST_FOREACH(item, &cur->sd_un.list, entries) {
			    sudo_printf(SUDO_CONV_INFO_MSG,
				"\t%s\n", item->value);
			}
		    }
		    break;
		case T_TUPLE:
		    for (def = cur->values; def->sval; def++) {
			if (cur->sd_un.tuple == def->nval) {
			    sudo_printf(SUDO_CONV_INFO_MSG, desc, def->sval);
			    break;
			}
		    }
		    sudo_printf(SUDO_CONV_INFO_MSG, "\n");
		    break;
	    }
	}
    }
    debug_return;
}

static bool
set_default_entry(struct sudo_defs_types *def, const char *val, int op,
    bool quiet)
{
    debug_decl(set_default_entry, SUDOERS_DEBUG_DEFAULTS)

    switch (def->type & T_MASK) {
	case T_LOGFAC:
	    if (!store_syslogfac(val, def, op)) {
		if (!quiet) {
		    if (val)
			sudo_warnx(U_("value `%s' is invalid for option `%s'"),
			    val, def->name);
		    else
			sudo_warnx(U_("no value specified for `%s'"), def->name);
		}
		debug_return_bool(false);
	    }
	    break;
	case T_LOGPRI:
	    if (!store_syslogpri(val, def, op)) {
		if (!quiet) {
		    if (val)
			sudo_warnx(U_("value `%s' is invalid for option `%s'"),
			    val, def->name);
		    else
			sudo_warnx(U_("no value specified for `%s'"), def->name);
		}
		debug_return_bool(false);
	    }
	    break;
	case T_STR:
	    if (!val) {
		/* Check for bogus boolean usage or lack of a value. */
		if (!ISSET(def->type, T_BOOL) || op != false) {
		    if (!quiet)
			sudo_warnx(U_("no value specified for `%s'"), def->name);
		    debug_return_bool(false);
		}
	    }
	    if (ISSET(def->type, T_PATH) && val && *val != '/') {
		if (!quiet) {
		    sudo_warnx(U_("values for `%s' must start with a '/'"),
			def->name);
		}
		debug_return_bool(false);
	    }
	    switch (store_str(val, def, op)) {
	    case true:
		/* OK */
		break;
	    case false:
		if (!quiet) {
		    sudo_warnx(U_("value `%s' is invalid for option `%s'"),
			val, def->name);
		}
		/* FALLTHROUGH */
	    default:
		debug_return_bool(false);
	    }
	    break;
	case T_INT:
	    if (!val) {
		/* Check for bogus boolean usage or lack of a value. */
		if (!ISSET(def->type, T_BOOL) || op != false) {
		    if (!quiet)
			sudo_warnx(U_("no value specified for `%s'"), def->name);
		    debug_return_bool(false);
		}
	    }
	    if (!store_int(val, def, op)) {
		if (!quiet) {
		    sudo_warnx(U_("value `%s' is invalid for option `%s'"),
			val, def->name);
		}
		debug_return_bool(false);
	    }
	    break;
	case T_UINT:
	    if (!val) {
		/* Check for bogus boolean usage or lack of a value. */
		if (!ISSET(def->type, T_BOOL) || op != false) {
		    if (!quiet)
			sudo_warnx(U_("no value specified for `%s'"), def->name);
		    debug_return_bool(false);
		}
	    }
	    if (!store_uint(val, def, op)) {
		if (!quiet) {
		    sudo_warnx(U_("value `%s' is invalid for option `%s'"),
			val, def->name);
		}
		debug_return_bool(false);
	    }
	    break;
	case T_FLOAT:
	    if (!val) {
		/* Check for bogus boolean usage or lack of a value. */
		if (!ISSET(def->type, T_BOOL) || op != false) {
		    if (!quiet)
			sudo_warnx(U_("no value specified for `%s'"), def->name);
		    debug_return_bool(false);
		}
	    }
	    if (!store_float(val, def, op)) {
		if (!quiet) {
		    sudo_warnx(U_("value `%s' is invalid for option `%s'"),
			val, def->name);
		}
		debug_return_bool(false);
	    }
	    break;
	case T_MODE:
	    if (!val) {
		/* Check for bogus boolean usage or lack of a value. */
		if (!ISSET(def->type, T_BOOL) || op != false) {
		    if (!quiet)
			sudo_warnx(U_("no value specified for `%s'"), def->name);
		    debug_return_bool(false);
		}
	    }
	    if (!store_mode(val, def, op)) {
		if (!quiet) {
		    sudo_warnx(U_("value `%s' is invalid for option `%s'"),
			val, def->name);
		}
		debug_return_bool(false);
	    }
	    break;
	case T_FLAG:
	    if (val) {
		if (!quiet) {
		    sudo_warnx(U_("option `%s' does not take a value"),
			def->name);
		}
		debug_return_bool(false);
	    }
	    def->sd_un.flag = op;
	    break;
	case T_LIST:
	    if (!val) {
		/* Check for bogus boolean usage or lack of a value. */
		if (!ISSET(def->type, T_BOOL) || op != false) {
		    if (!quiet)
			sudo_warnx(U_("no value specified for `%s'"), def->name);
		    debug_return_bool(false);
		}
	    }
	    if (!store_list(val, def, op)) {
		if (!quiet) {
		    sudo_warnx(U_("value `%s' is invalid for option `%s'"),
			val, def->name);
		}
		debug_return_bool(false);
	    }
	    break;
	case T_TUPLE:
	    if (!val && !ISSET(def->type, T_BOOL)) {
		if (!quiet)
		    sudo_warnx(U_("no value specified for `%s'"), def->name);
		debug_return_bool(false);
	    }
	    if (!store_tuple(val, def, op)) {
		if (!quiet) {
		    sudo_warnx(U_("value `%s' is invalid for option `%s'"),
			val, def->name);
		}
		debug_return_bool(false);
	    }
	    break;
    }
    if (def->callback)
	debug_return_bool(def->callback(&def->sd_un));

    debug_return_bool(true);
}

/*
 * Sets/clears an entry in the defaults structure
 * If a variable that takes a value is used in a boolean
 * context with op == 0, disable that variable.
 * Eg. you may want to turn off logging to a file for some hosts.
 * This is only meaningful for variables that are *optional*.
 */
bool
set_default(const char *var, const char *val, int op, bool quiet)
{
    struct sudo_defs_types *cur;
    int num;
    debug_decl(set_default, SUDOERS_DEBUG_DEFAULTS)

    for (cur = sudo_defs_table, num = 0; cur->name; cur++, num++) {
	if (strcmp(var, cur->name) == 0)
	    break;
    }
    if (!cur->name) {
	if (!quiet)
	    sudo_warnx(U_("unknown defaults entry `%s'"), var);
	debug_return_bool(false);
    }

    debug_return_bool(set_default_entry(cur, val, op, quiet));
}

/*
 * Set default options to compiled-in values.
 * Any of these may be overridden at runtime by a "Defaults" file.
 */
bool
init_defaults(void)
{
    static int firsttime = 1;
    struct sudo_defs_types *def;
    debug_decl(init_defaults, SUDOERS_DEBUG_DEFAULTS)

    /* Clear any old settings. */
    if (!firsttime) {
	for (def = sudo_defs_table; def->name; def++) {
	    switch (def->type & T_MASK) {
		case T_STR:
		    free(def->sd_un.str);
		    def->sd_un.str = NULL;
		    break;
		case T_LIST:
		    (void)list_op(NULL, 0, def, freeall);
		    break;
	    }
	    memset(&def->sd_un, 0, sizeof(def->sd_un));
	}
    }

    /* First initialize the flags. */
#ifdef LONG_OTP_PROMPT
    def_long_otp_prompt = true;
#endif
#ifdef IGNORE_DOT_PATH
    def_ignore_dot = true;
#endif
#ifdef ALWAYS_SEND_MAIL
    def_mail_always = true;
#endif
#ifdef SEND_MAIL_WHEN_NO_USER
    def_mail_no_user = true;
#endif
#ifdef SEND_MAIL_WHEN_NO_HOST
    def_mail_no_host = true;
#endif
#ifdef SEND_MAIL_WHEN_NOT_OK
    def_mail_no_perms = true;
#endif
#ifndef NO_TTY_TICKETS
    def_tty_tickets = true;
#endif
#ifndef NO_LECTURE
    def_lecture = once;
#endif
#ifndef NO_AUTHENTICATION
    def_authenticate = true;
#endif
#ifndef NO_ROOT_SUDO
    def_root_sudo = true;
#endif
#ifdef HOST_IN_LOG
    def_log_host = true;
#endif
#ifdef SHELL_IF_NO_ARGS
    def_shell_noargs = true;
#endif
#ifdef SHELL_SETS_HOME
    def_set_home = true;
#endif
#ifndef DONT_LEAK_PATH_INFO
    def_path_info = true;
#endif
#ifdef USE_INSULTS
    def_insults = true;
#endif
#ifdef ENV_EDITOR
    def_env_editor = true;
#endif
#ifdef UMASK_OVERRIDE
    def_umask_override = true;
#endif
    if ((def_iolog_file = strdup("%{seq}")) == NULL)
	goto oom;
    if ((def_iolog_dir = strdup(_PATH_SUDO_IO_LOGDIR)) == NULL)
	goto oom;
    if ((def_sudoers_locale = strdup("C")) == NULL)
	goto oom;
    def_env_reset = ENV_RESET;
    def_set_logname = true;
    def_closefrom = STDERR_FILENO + 1;
    if ((def_pam_service = strdup("sudo")) == NULL)
	goto oom;
#ifdef HAVE_PAM_LOGIN
    if ((def_pam_login_service = strdup("sudo-i")) == NULL)
	goto oom;
#else
    if ((def_pam_login_service = strdup("sudo")) == NULL)
	goto oom;
#endif
#ifdef NO_PAM_SESSION
    def_pam_session = false;
#else
    def_pam_session = true;
#endif
#ifdef HAVE_INNETGR
    def_use_netgroups = true;
#endif
    def_netgroup_tuple = false;
    def_sudoedit_checkdir = true;

    /* Syslog options need special care since they both strings and ints */
#if (LOGGING & SLOG_SYSLOG)
    (void) store_syslogfac(LOGFAC, &sudo_defs_table[I_SYSLOG], true);
    (void) store_syslogpri(PRI_SUCCESS, &sudo_defs_table[I_SYSLOG_GOODPRI],
	true);
    (void) store_syslogpri(PRI_FAILURE, &sudo_defs_table[I_SYSLOG_BADPRI],
	true);
#endif

    /* Password flags also have a string and integer component. */
    (void) store_tuple("any", &sudo_defs_table[I_LISTPW], true);
    (void) store_tuple("all", &sudo_defs_table[I_VERIFYPW], true);

    /* Then initialize the int-like things. */
#ifdef SUDO_UMASK
    def_umask = SUDO_UMASK;
#else
    def_umask = 0777;
#endif
    def_loglinelen = MAXLOGFILELEN;
    def_timestamp_timeout = TIMEOUT;
    def_passwd_timeout = PASSWORD_TIMEOUT;
    def_passwd_tries = TRIES_FOR_PASSWORD;
#ifdef HAVE_ZLIB_H
    def_compress_io = true;
#endif

    /* Now do the strings */
    if ((def_mailto = strdup(MAILTO)) == NULL)
	goto oom;
    if ((def_mailsub = strdup(N_(MAILSUBJECT))) == NULL)
	goto oom;
    if ((def_badpass_message = strdup(_(INCORRECT_PASSWORD))) == NULL)
	goto oom;
    if ((def_lecture_status_dir = strdup(_PATH_SUDO_LECTURE_DIR)) == NULL)
	goto oom;
    if ((def_timestampdir = strdup(_PATH_SUDO_TIMEDIR)) == NULL)
	goto oom;
    if ((def_passprompt = strdup(_(PASSPROMPT))) == NULL)
	goto oom;
    if ((def_runas_default = strdup(RUNAS_DEFAULT)) == NULL)
	goto oom;
#ifdef _PATH_SUDO_SENDMAIL
    if ((def_mailerpath = strdup(_PATH_SUDO_SENDMAIL)) == NULL)
	goto oom;
    if ((def_mailerflags = strdup("-t")) == NULL)
	goto oom;
#endif
#if (LOGGING & SLOG_FILE)
    if ((def_logfile = strdup(_PATH_SUDO_LOGFILE)) == NULL)
	goto oom;
#endif
#ifdef EXEMPTGROUP
    if ((def_exempt_group = strdup(EXEMPTGROUP)) == NULL)
	goto oom;
#endif
#ifdef SECURE_PATH
    if ((def_secure_path = strdup(SECURE_PATH)) == NULL)
	goto oom;
#endif
    if ((def_editor = strdup(EDITOR)) == NULL)
	goto oom;
    def_set_utmp = true;
    def_pam_setcred = true;

    /* Reset the locale. */
    if (!firsttime)
	sudoers_initlocale(NULL, def_sudoers_locale);

    /* Finally do the lists (currently just environment tables). */
    if (!init_envtables())
	goto oom;

    firsttime = 0;

    debug_return_bool(true);
oom:
    sudo_warnx(U_("%s: %s"), __func__, U_("unable to allocate memory"));
    debug_return_bool(false);
}

/*
 * Check whether a defaults entry matches the specified type.
 * Returns true if it matches, else false.
 */
static bool
default_type_matches(struct defaults *def, int what)
{
    debug_decl(default_type_matches, SUDOERS_DEBUG_DEFAULTS)

    switch (def->type) {
    case DEFAULTS:
	if (ISSET(what, SETDEF_GENERIC))
	    debug_return_bool(true);
	break;
    case DEFAULTS_USER:
	if (ISSET(what, SETDEF_USER))
	    debug_return_bool(true);
	break;
    case DEFAULTS_RUNAS:
	if (ISSET(what, SETDEF_RUNAS))
	    debug_return_bool(true);
	break;
    case DEFAULTS_HOST:
	if (ISSET(what, SETDEF_HOST))
	    debug_return_bool(true);
	break;
    case DEFAULTS_CMND:
	if (ISSET(what, SETDEF_CMND))
	    debug_return_bool(true);
	break;
    }
    debug_return_bool(false);
}

/*
 * Check whether a defaults entry's binding matches.
 * Returns true if it matches, else false.
 */
static bool
default_binding_matches(struct defaults *def, int what)
{
    debug_decl(default_binding_matches, SUDOERS_DEBUG_DEFAULTS)

    switch (def->type) {
    case DEFAULTS:
	debug_return_bool(true);
	break;
    case DEFAULTS_USER:
	if (userlist_matches(sudo_user.pw, def->binding) == ALLOW)
	    debug_return_bool(true);
	break;
    case DEFAULTS_RUNAS:
	if (runaslist_matches(def->binding, NULL, NULL, NULL) == ALLOW)
	    debug_return_bool(true);
	break;
    case DEFAULTS_HOST:
	if (hostlist_matches(sudo_user.pw, def->binding) == ALLOW)
	    debug_return_bool(true);
	break;
    case DEFAULTS_CMND:
	if (cmndlist_matches(def->binding) == ALLOW)
	    debug_return_bool(true);
	break;
    }
    debug_return_bool(false);
}

bool
is_early_default(const char *var)
{
    struct early_default *early;
    debug_decl(is_early_default, SUDOERS_DEBUG_DEFAULTS)

    for (early = early_defaults; early->var != NULL; early++) {
	if (strcmp(var, early->var) == 0)
	    debug_return_bool(true);
    }
    debug_return_bool(false);
}

bool
store_early_default(struct defaults *def, int what)
{
    struct early_default *early;
    debug_decl(store_early_default, SUDOERS_DEBUG_DEFAULTS)

    for (early = early_defaults; early->var != NULL; early++) {
	if (strcmp(def->var, early->var) == 0) {
	    if (default_type_matches(def, what) &&
		default_binding_matches(def, what)) {
		early->val = def->val;
		early->op = def->op;
		early->set = true;
	    }
	    debug_return_bool(true);
	}
    }
    debug_return_bool(false);
}

bool
apply_early_defaults(bool quiet)
{
    struct early_default *early;
    bool rc = true;
    debug_decl(apply_early_defaults, SUDOERS_DEBUG_DEFAULTS)

    for (early = early_defaults; early->var != NULL; early++) {
	if (early->set) {
	    if (!set_default(early->var, early->val, early->op, quiet))
		rc = false;
	    early->set = false;		/* clean state for next run */
	}
    }
    debug_return_bool(rc);
}

/*
 * Update the defaults based on what was set by sudoers.
 * Pass in an OR'd list of which default types to update.
 */
bool
update_defaults(int what, bool quiet)
{
    struct defaults *def;
    bool rc = true;
    debug_decl(update_defaults, SUDOERS_DEBUG_DEFAULTS)

    sudo_debug_printf(SUDO_DEBUG_INFO|SUDO_DEBUG_LINENO,
	"what: 0x%02x", what);

    /*
     * First apply Defaults values marked as early.
     * We only set early Defaults once (the last instance).
     */
    TAILQ_FOREACH(def, &defaults, entries)
	store_early_default(def, what);
    apply_early_defaults(quiet);

    /*
     * Then set the rest of the defaults.
     */
    TAILQ_FOREACH(def, &defaults, entries) {
	/* Skip Defaults marked as early, we already did them. */
	if (is_early_default(def->var))
	    continue;

	if (!default_type_matches(def, what) ||
	    !default_binding_matches(def, what))
	    continue;
	if (!set_default(def->var, def->val, def->op, quiet))
	    rc = false;
    }
    debug_return_bool(rc);
}

/*
 * Check the defaults entries without actually setting them.
 * Pass in an OR'd list of which default types to check.
 */
bool
check_defaults(int what, bool quiet)
{
    struct sudo_defs_types *cur, tmp;
    struct defaults *def;
    bool rc = true;
    debug_decl(check_defaults, SUDOERS_DEBUG_DEFAULTS)

    TAILQ_FOREACH(def, &defaults, entries) {
	if (!default_type_matches(def, what))
	    continue;
	for (cur = sudo_defs_table; cur->name != NULL; cur++) {
	    if (strcmp(def->var, cur->name) == 0)
		break;
	}
	if (cur->name == NULL) {
	    if (!quiet)
		sudo_warnx(U_("unknown defaults entry `%s'"), def->var);
	    rc = false;
	}
	/* Don't actually set the defaults value, just checking. */
	tmp = *cur;
	tmp.callback = NULL;
	if (!set_default_entry(&tmp, def->val, def->op, quiet))
	    rc = false;
    }
    debug_return_bool(rc);
}

static bool
store_int(const char *val, struct sudo_defs_types *def, int op)
{
    const char *errstr;
    int i;
    debug_decl(store_int, SUDOERS_DEBUG_DEFAULTS)

    if (op == false) {
	def->sd_un.ival = 0;
    } else {
	i = strtonum(val, INT_MIN, INT_MAX, &errstr);
	if (errstr != NULL) {
	    sudo_debug_printf(SUDO_DEBUG_ERROR|SUDO_DEBUG_LINENO,
		"%s: %s", val, errstr);
	    debug_return_bool(false);
	}
	def->sd_un.ival = i;
    }
    debug_return_bool(true);
}

static bool
store_uint(const char *val, struct sudo_defs_types *def, int op)
{
    const char *errstr;
    unsigned int u;
    debug_decl(store_uint, SUDOERS_DEBUG_DEFAULTS)

    if (op == false) {
	def->sd_un.uival = 0;
    } else {
	u = strtonum(val, 0, UINT_MAX, &errstr);
	if (errstr != NULL) {
	    sudo_debug_printf(SUDO_DEBUG_ERROR|SUDO_DEBUG_LINENO,
		"%s: %s", val, errstr);
	    debug_return_bool(false);
	}
	def->sd_un.uival = u;
    }
    debug_return_bool(true);
}

static bool
store_float(const char *val, struct sudo_defs_types *def, int op)
{
    char *endp;
    double d;
    debug_decl(store_float, SUDOERS_DEBUG_DEFAULTS)

    if (op == false) {
	def->sd_un.fval = 0.0;
    } else {
	d = strtod(val, &endp);
	if (*endp != '\0')
	    debug_return_bool(false);
	/* XXX - should check against HUGE_VAL */
	def->sd_un.fval = d;
    }
    debug_return_bool(true);
}

static bool
store_tuple(const char *val, struct sudo_defs_types *def, int op)
{
    struct def_values *v;
    debug_decl(store_tuple, SUDOERS_DEBUG_DEFAULTS)

    /*
     * Look up tuple value by name to find enum def_tuple value.
     * For negation to work the first element of enum def_tuple
     * must be equivalent to boolean false.
     */
    if (!val) {
	def->sd_un.ival = (op == false) ? 0 : 1;
    } else {
	for (v = def->values; v->sval != NULL; v++) {
	    if (strcmp(v->sval, val) == 0) {
		def->sd_un.tuple = v->nval;
		break;
	    }
	}
	if (v->sval == NULL)
	    debug_return_bool(false);
    }
    debug_return_bool(true);
}

static int
store_str(const char *val, struct sudo_defs_types *def, int op)
{
    debug_decl(store_str, SUDOERS_DEBUG_DEFAULTS)

    free(def->sd_un.str);
    if (op == false) {
	def->sd_un.str = NULL;
    } else {
	if ((def->sd_un.str = strdup(val)) == NULL) {
	    sudo_warnx(U_("%s: %s"), __func__, U_("unable to allocate memory"));
	    debug_return_int(-1);
	}
    }
    debug_return_int(true);
}

static bool
store_list(const char *str, struct sudo_defs_types *def, int op)
{
    const char *start, *end;
    debug_decl(store_list, SUDOERS_DEBUG_DEFAULTS)

    /* Remove all old members. */
    if (op == false || op == true)
	(void)list_op(NULL, 0, def, freeall);

    /* Split str into multiple space-separated words and act on each one. */
    if (op != false) {
	end = str;
	do {
	    /* Remove leading blanks, if nothing but blanks we are done. */
	    for (start = end; isblank((unsigned char)*start); start++)
		;
	    if (*start == '\0')
		break;

	    /* Find end position and perform operation. */
	    for (end = start; *end && !isblank((unsigned char)*end); end++)
		;
	    if (!list_op(start, end - start, def, op == '-' ? delete : add))
		debug_return_bool(false);
	} while (*end++ != '\0');
    }
    debug_return_bool(true);
}

static bool
store_syslogfac(const char *val, struct sudo_defs_types *def, int op)
{
    struct strmap *fac;
    debug_decl(store_syslogfac, SUDOERS_DEBUG_DEFAULTS)

    if (op == false) {
	def->sd_un.ival = false;
	debug_return_bool(true);
    }
    if (!val)
	debug_return_bool(false);
    for (fac = facilities; fac->name && strcmp(val, fac->name); fac++)
	;
    if (fac->name == NULL)
	debug_return_bool(false);		/* not found */

    def->sd_un.ival = fac->num;
    debug_return_bool(true);
}

static const char *
logfac2str(int n)
{
    struct strmap *fac;
    debug_decl(logfac2str, SUDOERS_DEBUG_DEFAULTS)

    for (fac = facilities; fac->name && fac->num != n; fac++)
	;
    debug_return_const_str(fac->name);
}

static bool
store_syslogpri(const char *val, struct sudo_defs_types *def, int op)
{
    struct strmap *pri;
    debug_decl(store_syslogpri, SUDOERS_DEBUG_DEFAULTS)

    if (op == false || !val)
	debug_return_bool(false);

    for (pri = priorities; pri->name && strcmp(val, pri->name); pri++)
	;
    if (pri->name == NULL)
	debug_return_bool(false); 	/* not found */

    def->sd_un.ival = pri->num;
    debug_return_bool(true);
}

static const char *
logpri2str(int n)
{
    struct strmap *pri;
    debug_decl(logpri2str, SUDOERS_DEBUG_DEFAULTS)

    for (pri = priorities; pri->name && pri->num != n; pri++)
	;
    debug_return_const_str(pri->name);
}

static bool
store_mode(const char *val, struct sudo_defs_types *def, int op)
{
    mode_t mode;
    const char *errstr;
    debug_decl(store_mode, SUDOERS_DEBUG_DEFAULTS)

    if (op == false) {
	def->sd_un.mode = 0777;
    } else {
	mode = sudo_strtomode(val, &errstr);
	if (errstr != NULL) {
	    sudo_debug_printf(SUDO_DEBUG_ERROR|SUDO_DEBUG_LINENO,
		"%s is %s", val, errstr);
	    debug_return_bool(false);
	}
	def->sd_un.mode = mode;
    }
    debug_return_bool(true);
}

static bool
list_op(const char *val, size_t len, struct sudo_defs_types *def, enum list_ops op)
{
    struct list_member *cur, *prev = NULL;
    debug_decl(list_op, SUDOERS_DEBUG_DEFAULTS)

    if (op == freeall) {
	while ((cur = SLIST_FIRST(&def->sd_un.list)) != NULL) {
	    SLIST_REMOVE_HEAD(&def->sd_un.list, entries);
	    free(cur->value);
	    free(cur);
	}
	debug_return_bool(true);
    }

    SLIST_FOREACH(cur, &def->sd_un.list, entries) {
	if ((strncmp(cur->value, val, len) == 0 && cur->value[len] == '\0')) {

	    if (op == add)
		debug_return_bool(true); /* already exists */

	    /* Delete node */
	    if (prev == NULL)
		SLIST_REMOVE_HEAD(&def->sd_un.list, entries);
	    else
		SLIST_REMOVE_AFTER(prev, entries);
	    free(cur->value);
	    free(cur);
	    break;
	}
	prev = cur;
    }

    /* Add new node to the head of the list. */
    if (op == add) {
	cur = calloc(1, sizeof(struct list_member));
	if (cur == NULL || (cur->value = strndup(val, len)) == NULL) {
	    sudo_warnx(U_("%s: %s"), __func__, U_("unable to allocate memory"));
	    free(cur);
	    debug_return_bool(false);
	}
	SLIST_INSERT_HEAD(&def->sd_un.list, cur, entries);
    }
    debug_return_bool(true);
}
