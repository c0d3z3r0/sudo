/*
 * CU sudo version 1.3.1 (based on Root Group sudo version 1.1)
 *
 * This software comes with no waranty whatsoever, use at your own risk.
 *
 * Please send bugs, changes, problems to sudo-bugs@cs.colorado.edu
 *
 */

/*
 *  sudo version 1.1 allows users to execute commands as root
 *  Copyright (C) 1991  The Root Group, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 1, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 **************************************************************************
 *
 *   sudo.c
 *
 *   This is the main() routine for sudo
 *
 *   sudo is a program to allow users to execute commands 
 *   as root.  The commands are defined in a global network-
 *   wide file and can be distributed.
 *
 *   sudo has been hacked far and wide.  Too many people to
 *   know about.  It's about time to come up with a secure
 *   version that will work well in a network.
 *
 *   This most recent version is done by:
 *
 *              Jeff Nieusma <nieusma@rootgroup.com>
 *              Dave Hieb    <davehieb@rootgroup.com>
 *
 *   However, due to the fact that both of the above are no longer
 *   working at Root Group, I am maintaining the "CU version" of
 *   sudo.
 *		Todd Miller  <millert@cs.colorado.edu>
 */

#ifndef lint
static char rcsid[] = "$Id$";
#endif /* lint */

#define MAIN

#include "config.h"

#include <stdio.h>
#ifdef STDC_HEADERS
#include <stdlib.h>
#endif /* STDC_HEADERS */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#ifdef HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif /* HAVE_STRINGS_H */
#ifdef HAVE_MALLOC_H 
#include <malloc.h>   
#endif /* HAVE_MALLOC_H */ 
#include <pwd.h>
#include <netdb.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#else
#include <sys/ioctl.h>
#endif /* HAVE_SYS_SOCKIO_H */
#ifdef _ISC
#include <sys/stream.h>
#include <sys/sioctl.h>
#include <sys/stropts.h>
#include <net/errno.h>
#endif /* _ISC */
#ifdef _MIPS
#include <net/soioctl.h>
#endif /* _MIPS */
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/param.h>
#if defined(__osf__) && defined(HAVE_C2_SECURITY)
#include <sys/security.h>
#include <prot.h>
#endif /* __osf__ && HAVE_C2_SECURITY */

#include "sudo.h"
#include "options.h"
#include "version.h"

#ifndef STDC_HEADERS
#ifndef __GNUC__		/* gcc has its own malloc */
extern char *malloc	__P((size_t));
#endif /* __GNUC__ */
#ifdef HAVE_STRDUP
extern char *strdup	__P((const char *));
#endif /* HAVE_STRDUP */
#endif /* STDC_HEADERS */


/*
 * local functions not visible outside sudo.c
 */
static int  parse_args		__P((void));
static void usage		__P((int));
static void load_globals	__P((void));
static int check_sudoers	__P((void));
static void load_interfaces	__P((void));
static void load_cmnd		__P((void));
static void add_env		__P((void));
static void rmenv		__P((char **, char *, int));
static void clean_env		__P((char **));
static char *uid2str		__P((uid_t));
extern int user_is_exempt	__P((void));

/*
 * Globals
 */
int Argc;
char **Argv;
char *cmnd = NULL;
char *user = NULL;
char *epasswd = NULL;
char host[MAXHOSTNAMELEN + 1];
struct interface *interfaces;
int num_interfaces;
char cwd[MAXPATHLEN + 1];
uid_t uid = (uid_t)-2;


/********************************************************************
 *
 *  main ()
 *
 *  the driving force behind sudo...
 */

main(argc, argv)
    int argc;
    char **argv;
{
    int rtn;
    int sudo_mode = MODE_RUN;
    extern char ** environ;

#if defined(__osf__) && defined(HAVE_C2_SECURITY)
    (void) set_auth_parameters();
#endif /* __osf__ && HAVE_C2_SECURITY */

    Argv = argv;
    Argc = argc;

    if (geteuid() != 0) {
	(void) fprintf(stderr, "Sorry, %s must be setuid root.\n", Argv[0]);
	exit(1);
    }

    /*
     * parse our arguments
     */
    sudo_mode = parse_args();

    switch(sudo_mode) {
	case MODE_VERSION :
	case MODE_HELP :
	    (void) printf("CU Sudo version %s\n", version);
	    if (sudo_mode == MODE_VERSION)
		exit(0);
	    else
		usage(0);
	    break;
	case MODE_VALIDATE :
	    cmnd = "validate";
	    break;
	case MODE_LIST :
	    cmnd = "list";
	    break;
    }

    /*
     * Close all file descriptors to make sure we have a nice
     * clean slate from which to work.  
     */
#ifdef HAVE_SYSCONF
    for (rtn = sysconf(_SC_OPEN_MAX) - 1; rtn > 3; rtn--)
	(void) close(rtn);
#else
    for (rtn = getdtablesize() - 1; rtn > 3; rtn--)
	(void) close(rtn);
#endif /* HAVE_SYSCONF */

    clean_env(environ);		/* clean up the environment (no LD_*) */

    load_globals();		/* load the user host cmnd and uid variables */

    rtn = check_sudoers();	/* check mode/owner on _PATH_SUDO_SUDOERS */
    if (rtn != ALL_SYSTEMS_GO) {
	log_error(rtn);
	set_perms(PERM_FULL_USER);
	inform_user(rtn);
	exit(1);
    }

    if (sudo_mode == MODE_RUN) {
	load_cmnd();		/* load the cmnd global variable */
    } else if (sudo_mode == MODE_KILL) {
	remove_timestamp();	/* remove the timestamp ticket file */
	exit(0);
    } else if (sudo_mode == MODE_LIST) {
#ifdef notyet
	(void) validate();	/* list the user's available commands */
#else
	(void) fprintf(stderr,
	    "Sorry, the list command is not currently implemented.\n");
#endif
	exit(0);
    }

    add_env();			/* add in SUDO_* envariables */

    rtn = validate();		/* validate the user */
    switch (rtn) {

	case VALIDATE_OK:
	    check_user();
	    log_error(ALL_SYSTEMS_GO);
	    if (sudo_mode == MODE_VALIDATE)
		exit(0);
	    set_perms(PERM_FULL_ROOT);
	    EXEC(cmnd, &Argv[1]);
	    perror(cmnd);		/* exec failed! */
	    exit(-1);
	    break;

	case VALIDATE_NO_USER:
	case VALIDATE_NOT_OK:
	case VALIDATE_ERROR:
	default:
	    log_error(rtn);
	    set_perms(PERM_FULL_USER);
	    inform_user(rtn);
	    exit(1);
	    break;
    }
}



/**********************************************************************
 *
 *  load_globals()
 *
 *  This function primes these important global variables:
 *  user, host, cwd, uid
 */

static void load_globals()
{
    struct passwd *pw_ent;
#ifdef FQDN
    struct hostent *h_ent;
#endif /* FQDN */
    char *p;

    uid = getuid();		/* we need to tuck this away for safe keeping */

#ifdef HAVE_TZSET
    (void) tzset();		/* set the timezone if applicable */
#endif /* HAVE_TZSET */

    /*
     * loading the user & epasswd global variable from the passwd file
     * (must be done as root to get real passwd on some systems)
     */
    set_perms(PERM_ROOT);
    if ((pw_ent = getpwuid(uid)) == NULL) {
	user = uid2str(uid);
	log_error(GLOBAL_NO_PW_ENT);
	inform_user(GLOBAL_NO_PW_ENT);
	exit(1);
    }

    user = strdup(pw_ent -> pw_name);
    epasswd = strdup(pw_ent -> pw_passwd);
    if (user == NULL || epasswd == NULL) {
	perror("malloc");
	(void) fprintf(stderr, "%s: cannot allocate memory!\n", Argv[0]);
	exit(1);
    }

    /*
     * We only want to be root when we absolutely need it.
     * Since we set euid and ruid to 0 above, this will set the euid
     * to the * uid of the caller so (ruid, euid) == (0, user's uid).
     */
    set_perms(PERM_USER);

#ifdef UMASK
    (void) umask((mode_t)UMASK);
#endif /* UMASK */

    /*
     * so we know where we are... (do as user)
     */
    if (!getcwd(cwd, (size_t) sizeof(cwd))) {
    	(void) fprintf(stderr, "%s:  Can't get working directory!\n", Argv[0]);
	exit(1);
    }

#ifdef NO_ROOT_SUDO
    if (uid == 0) {
	(void) fprintf(stderr, "You are already root, you don't need to use sudo.\n");
	exit(1);
    }
#endif

    /*
     * load the host global variable from gethostname()
     * and use gethostbyname() if we want it to be fully qualified.
     */
    if ((gethostname(host, MAXHOSTNAMELEN))) {
	strcpy(host, "localhost");
	log_error(GLOBAL_NO_HOSTNAME);
	inform_user(GLOBAL_NO_HOSTNAME);
#ifdef FQDN
    } else {
	if ((h_ent = gethostbyname(host)) == NULL)
	    log_error(GLOBAL_HOST_UNREGISTERED);
	else
	    strcpy(host, h_ent -> h_name);
    }
#else
    }

    /*
     * load a list of ip addresses and netmasks into
     * the interfaces array.
     */
    load_interfaces();

    /*
     * We don't want to return the fully quallified name unless FQDN is set
     */
    if ((p = strchr(host, '.')))
	*p = '\0';
#endif /* FQDN */
}



/**********************************************************************
 *
 * parse_args()
 *
 *  this function parses the arguments to sudo
 */

static int parse_args()
{
    int ret=MODE_RUN;			/* what mode is suod to be run in? */

    if (Argc < 2)			/* no options and no command */
	usage(1);

    if (Argv[1][0] == '-') {
	if (Argc > 2)			/* only one -? option allowed */
	    usage(1);

	if (Argv[1][1] != '\0' && Argv[1][2] != '\0') {
	    (void) fprintf(stderr, "%s: Please use single character options\n", Argv[0]);
	    usage(1);
	}

	switch (Argv[1][1]) {
	    case 'v':
		ret = MODE_VALIDATE;
		break;
	    case 'k':
		ret = MODE_KILL;
		break;
	    case 'l':
		ret = MODE_LIST;
		break;
	    case 'V':
		ret = MODE_VERSION;
		break;
	    case 'h':
		ret = MODE_HELP;
		break;
	    case '\0':
		(void) fprintf(stderr, "%s: '-' requires an argument\n",
		    Argv[0]);
		usage(1);
	    default:
		(void) fprintf(stderr, "%s: Illegal option %s\n", Argv[0],
		    Argv[1]);
		usage(1);
	}
    }

    return(ret);
}



/**********************************************************************
 *
 * usage()
 *
 *  this function just gives you instructions and exits
 */

static void usage(exit_val)
    int exit_val;
{
    (void) fprintf(stderr, "usage: %s -V | -h | -l | -v | -k | <command>\n", Argv[0]);
    exit(exit_val);
}



/**********************************************************************
 *
 *  clean_env()
 *
 *  This function builds cleans up the environ pointer so that all execv*()'s
 *  omit LD_* variables and hard-code PATH if SECURE_PATH is defined.
 */

static void clean_env(envp)
    char **envp;
{
    /*
     * omit all LD_* environmental vars
     */
    rmenv(envp, "LD_", 3);
#ifdef __hpux
    rmenv(envp, "SHLIB_PATH", 10);
#endif /* __hpux */
#ifdef _AIX
    rmenv(envp, "LIBPATH", 7);
#endif /* _AIX */
#ifdef __alpha
    rmenv(envp, "_RLD_", 5);
#endif /* __alpha */

    /* set IFS to a sane default (same as sh(1) */
    sudo_setenv("IFS", " \t\n");

#ifdef SECURE_PATH
    if (!user_is_exempt())
    sudo_setenv("PATH", SECURE_PATH);
#endif /* SECURE_PATH */
}



/**********************************************************************
 *
 * rmenv()
 *
 *  this function removes things from the environment that match the
 *  string "s" up to length len [ie: with strncmp()].
 */

static void rmenv(envp, s, len)
    char ** envp;				/* pointer to environment */
    char * s;					/* string to search for */
    int len;					/* how much of it to check */
{
    char ** tenvp;				/* temp env pointer */
    char ** move;				/* used to move around */

    /*
     * cycle through the environment and purge strings that match s
     */
    for (tenvp=envp; *tenvp; tenvp++) {
	if (!strncmp(*tenvp, s, len)) {
	    /* matched: remove by shifting everything below one up */
	    for (move=tenvp; *move; move++)
		*move = *(move+1);
	    tenvp--;
	}
    }
}



/**********************************************************************
 *
 * add_env()
 *
 *  this function adds sudo-specific variables into the environment
 */

static void add_env()
{
    char *idstr;

    /* add the SUDO_COMMAND envariable */
    if (sudo_setenv("SUDO_COMMAND", cmnd)) {
	perror("malloc");
	(void) fprintf(stderr, "%s: cannot allocate memory!\n", Argv[0]);
	exit(1);
    }

    /* add the SUDO_USER envariable */
    if (sudo_setenv("SUDO_USER", user)) {
	perror("malloc");
	(void) fprintf(stderr, "%s: cannot allocate memory!\n", Argv[0]);
	exit(1);
    }

    /* add the SUDO_UID envariable */
    idstr = uid2str(uid);
    if (sudo_setenv("SUDO_UID", idstr)) {
	perror("malloc");
	(void) fprintf(stderr, "%s: cannot allocate memory!\n", Argv[0]);
	exit(1);
    }
    (void) free(idstr);

    /* add the SUDO_GID envariable */
    idstr = uid2str((uid_t)getegid());
    if (sudo_setenv("SUDO_GID", idstr)) {
	perror("malloc");
	(void) fprintf(stderr, "%s: cannot allocate memory!\n", Argv[0]);
	exit(1);
    }
    (void) free(idstr);
}



/**********************************************************************
 *
 *  load_cmnd()
 *
 *  This function sets the cmnd global variable based on Argv[1]
 */

static void load_cmnd()
{
    if (strlen(Argv[1]) > MAXPATHLEN) {
	errno = ENAMETOOLONG;
	(void) fprintf(stderr, "%s: %s: Pathname too long\n", Argv[0], Argv[1]);
	exit(1);
    }

    /*
     * Resolve the path
     */
    if ((cmnd = find_path(Argv[1])) == NULL) {
	(void) fprintf(stderr, "%s: %s: command not found\n", Argv[0], Argv[1]);
	exit(1);
    }
}



/**********************************************************************
 *
 *  load_interfaces()
 *
 *  This function sets the interfaces global variable
 *  and sets the constituent ip addrs and netmasks.
 */

static void load_interfaces()
{
    unsigned long localhost_mask;
    struct ifconf ifconf;
    struct ifreq ifreq;
    struct sockaddr_in *sin;
    char buf[BUFSIZ];
    int sock, i, j;
#ifdef _ISC
    struct ifreq *ifrp;
    struct strioctl strioctl;
#endif /* _ISC */

    /* so we can skip localhost and its ilk */
    localhost_mask = inet_addr("127.0.0.0");

#ifdef _ISC
	sock = socket(AF_INET, SOCK_STREAM, 0);
#else
	sock = socket(AF_INET, SOCK_DGRAM, 0);
#endif /* _ISC */
    if (sock < 0) {
	perror("socket");
	exit(1);
    }

    /*
     * get interface configuration or return (leaving interfaces NULL)
     */
    ifconf.ifc_len = sizeof(buf);
    ifconf.ifc_buf = buf;
#ifdef _ISC
    strioctl.ic_cmd = SIOCGIFCONF;
    strioctl.ic_dp = (char *)&ifconf;
    strioctl.ic_len = sizeof(ifconf);
    if (ioctl(sock, I_STR, &strioctl) < 0) {
	/* networking probably not installed in kernel */
	return;
    }

    /*
     * Still can't get any more than the loopback interface out
     * of the INTERACTIVE ioctl :-(
     */
     num_interfaces = 1;

#else
    if (ioctl(sock, SIOCGIFCONF, (char *)(&ifconf)) < 0) {
	/* networking probably not installed in kernel */
	return;
    }

    /*
     * find out how many interfaces exist
     */
    num_interfaces = ifconf.ifc_len / sizeof(struct ifreq);

#endif /* _ISC */
    /*
     * malloc() space for interfaces array
     */
    interfaces = (struct interface *) malloc(sizeof(struct interface) *
	num_interfaces);
    if (interfaces == NULL) {
	perror("malloc");
	(void) fprintf(stderr, "%s: cannot allocate memory!\n", Argv[0]);
	exit(1);
    }

    /*
     * for each interface, get the ip address and netmask
     */
    for (i = 0, j = 0; i < num_interfaces; i++) {
	(void) strncpy(ifreq.ifr_name, ifconf.ifc_req[i].ifr_name,
	    sizeof(ifreq.ifr_name));

	/* get the ip address */
#ifdef _ISC
	strioctl.ic_cmd = SIOCGIFADDR;
	strioctl.ic_dp = (char *)&ifreq;
	strioctl.ic_len = sizeof(ifreq);
	if (ioctl(sock, I_STR, &strioctl) < 0) {
#else
	if (ioctl(sock, SIOCGIFADDR, (caddr_t)(&ifreq))) {
#endif /* _ISC */
	    /* non-fatal error if interface is down or not supported */
	    if (errno == EADDRNOTAVAIL || errno == EAFNOSUPPORT)
		continue;

	    (void) fprintf(stderr, "%s: Error, ioctl: SIOCGIFADDR ", Argv[0]);
	    perror("");
	    exit(1);
	}
	sin = (struct sockaddr_in *)&ifreq.ifr_addr;

	/* make sure we don't have a dupe (usually consecutive) */
	if (j > 0 && memcmp(&interfaces[j-1].addr, &(sin->sin_addr),
	    sizeof(sin->sin_addr)) == 0)
	    continue;

	/* store the ip address */
	(void) memcpy(&interfaces[j].addr, &(sin->sin_addr),
	    sizeof(struct in_addr));

	/* get the netmask */
#ifdef SIOCGIFNETMASK
	(void) strncpy(ifreq.ifr_name, ifconf.ifc_req[i].ifr_name,
	    sizeof(ifreq.ifr_name));
	if (ioctl(sock, SIOCGIFNETMASK, (char *)(&ifreq)) == 0) {
	    /* store the netmask */
	    (void) memcpy(&interfaces[j].netmask, &(sin->sin_addr),
		sizeof(struct in_addr));
	} else {
#else
	{
#endif /* SIOCGIFNETMASK */
	    if (IN_CLASSC(interfaces[j].addr.s_addr))
		interfaces[j].netmask.s_addr = htonl(IN_CLASSC_NET);
	    else if (IN_CLASSB(interfaces[j].addr.s_addr))
		interfaces[j].netmask.s_addr = htonl(IN_CLASSB_NET);
	    else
		interfaces[j].netmask.s_addr = htonl(IN_CLASSA_NET);
	}

	/* avoid localhost and friends */
	if ((interfaces[j].addr.s_addr & interfaces[j].netmask.s_addr) ==
	    localhost_mask)
	    continue;

	++j;
    }

    /* if there were bogus entries, realloc the array */
    if (i != j) {
	num_interfaces = j;
	interfaces = (struct interface *) realloc(interfaces,
	    sizeof(struct interface) * num_interfaces);
	if (interfaces == NULL) {
	    perror("realloc");
	    (void) fprintf(stderr, "%s: cannot allocate memory!\n", Argv[0]);
	    exit(1);
	}
    }
}



/**********************************************************************
 *
 *  check_sudoers()
 *
 *  This function check to see that the sudoers file is owned by
 *  SUDOERS_OWNER and not writable by anyone else.
 */

static int check_sudoers()
{
    struct stat statbuf;
    struct passwd *pw_ent;

    if (!(pw_ent = getpwnam(SUDOERS_OWNER)))
	return(SUDOERS_NO_OWNER);

    if (lstat(_PATH_SUDO_SUDOERS, &statbuf))
	return(NO_SUDOERS_FILE);
    else if (!S_ISREG(statbuf.st_mode))
	return(SUDOERS_NOT_FILE);
    else if (statbuf.st_uid != pw_ent -> pw_uid)
        return(SUDOERS_WRONG_OWNER);
    else if ((statbuf.st_mode & 0000066))
        return(SUDOERS_RW_OTHER); 

    return(ALL_SYSTEMS_GO);
}



/**********************************************************************
 *
 * set_perms()
 *
 *  this function sets real and effective uids and gids based on perm.
 */

void set_perms(perm)
    int perm;
{
    struct passwd *pw_ent;

    switch(perm) {
	case        PERM_ROOT :
				if (setuid(0)) {
				    perror("setuid(0)");
				    exit(1);
				}
			      	break;

	case   PERM_FULL_ROOT :
				if (setuid(0)) {  
				    perror("setuid(0)");
				    exit(1);
				}

				if (!(pw_ent = getpwuid(0))) {
				    perror("getpwuid(0)");
				} else if (setgid(pw_ent->pw_gid)) {
				    perror("setgid");
    	    	    	    	}
			      	break;

	case        PERM_USER : 
    	    	    	        if (seteuid(uid)) {
    	    	    	            perror("seteuid(uid)");
    	    	    	            exit(1); 
    	    	    	        }
			      	break;
				
	case   PERM_FULL_USER : 
				if (setuid(0)) {
				    perror("setuid(0)");
				    exit(1);
				}

				if (setuid(uid)) {
				    perror("setuid(uid)");
				    exit(1);
				}

			      	break;

	case   PERM_SUDOERS : 
				if (setuid(0)) {
				    perror("setuid(0)");
				    exit(1);
				}

				if (!(pw_ent = getpwnam(SUDOERS_OWNER))) {
				    (void) fprintf(stderr, "%s: no passwd entry for sudoers file owner (%s)\n", Argv[0], SUDOERS_OWNER);
				    exit(1);
				} else if (seteuid(pw_ent->pw_uid)) {
				    (void) fprintf(stderr, "%s: ",
							   SUDOERS_OWNER);
				    perror("");
				    exit(1);
    	    	    	    	}

			      	break;
    }
}



/**********************************************************************
 *
 * uid2str()
 *
 *  this function allocates memory for a strings version of uid,
 *  then converts uid to a string and returns it.
 */

static char *uid2str(uid)
    uid_t uid;
{
    int len;
    unsigned n;
    char *uidstr;

    for (len = 1, n = (unsigned) uid; (unsigned) (n = n / 10) != 0; )
	++len;
    
    uidstr = (char *) malloc(len+1);
    if (uidstr == NULL) {
	perror("malloc");
	(void) fprintf(stderr, "%s: cannot allocate memory!\n", Argv[0]);
	exit(1);
    }

    (void) sprintf(uidstr, "%u", (unsigned) uid);

    return(uidstr);
}
