/*

 $Id: log.c,v 1.9 2002/02/10 19:31:54 matt Exp $

 See COPYRIGHT for the license

*/
#include <sys/param.h>
#include <unistd.h>
#include <stdarg.h>
#include <syslog.h>
#include <stdio.h>
#include <pwd.h>
#include "ssmtp.h"

extern char *prog_name;
extern int log_level;


void log_event(int priority, char *format, ...)
{
	char buf[(BUF_SZ + 1)];
	va_list ap;

	va_start(ap, format);
	(void)vsnprintf(buf, BUF_SZ, format, ap);
	va_end(ap);

#ifdef LOGFILE
	if((fp = fopen("/tmp/ssmtp.log", "a")) != (FILE *)NULL) {
		(void)fprintf(fp, "%s\n", buf);
		(void)fclose(fp);
	}
	else {
		(void)fprintf(stderr, "Can't write to /tmp/ssmtp.log\n");
	}
#endif

#if HAVE_SYSLOG_H
#if OLDSYSLOG
	openlog("sSMTP", LOG_PID);
#else
	openlog("sSMTP", LOG_PID, LOG_MAIL);
#endif
	syslog(priority, "%s", buf);
	closelog();
#endif
}

/*
dead_letter() -- save stdin to ~/dead.letter, if possible
*/
void dead_letter(void)
{
	char path[(MAXPATHLEN + 1)], buf[(BUF_SZ + 1)];
	struct passwd *pw;
	uid_t uid;
	FILE *fp;

	uid = getuid();
	pw = getpwuid(uid);

	if(isatty(fileno(stdin))) {
		if(log_level > 0) {
			log_event(LOG_ERR, "stdin is a TTY - not saving to %s/dead.letter, pw->pw_dir");
		}
		return;
	}

	if(pw == (struct passwd *)NULL) {
		/* Far to early to save things. */
		if(log_level > 0) {
			log_event(LOG_ERR, "No sender (can't happen), failing horribly");
		}
		return;
	}

	if(snprintf(path, BUF_SZ, "%s/dead.letter", pw->pw_dir) == -1) {
		/* can't use die() here since dead_letter() is called from die() */
		exit(1);
	}

	if((fp = fopen(path, "a")) == (FILE *)NULL) {
		/* Perhaps the person doesn't have a homedir... */
		if(log_level > 0) {
			log_event(LOG_ERR, "Can't open %s, failing horribly!", path);
		}
		return;
	}

	/* Make sure we start on a new line, */
	/* with a blank line separating messages. */
	(void)fprintf(fp, "\n\n");

	while(fgets(buf, sizeof(buf), stdin)) {
		(void)fputs(buf, fp);
	}

	if(fclose(fp) == -1) {
		if(log_level > 0) {
			log_event(LOG_ERR, "Can't close %s/dead.letter, possibly truncated", pw->pw_dir);
		}
	}
}

void die(char *format, ...)
{
	char buf[(BUF_SZ + 1)];
	va_list ap;

	va_start(ap, format);
	(void)vsnprintf(buf, BUF_SZ, format, ap);
	va_end(ap);

	(void)fprintf(stderr, "%s: %s\n", prog_name, buf);
	log_event(LOG_ERR, "%s", buf);

	/* Send message to dead.letter */
	(void)dead_letter();

	exit(1);
}

void paq(char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	(void)vfprintf(stderr, format, ap);
	va_end(ap);

	exit(0);
}
