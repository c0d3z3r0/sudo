/*
 * Copyright (c) 1993-1996, 1998-2009 Todd C. Miller <Todd.Miller@courtesan.com>
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
#include <grp.h>

#include <sudo_usage.h>
#include "sudo.h"
#include "lbuf.h"

/* For getopt(3) */
extern char *optarg;
extern int optind;

extern struct user_details user_details;

/* XXX - better home for these and extern in header file */
int tgetpass_flags;
int user_closefrom = -1;
const char *list_user, *runas_user, *runas_group;

/*
 * Local functions.
 */
static void usage_excl(int) __attribute__((__noreturn__));

/*
 * Mapping of command line flags to name/value settings.
 */
static struct sudo_settings {
    const char *name;
    const char *value;
} sudo_settings[] = {
#define ARG_BSDAUTH_TYPE 0
    { "bsdauth_type" },
#define ARG_LOGIN_CLASS 1
    { "login_class" },
#define ARG_DEBUG_LEVEL 2
    { "debug_level" },
#define ARG_PRESERVE_ENVIRONMENT 3
    { "preserve_environment" },
#define ARG_RUNAS_GROUP 4
    { "runas_group" },
#define ARG_SET_HOME 5
    { "set_home" },
#define ARG_LOGIN_SHELL 6
    { "login_shell" },
#define ARG_IGNORE_TICKET 7
    { "ignore_ticket" },
#define ARG_PROMPT 8
    { "prompt" },
#define ARG_SELINUX_ROLE 9
    { "selinux_role" },
#define ARG_SELINUX_TYPE 10
    { "selinux_type" },
#define ARG_RUNAS_USER 11
    { "runas_user" },
#define ARG_PROGNAME 12
    { "progname" },
#define ARG_IMPLIED_SHELL 13
    { "implied_shell" },
#define ARG_PRESERVE_GROUPS 14
    { "preserve_groups" },
#define ARG_NONINTERACTIVE 15
    { "noninteractive" },
#define ARG_SUDOEDIT 16
    { "sudoedit" },
#define NUM_SETTINGS 17
    { NULL }
};

/*
 * Command line argument parsing.
 * Sets nargc and nargv which corresponds to the argc/argv we'll use
 * for the command to be run (if we are running one).
 */
int
parse_args(int argc, char **argv, int *nargc, char ***nargv, char ***settingsp,
    char ***env_addp)
{
    int mode = 0;		/* what mode is sudo to be run in? */
    int flags = 0;		/* mode flags */
    int valid_flags, ch;
    int i, j;
    char **settings;
    char **env_add;
    int nenv = 0;
    int env_size = 32;

    env_add = emalloc2(env_size, sizeof(char *));

    /* Pass progname to plugin so it can call setprogname() */
    sudo_settings[ARG_PROGNAME].value = getprogname();

    /* First, check to see if we were invoked as "sudoedit". */
    if (strcmp(getprogname(), "sudoedit") == 0) {
	mode = MODE_EDIT;
	sudo_settings[ARG_SUDOEDIT].value = "true";
    }

    /* Returns true if the last option string was "--" */
#define got_end_of_args	(optind > 1 && argv[optind - 1][0] == '-' && \
	    argv[optind - 1][1] == '-' && argv[optind - 1][2] == '\0')

    /* Returns true if next option is an environment variable */
#define is_envar (optind < argc && argv[optind][0] != '/' && \
	    strchr(argv[optind], '=') != NULL)

    /* Flags allowed when running a command */
    valid_flags = MODE_BACKGROUND|MODE_PRESERVE_ENV|MODE_RESET_HOME|
		  MODE_LOGIN_SHELL|MODE_NONINTERACTIVE|MODE_SHELL;
    /* XXX - should fill in settings at the end to avoid dupes */
    for (;;) {
	/*
	 * We disable arg permutation for GNU getopt().
	 * Some trickiness is required to allow environment variables
	 * to be interspersed with command line options.
	 */
	if ((ch = getopt(argc, argv, "+Aa:bC:c:D:Eeg:HhiKkLlnPp:r:Sst:U:u:Vv")) != -1) {
	    switch (ch) {
		case 'A':
		    SET(tgetpass_flags, TGP_ASKPASS);
		    break;
#ifdef HAVE_BSD_AUTH_H
		case 'a':
		    sudo_settings[ARG_BSDAUTH_TYPE].value = optarg;
		    break;
#endif
		case 'b':
		    SET(flags, MODE_BACKGROUND);
		    break;
		case 'C':
		    if ((user_closefrom = atoi(optarg)) < 3) {
			warningx("the argument to -C must be at least 3");
			usage(1);
		    }
		    break;
#ifdef HAVE_LOGIN_CAP_H
		case 'c':
		    sudo_settings[ARG_LOGIN_CLASS].value = optarg;
		    break;
#endif
		case 'D':
		    if ((debug_level = atoi(optarg)) < 1 || debug_level > 9) {
			warningx("the argument to -D must be between 1 and 9 inclusive");
			usage(1);
		    }
		    sudo_settings[ARG_DEBUG_LEVEL].value = optarg;
		    break;
		case 'E':
		    sudo_settings[ARG_PRESERVE_ENVIRONMENT].value = "true";
		    break;
		case 'e':
		    if (mode && mode != MODE_EDIT)
			usage_excl(1);
		    mode = MODE_EDIT;
		    sudo_settings[ARG_SUDOEDIT].value = "true";
		    valid_flags = MODE_NONINTERACTIVE;
		    break;
		case 'g':
		    runas_group = optarg;
		    sudo_settings[ARG_RUNAS_GROUP].value = optarg;
		    break;
		case 'H':
		    sudo_settings[ARG_SET_HOME].value = "true";
		    break;
		case 'h':
		    if (mode && mode != MODE_HELP) {
			if (strcmp(getprogname(), "sudoedit") != 0)
			    usage_excl(1);
		    }
		    mode = MODE_HELP;
		    valid_flags = 0;
		    break;
		case 'i':
		    sudo_settings[ARG_LOGIN_SHELL].value = "true";
		    SET(flags, MODE_LOGIN_SHELL);
		    break;
		case 'k':
		    sudo_settings[ARG_IGNORE_TICKET].value = "true";
		    break;
		case 'K':
		    sudo_settings[ARG_IGNORE_TICKET].value = "true";
		    if (mode && mode != MODE_KILL)
			usage_excl(1);
		    mode = MODE_KILL;
		    valid_flags = 0;
		    break;
		case 'l':
		    if (mode) {
			if (mode == MODE_LIST)
			    SET(flags, MODE_LONG_LIST);
			else
			    usage_excl(1);
		    }
		    mode = MODE_LIST;
		    valid_flags = MODE_NONINTERACTIVE|MODE_LONG_LIST;
		    break;
		case 'n':
		    SET(flags, MODE_NONINTERACTIVE);
		    sudo_settings[ARG_NONINTERACTIVE].value = "true";
		    break;
		case 'P':
		    sudo_settings[ARG_PRESERVE_GROUPS].value = "true";
		    break;
		case 'p':
		    sudo_settings[ARG_PROMPT].value = optarg;
		    break;
#ifdef HAVE_SELINUX
		case 'r':
		    sudo_settings[ARG_SELINUX_ROLE].value = optarg;
		    break;
		case 't':
		    sudo_settings[ARG_SELINUX_TYPE].value = optarg;
		    break;
#endif
		case 'S':
		    SET(tgetpass_flags, TGP_STDIN);
		    break;
		case 's':
		    SET(flags, MODE_SHELL);
		    break;
		case 'U':
		    /* XXX - sudo_getpwnam */
		    if ((getpwnam(optarg)) == NULL)
			errorx(1, "unknown user: %s", optarg);
		    list_user = optarg;
		    break;
		case 'u':
		    runas_user = optarg;
		    sudo_settings[ARG_RUNAS_USER].value = optarg;
		    break;
		case 'v':
		    if (mode && mode != MODE_VALIDATE)
			usage_excl(1);
		    mode = MODE_VALIDATE;
		    valid_flags = MODE_NONINTERACTIVE;
		    break;
		case 'V':
		    if (mode && mode != MODE_VERSION)
			usage_excl(1);
		    mode = MODE_VERSION;
		    valid_flags = 0;
		    break;
		default:
		    usage(1);
	    }
	} else if (!got_end_of_args && is_envar) {
	    if (nenv == env_size - 2) {
		env_size *= 2;
		env_add = erealloc3(env_add, env_size, sizeof(char *));
	    }
	    env_add[nenv++] = argv[optind];

	    /* Crank optind and resume getopt. */
	    optind++;
	} else {
	    /* Not an option or an environment variable -- we're done. */
	    break;
	}
    }
    env_add[nenv] = NULL;

    argc -= optind;
    argv += optind;

    if (!mode) {
	/* Defer -k mode setting until we know whether it is a flag or not */
	if (sudo_settings[ARG_IGNORE_TICKET].value != NULL) {
	    if (argc == 0) {
		mode = MODE_INVALIDATE;	/* -k by itself */
		sudo_settings[ARG_IGNORE_TICKET].value = NULL;
		valid_flags = 0;
	    }
	}
	if (!mode)
	    mode = MODE_RUN;		/* running a command */
    }

    if (argc > 0 && mode == MODE_LIST)
	mode = MODE_CHECK;

    if (ISSET(flags, MODE_LOGIN_SHELL)) {
	if (ISSET(flags, MODE_SHELL)) {
	    warningx("you may not specify both the `-i' and `-s' options");
	    usage(1);
	}
	if (ISSET(flags, MODE_PRESERVE_ENV)) {
	    warningx("you may not specify both the `-i' and `-E' options");
	    usage(1);
	}
	SET(flags, MODE_SHELL);
    }
    if ((flags & valid_flags) != flags)
	usage(1);
    if (mode == MODE_EDIT &&
       (ISSET(flags, MODE_PRESERVE_ENV) || env_add[0] != NULL)) {
	if (ISSET(mode, MODE_PRESERVE_ENV))
	    warningx("the `-E' option is not valid in edit mode");
	if (env_add[0] != NULL)
	    warningx("you may not specify environment variables in edit mode");
	usage(1);
    }
    if ((runas_user != NULL || runas_group != NULL) &&
	!ISSET(mode, MODE_EDIT | MODE_RUN | MODE_CHECK | MODE_VALIDATE)) {
	usage(1);
    }
    if (list_user != NULL && mode != MODE_LIST && mode != MODE_CHECK) {
	warningx("the `-U' option may only be used with the `-l' option");
	usage(1);
    }
    if (ISSET(tgetpass_flags, TGP_STDIN) && ISSET(tgetpass_flags, TGP_ASKPASS)) {
	warningx("the `-A' and `-S' options may not be used together");
	usage(1);
    }
    if ((argc == 0 && mode == MODE_EDIT) ||
	(argc > 0 && !ISSET(mode, MODE_RUN | MODE_EDIT | MODE_CHECK)))
	usage(1);
    if (argc == 0 && mode == MODE_RUN && !ISSET(flags, MODE_SHELL)) {
	SET(flags, (MODE_IMPLIED_SHELL | MODE_SHELL));
	sudo_settings[ARG_IMPLIED_SHELL].value = "true";
    }

    if (mode == MODE_HELP)
	usage(0);

    /*
     * For shell mode we need to rewrite argv
     */
    if (ISSET(mode, MODE_RUN) && ISSET(flags, MODE_SHELL)) {
	char **av;
	int ac;

	if (argc == 0) {
	    /* just the shell */
	    ac = argc + 1;
	    av = emalloc2(ac + 1, sizeof(char *));
	    memcpy(av + 1, argv, argc * sizeof(char *));
	} else {
	    /* shell -c "command" */
	    size_t size;
	    char *src, *dst, *end;

	    /* length of the command + NUL terminator */
	    size = (size_t)(argv[argc - 1] - argv[0]) +
		strlen(argv[argc - 1]) + 1;

	    ac = 3;
	    av = emalloc2(ac + 1, sizeof(char *));
	    av[1] = "-c";
	    av[2] = dst = emalloc(size);

	    src = argv[0];
	    for (end = src + size - 1; src < end; src++, dst++)
		*dst = *src == 0 ? ' ' : *src;
	    *dst = '\0';
	}
	av[0] = (char *)user_details.shell; /* plugin may override shell */
	av[ac] = NULL;

	argv = av;
	argc = ac;
    }

    /*
     * Format setting_pairs into settings array.
     */
    settings = emalloc2(NUM_SETTINGS + 1, sizeof(char *));
    for (i = 0, j = 0; i < NUM_SETTINGS; i++) {
	if (sudo_settings[i].value) {
	    sudo_debug(9, "settings: %s=%s", sudo_settings[i].name,
		sudo_settings[i].value);
	    settings[j] = fmt_string(sudo_settings[i].name,
		sudo_settings[i].value);
	    if (settings[j] == NULL)
		errorx(1, "unable to allocate memory");
	    j++;
	}
    }
    settings[j] = NULL;

    if (mode == MODE_EDIT) {
#if defined(HAVE_SETRESUID) || defined(HAVE_SETREUID) || defined(HAVE_SETEUID)
	/* Must have the command in argv[0]. */
	argc++;
	argv--;
	argv[0] = "sudoedit";
#else
	errorx(1, "sudoedit is not supported on this platform");
#endif
    }

    *settingsp = settings;
    *env_addp = env_add;
    *nargc = argc;
    *nargv = argv;
    return(mode | flags);
}

static int
usage_out(const char *buf)
{
    return fputs(buf, stderr);
}

/*
 * Give usage message and exit.
 * The actual usage strings are in sudo_usage.h for configure substitution.
 */
void
usage(int exit_val)
{
    struct lbuf lbuf;
    char *uvec[6];
    int i, ulen;

    /*
     * Use usage vectors appropriate to the progname.
     */
    if (strcmp(getprogname(), "sudoedit") == 0) {
	uvec[0] = SUDO_USAGE5 + 3;
	uvec[1] = NULL;
    } else {
	uvec[0] = SUDO_USAGE1;
	uvec[1] = SUDO_USAGE2;
	uvec[2] = SUDO_USAGE3;
	uvec[3] = SUDO_USAGE4;
	uvec[4] = SUDO_USAGE5;
	uvec[5] = NULL;
    }

    /*
     * Print usage and wrap lines as needed, depending on the
     * tty width.
     */
    ulen = (int)strlen(getprogname()) + 8;
    lbuf_init(&lbuf, usage_out, ulen, NULL, user_details.ts_cols);
    for (i = 0; uvec[i] != NULL; i++) {
	lbuf_append(&lbuf, "usage: ", getprogname(), uvec[i], NULL);
	lbuf_print(&lbuf);
    }
    lbuf_destroy(&lbuf);
    cleanup(0);
    exit(exit_val);
}

/*
 * Tell which options are mutually exclusive and exit.
 */
static void
usage_excl(int exit_val)
{
    warningx("Only one of the -e, -h, -i, -K, -l, -s, -v or -V options may be specified");
    usage(exit_val);
}
