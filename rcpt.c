/*

 $Id: rcpt.c,v 1.3 2002/04/01 12:58:54 matt Exp $

 See COPYRIGHT for the license

*/
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pwd.h>
#include "ssmtp.h"

extern char *root_user;
extern rcpt_t *rt;


/*
RCPT_parse() -- Break To|Cc|Bcc into individual addresses
*/
void RCPT_parse(char *str)
{
	bool_t in_quotes = False, got_addr = False;
	char *p, *q, *r;

#if 0
	(void)fprintf(stderr, "*** RCPT_parse(): str = [%s]\n", str);
#endif

	if((p = strdup(str)) == (char *)NULL) {
		die("RCPT_parse(): strdup() failed");
	}
	q = p;

	/* Replace <CR> and <TAB> */
	while(*q) {
		switch(*q) {
			case '\t':
			case '\n':
					*q = ' ';
		}
		q++;
	}
	q = p;
#if 0
	(void)fprintf(stderr, "*** RCPT_parse(): q = [%s]\n", q);
#endif

	r = q;
	while(*q) {
		if(*q == '"') {
			in_quotes = (in_quotes ? False : True);
		}

		/* End of string? */
		if(*(q + 1) == (char)NULL) {
			got_addr = True;
		}

		/* End of address? */
		if((*q == ',') && (in_quotes == False)) {
			got_addr = True;

			*q = (char)NULL;
		}

		if(got_addr) {
			while(*r && isspace(*r)) r++;

			RCPT_save(ADDR_parse(r));
			r = (q + 1);
#if 0
			(void)fprintf(stderr,
				"*** RCPT_parse(): r = [%s]\n", r);
#endif
			got_addr = False;
		}
		q++;
	}
	free(p);
}

/*
RCPT_save() -- Store entry into RCPT list
*/
void RCPT_save(char *str)
{
	char *p;

	/* Horrible botch for group stuff */
	p = str;
	while(*p) p++;
	if(*--p == ';') {
		return;
	}

#if 0
	(void)fprintf(stderr, "*** RCPT_save(): str = [%s]\n", str);
#endif

	if((rt->string = strdup(str)) == (char *)NULL) {
		die("RCPT_save() -- strdup() failed");
	}

	rt->next = (rcpt_t *)malloc(sizeof(rcpt_t));
	if(rt->next == (rcpt_t *)NULL) {
		die("RCPT_save() -- malloc() failed");
	}
	rt = rt->next;

	rt->next = (rcpt_t *)NULL;
}

/*
RCPT_remap() -- alias systems-level users to the person who
	reads their mail.  This is variously the owner of a workstation,
	the sysadmin of a group of stations and the postmaster otherwise.
	We don't just mail stuff off to root on the mailhub (:-))
*/
char *RCPT_remap(char *str)
{
	struct passwd *pw;

	if(strchr(str, '@') ||
		((pw = getpwnam(str)) == NULL) || (pw->pw_uid > MAXSYSUID)) {
		/* It's not a local systems-level user */
		return(append_domain(str));
	}
	else {
		return(append_domain(root_user));
	}
}
