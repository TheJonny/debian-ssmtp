/*

 $Id: net.c,v 1.8 2002/02/10 21:58:26 matt Exp $

 See COPYRIGHT for the license

*/
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <syslog.h>
#include <string.h>
#include <netdb.h>
#include "ssmtp.h"
#ifdef HAVE_SSL
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#ifdef HAVE_SSL
SSL *ssl;
#endif

extern int log_level;
extern int minus_v;

#ifdef INET6
/* protocol family which used in SMTP connection */
int protocolFamily = PF_UNSPEC;
#endif

/*
fd_getc() -- read a character from an fd
*/
ssize_t fd_getc(int fd, void *c)
{
#ifdef HAVE_SSL
	if(UseTLS==YES) { 
		return(SSL_read(ssl, c, 1));
	}
#endif
	return(read(fd, c, 1));
}

/*
fd_gets() -- get a characters from a fd instead of an fp
*/
char *fd_gets(char *buf, int size, int fd)
{
	int i = 0;
	char c;

	while((i < size) && (fd_getc(fd, &c) == 1)) {
		if(c == '\r');	/* Strip */
		else if(c == '\n') {
			break;
		}
		else {
			buf[i++] = c;
		}
	}
	buf[i] = (char)NULL;

	return(buf);
}

/*
SMTP_read() -- get a line and return the initial digit. Deal with continuation lines by reading to the last (non-continuation) line
*/
int SMTP_read(int fd, char *response)
{
	do {
		if(fd_gets(response, BUF_SZ, fd) == NULL) {
			return(NO);
		}
	}
	while(response[3] == '-');

	if(log_level > 0) {
		log_event(LOG_INFO, "%s\n", response);
	}

	if(minus_v) {
		(void)fprintf(stderr, "[<-] %s\n", response);
	}

	return(atoi(response) / 100);
}

/*
fd_puts() -- write to fd
*/
ssize_t fd_puts(int fd, const void *buf, size_t count) 
{
#ifdef HAVE_SSL
	if(UseTLS == YES) { 
		return(SSL_write(ssl, buf, count));
	}
#endif
	return(write(fd, buf, count));
}

/*
SMTP_write() -- a printf to an fd, which appends TCP/IP <CR/LF>
*/
void SMTP_write(int fd, char *format, ...)
{
	char buf[(BUF_SZ + 1)];
	va_list ap;

	va_start(ap, format);
	if(vsnprintf(buf, (BUF_SZ - 2), format, ap) == -1) {
		die("SMTP_write() -- vsnprintf() failed");
	}
	va_end(ap);

	if(log_level > 0) {
		log_event(LOG_INFO, "%s\n", buf);
	}

	if(minus_v) {
		(void)fprintf(stderr, "[->] %s\n", buf);
	}
	(void)strcat(buf, "\r\n");

	(void)fd_puts(fd, buf, strlen(buf));
}

/*
SMTP_open() --
*/
int SMTP_open(char *host, int port)
{
#ifdef INET6
	struct addrinfo hints, *ai0, *ai;
	char servname[NI_MAXSERV];
	int s;
#else
	struct sockaddr_in name;
	struct hostent *hent;
	int s, namelen;
#endif

#ifdef HAVE_SSL
	int err;

	/* Init SSL stuff */
	SSL_CTX *ctx;
	SSL_METHOD *meth;
	X509 *server_cert;

	SSL_load_error_strings();
	SSLeay_add_ssl_algorithms();
	meth=SSLv23_client_method();
	ctx = SSL_CTX_new(meth);
	if(!ctx) {
		log_event(LOG_ERR, "No SSL support inited\n");
		return(-1);
	}

	if(UseTLSCert==YES) { 
		if(SSL_CTX_use_certificate_chain_file(ctx, TLSCert) <= 0) {
			perror("Use certfile");
			return(-1);
		}

		if(SSL_CTX_use_PrivateKey_file(ctx, TLSCert, SSL_FILETYPE_PEM) <= 0) {
			perror("Use PrivateKey");
			return(-1);
		}

		if(!SSL_CTX_check_private_key(ctx)) {
			log_event(LOG_ERR, "Private key does not match the certificate public key\n");
			return(-1);
		}
	}
#endif

#ifdef INET6
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = protocolFamily;
	hints.ai_socktype = SOCK_STREAM;
	snprintf(servname, sizeof(servname), "%d", port);

	/* Check we can reach the host */
	if (getaddrinfo(host, servname, &hints, &ai0)) {
		log_event(LOG_ERR, "Unable to locate %s", host);
		return(-1);
	}

	for (ai = ai0; ai; ai = ai->ai_next) {
		/* Create a socket for the connection */
		s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (s < 0)
			continue;

		if (connect(s, ai->ai_addr, ai->ai_addrlen) < 0) {
			s = -1;
			continue;
		}
		break;
	}

	if (s < 0) {
		log_event (LOG_ERR, "unable to connect to \"%s\" port %d.\n",
			   host, port);
		return(-1);
	}
#else
	/* Check we can reach the host */
	if((hent = gethostbyname(host)) == (char)NULL) {
		log_event(LOG_ERR, "Unable to locate %s", host);
		return(-1);
	}

	/* Create a socket for the connection */
	if((s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		log_event(LOG_ERR, "Unable to create a socket");
		return(-1);
	}

/*
	name.sin_addr.s_addr = \
		htonl(((struct in_addr *)(hent->h_addr))->s_addr);
*/
	/* This SHOULD already be in Network Byte Order from gethostbyname() */
	name.sin_addr.s_addr = ((struct in_addr *)(hent->h_addr))->s_addr;
	name.sin_family = hent->h_addrtype;
	name.sin_port = htons(port);

	namelen = sizeof(struct sockaddr_in);
	if(connect(s, (struct sockaddr *)&name, namelen) < 0) {
		log_event(LOG_ERR, "Unable to connect to %s:%d", host, port);
		return(-1);
	}
#endif

#ifdef HAVE_SSL
	if(UseTLS == YES) {
		log_event(LOG_INFO, "Creating SSL connection to host");

		ssl = SSL_new(ctx);
		if(!ssl) {
			log_event(LOG_ERR, "SSL not working");
			return(-1);
		}
		SSL_set_fd(ssl, s);

		err = SSL_connect(ssl);
		if(err < 0) { 
			perror("SSL_connect");
			return(-1);
		}

		if(log_level > 0) { 
			log_event(LOG_INFO, "SSL connection using %s",
				SSL_get_cipher(ssl));
		}

		server_cert = SSL_get_peer_certificate(ssl);
		if(!server_cert) {
			return(-1);
		}
		X509_free(server_cert);

		/* TODO: Check server cert if changed! */
	}
#endif

	return(s);
}

/*
SMTP_OK() -- get a line and test the three-number string at the beginning.  If it starts with a 2, it's OK
*/
int SMTP_OK(int fd, char *response)
{
	return(SMTP_read(fd, response) == 2) ? YES : NO;
}
