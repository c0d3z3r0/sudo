/*
 * Copyright (c) 2011-2014 Todd C. Miller <Todd.Miller@courtesan.com>
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

#ifndef _SUDO_CONF_H
#define _SUDO_CONF_H

#include "sudo_queue.h"

#define GROUP_SOURCE_ADAPTIVE	0
#define GROUP_SOURCE_STATIC	1
#define GROUP_SOURCE_DYNAMIC	2

struct plugin_info {
    TAILQ_ENTRY(plugin_info) entries;
    const char *path;
    const char *symbol_name;
    char * const * options;
    int lineno;
};
TAILQ_HEAD(plugin_info_list, plugin_info);

/* Read main sudo.conf file. */
__dso_public void sudo_conf_read_v1(const char *conf_file);
#define sudo_conf_read(_a) sudo_conf_read_v1((_a))

/* Accessor functions. */
__dso_public const char *sudo_conf_askpass_path_v1(void);
__dso_public const char *sudo_conf_sesh_path_v1(void);
__dso_public const char *sudo_conf_noexec_path_v1(void);
__dso_public const char *sudo_conf_plugin_dir_path_v1(void);
__dso_public const char *sudo_conf_debug_flags_v1(void);
__dso_public struct plugin_info_list *sudo_conf_plugins_v1(void);
__dso_public bool sudo_conf_disable_coredump_v1(void);
__dso_public bool sudo_conf_probe_interfaces_v1(void);
__dso_public int sudo_conf_group_source_v1(void);
__dso_public int sudo_conf_max_groups_v1(void);
#define sudo_conf_askpass_path() sudo_conf_askpass_path_v1()
#define sudo_conf_sesh_path() sudo_conf_sesh_path_v1()
#define sudo_conf_noexec_path() sudo_conf_noexec_path_v1()
#define sudo_conf_plugin_dir_path() sudo_conf_plugin_dir_path_v1()
#define sudo_conf_debug_flags() sudo_conf_debug_flags_v1()
#define sudo_conf_plugins() sudo_conf_plugins_v1()
#define sudo_conf_disable_coredump() sudo_conf_disable_coredump_v1()
#define sudo_conf_probe_interfaces() sudo_conf_probe_interfaces_v1()
#define sudo_conf_group_source() sudo_conf_group_source_v1()
#define sudo_conf_max_groups() sudo_conf_max_groups_v1()

#endif /* _SUDO_CONF_H */
