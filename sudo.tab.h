#define COMMAND 257
#define ALIAS 258
#define DEFVAR 259
#define NTWKADDR 260
#define NETGROUP 261
#define USERGROUP 262
#define WORD 263
#define DEFAULTS 264
#define DEFAULTS_HOST 265
#define DEFAULTS_USER 266
#define DEFAULTS_RUNAS 267
#define RUNAS 268
#define NOPASSWD 269
#define PASSWD 270
#define NOEXEC 271
#define EXEC 272
#define MONITOR 273
#define NOMONITOR 274
#define ALL 275
#define COMMENT 276
#define HOSTALIAS 277
#define CMNDALIAS 278
#define USERALIAS 279
#define RUNASALIAS 280
#define ERROR 281
#ifndef YYSTYPE_DEFINED
#define YYSTYPE_DEFINED
typedef union {
    char *string;
    int BOOLEAN;
    struct sudo_command command;
    int tok;
} YYSTYPE;
#endif /* YYSTYPE_DEFINED */
extern YYSTYPE yylval;
