/*
 * Copyright (c) 1993-1996,1998-2005 Todd C. Miller <Todd.Miller@courtesan.com>
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
 *
 * For a brief history of sudo, please see the HISTORY file included
 * with this distribution.
 */

#define _SUDO_MAIN

#ifdef __TANDEM
# include <floss.h>
#endif

#include <config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/socket.h>
#ifdef HAVE_SETRLIMIT
# include <sys/time.h>
# include <sys/resource.h>
#endif
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
#include <fcntl.h>
#include <signal.h>
#include <grp.h>
#if TIME_WITH_SYS_TIME
# include <time.h>
#endif
#ifdef HAVE_SETLOCALE
# include <locale.h>
#endif
#include <netinet/in.h>
#include <netdb.h>
#if defined(HAVE_GETPRPWNAM) && defined(HAVE_SET_AUTH_PARAMETERS)
# ifdef __hpux
#  undef MAXINT
#  include <hpsecurity.h>
# else
#  include <sys/security.h>
# endif /* __hpux */
# include <prot.h>
#endif /* HAVE_GETPRPWNAM && HAVE_SET_AUTH_PARAMETERS */
#ifdef HAVE_LOGIN_CAP_H
# include <login_cap.h>
# ifndef LOGIN_DEFROOTCLASS
#  define LOGIN_DEFROOTCLASS	"daemon"
# endif
#endif

#include "sudo.h"
#include "interfaces.h"
#include "version.h"

#ifndef lint
__unused static const char rcsid[] = "$Sudo$";
#endif /* lint */

/*
 * Prototypes
 */
static void init_vars			__P((int, char **));
static int set_cmnd			__P((int));
static int parse_args			__P((int, char **));
static void initial_setup		__P((void));
static void set_loginclass		__P((struct passwd *));
static void usage			__P((int))
					    __attribute__((__noreturn__));
static void usage_excl			__P((int))
					    __attribute__((__noreturn__));
static struct passwd *get_authpw	__P((void));
extern int sudo_edit			__P((int, char **));
extern char **rebuild_env		__P((char **, int, int));

/*
 * Globals
 */
int Argc, NewArgc;
char **Argv, **NewArgv;
char *prev_user;
static int user_closefrom = -1;
struct sudo_user sudo_user;
struct passwd *auth_pw, *list_pw;
struct interface *interfaces;
int num_interfaces;
int tgetpass_flags;
uid_t timestamp_uid;
extern int errorlineno;
extern int parse_error;
extern char *errorfile;
#if defined(RLIMIT_CORE) && !defined(SUDO_DEVEL)
static struct rlimit corelimit;
#endif /* RLIMIT_CORE && !SUDO_DEVEL */
#ifdef HAVE_LOGIN_CAP_H
login_cap_t *lc;
#endif /* HAVE_LOGIN_CAP_H */
#ifdef HAVE_BSD_AUTH_H
char *login_style;
#endif /* HAVE_BSD_AUTH_H */
sigaction_t saved_sa_int, saved_sa_quit, saved_sa_tstp, saved_sa_chld;


int
main(argc, argv)
    int argc;
    char **argv;
{
    int validated = 0;
    int fd;
    int cmnd_status;
    int sudo_mode;
    int pwflag;
    sigaction_t sa;
    VOID *ld = NULL;
    extern char **environ;
#if defined(SUDO_DEVEL) && defined(__OpenBSD__)
    extern char *malloc_options;
    malloc_options = "AFGJPR";
#endif

#ifdef HAVE_SETLOCALE
    setlocale(LC_ALL, "");
#endif

    Argv = argv;
    if ((Argc = argc) < 1)
	usage(1);

    /* Must be done as the first thing... */
#if defined(HAVE_GETPRPWNAM) && defined(HAVE_SET_AUTH_PARAMETERS)
    (void) set_auth_parameters(Argc, Argv);
# ifdef HAVE_INITPRIVS
    initprivs();
# endif
#endif /* HAVE_GETPRPWNAM && HAVE_SET_AUTH_PARAMETERS */

    if (geteuid() != 0)
	errorx(1, "must be setuid root");

    /*
     * Signal setup:
     *	Ignore keyboard-generated signals so the user cannot interrupt
     *  us at some point and avoid the logging.
     *  Install handler to wait for children when they exit.
     */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = SIG_IGN;
    (void) sigaction(SIGINT, &sa, &saved_sa_int);
    (void) sigaction(SIGQUIT, &sa, &saved_sa_quit);
    (void) sigaction(SIGTSTP, &sa, &saved_sa_tstp);
    sa.sa_handler = reapchild;
    (void) sigaction(SIGCHLD, &sa, &saved_sa_chld);

    /*
     * Turn off core dumps and make sure fds 0-2 are open.
     */
    initial_setup();
    sudo_setpwent();
    sudo_setgrent();

    /* Parse our arguments. */
    sudo_mode = parse_args(Argc, Argv);

    /* Setup defaults data structures. */
    init_defaults();

    /* Load the list of local ip addresses and netmasks.  */
    load_interfaces();

    pwflag = 0;
    if (ISSET(sudo_mode, MODE_SHELL))
	user_cmnd = "shell";
    else if (ISSET(sudo_mode, MODE_EDIT))
	user_cmnd = "sudoedit";
    else
	switch (sudo_mode) {
	    case MODE_VERSION:
		(void) printf("Sudo version %s\n", version);
		if (getuid() == 0) {
		    putchar('\n');
		    (void) printf("Sudoers path: %s\n", _PATH_SUDOERS);
		    dump_auth_methods();
		    dump_defaults();
		    dump_interfaces();
		}
		exit(0);
		break;
	    case MODE_HELP:
		usage(0);
		break;
	    case MODE_VALIDATE:
		user_cmnd = "validate";
		pwflag = I_VERIFYPW;
		break;
	    case MODE_KILL:
	    case MODE_INVALIDATE:
		user_cmnd = "kill";
		pwflag = -1;
		break;
	    case MODE_LISTDEFS:
		list_options();
		exit(0);
		break;
	    case MODE_LIST:
		user_cmnd = "list";
		pwflag = I_LISTPW;
		break;
	    case MODE_CHECK:
		pwflag = I_LISTPW;
		break;
	}

    /* Must have a command to run... */
    if (user_cmnd == NULL && NewArgc == 0)
	usage(1);

    init_vars(sudo_mode, environ);	/* XXX - move this? */

#ifdef HAVE_LDAP
    if ((ld = sudo_ldap_open()) != NULL)
	sudo_ldap_update_defaults(ld);

    if (!def_ignore_local_sudoers)
#endif
    {
	/* Parse sudoers and set any defaults listed in it. */
	if (parse_sudoers(_PATH_SUDOERS) || parse_error)
	    log_error(0, "parse error in %s near line %d", errorfile, errorlineno);
	if (!update_defaults(SKIP_CMND))
	    log_error(NO_STDERR|NO_EXIT, "problem with defaults entries");
    }

    /* This goes after sudoers is parsed since it may have timestamp options. */
    if (sudo_mode == MODE_KILL || sudo_mode == MODE_INVALIDATE) {
	remove_timestamp((sudo_mode == MODE_KILL));
	exit(0);
    }

    /* Is root even allowed to run sudo? */
    if (user_uid == 0 && !def_root_sudo) {
	(void) fprintf(stderr,
	    "Sorry, %s has been configured to not allow root to run it.\n",
	    getprogname());
	exit(1);
    }

    /* Check for -C overriding def_closefrom. */
    if (user_closefrom >= 0 && user_closefrom != def_closefrom) {
	if (!def_closefrom_override)
	    errorx(1, "you are not permitted to use the -C option");
	else
	    def_closefrom = user_closefrom;
    }

    cmnd_status = set_cmnd(sudo_mode);

#ifdef HAVE_LDAP
    if (ld != NULL)
	validated = sudo_ldap_check(ld, pwflag);
    /* Fallback to sudoers if we are allowed to and we aren't validated. */
    if (!def_ignore_local_sudoers && !ISSET(validated, VALIDATE_OK))
#endif
	validated = sudoers_lookup(pwflag);
    if (safe_cmnd == NULL)
	safe_cmnd = estrdup(user_cmnd);

    /*
     * Look up the timestamp dir owner if one is specified.
     */
    if (def_timestampowner) {
	struct passwd *pw;

	if (*def_timestampowner == '#')
	    pw = sudo_getpwuid(atoi(def_timestampowner + 1));
	else
	    pw = sudo_getpwnam(def_timestampowner);
	if (!pw)
	    log_error(0, "timestamp owner (%s): No such user",
		def_timestampowner);
	timestamp_uid = pw->pw_uid;
    }

    /* If given the -P option, set the "preserve_groups" flag. */
    if (ISSET(sudo_mode, MODE_PRESERVE_GROUPS))
	def_preserve_groups = TRUE;

    /* If no command line args and "set_home" is not set, error out. */
    if (ISSET(sudo_mode, MODE_IMPLIED_SHELL) && !def_shell_noargs)
	usage(1);

    /* Bail if a tty is required and we don't have one.  */
    if (def_requiretty) {
	if ((fd = open(_PATH_TTY, O_RDWR|O_NOCTTY)) == -1)
	    log_error(NO_MAIL, "sorry, you must have a tty to run sudo");
	else
	    (void) close(fd);
    }

    /* Fill in passwd struct based on user we are authenticating as.  */
    auth_pw = get_authpw();

    /* Require a password if sudoers says so.  */
    if (def_authenticate)
	check_user(ISSET(validated, FLAG_CHECK_USER));

    /* If run as root with SUDO_USER set, set sudo_user.pw to that user. */
    /* XXX - causes confusion when root is not listed in sudoers */
    if (sudo_mode & (MODE_RUN | MODE_EDIT) && prev_user != NULL) {
	if (user_uid == 0 && strcmp(prev_user, "root") != 0) {
	    struct passwd *pw;

	    if ((pw = sudo_getpwnam(prev_user)) != NULL)
		    sudo_user.pw = pw;
	}
    }

    /* Build a new environment based on the rules in sudoers. */
    if (ISSET(sudo_mode, MODE_RUN))
	environ = rebuild_env(environ, sudo_mode, def_noexec);

    if (ISSET(validated, VALIDATE_OK)) {
	/* Finally tell the user if the command did not exist. */
	if (cmnd_status == NOT_FOUND_DOT) {
	    warningx("ignoring `%s' found in '.'\nUse `sudo ./%s' if this is the `%s' you wish to run.", user_cmnd, user_cmnd, user_cmnd);
	    exit(1);
	} else if (cmnd_status == NOT_FOUND) {
	    warningx("%s: command not found", user_cmnd);
	    exit(1);
	}

	log_auth(validated, 1);
	if (sudo_mode == MODE_VALIDATE)
	    exit(0);
	else if (sudo_mode == MODE_CHECK)
	    exit(display_cmnd(ld, list_pw ? list_pw : sudo_user.pw));
	else if (sudo_mode == MODE_LIST) {
	    display_privs(ld, list_pw ? list_pw : sudo_user.pw);
	    exit(0);
	}

#ifdef HAVE_LDAP
	if (ld != NULL) {
	    sudo_ldap_close(ld);
	    ld = NULL;
	}
#endif

#ifdef HAVE_SYSTRACE 
	if (def_monitor)
	    systrace_attach(getpid());
#endif

	/* Override user's umask if configured to do so. */
	if (def_umask != 0777)
	    (void) umask(def_umask);

	/* Restore coredumpsize resource limit. */
#if defined(RLIMIT_CORE) && !defined(SUDO_DEVEL)
	(void) setrlimit(RLIMIT_CORE, &corelimit);
#endif /* RLIMIT_CORE && !SUDO_DEVEL */

	/* Become specified user or root if executing a command. */
	if (ISSET(sudo_mode, MODE_RUN))
	    set_perms(PERM_FULL_RUNAS);

	if (ISSET(sudo_mode, MODE_LOGIN_SHELL)) {
	    char *p;

	    /* Convert /bin/sh -> -sh so shell knows it is a login shell */
	    if ((p = strrchr(NewArgv[0], '/')) == NULL)
		p = NewArgv[0];
	    *p = '-';
	    NewArgv[0] = p;

	    /* Change to target user's homedir. */
	    if (chdir(runas_pw->pw_dir) == -1)
		warning("unable to change directory to %s", runas_pw->pw_dir);
	}

	if (ISSET(sudo_mode, MODE_EDIT))
	    exit(sudo_edit(NewArgc, NewArgv));

	/* Restore signal handlers before we exec. */
	(void) sigaction(SIGINT, &saved_sa_int, NULL);
	(void) sigaction(SIGQUIT, &saved_sa_quit, NULL);
	(void) sigaction(SIGTSTP, &saved_sa_tstp, NULL);
	(void) sigaction(SIGCHLD, &saved_sa_chld, NULL);

	/* Close the password and group files and free up memory. */
	sudo_endpwent();
	sudo_endgrent();

	closefrom(def_closefrom + 1);

#ifndef PROFILING
	if (ISSET(sudo_mode, MODE_BACKGROUND) && fork() > 0)
	    exit(0);
	else
	    execve(safe_cmnd, NewArgv, environ);
#else
	exit(0);
#endif /* PROFILING */
	/*
	 * If we got here then execve() failed...
	 */
	if (errno == ENOEXEC) {
	    NewArgv--;			/* at least one extra slot... */
	    NewArgv[0] = "sh";
	    NewArgv[1] = safe_cmnd;
	    execve(_PATH_BSHELL, NewArgv, environ);
	}
	warning("unable to execute %s", safe_cmnd);
	exit(127);
    } else if (ISSET(validated, FLAG_NO_USER) || (validated & FLAG_NO_HOST)) {
	log_auth(validated, 1);
	exit(1);
    } else if (ISSET(validated, VALIDATE_NOT_OK)) {
	if (def_path_info) {
	    /*
	     * We'd like to not leak path info at all here, but that can
	     * *really* confuse the users.  To really close the leak we'd
	     * have to say "not allowed to run foo" even when the problem
	     * is just "no foo in path" since the user can trivially set
	     * their path to just contain a single dir.
	     */
	    log_auth(validated,
		!(cmnd_status == NOT_FOUND_DOT || cmnd_status == NOT_FOUND));
	    if (cmnd_status == NOT_FOUND)
		warningx("%s: command not found", user_cmnd);
	    else if (cmnd_status == NOT_FOUND_DOT)
		warningx("ignoring `%s' found in '.'\nUse `sudo ./%s' if this is the `%s' you wish to run.", user_cmnd, user_cmnd, user_cmnd);
	} else {
	    /* Just tell the user they are not allowed to run foo. */
	    log_auth(validated, 1);
	}
	exit(1);
    } else {
	/* should never get here */
	log_auth(validated, 1);
	exit(1);
    }
    exit(0);	/* not reached */
}

/*
 * Initialize timezone, set umask, fill in ``sudo_user'' struct and
 * load the ``interfaces'' array.
 */
static void
init_vars(sudo_mode, envp)
    int sudo_mode;
    char **envp;
{
    char *p, **ep, thost[MAXHOSTNAMELEN];
    int nohostname;

    /* Sanity check command from user. */
    if (user_cmnd == NULL && strlen(NewArgv[0]) >= PATH_MAX)
	errorx(1, "%s: File name too long", NewArgv[0]);

#ifdef HAVE_TZSET
    (void) tzset();		/* set the timezone if applicable */
#endif /* HAVE_TZSET */

    /* Default value for cmnd and cwd, overridden later. */
    if (user_cmnd == NULL)
	user_cmnd = NewArgv[0];
    (void) strlcpy(user_cwd, "unknown", sizeof(user_cwd));

    /*
     * We avoid gethostbyname() if possible since we don't want
     * sudo to block if DNS or NIS is hosed.
     * "host" is the (possibly fully-qualified) hostname and
     * "shost" is the unqualified form of the hostname.
     */
    nohostname = gethostname(thost, sizeof(thost));
    if (nohostname)
	user_host = user_shost = "localhost";
    else {
	user_host = estrdup(thost);
	if (def_fqdn) {
	    /* Defer call to set_fqdn() until log_error() is safe. */
	    user_shost = user_host;
	} else {
	    if ((p = strchr(user_host, '.'))) {
		*p = '\0';
		user_shost = estrdup(user_host);
		*p = '.';
	    } else {
		user_shost = user_host;
	    }
	}
    }

    if ((p = ttyname(STDIN_FILENO)) || (p = ttyname(STDOUT_FILENO))) {
	if (strncmp(p, _PATH_DEV, sizeof(_PATH_DEV) - 1) == 0)
	    p += sizeof(_PATH_DEV) - 1;
	user_tty = estrdup(p);
    } else
	user_tty = "unknown";

    for (ep = envp; *ep; ep++) {
	switch (**ep) {
	    case 'P':
		if (strncmp("PATH=", *ep, 5) == 0)
		    user_path = *ep + 5;
		break;
	    case 'S':
		if (strncmp("SHELL=", *ep, 6) == 0)
		    user_shell = *ep + 6;
		else if (!user_prompt && strncmp("SUDO_PROMPT=", *ep, 12) == 0)
		    user_prompt = *ep + 12;
		else if (strncmp("SUDO_USER=", *ep, 10) == 0)
		    prev_user = *ep + 10;
		break;

	    }
    }

    /*
     * Get a local copy of the user's struct passwd with the shadow password
     * if necessary.  It is assumed that euid is 0 at this point so we
     * can read the shadow passwd file if necessary.
     */
    if ((sudo_user.pw = sudo_getpwuid(getuid())) == NULL) {
	/* Need to make a fake struct passwd for logging to work. */
	struct passwd pw;
	char pw_name[MAX_UID_T_LEN + 1];

	pw.pw_uid = getuid();
	(void) snprintf(pw_name, sizeof(pw_name), "%lu",
	    (unsigned long) pw.pw_uid);
	pw.pw_name = pw_name;
	sudo_user.pw = &pw;

	/*
	 * If we are in -k/-K mode, just spew to stderr.  It is not unusual for
	 * users to place "sudo -k" in a .logout file which can cause sudo to
	 * be run during reboot after the YP/NIS/NIS+/LDAP/etc daemon has died.
	 */
	if (sudo_mode & (MODE_INVALIDATE|MODE_KILL))
	    errorx(1, "uid %s does not exist in the passwd file!", pw_name);
	log_error(0, "uid %s does not exist in the passwd file!", pw_name);
    }
    if (user_shell == NULL || *user_shell == '\0')
	user_shell = estrdup(sudo_user.pw->pw_shell);

    /* It is now safe to use log_error() and set_perms() */

#ifdef HAVE_GETGROUPS
    if ((user_ngroups = getgroups(0, NULL)) > 0) {
	user_groups = emalloc2(user_ngroups, sizeof(gid_t));
	if (getgroups(user_ngroups, user_groups) < 0)
	    log_error(USE_ERRNO|MSG_ONLY, "can't get group vector");
    } else
	user_ngroups = 0;
#endif

    if (def_fqdn)
	set_fqdn();			/* may call log_error() */

    if (nohostname)
	log_error(USE_ERRNO|MSG_ONLY, "can't get hostname");

    set_runaspw(*user_runas);		/* may call log_error() */
    if (*user_runas[0] == '#' && runas_pw->pw_name[0] != '#')
	*user_runas = estrdup(runas_pw->pw_name);

    /*
     * Get current working directory.  Try as user, fall back to root.
     */
    set_perms(PERM_USER);
    if (!getcwd(user_cwd, sizeof(user_cwd))) {
	set_perms(PERM_ROOT);
	if (!getcwd(user_cwd, sizeof(user_cwd))) {
	    warningx("cannot get working directory");
	    (void) strlcpy(user_cwd, "unknown", sizeof(user_cwd));
	}
    } else
	set_perms(PERM_ROOT);

    /*
     * If we were given the '-e', '-i' or '-s' options we need to redo
     * NewArgv and NewArgc.
     */
    if ((sudo_mode & (MODE_SHELL | MODE_EDIT))) {
	char **dst, **src = NewArgv;

	/* Allocate an extra slot for execve() failure (ENOEXEC). */
	NewArgv = (char **) emalloc2((++NewArgc + 2), sizeof(char *));
	NewArgv++;
	if (ISSET(sudo_mode, MODE_EDIT))
	    NewArgv[0] = "sudoedit";
	else if (ISSET(sudo_mode, MODE_LOGIN_SHELL))
	    NewArgv[0] = estrdup(runas_pw->pw_shell);
	else if (user_shell && *user_shell)
	    NewArgv[0] = user_shell;
	else
	    errorx(1, "unable to determine shell");

	/* copy the args from NewArgv */
	for (dst = NewArgv + 1; (*dst = *src) != NULL; ++src, ++dst)
	    continue;
    }

    /* Set login class if applicable. */
    set_loginclass(sudo_user.pw);
}

/*
 * Fill in user_cmnd, user_args, user_base and user_stat variables
 * and apply any command-specific defaults entries.
 */
static int
set_cmnd(sudo_mode)
    int sudo_mode;
{
    int rval;

    /* Resolve the path and return. */
    rval = FOUND;
    user_stat = emalloc(sizeof(struct stat));
    if (sudo_mode & (MODE_RUN | MODE_EDIT | MODE_CHECK)) {
	if (ISSET(sudo_mode, MODE_RUN | MODE_CHECK)) {
	    set_perms(PERM_RUNAS);
	    rval = find_path(NewArgv[0], &user_cmnd, user_stat, user_path);
	    set_perms(PERM_ROOT);
	    if (rval != FOUND) {
		/* Failed as root, try as invoking user. */
		set_perms(PERM_USER);
		rval = find_path(NewArgv[0], &user_cmnd, user_stat, user_path);
		set_perms(PERM_ROOT);
	    }
	}

	/* set user_args */
	if (NewArgc > 1) {
	    char *to, **from;
	    size_t size, n;

	    /* If we didn't realloc NewArgv it is contiguous so just count. */
	    if (!(sudo_mode & (MODE_SHELL | MODE_EDIT))) {
		size = (size_t) (NewArgv[NewArgc-1] - NewArgv[1]) +
			strlen(NewArgv[NewArgc-1]) + 1;
	    } else {
		for (size = 0, from = NewArgv + 1; *from; from++)
		    size += strlen(*from) + 1;
	    }

	    /* Alloc and build up user_args. */
	    user_args = (char *) emalloc(size);
	    for (to = user_args, from = NewArgv + 1; *from; from++) {
		n = strlcpy(to, *from, size - (to - user_args));
		if (n >= size - (to - user_args))
		    errorx(1, "internal error, init_vars() overflow");
		to += n;
		*to++ = ' ';
	    }
	    *--to = '\0';
	}
    }
    if ((user_base = strrchr(user_cmnd, '/')) != NULL)
	user_base++;
    else
	user_base = user_cmnd;

    if (!update_defaults(ONLY_CMND))
	log_error(NO_STDERR|NO_EXIT, "problem with defaults entries");

    return(rval);
}

/*
 * Command line argument parsing, can't use getopt(3).
 */
static int
parse_args(argc, argv)
    int argc;
    char **argv;
{
    int rval = MODE_RUN;		/* what mode is sudo to be run in? */
    int excl = 0;			/* exclusive arg, no others allowed */

    NewArgv = argv + 1;
    NewArgc = argc - 1;

    /* First, check to see if we were invoked as "sudoedit". */
    if (strcmp(getprogname(), "sudoedit") == 0) {
	rval = MODE_EDIT;
	excl = 'e';
    } else
	rval = MODE_RUN;

    if (NewArgc == 0 && rval == MODE_RUN) {	/* no options and no command */
	SET(rval, (MODE_IMPLIED_SHELL | MODE_SHELL));
	return(rval);
    }

    while (NewArgc > 0 && NewArgv[0][0] == '-') {
	if (NewArgv[0][1] != '\0' && NewArgv[0][2] != '\0')
	    warningx("please use single character options");

	switch (NewArgv[0][1]) {
	    case 'p':
		/* Must have an associated prompt. */
		if (NewArgv[1] == NULL)
		    usage(1);

		user_prompt = NewArgv[1];

		NewArgc--;
		NewArgv++;
		break;
	    case 'u':
		/* Must have an associated runas user. */
		if (NewArgv[1] == NULL)
		    usage(1);

		user_runas = &NewArgv[1];

		NewArgc--;
		NewArgv++;
		break;
#ifdef HAVE_BSD_AUTH_H
	    case 'a':
		/* Must have an associated authentication style. */
		if (NewArgv[1] == NULL)
		    usage(1);

		login_style = NewArgv[1];

		NewArgc--;
		NewArgv++;
		break;
#endif
#ifdef HAVE_LOGIN_CAP_H
	    case 'c':
		/* Must have an associated login class. */
		if (NewArgv[1] == NULL)
		    usage(1);

		login_class = NewArgv[1];
		def_use_loginclass = TRUE;

		NewArgc--;
		NewArgv++;
		break;
#endif
	    case 'C':
		if (NewArgv[1] == NULL)
		    usage(1);
		if ((user_closefrom = atoi(NewArgv[1])) < 3) {
		    warningx("the argument to -C must be at least 3");
		    usage(1);
		}
		NewArgc--;
		NewArgv++;
		break;
	    case 'b':
		SET(rval, MODE_BACKGROUND);
		break;
	    case 'e':
		rval = MODE_EDIT;
		if (excl && excl != 'e')
		    usage_excl(1);
		excl = 'e';
		break;
	    case 'v':
		rval = MODE_VALIDATE;
		if (excl && excl != 'v')
		    usage_excl(1);
		excl = 'v';
		break;
	    case 'i':
		SET(rval, (MODE_LOGIN_SHELL | MODE_SHELL));
		def_env_reset = TRUE;
		if (excl && excl != 'i')
		    usage_excl(1);
		excl = 'i';
		break;
	    case 'k':
		rval = MODE_INVALIDATE;
		if (excl && excl != 'k')
		    usage_excl(1);
		excl = 'k';
		break;
	    case 'K':
		rval = MODE_KILL;
		if (excl && excl != 'K')
		    usage_excl(1);
		excl = 'K';
		break;
	    case 'L':
		rval = MODE_LISTDEFS;
		if (excl && excl != 'L')
		    usage_excl(1);
		excl = 'L';
		break;
	    case 'l':
		rval = MODE_LIST;
		if (excl && excl != 'l')
		    usage_excl(1);
		excl = 'l';
		break;
	    case 'V':
		rval = MODE_VERSION;
		if (excl && excl != 'V')
		    usage_excl(1);
		excl = 'V';
		break;
	    case 'h':
		rval = MODE_HELP;
		if (excl && excl != 'h')
		    usage_excl(1);
		excl = 'h';
		break;
	    case 's':
		SET(rval, MODE_SHELL);
		if (excl && excl != 's')
		    usage_excl(1);
		excl = 's';
		break;
	    case 'H':
		SET(rval, MODE_RESET_HOME);
		break;
	    case 'P':
		SET(rval, MODE_PRESERVE_GROUPS);
		break;
	    case 'S':
		SET(tgetpass_flags, TGP_STDIN);
		break;
	    case 'U':
		/* Must have an associated list user. */
		if (NewArgv[1] == NULL)
		    usage(1);
		if ((list_pw = sudo_getpwnam(NewArgv[1])) == NULL)
		    errorx(1, "unknown user %s", NewArgv[1]);
		NewArgc--;
		NewArgv++;
		break;
	    case '-':
		NewArgc--;
		NewArgv++;
		if (rval == MODE_RUN)
		    SET(rval, (MODE_IMPLIED_SHELL | MODE_SHELL));
		return(rval);
	    case '\0':
		warningx("'-' requires an argument");
		usage(1);
	    default:
		warningx("illegal option `%s'", NewArgv[0]);
		usage(1);
	}
	NewArgc--;
	NewArgv++;
    }
    if (NewArgc > 0 && rval == MODE_LIST)
	rval = MODE_CHECK;

    if (user_runas != NULL && !ISSET(rval, (MODE_EDIT|MODE_RUN|MODE_CHECK))) {
	if (excl != '\0')
	    warningx("the `-u' and '-%c' options may not be used together",
		excl);
	usage(1);
    }
    if (list_pw != NULL && rval != MODE_LIST && rval != MODE_CHECK) {
	if (excl != '\0')
	    warningx("the `-U' and '-%c' options may not be used together",
		excl);
	usage(1);
    }
    if ((NewArgc == 0 && (rval & MODE_EDIT)) ||
	(NewArgc > 0 && !(rval & (MODE_RUN | MODE_EDIT | MODE_CHECK))))
	usage(1);

    return(rval);
}

/*
 * Open sudoers and sanity check mode/owner/type.
 * Returns a handle to the sudoers file.
 */
FILE *
open_sudoers(sudoers, keepopen)
    const char *sudoers;
    int *keepopen;
{
    struct stat statbuf;
    FILE *fp = NULL;
    char c;
    int rootstat, i;

    /*
     * Fix the mode and group on sudoers file from old default.
     * Only works if file system is readable/writable by root.
     */
    if ((rootstat = stat_sudoers(sudoers, &statbuf)) == 0 &&
	SUDOERS_UID == statbuf.st_uid && SUDOERS_MODE != 0400 &&
	(statbuf.st_mode & 0007777) == 0400) {

	if (chmod(sudoers, SUDOERS_MODE) == 0) {
	    warningx("fixed mode on %s", sudoers);
	    SET(statbuf.st_mode, SUDOERS_MODE);
	    if (statbuf.st_gid != SUDOERS_GID) {
		if (!chown(sudoers, (uid_t) -1, SUDOERS_GID)) {
		    warningx("set group on %s", sudoers);
		    statbuf.st_gid = SUDOERS_GID;
		} else
		    warning("unable to set group on %s", sudoers);
	    }
	} else
	    warning("unable to fix mode on %s", sudoers);
    }

    /*
     * Sanity checks on sudoers file.  Must be done as sudoers
     * file owner.  We already did a stat as root, so use that
     * data if we can't stat as sudoers file owner.
     */
    set_perms(PERM_SUDOERS);

    if (rootstat != 0 && stat_sudoers(sudoers, &statbuf) != 0)
	log_error(USE_ERRNO, "can't stat %s", sudoers);
    else if (!S_ISREG(statbuf.st_mode))
	log_error(0, "%s is not a regular file", sudoers);
    else if (statbuf.st_size == 0)
	log_error(0, "%s is zero length", sudoers);
    else if ((statbuf.st_mode & 07777) != SUDOERS_MODE)
	log_error(0, "%s is mode 0%o, should be 0%o", sudoers,
	    (unsigned int) (statbuf.st_mode & 07777),
	    (unsigned int) SUDOERS_MODE);
    else if (statbuf.st_uid != SUDOERS_UID)
	log_error(0, "%s is owned by uid %lu, should be %lu", sudoers,
	    (unsigned long) statbuf.st_uid, (unsigned long) SUDOERS_UID);
    else if (statbuf.st_gid != SUDOERS_GID)
	log_error(0, "%s is owned by gid %lu, should be %lu", sudoers,
	    (unsigned long) statbuf.st_gid, (unsigned long) SUDOERS_GID);
    else {
	/* Solaris sometimes returns EAGAIN so try 10 times */
	for (i = 0; i < 10 ; i++) {
	    errno = 0;
	    if ((fp = fopen(sudoers, "r")) == NULL ||
		fread(&c, sizeof(c), 1, fp) != 1) {
		fp = NULL;
		if (errno != EAGAIN && errno != EWOULDBLOCK)
		    break;
	    } else
		break;
	    sleep(1);
	}
	if (fp == NULL)
	    log_error(USE_ERRNO, "can't open %s", sudoers);
	rewind(fp);
	(void) fcntl(fileno(fp), F_SETFD, 1);
    }

    set_perms(PERM_ROOT);		/* change back to root */
    return(fp);
}

/*
 * Close all open files (except std*) and turn off core dumps.
 * Also sets the set_perms() pointer to the correct function.
 */
static void
initial_setup()
{
    int miss[3], devnull = -1;
#if defined(RLIMIT_CORE) && !defined(SUDO_DEVEL)
    struct rlimit rl;

    /*
     * Turn off core dumps.
     */
    (void) getrlimit(RLIMIT_CORE, &corelimit);
    memcpy(&rl, &corelimit, sizeof(struct rlimit));
    rl.rlim_cur = 0;
    (void) setrlimit(RLIMIT_CORE, &rl);
#endif /* RLIMIT_CORE && !SUDO_DEVEL */

    /*
     * stdin, stdout and stderr must be open; set them to /dev/null
     * if they are closed and close all other fds.
     */
    miss[STDIN_FILENO] = fcntl(STDIN_FILENO, F_GETFL, 0) == -1;
    miss[STDOUT_FILENO] = fcntl(STDOUT_FILENO, F_GETFL, 0) == -1;
    miss[STDERR_FILENO] = fcntl(STDERR_FILENO, F_GETFL, 0) == -1;
    if (miss[STDIN_FILENO] || miss[STDOUT_FILENO] || miss[STDERR_FILENO]) {
	if ((devnull = open(_PATH_DEVNULL, O_RDWR, 0644)) != -1) {
	    if (miss[STDIN_FILENO])
		(void) dup2(devnull, STDIN_FILENO);
	    if (miss[STDOUT_FILENO])
		(void) dup2(devnull, STDOUT_FILENO);
	    if (miss[STDERR_FILENO])
		(void) dup2(devnull, STDERR_FILENO);
	    if (devnull > STDERR_FILENO)
		close(devnull);
	}
    }
}

#ifdef HAVE_LOGIN_CAP_H
static void
set_loginclass(pw)
    struct passwd *pw;
{
    int errflags;

    /*
     * Don't make it a fatal error if the user didn't specify the login
     * class themselves.  We do this because if login.conf gets
     * corrupted we want the admin to be able to use sudo to fix it.
     */
    if (login_class)
	errflags = NO_MAIL|MSG_ONLY;
    else
	errflags = NO_MAIL|MSG_ONLY|NO_EXIT;

    if (login_class && strcmp(login_class, "-") != 0) {
	if (strcmp(*user_runas, "root") != 0 && user_uid != 0)
	    errorx(1, "only root can use -c %s", login_class);
    } else {
	login_class = pw->pw_class;
	if (!login_class || !*login_class)
	    login_class =
		(pw->pw_uid == 0) ? LOGIN_DEFROOTCLASS : LOGIN_DEFCLASS;
    }

    lc = login_getclass(login_class);
    if (!lc || !lc->lc_class || strcmp(lc->lc_class, login_class) != 0) {
	log_error(errflags, "unknown login class: %s", login_class);
	if (!lc)
	    lc = login_getclass(NULL);	/* needed for login_getstyle() later */
    }
}
#else
static void
set_loginclass(pw)
    struct passwd *pw;
{
}
#endif /* HAVE_LOGIN_CAP_H */

/*
 * Look up the fully qualified domain name and set user_host and user_shost.
 */
void
set_fqdn()
{
    struct hostent *hp;
    char *p;

    if (!(hp = gethostbyname(user_host))) {
	log_error(MSG_ONLY|NO_EXIT,
	    "unable to lookup %s via gethostbyname()", user_host);
    } else {
	if (user_shost != user_host)
	    efree(user_shost);
	efree(user_host);
	user_host = estrdup(hp->h_name);
    }
    if ((p = strchr(user_host, '.'))) {
	*p = '\0';
	user_shost = estrdup(user_host);
	*p = '.';
    } else {
	user_shost = user_host;
    }
}

/*
 * Get passwd entry for the user we are going to run commands as.
 * By default, this is "root".  Updates runas_pw as a side effect.
 */
int
set_runaspw(user)
    char *user;
{
    if (runas_pw != NULL) {
	if (user_runas != &def_runas_default)
	    return(TRUE);		/* don't override -u option */
    }
    if (*user == '#') {
	if ((runas_pw = sudo_getpwuid(atoi(user + 1))) == NULL)
	    runas_pw = sudo_fakepwnam(user);
    } else {
	if ((runas_pw = sudo_getpwnam(user)) == NULL)
	    log_error(NO_MAIL|MSG_ONLY, "no passwd entry for %s!", user);
    }
    return(TRUE);
}

/*
 * Get passwd entry for the user we are going to authenticate as.
 * By default, this is the user invoking sudo.  In the most common
 * case, this matches sudo_user.pw or runas_pw.
 */
static struct passwd *
get_authpw()
{
    struct passwd *pw;

    if (def_rootpw) {
	if ((pw = sudo_getpwuid(0)) == NULL)
	    log_error(0, "uid 0 does not exist in the passwd file!");
    } else if (def_runaspw) {
	if ((pw = sudo_getpwnam(def_runas_default)) == NULL)
	    log_error(0, "user %s does not exist in the passwd file!",
		def_runas_default);
    } else if (def_targetpw) {
	if (runas_pw->pw_name == NULL)
	    log_error(NO_MAIL|MSG_ONLY, "no passwd entry for %lu!",
		(unsigned long) runas_pw->pw_uid);
	pw = runas_pw;
    } else
	pw = sudo_user.pw;

    return(pw);
}

/*
 * Stub for error()/errorx()
 */
void
cleanup(gotsignal)
    int gotsignal;
{
    if (!gotsignal) {
	sudo_endpwent();
	sudo_endgrent();
    }
}

/*
 * Tell which options are mutually exclusive and exit.
 */
static void
usage_excl(exit_val)
    int exit_val;
{
    warningx("Only one of the -e, -h, -i, -k, -K, -l, -s, -v or -V options may be specified");
    usage(exit_val);
}

/*
 * Give usage message and exit.
 */
static void
usage(exit_val)
    int exit_val;
{
    char **p;
    int linelen, linemax, ulen;
    static char *uvec[] = {
	" [-bHPS]",
#ifdef HAVE_BSD_AUTH_H
	" [-a auth_type]",
#endif
	" [-C fd]",
#ifdef HAVE_LOGIN_CAP_H
	" [-c class|-]",
#endif
	" [-p prompt]",
	" [-u username|#uid]",
	" { -e file [...] | -i | -s | <command> }",
	NULL
    };

    /*
     * For sudoedit, replace the last entry in the usage vector.
     * For sudo, print the secondary usage.
     */
    if (strcmp(getprogname(), "sudoedit") == 0) {
	/* Replace the last entry in the usage vector. */
	for (p = uvec; p[1] != NULL; p++)
	    continue;
	*p = " file [...]";
    } else {
	fprintf(stderr, "usage: %s -K | -L | -V | -h | -k | -v\n",
	    getprogname());
	fprintf(stderr,
	    "usage: %s [-U username] [-u username|#uid] -l [command]\n",
	    getprogname());
    }

    /*
     * Print the main usage and wrap lines as needed.
     * Assumes an 80-character wide terminal, which is kind of bogus...
     */
    ulen = (int)strlen(getprogname()) + 7;
    linemax = 80;
    linelen = linemax - ulen;
    printf("usage: %s", getprogname());
    for (p = uvec; *p != NULL; p++) {
	if (linelen == linemax || (linelen -= strlen(*p)) >= 0) {
	    fputs(*p, stdout);
	} else {
	    p--;
	    linelen = linemax;
	    printf("\n%*s", ulen, "");
	}
    }
    putchar('\n');
    exit(exit_val);
}
