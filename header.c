/*

 $Id: header.c,v 1.5 2002/04/01 12:57:58 matt Exp $

 See COPYRIGHT for the license

*/
#include <stdlib.h>
#include <string.h>
#include "ssmtp.h"

extern bool_t FROM_override;
extern bool_t SEEN_from;
extern bool_t SEEN_date;
extern bool_t minus_t;
extern headers_t *ht;
extern char *full_from;


/*
HEADER_record() -- note which headers we've seen
*/
void HEADER_record(char *str)
{
	char *p;

#if 0
	(void)fprintf(stderr, "*** HEADER_record(): str = [%s]\n", str);
#endif

	if(strncasecmp(str, "From:", 5) == 0) {
		if((full_from = strdup((str + 6))) == (char *)NULL) {
			die("HEADER_record():");
		}
		SEEN_from = True;
	}
#ifdef HASTO_OPTION
	else if(strncasecmp(str, "To:" ,3) == 0) {
		SEEN_to = True;
	}
#endif
	else if(strncasecmp(str, "Date:", 5) == 0) {
		SEEN_date = True;
	}

	if(minus_t) {
		/* Need to figure out recipients from the e-mail */
		if(strncasecmp(str, "To:", 3) == 0) {
			p = (str + 4);
			RCPT_parse(p);
		}
		else if(strncasecmp(str, "Bcc:", 4) == 0) {
			p = (str + 5);
			RCPT_parse(p);
		}
		else if(strncasecmp(str, "CC:", 3) == 0) {
			p = (str + 4);
			RCPT_parse(p);
		}
	}
}

/*
HEADER_parse() -- Break headers into seperate entries
*/
void HEADER_parse(FILE *stream)
{
	size_t size = BUF_SZ, len = 0;
	char *p = (char *)NULL, *q;
	bool_t in_headers = True;
	char c, l = (char)NULL;

	while(in_headers && ((c = (char)fgetc(stream)) != EOF)) {
		if((p == (char *)NULL) || (len >= size)) {
			size += BUF_SZ;

			p = (char *)realloc(p, (size * sizeof(char)));
			if(p == (char *)NULL) {
				die("HEADER_parse() -- realloc() failed");
			}
			q = (p + len);
		}
		len++;

		if(l == '\n') {
			switch(c) {
				case ' ':
				case '\t':
						break;

				case '\n':
						in_headers = False;

				default:
						*q = (char)NULL;
						if((q = strrchr(p, '\n'))) {
							*q = (char)NULL;
						}
						HEADER_save(p);

						q = p;
						len = 0;
			}
		}
		*q++ = c;

		l = c;
	}
	(void)free(p);
}

/*
HEADER_save() -- Store entry into header list
*/
void HEADER_save(char *str)
{
	char buf[(BUF_SZ + 1)];
	char *p;

	if((p = strdup(str)) == (char *)NULL) {
		die("HEADER_save() -- strdup() failed");
	}

#ifdef REWRITE_DOMAIN
	if(FROM_override == False) {
		if(strncpy(buf, str, BUF_SZ) == (char *)NULL) {
			die("HEADER_save() -- strncpy() failed");
		}
		FROM_rewrite(buf, BUF_SZ);

		if((p = strdup(buf)) == (char *)NULL) {
			die("HEADER_save() -- strdup() failed");
		}
	}
#endif
	ht->string = p;

	ht->next = (headers_t *)malloc(sizeof(headers_t));
	if(ht->next == (headers_t *)NULL) {
		die("HEADER_save() -- malloc() failed");
	}
	ht = ht->next;

	ht->next = (headers_t *)NULL;
}
