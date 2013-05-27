/* 
   Simple IRCbot skelton.
   Written by Ole Fredrik Skudsvik <ole.skudsvik@gmail.com> 2012.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>

/* Macro used in parseData. */
#define addToBufUntilChr(p, c, dest) for(i=0, p++; *p != c && *p != ' ' && *p != '\0'; p++, i++); \
										   dest = (char*)malloc(sizeof(char) * i+1); \
snprintf(dest, i+1, "%s", (p-i));

#define BUFSIZE 1024

/* Global variables. */
int     IRCSOCKET;
char    CHANNEL[32];
char    NICK[32];
char    IRCSERVER[128];
int     PORT;

/* Holder struct for IRC message. */
typedef struct {
		char** params;
		char* trailing;
		char* nick;
		char* user;
		char* host;
		int numParams;
} ircPacket;

/* Holder struct for IRC command handlers. */
typedef struct {
		const char* command;
		void (*handlerFunc)(ircPacket* ircP);
} ircHandler;

/* Forward declarations. */
void cmd_ping(ircPacket* ircP);
void cmd_376(ircPacket* ircP);
void cmd_privmsg(ircPacket* ircP);

/* IRC command handlers. */
ircHandler ircHandlers[] = {
		{ "PING", cmd_ping }, /* PING */
		{ "376", cmd_376 }, /* END OF MOTD */
		{ "PRIVMSG", cmd_privmsg }
};

/* Free dynamic allocated memory. */
void free_ircpacket(ircPacket* ircP) {
		int i;

		for (i = 0; i < ircP->numParams; i++) {
				if (ircP->params[i] != NULL) {
						free(ircP->params[i]);
						ircP->params[i] = NULL;
				}
		}

		if (ircP->params != NULL) free(ircP->params);
		if (ircP->trailing != NULL) free(ircP->trailing);
}

/* Parse IRC message into a ircPacket structure. */
int parseData(char* buf, ircPacket* ircP) {
		char *p = buf;
		unsigned int paramLen = 0, i;

		ircP->numParams	= 0;
		ircP->trailing	= NULL;
		ircP->params	= NULL;
		ircP->nick	    = NULL;
		ircP->user	    = NULL;
		ircP->host	    = NULL;

		if (!*p) return 0;

		do {
				/* Extract nick, user and host. */
				if (*p == ':' && p == buf) {
						addToBufUntilChr(p, '!', ircP->nick); /* Nick. */
						if (*p == '!') { addToBufUntilChr(p, '@', ircP->user); } /* User. */
						if (*p == '@') { addToBufUntilChr(p, ' ', ircP->host); } /* Host. */
						if (*p == ' ') p++; /* Don't send leading space to the param parser. */
						paramLen = 0;
						continue;
				}

				/* Extract stuff after : (trailing). */
				if (*p == ':' && p != buf) {
						p++;
						ircP->trailing = (char*)malloc(sizeof(char) * strlen(p)+1);
						snprintf(ircP->trailing, strlen(p)+1, "%s", p);
						break;
				}

				/* Extract parameters. */
				if (*p == ' ' || *p == '\0') {			
						ircP->params = (char**)realloc(ircP->params, sizeof(char *) * (ircP->numParams+1));
						ircP->params[ircP->numParams] = (char*) malloc(paramLen+1);
						snprintf(ircP->params[ircP->numParams], paramLen+1, "%s", p-paramLen);
						ircP->numParams++;
						paramLen = 0;
				} else paramLen++;

				p++;
		} while (*p != '\0');

		return ircP->numParams;
}

/* Send RAW IRC command. */
int irc_send(const char* fmt, ...) {
		char buf[BUFSIZE] = { 0 };

		va_list	listPointer;
		va_start(listPointer, fmt);
		vsnprintf(buf, BUFSIZE-2, fmt, listPointer);
		va_end(listPointer);

		printf("> %s.\n", buf);
		strcat(buf, "\r\n");

		return send(IRCSOCKET, buf, strlen(buf), 0);
}

/* Respond to PING. */
void cmd_ping(ircPacket *ircP) {
		irc_send("PONG :%s", ircP->trailing);
}

/* Join channels on 376 END OF MOTD. */
void cmd_376(ircPacket *ircP) {
		irc_send("JOIN #%s", CHANNEL);
}

/* Respond if someone writes the bot's nickname. */
void cmd_privmsg(ircPacket* ircP) {
		if (strncmp(ircP->trailing, NICK, strlen(NICK)) == 0) {
				irc_send("PRIVMSG %s :Hello %s! Your user@host is %s@%s!", 
								ircP->params[1], ircP->nick, ircP->user, ircP->host);
		}
}

void error(int exitval, const char* msg) {
		perror(msg);
		exit(exitval);
}

int main(int argc, char** argv) {
		struct sockaddr_in sin;
		struct hostent* connectHost;
		char recvBuf[BUFSIZE], c;
		int recvLen = 0, i, port;
		ircPacket ircP;

		if (argc != 5) {
				printf("Usage: %s <server> <port> <nick> <channel (without #)>.\n", argv[0]);
				return 0;
		}

		strncpy(IRCSERVER, argv[1], 32);
		strncpy(NICK, argv[3], 32);
		strncpy(CHANNEL, argv[4], 32);
		port = atoi(argv[2]);

		connectHost = gethostbyname(IRCSERVER);
		IRCSOCKET = socket(AF_INET, SOCK_STREAM, 0);

		if (IRCSOCKET == -1) {
				perror("Could not open socket.\n");
				exit(1);
		}

		sin.sin_family	= AF_INET;
		sin.sin_port	= htons(port);
		sin.sin_addr 	= *((struct in_addr*)connectHost->h_addr);

		if (connect(IRCSOCKET, (struct sockaddr *)&sin, sizeof(struct sockaddr)) == -1)
				error(1, "Could not connect");

		irc_send("USER %s %s %s :Simple ircbot", NICK, NICK, IRCSERVER);
		irc_send("NICK :%s", NICK);

		while(1) {
				for (i = 0; recvLen = recv(IRCSOCKET, &c, sizeof(char), 0), c!='\n' && i <= BUFSIZE; recvBuf[i++] = c);

				if (recvLen == 0) 
						error(0, "Server closed connection");

				if (recvLen == -1) 
						error(1, "Error on recv()");

				recvBuf[i-1] = '\0';

				printf("< %s.\n", recvBuf);
				parseData(recvBuf, &ircP);

				if (ircP.numParams > 0) {
						for (i = 0; i < (int)(sizeof(ircHandlers) / sizeof(ircHandler)); i++) {
								if (strcmp(ircHandlers[i].command, ircP.params[0]) == 0)
										ircHandlers[i].handlerFunc(&ircP);
						}
				}

				free_ircpacket(&ircP);
		}

		close(IRCSOCKET);
		return 0;
}
