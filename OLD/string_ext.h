/*
** strings.h -- Local strings routines.
*/
#ifdef NOSTRINGS
#define NOSTRDUP
#define NOSTRNCASECMP
#define NOSTRDUP
#define NOSTRSTR
#endif

#ifdef NOSTRDUP
char *strdup(/* char *p */);
#endif

#ifdef NOSTRNCASECMP
int strncasecmp(/* char *p, *q; int n */);
#endif

#ifdef NOSTRTOK
char *strtok(/* char *input, *separators */);
#endif

#ifdef NOSTRSTR
char *strstr(/* char *string, *substr */);
#endif
