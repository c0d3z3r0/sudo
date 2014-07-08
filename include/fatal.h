/*
 * Copyright (c) 2004, 2010-2014 Todd C. Miller <Todd.Miller@courtesan.com>
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

#ifndef _SUDO_FATAL_H_
#define	_SUDO_FATAL_H_

#include <stdarg.h>

/*
 * We wrap fatal/fatalx and warn/warnx so that the same output can
 * go to the debug file, if there is one.
 */
#if (defined(SUDO_ERROR_WRAP) && SUDO_ERROR_WRAP == 0) || defined(NO_VARIADIC_MACROS)
# define sudo_fatal sudo_fatal_nodebug
# define sudo_fatalx sudo_fatalx_nodebug
# define sudo_warn sudo_warn_nodebug
# define sudo_warnx sudo_warnx_nodebug
# define sudo_vfatal(fmt, ap) sudo_vfatal_nodebug((fmt), (ap))
# define sudo_vfatalx(fmt, ap) sudo_vfatalx_nodebug((fmt), (ap))
# define sudo_vwarn(fmt, ap) sudo_vwarn_nodebug((fmt), (ap))
# define sudo_vwarnx(fmt, ap) sudo_vwarnx_nodebug((fmt), (ap))
#else /* SUDO_ERROR_WRAP */
# if defined(__GNUC__) && __GNUC__ == 2
#  define sudo_fatal(fmt...) do {					       \
    sudo_debug_printf2(__func__, __FILE__, __LINE__,			       \
	SUDO_DEBUG_ERROR|SUDO_DEBUG_LINENO|SUDO_DEBUG_ERRNO|sudo_debug_subsys, \
	fmt);								       \
    sudo_fatal_nodebug(fmt);						       \
} while (0)
#  define sudo_fatalx(fmt...) do {					       \
    sudo_debug_printf2(__func__, __FILE__, __LINE__,			       \
	SUDO_DEBUG_ERROR|SUDO_DEBUG_LINENO|sudo_debug_subsys, fmt);	       \
    sudo_fatalx_nodebug(fmt);						       \
} while (0)
#  define sudo_warn(fmt...) do {					       \
    sudo_debug_printf2(__func__, __FILE__, __LINE__,			       \
	SUDO_DEBUG_ERROR|SUDO_DEBUG_LINENO|SUDO_DEBUG_ERRNO|sudo_debug_subsys, \
	fmt);								       \
    sudo_warn_nodebug(fmt);						       \
} while (0)
#  define sudo_warnx(fmt...) do {					       \
    sudo_debug_printf2(__func__, __FILE__, __LINE__,			       \
	SUDO_DEBUG_ERROR|SUDO_DEBUG_LINENO|sudo_debug_subsys, fmt);	       \
    sudo_warnx_nodebug(fmt);						       \
} while (0)
# else
#  define sudo_fatal(...) do {						       \
    sudo_debug_printf2(__func__, __FILE__, __LINE__,			       \
	SUDO_DEBUG_ERROR|SUDO_DEBUG_LINENO|SUDO_DEBUG_ERRNO|sudo_debug_subsys, \
	__VA_ARGS__);							       \
    sudo_fatal_nodebug(__VA_ARGS__);					       \
} while (0)
#  define sudo_fatalx(...) do {						       \
    sudo_debug_printf2(__func__, __FILE__, __LINE__,			       \
	SUDO_DEBUG_ERROR|SUDO_DEBUG_LINENO|sudo_debug_subsys, __VA_ARGS__);    \
    sudo_fatalx_nodebug(__VA_ARGS__);					       \
} while (0)
#  define sudo_warn(...) do {						       \
    sudo_debug_printf2(__func__, __FILE__, __LINE__,			       \
	SUDO_DEBUG_WARN|SUDO_DEBUG_LINENO|SUDO_DEBUG_ERRNO|sudo_debug_subsys,  \
	__VA_ARGS__);							       \
    sudo_warn_nodebug(__VA_ARGS__);					       \
} while (0)
#  define sudo_warnx(...) do {						       \
    sudo_debug_printf2(__func__, __FILE__, __LINE__,			       \
	SUDO_DEBUG_WARN|SUDO_DEBUG_LINENO|sudo_debug_subsys, __VA_ARGS__);     \
    sudo_warnx_nodebug(__VA_ARGS__);					       \
} while (0)
# endif /* __GNUC__ == 2 */
# define sudo_vfatal(fmt, ap) do {					       \
    va_list ap2;							       \
    va_copy(ap2, (ap));							       \
    sudo_debug_vprintf2(__func__, __FILE__, __LINE__,			       \
	SUDO_DEBUG_ERROR|SUDO_DEBUG_LINENO|SUDO_DEBUG_ERRNO|sudo_debug_subsys, \
	(fmt), ap2);							       \
    sudo_vfatal_nodebug((fmt), (ap));					       \
} while (0)
# define sudo_vfatalx(fmt, ap) do {					       \
    va_list ap2;							       \
    va_copy(ap2, (ap));							       \
    sudo_debug_vprintf2(__func__, __FILE__, __LINE__,			       \
	SUDO_DEBUG_ERROR|SUDO_DEBUG_LINENO|sudo_debug_subsys, (fmt), ap2);     \
    sudo_vfatalx_nodebug((fmt), (ap));					       \
} while (0)
# define sudo_vwarn(fmt, ap) do {					       \
    va_list ap2;							       \
    va_copy(ap2, (ap));							       \
    sudo_debug_vprintf2(__func__, __FILE__, __LINE__,			       \
	SUDO_DEBUG_WARN|SUDO_DEBUG_LINENO|SUDO_DEBUG_ERRNO|sudo_debug_subsys,  \
	(fmt), ap2);							       \
    sudo_vwarn_nodebug((fmt), (ap));					       \
} while (0)
# define sudo_vwarnx(fmt, ap) do {					       \
    va_list ap2;							       \
    va_copy(ap2, (ap));							       \
    sudo_debug_vprintf2(__func__, __FILE__, __LINE__,			       \
	SUDO_DEBUG_WARN|SUDO_DEBUG_LINENO|sudo_debug_subsys, (fmt), ap2);      \
    sudo_vwarnx_nodebug((fmt), (ap));					       \
} while (0)
#endif /* SUDO_ERROR_WRAP */

extern int (*sudo_printf)(int msg_type, const char *fmt, ...);

__dso_public int  sudo_fatal_callback_deregister(void (*func)(void));
__dso_public int  sudo_fatal_callback_register(void (*func)(void));
__dso_public char *sudo_warn_gettext(const char *msgid) __format_arg(1);
__dso_public char *sudo_warn_strerror(int errnum);
__dso_public void sudo_fatal_nodebug(const char *, ...) __printf0like(1, 2) __attribute__((__noreturn__));
__dso_public void sudo_fatalx_nodebug(const char *, ...) __printflike(1, 2) __attribute__((__noreturn__));
__dso_public void sudo_vfatal_nodebug(const char *, va_list ap) __printf0like(1, 0) __attribute__((__noreturn__));
__dso_public void sudo_vfatalx_nodebug(const char *, va_list ap) __printflike(1, 0) __attribute__((__noreturn__));
__dso_public void sudo_warn_nodebug(const char *, ...) __printf0like(1, 2);
__dso_public void sudo_warnx_nodebug(const char *, ...) __printflike(1, 2);
__dso_public void sudo_vwarn_nodebug(const char *, va_list ap) __printf0like(1, 0);
__dso_public void sudo_vwarnx_nodebug(const char *, va_list ap) __printflike(1, 0);

#endif /* _SUDO_FATAL_H_ */
