/*

 $Id: util.c,v 1.2 2002/02/10 21:05:25 matt Exp $

 See COPYRIGHT for the license

*/
#include <sys/param.h>
#include <string.h>
#include <ctype.h>
#include "ssmtp.h"


/*
basename() --
*/
char *basename(char *path)
{
	static char buf[MAXPATHLEN +1];
	char *ptr;

	ptr = strrchr(path, '/');
	if(ptr) {
		if(strncpy(buf, ++ptr, MAXPATHLEN) == (char *)NULL) {
			die("basename() -- strncpy() failed");
		}
	}
	else {
		if(strncpy(buf, path, MAXPATHLEN) == (char *)NULL) {
			die("basename() -- strncpy() failed");
		}
	}
	buf[MAXPATHLEN] = (char)NULL;

	return(buf);
}

/*
STRIP__leading_space() --
*/
char *STRIP__leading_space(char *str)
{
	char *p;

	p = str;
	while(*p && isspace(*p)) p++;

	return(p);
}

char *STRIP__trailing_space(char *str)
{
	char *p;

	p = (str + strlen(str));
	while(isspace(*--p)) {
		*p = (char)NULL;
	}

	return(p);
}

/*
CR_strip() -- Remove *last* CR from input 
*/
bool_t CR_strip(char *str)
{
	char *p;

	if((p = strrchr(str, '\n'))) {  
		*p = (char)NULL;

		return(True);  
	}

	return(False);
}

