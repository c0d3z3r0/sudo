/*
 * Copyright (c) 1999-2005, 2007-2012 Todd C. Miller <Todd.Miller@courtesan.com>
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
#include <errno.h>

#include "sudo.h"
#include "sudo_plugin.h"
#include "sudo_plugin_int.h"

extern int tgetpass_flags; /* XXX */

/*
 * Sudo conversation function.
 */
int
sudo_conversation(int num_msgs, const struct sudo_conv_message msgs[],
    struct sudo_conv_reply replies[], struct sudo_conv_callback *callback)
{
    struct sudo_conv_reply *repl;
    const struct sudo_conv_message *msg;
    char *pass;
    int n, flags = tgetpass_flags;
    const int conv_debug_instance = sudo_debug_get_active_instance();

    sudo_debug_set_active_instance(sudo_debug_instance);

    for (n = 0; n < num_msgs; n++) {
	msg = &msgs[n];
	repl = &replies[n];
	switch (msg->msg_type & 0xff) {
	    case SUDO_CONV_PROMPT_ECHO_ON:
	    case SUDO_CONV_PROMPT_MASK:
		if (msg->msg_type == SUDO_CONV_PROMPT_ECHO_ON)
		    SET(flags, TGP_ECHO);
		else
		    SET(flags, TGP_MASK);
		/* FALLTHROUGH */
	    case SUDO_CONV_PROMPT_ECHO_OFF:
		if (ISSET(msg->msg_type, SUDO_CONV_PROMPT_ECHO_OK))
		    SET(flags, TGP_NOECHO_TRY);
		/* Read the password unless interrupted. */
		pass = tgetpass(msg->msg, msg->timeout, flags, callback);
		if (pass == NULL)
		    goto err;
		if ((repl->reply = strdup(pass)) == NULL) {
		    sudo_fatalx_nodebug(U_("%s: %s"), "sudo_conversation",
			U_("unable to allocate memory"));
		}
		memset_s(pass, SUDO_CONV_REPL_MAX, 0, strlen(pass));
		break;
	    case SUDO_CONV_INFO_MSG:
		if (msg->msg)
		    (void) fputs(msg->msg, stdout);
		break;
	    case SUDO_CONV_ERROR_MSG:
		if (msg->msg)
		    (void) fputs(msg->msg, stderr);
		break;
	    default:
		goto err;
	}
    }

    sudo_debug_set_active_instance(conv_debug_instance);
    return 0;

err:
    /* Zero and free allocated memory and return an error. */
    do {
	repl = &replies[n];
	if (repl->reply != NULL) {
	    memset_s(repl->reply, SUDO_CONV_REPL_MAX, 0, strlen(repl->reply));
	    free(repl->reply);
	    repl->reply = NULL;
	}
    } while (n--);

    sudo_debug_set_active_instance(conv_debug_instance);
    return -1;
}

int
sudo_conversation_1_7(int num_msgs, const struct sudo_conv_message msgs[],
    struct sudo_conv_reply replies[])
{
    return sudo_conversation(num_msgs, msgs, replies, NULL);
}

int
sudo_conversation_printf(int msg_type, const char *fmt, ...)
{
    va_list ap;
    int len;
    const int conv_debug_instance = sudo_debug_get_active_instance();

    sudo_debug_set_active_instance(sudo_debug_instance);

    switch (msg_type) {
    case SUDO_CONV_INFO_MSG:
	va_start(ap, fmt);
	len = vfprintf(stdout, fmt, ap);
	va_end(ap);
	break;
    case SUDO_CONV_ERROR_MSG:
	va_start(ap, fmt);
	len = vfprintf(stderr, fmt, ap);
	va_end(ap);
	break;
    default:
	len = -1;
	errno = EINVAL;
	break;
    }

    sudo_debug_set_active_instance(conv_debug_instance);
    return len;
}
