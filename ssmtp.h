/*

 $Id: ssmtp.h,v 2.5 2002/02/10 19:32:28 matt Exp $

 See COPYRIGHT for the license

*/
#include <sys/types.h>
#include <stdio.h>
#include <pwd.h>

#define BUF_SZ  (1024 * 2)	/* A pretty large buffer, but not outrageous */

#define MAXWAIT (10 * 60)	/* Maximum wait between commands, in seconds */
#define MEDWAIT (5 * 60)

#define MAXSYSUID 999		/* Highest UID which is a system account */

#ifndef _POSIX_ARG_MAX
#define MAXARGS 4096
#else
#define MAXARGS  _POSIX_ARG_MAX
#endif

#define YES 1
#define NO 0

typedef enum {False, True} bool_t;

struct string_list {
	char *string;
	struct string_list *next;
};

typedef struct string_list headers_t;
typedef struct string_list rcpt_t;

/* main.c */
char *ADDR_parse(char *);
char *append_domain(char *);
char *FROM_strip(char *);
void FROM_rewrite(char *, int);
void FROM_format(void);
void handler(void);
void standardise(char *);
void revaliases(struct passwd *);
int ssmtp(char **);
char *basename(char *);

/* header.c */
void HEADER_record(char *);
void HEADER_parse(FILE *);
void HEADER_save(char *);

/* rcpt.c */
void RCPT_parse(char *);
void RCPT_save(char *);
char * RCPT_remap(char *);

/* net.c */
ssize_t fd_getc(int, void *);
int SMTP_read(int, char *);
ssize_t fd_puts(int, const void *, size_t);
void SMTP_write(int, char *, ...);
int SMTP_open(char *, int);
int SMTP_OK(int, char *);

/* log.c */
void log_event(int, char *, ...);
void dead_letter(void);
void die(char *, ...);
void paq(char *, ...);

/* options.c */
char **parse_options(int, char **);
bool_t read_config(void);
void parse_config(FILE *);

/* util.c */
char *STRIP__leading_space(char *);
char *STRIP__trailing_space(char *);

/* arpadate.c */
void get_arpadate(char *);

/* base64.c */
void to64frombits(unsigned char *, const unsigned char *, int);
int from64tobits(char *, const char *);

