/*

 $Id: from.c,v 1.7 2002/04/01 12:57:11 matt Exp $

 See COPYRIGHT for the license

*/
#include <stdlib.h>
#include <string.h>
#include "ssmtp.h"

extern bool_t FROM_override;
extern bool_t SEEN_from;
extern char *full_from;
extern char *mail_from;
extern char *full_name;
extern char *minus_f;
extern char *minus_F;


/* 
FROM_strip() -- transforms "Name <login@host>" into "login@host" or "login@host (Real name)"
*/
char *FROM_strip(char *str)
{
	char *p, *q;

#if 0
	(void)fprintf(stderr, "*** FROM_strip(): str = [%s]\n", str);
#endif

	if(strncmp("From: ", str, 6) == 0) {
		str += 6;
	}

	/* Remove the real name if necessary - just send the address */
	if((p = ADDR_parse(str)) == (char *)NULL) {
		die("FROM_strip() -- ADDR_parse() failed");
	}

	if((q = strdup(p)) == (char)NULL) {
		die("FROM_strip() -- strdup() failed");
	}

	return(q);
}

#ifdef REWRITE_DOMAIN
/*
FROM_rewrite() -- rewrite From: - Evil, nasty, immoral header-rewrite code 8-)
*/
void FROM_rewrite(char *str, int sz)
{
	if(strncasecmp(str, "From:", 5) == 0) {
		if(snprintf(str, sz, "From: %s", full_from) == -1) {
			die("FROM_rewrite() -- snprintf() failed");
		}
#if 0
		(void)fprintf(stderr, "*** FROM_rewrite(): str = [%s]\n", str);
#endif
	}
}
#endif

/*
FROM_format() -- 
*/
void FROM_format(void)
{
	char buf[(BUF_SZ + 1)];

	if(SEEN_from) {
#if 0
		(void)fprintf(stderr,
			"*** FROM_format(): full_from = [%s]\n", full_from);
#endif
		mail_from = append_domain(FROM_strip(full_from));
#if 0
		(void)fprintf(stderr,
			"*** FROM_format(): mail_from = [%s]\n", mail_from);
#endif
	}

	if(FROM_override) {
		if(minus_f) {
			mail_from = append_domain(minus_f);
		}

		if(minus_F) {
			if(snprintf(buf, BUF_SZ,
				"\"%s\" <%s>", minus_F, mail_from) == -1) {
				die("FROM_format() -- snprintf() failed");
			}
		}
		else if(full_name) {
			if(snprintf(buf, BUF_SZ,
				"\"%s\" <%s>", full_name, mail_from) == -1) {
				die("FROM_format() -- snprintf() failed");
			}
		}
		else {
			if(snprintf(buf, BUF_SZ, "%s", mail_from) == -1) {
				die("FROM_format() -- snprintf() failed");
			}
		}

		if((full_from = strdup(buf)) == (char)NULL) {
			die("FROM_format() -- strdup() failed");
		}
	}
	else {
		if(full_name) {
			if(snprintf(buf, BUF_SZ,
				"\"%s\" <%s>", full_name, mail_from) == -1) {
				die("FROM_format() -- snprintf() failed");
			}

			if((full_from = strdup(buf)) == (char)NULL) {
				die("FROM_format() -- strdup() failed");
			}
		}
		else {
			full_from = mail_from;
		}
	}
#if 0
	(void)fprintf(stderr,
		"*** FORM_format(): full_from = [%s]\n", full_from);
#endif
}
