/*

 $Id: options.c,v 1.5 2002/02/10 21:58:26 matt Exp $

 See COPYRIGHT for the license

*/
#include <sys/param.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include "ssmtp.h"

extern char *prog_name;

bool_t do_rewrite = False;
bool_t FROM_override = False;
bool_t minus_t = False;
bool_t minus_v = False;

char *auth_user = (char)NULL;
char *auth_pass = (char)NULL;
char *mail_hub = "mailhub";
char *root_user = "postmaster";
char *rewrite_domain = (char)NULL;
char host_name[MAXHOSTNAMELEN] = "localhost";
char *mail_from = (char)NULL;
char *minus_f = (char)NULL;
char *minus_F = (char)NULL;

int smtp_port = 25;

#ifdef DEBUG
int log_level = 1;
#else
int log_level = 0;
#endif


/*
parse_options() -- pull the options out of the command-line,
process them (and special-case calls to mailq, etc), and return the rest
*/
char **parse_options(int argc, char *argv[])
{
	static char Version[] = "$Revision: 1.5 $";
	static char *new_argv[MAXARGS];
	int i, j, add, new_argc;

	new_argv[0] = argv[0];
	new_argc = 1;

	if(strcmp(prog_name, "mailq") == 0) {
		/* Someone wants to know the queue state... */
		paq("mailq: Mail queue is empty\n");
	}
	else if(strcmp(prog_name, "newaliases") == 0) {
		/* Someone wanted to recompile aliases */
		paq("newaliases: Aliases are not used in sSMTP\n");
	}

	i = 1;
	while(i < argc) {
		if(argv[i][0] != '-') {
			new_argv[new_argc++] = argv[i++];
			continue;
		}
		j = 0;

		add = 1;
		while(argv[i][++j] != (char)NULL) {
			switch(argv[i][j]) {
#ifdef INET6
			case '6':
				protocolFamily = PF_INET6;
				continue;

			case '4':
				protocolFamily = PF_INET;
			continue;
#endif

			case 'a':
				switch(argv[i][++j]) {
				case 'u':
					if((!argv[i][(j + 1)])
						&& argv[(i + 1)]) {
						auth_user = strdup(argv[i+1]);
						if(auth_user == (char *)NULL) {
							die("X() -- strdup() failed");
						}
						add++;
					}
					else {
						auth_user = strdup(argv[i]+j+1);
						if(auth_user == (char *)NULL) {
							die("X() -- strdup() failed");
						}
					}
					goto exit;

				case 'p':
					if((!argv[i][(j + 1)])
						&& argv[(i + 1)]) {
						auth_pass = strdup(argv[i+1]);
						if(auth_pass == (char *)NULL) {
							die("X() -- strdup() failed");
						}
						add++;
					}
					else {
						auth_pass = strdup(argv[i]+j+1);
						if(auth_pass == (char *)NULL) {
							die("X() -- strdup() failed");
						}
					}
					goto exit;
				}
				goto exit;

			case 'b':
				switch(argv[i][++j]) {

				case 'a':	/* ARPANET mode */
						paq("-ba is not supported by sSMTP\n");
				case 'd':	/* Run as a daemon */
						paq("-bd is not supported by sSMTP\n");
				case 'i':	/* Initialise aliases */
						paq("%s: Aliases are not used in sSMTP\n", prog_name);
				case 'm':	/*  Default addr processing */
						continue;

				case 'p':	/* Print mailqueue */
						paq("%s: Mail queue is empty\n", prog_name);
				case 's':	/* Read SMTP from stdin */
						paq("-bs is not supported by sSMTP\n");
				case 't':	/* Test mode */
						paq("-bt is meaningless to sSMTP\n");
				case 'v':	/*  Verify names only */
						paq("-bv is meaningless to sSMTP\n");
				case 'z':	/* Create  freeze file */
						paq("-bz is meaningless to sSMTP\n");
				}

			/* Configfile name */
			case 'C':
				goto exit;

			/* Debug */
			case 'd':
				log_level = 1;
				/* Almost the same thing... */
				minus_v = True;

				continue;

			/* Insecure channel, don't trust userid */
			case 'E':
					continue;

			case 'R':
				/* Amount of the message to be returned */
				if(!argv[i][j+1]) {
					add++;
					goto exit;
				}
				else {
					/* Process queue for recipient */
					continue;
				}

			/* Fullname of sender */
			case 'F':
				if((!argv[i][(j + 1)]) && argv[(i + 1)]) {
					minus_F = strdup(argv[(i + 1)]);
					if(minus_F == (char *)NULL) {
						die("X() -- strdup() failed");
					}
					add++;
				}
				else {
					minus_F = strdup(argv[i]+j+1);
					if(minus_F == (char *)NULL) {
						die("X() -- strdup() failed");
					}
				}
				goto exit;

			/* Set from/sender address */
			case 'f':
			/* Obsolete -f flag */
			case 'r':
				if((!argv[i][(j + 1)]) && argv[(i + 1)]) {
					minus_f = strdup(argv[(i + 1)]);
					if(minus_f == (char *)NULL) {
						die("parse_options() -- strdup() failed");
					}
					add++;
				}
				else {
					minus_f = strdup(argv[i]+j+1);
					if(minus_f == (char *)NULL) {
						die("parse_options() -- strdup() failed");
					}
				}
				goto exit;

			/* Set hopcount */
			case 'h':
				continue;

			/* Ignore originator in adress list */
			case 'm':
				continue;

			/* Use specified message-id */
			case 'M':
				goto exit;

			/* DSN options */
			case 'N':
				add++;
				goto exit;

			/* No aliasing */
			case 'n':
				continue;

			case 'o':
				switch(argv[i][++j]) {

				/* Alternate aliases file */
				case 'A':
					goto exit;

				/* Delay connections */
				case 'c':
					continue;

				/* Run newaliases if required */
				case 'D':
					paq("%s: Aliases are not used in sSMTP\n", prog_name);

				/* Deliver now, in background or queue */
				/* This may warrant a diagnostic for b or q */
				case 'd':
						continue;

				/* Errors: mail, write or none */
				case 'e':
					j++;
					continue;

				/* Set tempfile mode */
				case 'F':
					goto exit;

				/* Save ``From ' lines */
				case 'f':
					continue;

				/* Set group id */
				case 'g':
					goto exit;

				/* Helpfile name */
				case 'H':
					continue;

				/* DATA ends at EOF, not \n.\n */
				case 'i':
					continue;

				/* Log level */
				case 'L':
					goto exit;

				/* Send to me if in the list */
				case 'm':
					continue;

				/* Old headers, spaces between adresses */
				case 'o':
					paq("-oo is not supported by sSMTP\n");

				/* Queue dir */
				case 'Q':
					goto exit;

				/* Read timeout */
				case 'r':
					goto exit;

				/* Always init the queue */
				case 's':
					continue;

				/* Stats file */
				case 'S':
					goto exit;

				/* Queue timeout */
				case 'T':
					goto exit;

				/* Set timezone */
				case 't':
					goto exit;

				/* Set uid */
				case 'u':
					goto exit;

				/* Set verbose flag */
				case 'v':
					minus_v = True;
					continue;
				}
				break;

			/* Process the queue [at time] */
			case 'q':
					paq("%s: Mail queue is empty\n", prog_name);

			/* Read message's To/Cc/Bcc lines */
			case 't':
				minus_t = True;
				continue;

			/* minus_v (ditto -ov) */
			case 'v':
				minus_v = True;
				break;

			/*  Say version and quit */
			/* Similar as die, but no logging */
			case 'V':
				paq("sSMTP %s (not sendmail at all)\n", Version);
			}
		}

		exit:
		i += add;
	}
	new_argv[new_argc] = NULL;

	if(new_argc <= 1 && !minus_t) {
		paq("%s: No recipients supplied - mail will not be sent\n", prog_name);
	}

	if(new_argc > 1 && minus_t)
		paq("%s: recipients with -t option not supported\n", prog_name);

	return(&new_argv[0]);
}

/*
read_config() -- Config file access routine
*/
bool_t read_config(void)
{
	FILE *fp;  

	if((fp = fopen(CONFIGURATION_FILE, "r"))) {
		parse_config(fp);
	}
	else {
		return(False);
	}
	fclose(fp);

	return(True);
}

/*
parse_config() -- parse config file, extract values of a few predefined variables
*/
void parse_config(FILE *fp)
{
	char buf[(BUF_SZ + 1)], *p, *q, *r;

	while(fgets(buf, sizeof(buf), fp)) {
		/* Make comments invisible. */
		if((p = strchr(buf, '#'))) {
			*p = (char)NULL;
		}

		/* Ignore malformed lines and comments. */
		if(strchr(buf, '=') == (char *)NULL) continue;

		/* Parse out keywords. */
		if(((p = strtok(buf, "= \t\n")) != (char *)NULL)
			&& ((q = strtok(NULL, "= \t\n:")) != (char *)NULL)) {
			if(strcasecmp(p, "Root") == 0) {
				if((root_user = strdup(q)) == (char *)NULL) {
					die("parse_config() -- strdup() failed");
				}

				if(log_level > 0) {
					log_event(LOG_INFO, "Set Root=\"%s\"\n", root_user);
				}
			}
			else if(strcasecmp(p, "MailHub") == 0) {
				if((mail_hub = strdup(q)) == (char *)NULL) {
					die("parse_config() -- strdup() failed");
				}

				if((r = strtok(NULL, "= \t\n:")) != NULL) {
					smtp_port = atoi(r);
				}

				if(log_level > 0) {
					log_event(LOG_INFO, "Set MailHub=\"%s\"\n", mail_hub);
					log_event(LOG_INFO, "Set RemotePort=\"%d\"\n", smtp_port);
				}
			}
			else if(strcasecmp(p, "HostName") == 0) {
				if(strncpy(host_name, q, MAXHOSTNAMELEN) == NULL) {
					die("parse_config() -- strncpy() failed");
				}

				if(log_level > 0) {
					log_event(LOG_INFO, "Set HostName=\"%s\"\n", host_name);
				}
			}
#ifdef REWRITE_DOMAIN
			else if(strcasecmp(p, "RewriteDomain") == 0) {
				rewrite_domain = strdup(q);
				if(rewrite_domain == (char *)NULL) {
					die("parse_config() -- strdup() failed");
				}

				do_rewrite = True;
				if(log_level > 0) {
					log_event(LOG_INFO, "Set RewriteDomain=\"%s\"\n", rewrite_domain);
				}
			}
#endif
			else if(strcasecmp(p, "FromLineOverride") == 0) {
				if(strcasecmp(q, "yes") == 0) {
					FROM_override = True;
				}
				else {
					FROM_override = False;
				}

				if(log_level > 0) {
					log_event(LOG_INFO, "Set FromLineOverride=\"%s\"\n", FROM_override ? "True" : "False");
				}
			}
			else if(strcasecmp(p, "RemotePort") == 0) {
				smtp_port = atoi(q);

				if(log_level > 0) {
					log_event(LOG_INFO, "Set RemotePort=\"%d\"\n", smtp_port);
				}
			}
#ifdef HAVE_SSL
			else if(strcasecmp(p, "UseTLS") == 0) {
				if(strcasecmp(q, "yes") == 0) {
					UseTLS = True;
				}
				else {
					UseTLS = False;
				}

				if(log_level > 0) { 
					log_event(LOG_INFO, "Set UseTLS=\"%s\"\n", UseTLS ? "True" : "False");
				}
			}
			else if(strcasecmp(p, "UseTLSCert") == 0) {
				if(strcasecmp(q, "yes") == 0) {
					UseTLSCert = True;
				}
				else {
					UseTLSCert = False;
				}

				if(log_level > 0) {
					log_event(LOG_INFO, "Set UseTLSCert=\"%s\"\n", UseTLSCert ? "True" : "False");
				}
			}
			else if(strcasecmp(p, "TLSCert") == 0) {
				if((TLSCert = strdup(q)) == (char *)NULL) {
					die("parse_config() -- strdup() failed");
				}

				if(log_level > 0) {
					log_event(LOG_INFO, "Set TLSCert=\"%s\"\n", TLSCert);
				}
			}
#endif
			else {
				log_event(LOG_INFO, "Unable to set %s=\"%s\"\n", p, q);
			}
		}
	}

	return;
}
