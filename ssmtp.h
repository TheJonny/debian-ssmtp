/*
 * spop.h -- Sizing and return-code defines, etc. Stuff you
 *	don't want to think about in the main program.
 */
#define YES   1
#define NO    0 
#define ERR   (-1)

#define MAXLINE	(1024*2) /* A pretty large buffer, but not outrageous. */
#define MAXWAIT (10*60)  /* Maximum wait between commands, in seconds. */
#define MEDWAIT (5*60)

#ifndef _POSIX_ARG_MAX
#define MAXARGS 4096
#else
#define MAXARGS  _POSIX_ARG_MAX
#endif

#ifdef lint
extern char *sprintf();	/* Ah yes, backward compatability with errors... */
extern void	exit();
#endif
