#define def_syslog              (sudo_defs_table[0].sd_un.ival)
#define I_SYSLOG                0
#define def_syslog_goodpri      (sudo_defs_table[1].sd_un.ival)
#define I_SYSLOG_GOODPRI        1
#define def_syslog_badpri       (sudo_defs_table[2].sd_un.ival)
#define I_SYSLOG_BADPRI         2
#define def_long_otp_prompt     (sudo_defs_table[3].sd_un.flag)
#define I_LONG_OTP_PROMPT       3
#define def_ignore_dot          (sudo_defs_table[4].sd_un.flag)
#define I_IGNORE_DOT            4
#define def_mail_always         (sudo_defs_table[5].sd_un.flag)
#define I_MAIL_ALWAYS           5
#define def_mail_badpass        (sudo_defs_table[6].sd_un.flag)
#define I_MAIL_BADPASS          6
#define def_mail_no_user        (sudo_defs_table[7].sd_un.flag)
#define I_MAIL_NO_USER          7
#define def_mail_no_host        (sudo_defs_table[8].sd_un.flag)
#define I_MAIL_NO_HOST          8
#define def_mail_no_perms       (sudo_defs_table[9].sd_un.flag)
#define I_MAIL_NO_PERMS         9
#define def_tty_tickets         (sudo_defs_table[10].sd_un.flag)
#define I_TTY_TICKETS           10
#define def_lecture             (sudo_defs_table[11].sd_un.tuple)
#define I_LECTURE               11
#define def_lecture_file        (sudo_defs_table[12].sd_un.str)
#define I_LECTURE_FILE          12
#define def_authenticate        (sudo_defs_table[13].sd_un.flag)
#define I_AUTHENTICATE          13
#define def_root_sudo           (sudo_defs_table[14].sd_un.flag)
#define I_ROOT_SUDO             14
#define def_log_host            (sudo_defs_table[15].sd_un.flag)
#define I_LOG_HOST              15
#define def_log_year            (sudo_defs_table[16].sd_un.flag)
#define I_LOG_YEAR              16
#define def_shell_noargs        (sudo_defs_table[17].sd_un.flag)
#define I_SHELL_NOARGS          17
#define def_set_home            (sudo_defs_table[18].sd_un.flag)
#define I_SET_HOME              18
#define def_always_set_home     (sudo_defs_table[19].sd_un.flag)
#define I_ALWAYS_SET_HOME       19
#define def_path_info           (sudo_defs_table[20].sd_un.flag)
#define I_PATH_INFO             20
#define def_fqdn                (sudo_defs_table[21].sd_un.flag)
#define I_FQDN                  21
#define def_insults             (sudo_defs_table[22].sd_un.flag)
#define I_INSULTS               22
#define def_requiretty          (sudo_defs_table[23].sd_un.flag)
#define I_REQUIRETTY            23
#define def_env_editor          (sudo_defs_table[24].sd_un.flag)
#define I_ENV_EDITOR            24
#define def_rootpw              (sudo_defs_table[25].sd_un.flag)
#define I_ROOTPW                25
#define def_runaspw             (sudo_defs_table[26].sd_un.flag)
#define I_RUNASPW               26
#define def_targetpw            (sudo_defs_table[27].sd_un.flag)
#define I_TARGETPW              27
#define def_use_loginclass      (sudo_defs_table[28].sd_un.flag)
#define I_USE_LOGINCLASS        28
#define def_set_logname         (sudo_defs_table[29].sd_un.flag)
#define I_SET_LOGNAME           29
#define def_stay_setuid         (sudo_defs_table[30].sd_un.flag)
#define I_STAY_SETUID           30
#define def_preserve_groups     (sudo_defs_table[31].sd_un.flag)
#define I_PRESERVE_GROUPS       31
#define def_loglinelen          (sudo_defs_table[32].sd_un.ival)
#define I_LOGLINELEN            32
#define def_timestamp_timeout   (sudo_defs_table[33].sd_un.fval)
#define I_TIMESTAMP_TIMEOUT     33
#define def_passwd_timeout      (sudo_defs_table[34].sd_un.fval)
#define I_PASSWD_TIMEOUT        34
#define def_passwd_tries        (sudo_defs_table[35].sd_un.ival)
#define I_PASSWD_TRIES          35
#define def_umask               (sudo_defs_table[36].sd_un.mode)
#define I_UMASK                 36
#define def_logfile             (sudo_defs_table[37].sd_un.str)
#define I_LOGFILE               37
#define def_mailerpath          (sudo_defs_table[38].sd_un.str)
#define I_MAILERPATH            38
#define def_mailerflags         (sudo_defs_table[39].sd_un.str)
#define I_MAILERFLAGS           39
#define def_mailto              (sudo_defs_table[40].sd_un.str)
#define I_MAILTO                40
#define def_mailfrom            (sudo_defs_table[41].sd_un.str)
#define I_MAILFROM              41
#define def_mailsub             (sudo_defs_table[42].sd_un.str)
#define I_MAILSUB               42
#define def_badpass_message     (sudo_defs_table[43].sd_un.str)
#define I_BADPASS_MESSAGE       43
#define def_timestampdir        (sudo_defs_table[44].sd_un.str)
#define I_TIMESTAMPDIR          44
#define def_timestampowner      (sudo_defs_table[45].sd_un.str)
#define I_TIMESTAMPOWNER        45
#define def_exempt_group        (sudo_defs_table[46].sd_un.str)
#define I_EXEMPT_GROUP          46
#define def_passprompt          (sudo_defs_table[47].sd_un.str)
#define I_PASSPROMPT            47
#define def_passprompt_override (sudo_defs_table[48].sd_un.flag)
#define I_PASSPROMPT_OVERRIDE   48
#define def_runas_default       (sudo_defs_table[49].sd_un.str)
#define I_RUNAS_DEFAULT         49
#define def_secure_path         (sudo_defs_table[50].sd_un.str)
#define I_SECURE_PATH           50
#define def_editor              (sudo_defs_table[51].sd_un.str)
#define I_EDITOR                51
#define def_listpw              (sudo_defs_table[52].sd_un.tuple)
#define I_LISTPW                52
#define def_verifypw            (sudo_defs_table[53].sd_un.tuple)
#define I_VERIFYPW              53
#define def_noexec              (sudo_defs_table[54].sd_un.flag)
#define I_NOEXEC                54
#define def_ignore_local_sudoers (sudo_defs_table[55].sd_un.flag)
#define I_IGNORE_LOCAL_SUDOERS  55
#define def_closefrom           (sudo_defs_table[56].sd_un.ival)
#define I_CLOSEFROM             56
#define def_closefrom_override  (sudo_defs_table[57].sd_un.flag)
#define I_CLOSEFROM_OVERRIDE    57
#define def_setenv              (sudo_defs_table[58].sd_un.flag)
#define I_SETENV                58
#define def_env_reset           (sudo_defs_table[59].sd_un.flag)
#define I_ENV_RESET             59
#define def_env_check           (sudo_defs_table[60].sd_un.list)
#define I_ENV_CHECK             60
#define def_env_delete          (sudo_defs_table[61].sd_un.list)
#define I_ENV_DELETE            61
#define def_env_keep            (sudo_defs_table[62].sd_un.list)
#define I_ENV_KEEP              62
#define def_role                (sudo_defs_table[63].sd_un.str)
#define I_ROLE                  63
#define def_type                (sudo_defs_table[64].sd_un.str)
#define I_TYPE                  64
#define def_env_file            (sudo_defs_table[65].sd_un.str)
#define I_ENV_FILE              65
#define def_sudoers_locale      (sudo_defs_table[66].sd_un.str)
#define I_SUDOERS_LOCALE        66
#define def_visiblepw           (sudo_defs_table[67].sd_un.flag)
#define I_VISIBLEPW             67
#define def_pwfeedback          (sudo_defs_table[68].sd_un.flag)
#define I_PWFEEDBACK            68
#define def_fast_glob           (sudo_defs_table[69].sd_un.flag)
#define I_FAST_GLOB             69
#define def_umask_override      (sudo_defs_table[70].sd_un.flag)
#define I_UMASK_OVERRIDE        70
#define def_log_input           (sudo_defs_table[71].sd_un.flag)
#define I_LOG_INPUT             71
#define def_log_output          (sudo_defs_table[72].sd_un.flag)
#define I_LOG_OUTPUT            72
#define def_compress_io         (sudo_defs_table[73].sd_un.flag)
#define I_COMPRESS_IO           73
#define def_use_pty             (sudo_defs_table[74].sd_un.flag)
#define I_USE_PTY               74
#define def_group_plugin        (sudo_defs_table[75].sd_un.str)
#define I_GROUP_PLUGIN          75
#define def_iolog_dir           (sudo_defs_table[76].sd_un.str)
#define I_IOLOG_DIR             76
#define def_iolog_file          (sudo_defs_table[77].sd_un.str)
#define I_IOLOG_FILE            77
#define def_set_utmp            (sudo_defs_table[78].sd_un.flag)
#define I_SET_UTMP              78
#define def_utmp_runas          (sudo_defs_table[79].sd_un.flag)
#define I_UTMP_RUNAS            79
#define def_privs               (sudo_defs_table[80].sd_un.str)
#define I_PRIVS                 80
#define def_limitprivs          (sudo_defs_table[81].sd_un.str)
#define I_LIMITPRIVS            81
#define def_exec_background     (sudo_defs_table[82].sd_un.flag)
#define I_EXEC_BACKGROUND       82
#define def_maxseq              (sudo_defs_table[83].sd_un.ival)
#define I_MAXSEQ                83

enum def_tuple {
	never,
	once,
	always,
	any,
	all
};
