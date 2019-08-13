/*
 * sSMTP sendmail -- send messages via smtp to a mailhub for local delivery 
 *	or forwarding. This program is used in place of /usr/lib/sendmail,
 *      called by /bin/mail (et all).   sSMTP does a selected subset of 
 *      sendmail's standard tasks (including exactly one rewriting task), and
 *      explains if you ask it to do something it can't.  It then sends
 *      the mail to the mailhub via an smtp connection.  Believe it or not,
 *      this is nothing but a filter.  You can receive mail with inetd, an
 *	inverse filter and /bin/mail -d.
 *
 *  September 1997 Christoph Lameter
 *   - Fixed up to use more modern C (attempt to fix problems)
 *   - Fixed scores of bugs (I doubt it ever worked before)
 *   - Made it work under Debian/Linux
 *   - Add support for -t option. Limit header to 4K.
 *
 *  October 1997 Hugo Haas
 *   - Added the reverse aliases process for the From: field
 *   - Send the required headers at the beginning
 *   - Send only one recipient at a time
 *   - Changed the header parsing to avoid a bug due to mailx
 *
 *  November 1997 Hugo Haas
 *   - Changed the RCPT stuff which was wrong for arguments with "<>"
 *
 *  December 1997 Hugo Haas
 *   - Changed the MAIL FROM command to be RFC821 compliant (Debian Bug#15690)
 *   - Modified the recordReciepient function: no memory was allocated for
 *	the last recipient
 *   - Added the sending of the recorded recipients (Debian Bug#15690)
 *	The old way to do it was wrong. (Removed the argv=reciepients stuff)
 *
 *  January 1998 Hugo Haas
 *   - Changed the header parsing because it gobbled pseudo-header lines (Debian Bug#17240)
 *   - Changed the RewriteDomain option
 *
 *  January 1998 Hugo Haas
 *   - Changed the configuration parsing (Debian Bug#17470)
 *   - Changed the logging: verbosity reduced
 *
 *  March 1998 Hugo Haas
 *   - No more adding "To: postmaster" (qmail and sendmail do this)
 *   - Improved "-f", "-F" and "-r" options
 *
 *  March 1998 Hugo Haas
 *   - Moved the configuration files to /etc/ssmtp
 *
 *  April 1998 Hugo Haas
 *   - Removed quote in the From: line
 *
 *  April 1998 Hugo Haas
 *   - Removed awful getDate() method; replaced by get_arpadate()
 *
 *  April 1998 Hugo Haas
 *   - Now ignoring -R keyword, -N dsn stuff
 *
 *  April 1998 Hugo Haas
 *   - Made 'Root' option work
 *   - Handled the case when the user has no name (do not send "(null)")
 */

#include <stdio.h>
#include <limits.h>
#include <pwd.h>       /* For getpwent. */
#include <sys/types.h> /* For sockets. */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#ifdef SYSLOG
#include <syslog.h>    /* For logging. */
#else
#define LOG_ERR 0
#define LOG_INFO 0
#endif
#include <signal.h>    /* For the timer and signals. */
#include <setjmp.h>
#include <string.h>

#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>

#include "string_ext.h"   /* Local additions. */
#include "ssmtp.h"
#include "patchlevel.h"

#ifndef MAILHUB
#define MAILHUB "mailhost" /* A surprisingly usefull default. */
#endif
#define PORTNUMBER 25 /* From the Assigned Numbers RFC. */
#define REVALIASES_FILE "/etc/ssmtp/revaliases" /* Revaliases definition file */

char	*Version = VERSION;	/* The version of the program. */
char	*ProgName = NULL;       /* It's name. */
char	*MailHub = MAILHUB;	/* The place to send the mail. */
char	HostName[MAXLINE];	/* Our name, unless overridden. */
#ifdef REWRITE_DOMAIN
char *RewriteDomain = "localhost"; /* Place to claim to be. */
int	UseRD = NO;		/* Do we have to rewrite the domain? */
#endif
char	*Root = "postmaster";	/* Person to send root's mail to. */
struct passwd *Sender = NULL;   /* The person sending the mail. */
jmp_buf TimeoutJmpBuf;		/* Timeout waiting for input from network. */
int     Verbose = NO;		/* Tell the user what's happening. */
int	Toption = NO;		/* Was a T option given? */
int	LogLevel =		/* Tell the log what's happening. */
#ifdef DEBUG
		1;
#else
		0;
#endif

/* Current date in RFC format. */
#include "arpadate.c"
char	DateString[ARPADATE_LENGTH];

char	*fromLine(void);
char	*properRecipient(char *);

int	openSocket(char *,int);
int	getOkFromSmtp(int,char *);
int	getFromSmtp(int,char *);


/*
 * log_event -- log something to syslog and the log file.
 */
/*VARARGS*/
void log_event(syslog_code,format,p1,p2,p3,p4,p5)
     int syslog_code; char *format,*p1,*p2,*p3,*p4,*p5; 
{
#ifdef SYSLOG
 	static int syslogOpen = NO;
#endif
#ifdef LOGFILE
	FILE *fp;

	if ((fp= fopen("/tmp/ssmtp.log","a")) != NULL) {
		(void) fprintf(fp,format,p1,p2,p3,p4,p5);
		(void) putc('\n',fp);
		(void) fclose(fp);
	}
	else {
		/* oops! */
		(void) fprintf(stderr,"Can't write to /tmp/ssmtp.log\n");
	}
#endif
#ifdef SYSLOG
 	if (syslogOpen == NO) {
 	       syslogOpen = YES;
#ifdef OLDSYSLOG
		openlog("sSMTP mail", LOG_PID);
#else
		openlog("sSMTP mail", LOG_PID, LOG_MAIL);
#endif
	}
	(void) syslog(syslog_code,format,p1,p2,p3,p4,p5);
#endif
}

/*
 * putToSmtp -- a printf to an fd, which appends TCP/IP <CR/LF>.
 */
void putToSmtp(int fd, char *format, ...) {
	va_list args;
	char line[MAXLINE];
	va_start(args, format);
	(void) vsprintf(line,format,args);
	if (LogLevel > 0) {
		log_event(LOG_INFO,"Sent \"%s\" to smtp port.\n",line);
	}
	/*	printf("%s\n",line); */
	(void) strcat(line,"\r\n");
	(void) write(fd,line,strlen(line));
	va_end(args);
}

void flush(void);

/*
 * die -- say something and exit with a non-zero return code.
 *	Save the message on stdin in dead.letter.
 */
/*VARARGS*/
void die(format,p1,p2,p3,p4,p5) char *format,*p1,*p2,*p3,*p4,*p5; {
	(void) fprintf(stderr,"%s: ",ProgName);
	(void) fprintf(stderr,format,p1,p2,p3,p4,p5);
	(void) putc('\n',stderr);
	flush(); /* Send message to dead.letter */
	log_event(LOG_ERR,format,p1,p2,p3,p4,p5);
	exit(1);
}


char *reciepients[100];
char **rec=reciepients;

void rrec(char *re) {
	char *p,*r,*ptr;

/*	fprintf(stderr,"RRR=%s\n",re); */
	if (!*re) {
		free(re);
		return;
	}
	if ((p=index(re,'<'))) {
		ptr=p+1;
		if ((p=index(ptr,'>'))) {
			r=malloc(p-ptr+1);
			r[p-ptr]=0;
			memcpy(r,ptr,p-ptr);
			*rec++=r;
			free(re);
		}
		else
			die ("Syntax error in recipient %s",p);
	}
	else
		*rec++=re;
/*	fprintf(stderr,"RRRE=%s\n",rec[-1]); */
}

void recordReciepient(char *line) {
	char *p,*q,*r;

/*  fprintf(stderr,"RR=%s\n",line); */
	q=p=line;
	do {
		while (*q==' ' || *q==',') q++;
		if (!*q)
			return;
		p=q;
		while (*p && *p!=',') p++;
		if (*p) {
			r=malloc(p-q+1);
			memcpy(r,q,p-q);
			r[p-q]=0;
			rrec(r);
			q=++p;
		}
	} while (*p);
	r=(char *)malloc(strlen(q)+1);
	strcpy(r,q);
	rrec(r);
}

/*
** Supporting libraries -- header insertion.
*/
static int hasFrom = NO,
/*	hasTo = NO,*/
	hasDate = NO;
/*
 * recordRequiredHeaders -- note which ones we've seen.
 */
void recordRequiredHeaders(char *line) {

	if (*line == ' ' || *line == '\t') {
		return;
	}
	else if (strncasecmp(line,"From:",5)==0) {
		hasFrom = YES;
	}
/*	else if (strncasecmp(line,"To:",3)==0) {
		hasTo = YES;
	}*/
	else if (strncasecmp(line,"Date:",5)==0) {
		hasDate = YES;
	}
	if (Toption)
	{	/* Need to figure out reciepients from the e-mail */
		if (strncasecmp(line,"To:",3)==0) recordReciepient(line+4); else
		if (strncasecmp(line,"Bcc:",4)==0) recordReciepient(line+5); else
		if (strncasecmp(line,"CC:",3)==0) recordReciepient(line+4);
	}
}

/*
 * addRequiredHeaders -- add ones that have been missed.
 */
void addRequiredHeaders(int fd) {

	if (hasFrom == NO) {
		putToSmtp(fd,"From: %s", fromLine());
	}
/*	if (hasTo == NO) {*/
		/* Can't happen, therefor... */
/*		putToSmtp(fd,"To: postmaster");
	}*/
	if (hasDate == NO) {
		putToSmtp(fd,"Date: %s",DateString);
	}
}

/*
 * addInitialHeaders -- prepend prerequisite timstamp
 *	and actual date lines.
 */
void addInitialHeaders(int fd) { 

	putToSmtp(fd,"Received: by %s (sSMTP sendmail emulation); %s",
		  HostName, DateString);

}

#ifdef REWRITE_DOMAIN
/*
 * fixFromLine -- replace whole From: header with standardized pattern.
 *      Evil, nasty, immoral header-rewriting code (:-)).
 */
int fixFromLines(char *line) {
        static int inHeaders = YES;

	if (strncasecmp(line,"From:",5) == 0) {
		(void) sprintf(line, "From: %s", fromLine());
	}
	if (*line == (char) NULL) {
	       inHeaders = NO;
	}
	return inHeaders;
}
#endif

/*
 * properRecipient -- alias systems-level users to the person who
 *	reads their mail.  This is variously the owner of a workstation,
 *	the sysadmin of a group of stations and the postmaster otherwise.
 *	We don't just mail stuff off to root on the mailhub (:-)).
 */
char *properRecipient(char *s) {
	struct passwd *p;

	if (strchr(s,'@')
	   || (p = getpwnam(s)) == NULL
	   || p->pw_uid > 10) {
		/* It's not a local systems-level user. */
		return s;
	}
	else {
		return Root;
	}
	/*NOTREACHED*/
}

/*
 * fromLine -- generate a from line in standard format. Used whenever
 *	we need one in standard format, "Real Name <id@site>" or "id@site"
 */
char *fromLine(void) {
	FILE *rev_file;
	static char buffer[MAXLINE];
	char	line[MAXLINE],*p,*phrase;
	/* Try to open the reverse aliases file */
	rev_file=fopen(REVALIASES_FILE,"r");
	/* Search if a reverse alias is definied for the sender */
	if (rev_file) {
		while (fgets(line,sizeof line,rev_file)) {
			/* Make comments invisible. */
			if ((p= strchr(line,'#'))) {
				*p = '\0';
			}
			/* Ignore malformed lines and comments. */
			if (strchr(line,':') == NULL)
				continue;
			/* Parse the alias */
			if (((p= strtok(line,":")) != NULL)
			      && !strcmp(p,Sender->pw_name)) {
				p= strtok(NULL," \t\n");
				if (p) {
					if ((phrase = strtok(Sender->pw_gecos, ",;")) != NULL)
						(void) sprintf(buffer,"%s <%s>",phrase,p);
					else
						(void) sprintf(buffer,"%s",p);
					return buffer;
				}
			}
		}
	}
	if ((phrase = strtok(Sender->pw_gecos, ",;")) != NULL)
		(void) sprintf(buffer,"%s <%s@%s>",
			phrase,
			Sender->pw_name,
#ifdef REWRITE_DOMAIN
			UseRD == YES ? RewriteDomain : HostName);
#else
			HostName);
#endif
	else
		(void) sprintf(buffer,"%s@%s",
			Sender->pw_name,
#ifdef REWRITE_DOMAIN
			UseRD == YES ? RewriteDomain : HostName);
#else
			HostName);
#endif
/*	fprintf(stderr,"Fromline generated: %s (%s)",buffer,RewriteDomain); */
	return buffer;
}


/*
** Supporting libraries -- signals
*/
/*
 * alarmHandler -- a ``normal'' non-portable version of an alarm handler.
 *	Alas, setting a flag and returning is not fully functional in
 *      BSD: system calls don't fail when reading from a ``slow'' device
 *      like a socket. So we longjump instead, which is erronious on
 *      a small number of machines and ill-defined in the language.
 */
void alarmHandler(void) {
	extern jmp_buf TimeoutJmpBuf;
	longjmp(TimeoutJmpBuf,(int)1);
}

/*
** emergency exit functions.
*/

/*
 * flush -- save stdin to dead.letter, if you can.
 */
void flush(void) {
	char	line[MAXLINE];
	FILE	*fp;

	if (isatty(fileno(stdin))) {
		if (LogLevel > 0) {
			log_event(LOG_ERR,"stdin appears to be a terminal. Not saving to dead.letter.");
		}
		return;
	}
	if (Sender == NULL) {
		/* Far to early to save things. */
		if (LogLevel > 0) {
			log_event(LOG_ERR, "No sender (can't happen), failing horribly.");
		}
		return;
	}
	(void) sprintf(line,"%s/dead.letter",Sender->pw_dir);
	if ((fp= fopen(line,"a")) == NULL) {
		/* Perhaps the person doesn't have a homedir... */
		if (LogLevel > 0) {
				log_event(LOG_ERR,"Can't open %s, failing horribly.",
					  line);
		}
		return;
	}
	(void) putc('\n',fp); /* Make sure we start on a new line, */
	(void) putc('\n',fp); /* with a blank line separating messages. */
	while (fgets(line,sizeof line,stdin)) {
		(void) fputs(line,fp);
	}
	if (fclose(fp) == ERR) {
		if (LogLevel > 0) {
			log_event(LOG_ERR,"Can't close %s/dead.letter, possibly truncated.",
				  Sender->pw_dir);
		}
	}
}

/*
** Reporting amd logging library functions 
*/


/*
** Local/peculiar string manipulation.
*/

/*
 * standardize -- trim off '\n's, double leading dots.
 */
void standardize(char *p) {
	char *q;

	if (*p == '.' && *(p+1) == '\n') {
		/* Double it, in hopes smtp will single it. */
		*(p+1) = '.';
		return;
	}
	for (q=p; *q; q++)
		;
	*--q = '\0';
}

/*
 * parseConfig -- parse config file, extract values of a few
 *	predefined variables.
 */
void parseConfig(FILE *fp) {
	char	line[MAXLINE],
		*p,*q;

	while (fgets(line,sizeof line,fp)) {
		/* Make comments invisible. */
		if ((p= strchr(line,'#'))) {
			*p = '\0';
		}
		/* Ignore malformed lines and comments. */
		if (strchr(line,'=') == NULL) 
			continue;
		/* Parse out keywords. */
		if (((p=strtok(line,"= \t\n")) != NULL) && ((q=strtok(NULL,"= \t\n")) != NULL)) {
			if (strcasecmp(p,"Root")==0) {
				Root = strdup(q);
				if (LogLevel > 0) {
				        log_event(LOG_INFO,
					       "Set Root=\"%s\".\n",Root);
				 }
			}
			else if (strcasecmp(p,"MailHub")==0) {
				MailHub = strdup(q);
				if (LogLevel > 0) {
					log_event(LOG_INFO,
						"Set MailHub=\"%s\".\n",MailHub);
				 }
			}
			else if (strcasecmp(p,"HostName")==0) {
				(void) strcpy(HostName,strdup(q));
				if (LogLevel > 0) {
					log_event(LOG_INFO,
						"Set HostName=\"%s\".\n",HostName);
				 }
			}

#ifdef REWRITE_DOMAIN
			else if (strcasecmp(p,"RewriteDomain")==0) {
				RewriteDomain = strdup(q);
				UseRD = YES;
				if (LogLevel > 0) {
				        log_event(LOG_INFO,
					       "Set RewriteDomain=\"%s\".\n",
						  RewriteDomain);
				 }
			}
#endif
			else {
				  log_event(LOG_INFO,
					"Unable to set %s=\"%s\".\n",
						p, q);
				 }
		}
	}
	return;
}


/*
** Config file access routines.
*/
int getConfig(void) {
	FILE	*fp;
	static char *locations[] = {
			"/etc/ssmtp/ssmtp.conf",
			NULL
	};
	char	**lp;

	for (lp= &locations[0]; *lp; lp++) {
		if ((fp= fopen(*lp,"r")) != NULL) {
			parseConfig(fp);
			(void) fclose(fp);
			return YES;
		}
	}
	return NO; /* We use the default for everything. */
}

/*
 * doOptions -- pull the options out of the command-line, process them 
 *      (and special-case calls to mailq, etc), and return the rest.
 */
char ** doOptions(int argc, char *argv[]) {
	int    i, 
		newArgC;
	static char *newArgV[MAXARGS];

	newArgV[0] = argv[0];
	newArgC = 1;

	if (strstr(argv[0],"mailq") != NULL) {
		/* Someone wants to know the queue state... */
		(void) printf("Mail queue is empty.\n");
		exit(0);
		}
	else if (strstr(argv[0],"newalias") != NULL) {
		/* Someone wanted to recompile aliases. */
		/* This is slightly more like to be a human... */
		die("newalias is meaningless to sSMTP: it doesn't do aliases.");
	}
	
	for (i=1; i < argc; i++) {
		if (argv[i][0] != '-') {
			newArgV[newArgC++] = argv[i];
			continue;
		}
		switch(argv[i][1]) {
		case 'b':
			switch (argv[i][2]) {
			case 'a': /* ARPANET mode. */
				die("-ba is not supported by sSMTP sendmail, nor is -t.");
			case 'd': /* Run as a daemon. */
				die("-bd is not supported by sSMTP sendmail. Use rSMTP under inetd instead.");
			case 'i': /* Initialize aliases. */	      continue;
			case 'm': /*  Default addr processing. */     continue;
			case 'p': /* Print mailqueue. */
				die("Mail queue is empty.");
			case 's': /* Read smtp from stdin. */
				die("-bs is not supported by sSMTP sendmail."); 
			case 't': /* Test mode. */
				die("-bt is meaningless to sSMTP sendmail. It doesn't route.");
			case 'v': /*  Verify names only. */
				die("-bv is meaningless to sSMTP sendmail. It doesn't route.");
			case 'z': /* Create  freeze file. */
				die("-bz is meaningless to sSMTP sendmail. It isn't programmable.");
			}
		case 'C': /* Configfile name. */		      continue;
		case 'd': /* Debug. */
			LogLevel = 1;
			Verbose = YES; /* Almost the same thing... */
			break;
		case 'E': /* insecure channel, don't trust userid. */ continue;
		case 'R': /* amount of the message to be returned */
			  /* Process queue for recipient. */
		case 'F': /* fullname of sender. */
		case 'f': /* Set from/sender address. */
		case 'r': /* Obsolete -f flag. */
			if (!argv[i][2])
				i++;
			continue;
				/* Should I support these??? When? */
		case 'h': /* Set hopcount. */			      continue;
		case 'm': /* Ignore originator in adress list. */     continue;
		case 'M': /* USe specified message-id. */	      continue;
		case 'N': /* dsn options */			i++;  continue;
		case 'n': /* No aliasing. */			      continue;
		case 'o':
			switch (argv[i][2]) {
			case 'A': /* Alternate aliases file. */	      continue;
			case 'c': /* Delay connections. */	      continue;
			case 'D': /* Run newaliases if rqd. */	      continue;
			case 'd': /* Deliver now, in background or queue. */
				/* This may warrant a diagnostic for b or q. */
				continue;
			case 'e': /* Errors: mail, write or none. */  continue;
			case 'F': /* Set tempfile mode. */	      continue;
			case 'f': /* Save ``From ' lines. */	      continue;
			case 'g': /* Set group id. */		      continue;
			case 'H': /* Helpfile name. */		      continue;
			case 'i': /* DATA ends at EOF, not \n.\n */   continue;
			case 'L': /* Log level. */		      continue;
			case 'm': /* Send to me if in the list. */    continue;
			case 'o': /* Old headers, spaces between adresses. */
				die("-oo (old header format) is not supported by sSMTP sendmail.");
			case 'Q': /* Queue dir. */		      continue;
			case 'r': /* Read timeout. */		      continue;
			case 's': /* Always init the queue. */	      continue;
			case 'S': /* Stats file. */		      continue;
			case 'T': /* Queue timeout. */		      continue;
			case 't': /* Set timezone. */		      continue;
			case 'u': /* Set uid. */		      continue;
			case 'v': /* Set verbose flag. */
				Verbose = YES;
				continue;
			}
			break;
		case 'q': /* Process the queue [at time] */
			die("Mail queue is empty.");
		case 't': /* Read message's To/Cc/Bcc lines. */
				Toption = YES; continue;
		case 'v': /* Verbose (ditto -ov). */
			Verbose = YES;
			break;
		case 'V': /*  Say version and quit. */
		/* Similar as die, but no logging */
			fprintf(stderr,
				"sSMTP version %s (not sendmail at all)\n",
				Version);
		        flush(); /* Send message to dead.letter */
			exit(0);
			break;
		}
	}
	newArgV[newArgC] = NULL;
	if (newArgC <= 1 && ! Toption) {
		die("no recipients supplied: no mail will be sent.");
	}
	if (newArgC > 1 && Toption) die ("Recipientlist with -t option not supported.");
	return &newArgV[0];
}

#ifdef REWRITE_DOMAIN 
int fixFromLines();
#else
#define fixFromLines(buffer) NO
#endif

char *headers;	/* Pointer to beginning of headers */
char *headere;	/* End of header */
#define CHUNK 4000	/* Sizeof allocation */

/*
 * ssmtp -- send the message (exactly one) from stdin to the smtp
 *	port on the mailhub.
 */
int ssmtp(char *argv[])  {
	char buffer[MAXLINE],*p,*c;
	int fd, i;

	if (getConfig() == NO) {
		log_event(LOG_INFO,"No ssmtp.conf in /etc/ssmtp.");
	}
	
	/* Header analysis first */
	/* allocation algorithm for header buffer missing */
	headere=headers=malloc(CHUNK);
	while ((fgets(buffer,sizeof buffer,stdin) != NULL) && (buffer[0]!='\n')) {
		if (Verbose) fprintf(stderr,"Read Header: %s",buffer);
		/* Trim off \n, double leading .'s */
		standardize(buffer);
		recordRequiredHeaders(buffer);
	        fixFromLines(buffer);
	        i=strlen(buffer)+1;
	        if (headere+i-headers >CHUNK) die ("Header too large Max is %d characters",CHUNK);
		strcpy(headere,buffer);headere+=strlen(buffer)+1;
		if (*buffer == '\0') {
			break;
		}
	}
	if (Toption) {
		/* Sorry no support for the exclusions on the command line */
		/*	argv=reciepients;	*/
		*rec++=NULL;
	}

	/* Now to the delivery of the message */
	(void) signal(SIGALRM,(void (*)())alarmHandler); /* Catch SIGALRMs. */ 
	(void) alarm((unsigned)MAXWAIT); /* Set initial timer. */
	if (setjmp(TimeoutJmpBuf) != 0) {
		/* Then the timer has gone off and we bail out. */
		die("connection lost in middle of processing, exiting."); 
	}
	if ((fd=openSocket(MailHub,PORTNUMBER)) == ERR) {
		die("can't open the smtp port (%d) on %s.",
			PORTNUMBER,MailHub);
	}
	else if (getOkFromSmtp(fd,buffer) == NO) {
    		die("didn't get initial OK message from smtp server.");
	}
	if (Verbose) {
		(void) fprintf(stderr,"Connected to smtp server %s\n",MailHub);
	}
	if (LogLevel > 0) {
		log_event(LOG_INFO,"Connected to smtp server %s\n",MailHub);
	}

	/* Send "HELO", hostname. */
	putToSmtp(fd,"HELO %s",HostName);
	(void) alarm((unsigned)MEDWAIT); 
	if (getOkFromSmtp(fd,buffer) == NO) {
    		die("server didn't accept hostname %s, replied \"%s\".",
			HostName,buffer);
	}

	/* Send "MAIL FROM:" line */
	strcpy(buffer,fromLine());
	/* Remove the real name if necessary - just send the address */
	c=strchr(buffer,'<');
	if (c) {
		c++;
		p=strchr(c,'>');
		if (p)
			*p='\0';
		}
	else
		c=buffer;
	putToSmtp(fd,"MAIL FROM:<%s>",c);

	(void) alarm((unsigned)MEDWAIT); 
	if (getOkFromSmtp(fd,buffer) == NO) {
    		die("smtp server didn't accept MAIL From, replied \"%s\".",
			buffer);
	}
	if (Verbose) {
		(void) fprintf(stderr,"Server accepted MAIL FROM: %s line.\n",
			  fromLine());
	}
	if (LogLevel > 0) {
		log_event(LOG_INFO,"Server accepted MAIL FROM: %s line.\n",
			  fromLine());
	}

	/* Send all the To: adresses. */
	/* Either we're using the -t option, or we're using the arguments */
	if (Toption) {
		if (!*reciepients)
			die("No recipient specified although -t option specified");
		i=0;
		do {
			putToSmtp(fd,"RCPT TO:<%s>",properRecipient(reciepients[i]));
			(void) alarm((unsigned)MEDWAIT);
			if (getOkFromSmtp(fd,buffer) == NO) {
				die("smtp server didn't accept RCPT To: command, replied \"%s\".",
					buffer);
			}
		} while (reciepients[++i]);
	}
	else {
		for (i=1; argv[i] != NULL; i++) {
			p=strtok(argv[i],",");
			while(p) {
				/* Address "My friend <foo@bar>" -> "foo@bar" */
				strcpy(buffer,p);
				c=strchr(buffer,'<');
				if (c) {
					c++;
					p=strchr(c,'>');
					if (p)
						*p='\0';
				}
				else
					c=buffer;
				putToSmtp(fd,"RCPT TO:<%s>",properRecipient(c));
				(void) alarm((unsigned)MEDWAIT); 
				if (getOkFromSmtp(fd,buffer) == NO) {
					die("smtp server didn't accept RCPT To: command, replied \"%s\".",
						buffer);
				}
				p=strtok(NULL,",");
			}
		}
	}
	if (Verbose) {
		(void) fprintf(stderr,"Server accepted To: line(s).\n");
	}
	if (LogLevel > 0) {
		log_event(LOG_INFO,"Server accepted To: line(s).\n");
	}

	/* Send DATA. */
	putToSmtp(fd,"DATA");
	(void) alarm((unsigned)MEDWAIT); 
	if (getFromSmtp(fd,buffer) != 3) {
		/* Oops, we were expecting "354 send your data". */
    		die("smtp server didn't accept DATA, replied \"%s\".",
			buffer);
	}
	if (Verbose) {
		(void) fprintf(stderr,"Message body transmission started.\n");
	}

	/* Send headers, with optional From: rewriting. */
	addInitialHeaders(fd);
	addRequiredHeaders(fd);	
	for(p=headers;p<headere;p+=strlen(p)+1) {
		putToSmtp(fd,"%s",p);
		(void) alarm((unsigned)MEDWAIT);
	}
	/* End of headers, start body. */
	putToSmtp(fd,""); /* Seperate headers and body. */
	while (fgets(buffer,sizeof buffer,stdin) != NULL) {
		/* Trim off \n, double leading .'s */
		standardize(buffer);
		putToSmtp(fd,"%s",buffer);
		(void) alarm((unsigned)MEDWAIT); 
	}
	/* End of body. */
	putToSmtp(fd,".");

	(void) alarm((unsigned)MAXWAIT); 
	if (getOkFromSmtp(fd,buffer) == NO) {
		die("smtp server wouldn't accept message, replied \"%s\".",
			buffer);
	}
	if (Verbose) {
		(void) fprintf(stderr,"Message body transmission complete.\n");
	}
	/* Close conection. */
	(void) signal(SIGALRM,SIG_IGN); 
		putToSmtp(fd,"QUIT");
	(void) getOkFromSmtp(fd,buffer);
	(void) close(fd);
	(void) log_event(LOG_INFO,"%s sent mail for %s",
		ProgName,Sender->pw_name);
	return 0;
}


/*
** Supporting libraries -- i/o.
*/

/*
 * openSocket -- open a socket on a specified machine.
 *      Adapted from code by Blair P. Houghton:
 *      Copyright 1991 Blair P. Houghton, All Rights Reserved, 
 *      copying and distribution permitted with copyright intact.
 */
int openSocket(char *hostName, int portNumber) {
	int fd;		/* socket to "plug" into the socket */
	struct sockaddr_in socketname;	/* mode, addr, and port data for */
						/* the socket */
	struct hostent *remote_host;	/* internet numbers, names */

	if ( (fd=socket(AF_INET,SOCK_STREAM,0)) < 0 ) {
		log_event(LOG_ERR,"unable to create a socket.\n");
		return ERR;
	}

	/* plug it into the listening socket */
	socketname.sin_family = AF_INET;
	if ((remote_host=gethostbyname(hostName)) == (struct hostent *)NULL) {
		log_event(LOG_ERR,"unable to locate host %s.\n",hostName);
		return ERR;
	}
	(void) bcopy((char *)remote_host->h_addr,(char *)&socketname.sin_addr,
		remote_host->h_length);
	socketname.sin_port = htons(portNumber);

	if (connect(fd,(struct sockaddr *)&socketname,sizeof socketname) < 0) {
		log_event(LOG_ERR,"unable to connect to \"%s\" port %d.\n",
			hostName,portNumber);
		return ERR;
	}
	return fd;
}

/*
 * getOkFromSmtp -- get a line and test the three-number string
 *      at the beginning.  If it starts with a 2, it's OK.
 */
int getOkFromSmtp(int fd, char *response) { 

	return (getFromSmtp(fd,response) == 2)? YES: NO;
}


/*
 * getFromSmtp -- get a line and return the initial digit.  Deal with
 *	continuation lines by reading to the last (non-continuation) line.
 */
int getFromSmtp(int fd, char *response) { 
	char *getLine();

	do {
		if (getLine(response,MAXLINE,fd) == NULL) {
			*response = '\0';
			return NO;
		}
	} while (response[3] == '-');
	if (LogLevel > 0) {
		log_event(LOG_INFO,"Received \"%s\" from smtp port.\n",response);
	}
	return atoi(response) / 100;
}
	



/*
 * getLine -- get a line of text from a fd instead of an fp.
 */
char *getLine(char *line, int size, int fd) {
	int     i;
	char    ch;

	for (i=0; read(fd,&ch,1) == 1;) {
		if (i == size-1) {
			/* Truncate like fgets. */
			line[i] = '\0';
			return line;
		}
		else if (ch == '\r')
			; /* Strip it. */
		else if (ch == '\n') {
			break;
		}
		else {
			line[i++] = ch;
#ifdef DEBUG
			line [i] = '\0';
#endif
		}
	}
	line[i] = '\0';
	return line;
}


/*
 * main -- make the program behave like sendmail, then call ssmtp.
 */
int main(int argc, char *argv[]) {
	char **newArgv;

	/* Try to be bulletproof (:-)) */
	(void) signal(SIGHUP,SIG_IGN);
	(void) signal(SIGINT,SIG_IGN);
	(void) signal(SIGTTIN,SIG_IGN);
	(void) signal(SIGTTOU,SIG_IGN);

	/* Set the globals. */
	ProgName = argv[0];
	if (gethostname(HostName,sizeof(HostName)) == ERR) {
		die("can't find the name of this host, %s, exiting.",
			"(an impossible condition)");
	}
	if ((Sender= getpwuid(getuid())) == NULL) {
		die("couldn't find password entry for sender (uid %d).",
			getuid());
	}
	(void) get_arpadate(DateString);

	newArgv = doOptions(argc,argv); 
	exit(ssmtp(newArgv));
	/*NOTREACHED*/
}
