/*

 $Id: main.c,v 2.54 2002/04/01 12:59:24 matt Exp $

 sSMTP -- send messages via SMTP to a mailhub for local delivery or forwarding.
 This program is used in place of /usr/sbin/sendmail, called by "mail" (et all).
 sSMTP does a selected subset of sendmail's standard tasks (including exactly
 one rewriting task), and explains if you ask it to do something it can't. It
 then sends the mail to the mailhub via an SMTP connection. Believe it or not,
 this is nothing but a filter

 See COPYRIGHT for the license

*/
#include <sys/param.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <signal.h>
#include <setjmp.h>
#include <string.h>
#include "ssmtp.h"

char *prog_name = (char)NULL;

jmp_buf TimeoutJmpBuf;		/* Timeout waiting for input from network. */

rcpt_t RCPT_list, *rt;
headers_t headers, *ht;

bool_t SEEN_from = False;
#ifdef HASTO_OPTION
bool_t SEEN_to = False;
#endif
bool_t SEEN_date = False;

bool_t UseTLS = False;		/* Use SSL to transfer mail to HUB */
bool_t UseTLSCert = False;	/* Use a certificate to transfer SSL mail */
char *TLSCert = "/etc/ssl/certs/ssmtp.pem";	/* Default Certificate */ 

char *full_from = (char)NULL;	/* Use this as the From: address */
char *full_name;

/* Current date in RFC format. */
#define ARPADATE_LENGTH 32
char arpa_date[ARPADATE_LENGTH];

extern bool_t do_rewrite;
extern bool_t minus_t;

extern char *rewrite_domain;
extern char host_name[];
extern char *mail_from;
extern char *mail_hub;
extern char *auth_user;
extern char *auth_pass;
extern char *minus_F;

extern int smtp_port;
extern int log_level;


#ifdef INET6
extern int protocolFamily;	/* protocol family which used in SMTP connection */
#endif

/*
ADDR_parse() -- parse <user@domain.com> from full email address
*/
char *ADDR_parse(char *str)
{
	char *p, *q, *r;

#if 0
	(void)fprintf(stderr, "*** ADDR_parse(): str = [%s]\n", str);
#endif

	if((p = strdup(str)) == (char)NULL) {
		die("ADDR_parse() -- strdup() failed");
	}

	if((q = strchr(p, '<'))) {
		r = ++q;
		while(*q && (*q != '>')) q++;
		*q = (char)NULL;
	}
	else {
		q = STRIP__leading_space(p);
		if(*q == '(') {
			while((*q++ != ')'));
		}
		r = STRIP__leading_space(q);
#if 0
		(void)fprintf(stderr, "*** ADDR_parse(): r = [%s]\n", r);
#endif

		q = STRIP__trailing_space(r);
		if(*q == ')') {
			while((*--q != '('));
			*q = (char)NULL;
		}
		(void)STRIP__trailing_space(r);
	}
#if 0
	(void)fprintf(stderr, "*** ADDR_parse(): r = [%s]\n", r);
#endif

	return(r);
}

/*
append_domain() -- Fix up address with @domain.com
*/
char *append_domain(char *str)
{
	char buf[(BUF_SZ + 1)];

	if(strchr(str, '@') == (char)NULL) {
		if(snprintf(buf, BUF_SZ, "%s@%s", str,
#ifdef REWRITE_DOMAIN
			do_rewrite == True ? rewrite_domain : host_name
#else
			host_name
#endif
			) == -1) {
				die("append_domain() -- snprintf() failed");
		}
		return(strdup(buf));
	}

	return(str);
}

/*
handler() -- a "normal" non-portable version of an alarm handler
      Alas, setting a flag and returning is not fully functional in
      BSD: system calls don't fail when reading from a ``slow'' device
      like a socket. So we longjump instead, which is erronious on
      a small number of machines and ill-defined in the language
*/
void handler(void)
{
	extern jmp_buf TimeoutJmpBuf;

	longjmp(TimeoutJmpBuf, (int)1);
}

/*
standardise() -- trim off '\n's, double leading dots
*/
void standardise(char *str)
{
	size_t sl;
	char *p;

	if((p = strchr(str, '\n'))) {
		*p = (char)NULL;
	}

	/* Any line beginning with a dot has an additional dot inserted;
	not just a line consisting solely of a dot.  Thus we have to
	slide the buffer down one.  */
	sl = strlen(str);

	if(*str == '.') {
		if((sl + 2) > BUF_SZ) {
			die("standardise() -- Buffer overflow");
		}
		(void)memmove((str + 1), str, (sl + 1));  /* Copy trailing \0 */

		*str = '.';
	}
}

/*
revaliases() -- parse the reverse alias file, fix globals to use any entry for sender
*/
void revaliases(struct passwd *pw)
{
	char buf[(BUF_SZ + 1)], *p, *r;
	FILE *fp;

	/* Try to open the reverse aliases file */
	if((fp = fopen(REVALIASES_FILE, "r"))) {
		/* Search if a reverse alias is definied for the sender */
		while(fgets(buf, sizeof(buf), fp)) {
			/* Make comments invisible. */
			if((p = strchr(buf, '#'))) {
				*p = (char)NULL;
			}

			/* Ignore malformed lines and comments. */
			if(strchr(buf, ':') == (char)NULL) {
				continue;
			}

			/* Parse the alias */
			if(((p = strtok(buf, ":"))) && !strcmp(p, pw->pw_name)) {
				if((p = strtok(NULL, ": \t\r\n"))) {
					mail_from = strdup(p);
					if(mail_from == (char *)NULL) {
						die("X() -- strdup() failed");
					}
				}

				if((p = strtok(NULL, " \t\r\n:"))) {
					mail_hub = strdup(p);
					if(mail_from == (char *)NULL) {
						die("X() -- strdup() failed");
					}

					if((r = strtok(NULL, " \t\r\n:"))) {
						smtp_port = atoi(r);
					}

					if(log_level > 0) {
						log_event(LOG_INFO, "Set MailHub=\"%s\"\n", mail_hub);
						log_event(LOG_INFO, "via SMTP Port Number=\"%d\"\n", smtp_port);
					}
				}
			}
		}
	}

	fclose(fp);
}


/*
ssmtp() -- send the message (exactly one) from stdin to the mailhub SMTP port
*/
int ssmtp(char *argv[])
{
	char buf[(BUF_SZ + 1)], *p, *sd;
	struct passwd *pw;
	int i, sock;
	uid_t uid;

	uid = getuid();
	if((pw = getpwuid(uid)) == (struct passwd *)NULL) {
		die("Could not find password entry for UID %d", uid);
	}
	get_arpadate(arpa_date);

	if(read_config() == False) {
		log_event(LOG_INFO, "No ssmtp.conf in %s", SSMTPCONFDIR);
	}

	if((p = strtok(pw->pw_gecos, ";,"))) {
		if((full_name = strdup(p)) == (char *)NULL) {
			die("ssmtp() -- strdup() failed");
		}
	}
	revaliases(pw);

	/* revaliases() may have defined this */
	if(mail_from == (char *)NULL) {
		mail_from = append_domain(pw->pw_name);
	}

	/* Parse headers */
	ht = &headers;
	HEADER_parse(stdin);

	ht = &headers; rt = &RCPT_list;
	while(ht->next) {
		HEADER_record(ht->string);
		ht = ht->next;
	}
	/* Finished header processing */

	FROM_format();

	/* Now to the delivery of the message */
	(void)signal(SIGALRM, (void(*)())handler);	/* Catch SIGALRM */
	(void)alarm((unsigned) MAXWAIT);		/* Set initial timer */
	if(setjmp(TimeoutJmpBuf) != 0) {
		/* Then the timer has gone off and we bail out. */
		die("Connection lost in middle of processing");
	}

	if((sock = SMTP_open(mail_hub, smtp_port)) == -1) {
		die("Cannot open %s:%d", mail_hub, smtp_port);
	}
	else if(SMTP_OK(sock, buf) == False) {
		die("Did not get initial OK message from SMTP server");
	}

	/* If user supplied username and password, then try ELHO */
	/* do not really know if this is required or not...      */
	if(auth_user) {
		SMTP_write(sock, "EHLO %s", host_name);
	}
	else {
		SMTP_write(sock, "HELO %s", host_name);
	}
	(void)alarm((unsigned) MEDWAIT);

	if(SMTP_OK(sock, buf) == False) {
		die("%s (%s)", buf, host_name);
	}

	/* Try to log in if username was supplied */
	if(auth_user) {
		memset(buf, 0, sizeof(buf));
		to64frombits(buf, auth_user, strlen(auth_user));
		SMTP_write(sock, "AUTH LOGIN %s", buf);

		(void)alarm((unsigned) MEDWAIT);
		if(SMTP_read(sock, buf) != 3) {
			die("Server didn't accept AUTH LOGIN (%s)", buf);
		}
		memset(buf, 0, sizeof(buf));

		to64frombits(buf, auth_pass, strlen(auth_pass));
		SMTP_write(sock, "%s", buf);
		(void)alarm((unsigned) MEDWAIT);

		if(SMTP_OK(sock, buf) == False) {
			die("Authorization failed (%s)", buf);
		}
	}

	/* Send "MAIL FROM:" line */
	SMTP_write(sock, "MAIL FROM:<%s>", mail_from);

	(void)alarm((unsigned) MEDWAIT);

	if(SMTP_OK(sock, buf) == NO) {
		die("%s", buf);
	}

	/* Send all the To: adresses. */
	/* Either we're using the -t option, or we're using the arguments */
	if(minus_t) {
		if(RCPT_list.next == (rcpt_t *)NULL) {
			die("No recipients specified although -t option used");
		}
		rt = &RCPT_list;

		while(rt->next) {
			sd = strdup(RCPT_remap(rt->string));
			if(sd == (char *)NULL) {
				die("X() -- strdup() failed");
			}
			SMTP_write(sock, "RCPT TO:<%s>", sd);

			(void)alarm((unsigned)MEDWAIT);

			if(SMTP_OK(sock, buf) == NO) {
				die("RCPT TO:<%s> (%s)", sd, buf);
			}
			free(sd);

			rt = rt->next;
		}
	}
	else {
		for(i = 1; argv[i] != NULL; i++) {
			p = strtok(argv[i], ",");
			while(p) {
				/* RFC822 Address  -> "foo@bar" */
				sd = strdup(RCPT_remap(ADDR_parse(p)));
				if(sd == (char *)NULL) {
					die("X() -- strdup() failed");
				}
				SMTP_write(sock, "RCPT TO:<%s>", sd);

				(void)alarm((unsigned) MEDWAIT);

				if(SMTP_OK(sock, buf) == NO) {
					die("RCPT TO:<%s> (%s)", sd, buf);
				}
				free(sd);

				p = strtok(NULL, ",");
			}
		}
	}

	/* Send DATA */
	SMTP_write(sock, "DATA");

	(void)alarm((unsigned) MEDWAIT);

	if(SMTP_read(sock, buf) != 3) {
		/* Oops, we were expecting "354 send your data" */
		die("%s", buf);
	}

	SMTP_write(sock,
		"Received: by %s (sSMTP sendmail emulation); %s",
		host_name, arpa_date);

	if(SEEN_from == False) {
		SMTP_write(sock, "From: %s", full_from);
	}

	if(SEEN_date == False) {
		SMTP_write(sock, "Date: %s", arpa_date);
	}

#ifdef HASTO_OPTION
	if(SEEN_to == False) {
		SMTP_write(sock, "To: postmaster");
	}
#endif

	ht = &headers;
	while(ht->next) {
/*
		if(strncpy(buf, ht->string, BUF_SZ) == (char *)NULL) {
			die("ssmtp() -- strncpy() failed");
		}
		standardise(buf);
*/

		SMTP_write(sock, "%s", ht->string);
		ht = ht->next;
	}

	(void)alarm((unsigned) MEDWAIT);

	/* End of headers, start body. */
	SMTP_write(sock, "");

	while(fgets(buf, sizeof(buf), stdin)) {
		/* Trim off \n, double leading .'s */
		standardise(buf);

		SMTP_write(sock, "%s", buf);

		(void)alarm((unsigned) MEDWAIT);
	}
	/* End of body */

	SMTP_write(sock, ".");
	(void)alarm((unsigned) MAXWAIT);

	if(SMTP_OK(sock, buf) == NO) {
		die("%s", buf);
	}

	/* Close conection. */
	(void)signal(SIGALRM, SIG_IGN);

	SMTP_write(sock, "QUIT");
	(void)SMTP_OK(sock, buf);
	(void)close(sock);

	log_event(LOG_INFO,
		"Sent mail for %s (%s)", FROM_strip(mail_from), buf);

	return(0);
}

/*
main() -- make the program behave like sendmail, then call ssmtp
*/
int main(int argc, char **argv)
{
	char **new_argv;

	/* Try to be bulletproof :-) */
	(void)signal(SIGHUP, SIG_IGN);
	(void)signal(SIGINT, SIG_IGN);
	(void)signal(SIGTTIN, SIG_IGN);
	(void)signal(SIGTTOU, SIG_IGN);

	/* Set the globals. */
	prog_name = basename(argv[0]);

	if(gethostname(host_name, MAXHOSTNAMELEN) == -1) {
		die("Cannot get the name of this machine");
	}
	new_argv = parse_options(argc, argv);

	exit(ssmtp(new_argv));
}
