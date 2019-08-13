#
# A makefile with a complete set of defaults for sSMTP
#
# Things ssmtp needs to know as -D options:
#	MAILHUB, the machine to send all mail via. Default
#		is ``mailhost''.
#	REWRITE_DOMAIN, the domain to rewrite mail as coming
#		from.  Only rewrites ``From:'' line if this
#		variable is set, and nothing whatsoever if unset.
#		NOT required if using zmailer or a sane sendmail
#		on the mailhub. Defaults to off.
#	LOGGING, the mechanism use to log problems and successes:
#		SYSLOG, logging is to be done to syslog(2).
#		LOGFILE, if a file gets it too.
#		Default is no logging, which is not smart.
#	LACKING, the flags used to select optional replacement code:
#		NOSTRDUP, NOSTRSTR, NOSTRNCASECMP, NOSTRTOK for strings,
#		OLDSYSLOG, to indicate obsolete syslog/openlog function.
#		Defaults to empty.
#

MAILHUB=mail
LOGGING=-DSYSLOG # I recommend this strongly.

LACKING= # For SunOS 4.1.1, SPARCs and 3/60s

#BASEFLAGS=-DMAILHUB=\"${MAILHUB}\" ${LOGGING} ${LACKING} \
#	-DREWRITE_DOMAIN=\"${REWRITE_DOMAIN}\" 
#	Do not define the above unless rewriting is necessary.
#BASEFLAGS=-DMAILHUB=\"${MAILHUB}\" ${LOGGING} ${LACKING} -DREWRITE_DOMAIN
BASEFLAGS=${LOGGING} ${LACKING} -DREWRITE_DOMAIN

# (End of tuning section).

# Places to install things, used to relocate things for Ultrix kits.
# 	ROOT normally is the empty string...
#ROOT=
DESTDIR=${ROOT}/usr/sbin
MANDIR=${ROOT}/usr/man/man8
ETCDIR=${ROOT}/etc
SSMTPCONFDIR=${ETCDIR}/ssmtp
# (End of relocation section)

# Uncomment this if you have defined LACKING
#SRCS= ssmtp.c string_ext.c
SRCS= ssmtp.c

OBJS=$(SRCS:.c=.o)

FLAGS= -Wall -O6 ${BASEFLAGS}
#FLAGS= -Wall -g ${BASEFLAGS}

CFLAGS= ${FLAGS} ${BASEFLAGS}

all: ssmtp

install: ssmtp
	test `whoami` = root
	install -d -m 755 ${DESTDIR}
	install -s -m 755 ssmtp ${DESTDIR}/ssmtp
	install -d -m 755 ${MANDIR}
	install -m 644 ssmtp.8 ${MANDIR}/ssmtp.8
	install -d -m 755 ${SSMTPCONFDIR}
	install -m 644 ssmtp.conf ${SSMTPCONFDIR}/ssmtp.conf
	install -m 644 revaliases ${SSMTPCONFDIR}/revaliases


install-sendmail: ssmtp install
	ln -s ssmtp ${DESTDIR}/sendmail
	install -d -m 755 ${DESTDIR}/../lib
	ln -s ../sbin/sendmail ${DESTDIR}/../lib
	ln -s ssmtp.8 ${MANDIR}/sendmail.8

deinstall:
	test `whoami` = root
	rm -f ${DESTDIR}/ssmtp
	rm -f ${MANDIR}/ssmtp.8
	rm -f ${SSMTPCONFDIR}/mail.conf ${SSMTPCONFDIR}/revaliases
	rmdir ${SSMTPCONFDIR}

deinstall-sendmail: deinstall
	rm -f ${DESTDIR}/sendmail ${DESTDIR}/../lib/sendmail
	rm -f ${MANDIR}/sendmail.8

clean:
	rm -f ssmtp *.o core

# Binaries:
ssmtp: ${OBJS}
	${CC} -o ssmtp ${CFLAGS} ${OBJS}
