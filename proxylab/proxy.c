#include <stdio.h>

#include "cache.h"
#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define MIN_PORT_NUMBER 4500
#define MAX_PORT_NUMBER 65000

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_str = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding = "Accept-Encoding: gzip, deflate\r\n";

static const char *connection = "Connection: close\r\n";
static const char *proxy_connection = "Proxy-Connection: close\r\n";
static const char *init_version = "HTTP/1.0\r\n";
static const char *fail_str = "HTTP/1.0 400 Bad Request\r\n";

CacheList *list;

/* function prototypes */
void usage();
void handle_client(int *arg);
int read_request(char *str, int client_fd, char *host, char *port, char *cache_index, char *resource);
int forward_to_server(char *host, char *port, int *server_fd, char *request_str);
int read_and_forward_response(int server_fd, int client_fd, char *cache_index, char *content);
int forward_content_to_client(int client_fd, char *content, unsigned int len);

int append(char *content, char *str, unsigned int add_size, unsigned int *prev_size);
int parse_request(char *str, char *method, char *protocol, char *host_port, char *resource, char *version);
void get_host_and_port(char *host_port, char *host, char *port);
void close_fd(int *client_fd, int *server_fd);

int main(int argc, char **argv)
{
	int port;
	struct sockaddr_in clientaddr;
	int clientlen = sizeof(clientaddr);
	int listenfd;
	
	port = atoi(argv[1]);
	if (port < MIN_PORT_NUMBER || port > MAX_PORT_NUMBER){
		printf("invalid port number");
		exit(1);
	}
	
	list = cache_init();
	
    Signal(SIGPIPE, SIG_IGN);
	
	listenfd = Open_listenfd(port);
	while (1){
		pthread_t tid;
		int *connfdp = Malloc(sizeof(int));
		*connfdp = -1;
		*connfdp = Accept(listenfd, (SA *) &clientaddr, (socklen_t *)&clientlen);
		Pthread_create(&tid, NULL, (void *)handle_client, (void *)connfdp);
	}
	
	cache_destruct(list);
	return 0;
}


void handle_client(int *varg){
	printf("pid: %u\n",(unsigned int)Pthread_self());
	Pthread_detach(Pthread_self());
	
	int client_fd = (int)(*varg);
	int server_fd = -1;
	
	char tmp_str[MAXBUF];
	
	char request_str[MAXBUF];
	char host[MAXBUF], port[MAXBUF], resource[MAXBUF];
	char cache_index[MAXBUF], content[MAX_OBJECT_SIZE];
	
	unsigned int len;
	
	int r_value = read_request(request_str, client_fd, host, port, cache_index, resource);
	
	printf("Read data from %s:%s\n",host,port);
	fflush(stdout);
	
	if (!r_value) {
		if (!read_node_content(list, cache_index, content, &len)) {
			printf("cache hit!\n");
			if (forward_content_to_client(client_fd, content, len) == -1)
				fprintf(stderr, "forward content to client error.\n");
			Close(client_fd);
			return;
		}
		else {
			int server_value = forward_to_server(host, port, &server_fd, request_str);
			if (server_value == -1){
				fprintf(stderr, "forward content to server error.\n");
				strcpy(tmp_str, fail_str);
				Rio_writen(client_fd, tmp_str, strlen(fail_str));
			}
			else if (server_value == -2) {
				fprintf(stderr, "forward content to server error(dns look up fail).\n");
				strcpy(tmp_str, fail_str);
				Rio_writen(client_fd, tmp_str, strlen(fail_str));
			}
			else {	
				int f_value = read_and_forward_response(server_fd, client_fd, cache_index, content);
				if (f_value == -1)
					fprintf(stderr, "forward content to client error.\n");
				else if (f_value == -2 && list)
					fprintf(stderr, "save content to cache error.\n");
			}
		}
		close_fd(&client_fd, &server_fd);
	}
	
    	return;
}

int read_request(char *str, int client_fd, char *host, char *port, char *cache_index, char *resource) {
	char tmpstr[MAXBUF];
	char method[MAXBUF], protocol[MAXBUF], host_port[MAXBUF];
	char version[MAXBUF];
	
	rio_t rio_client;
	
	Rio_readinitb(&rio_client, client_fd);
	if (Rio_readlineb(&rio_client, tmpstr, MAXBUF) == -1)
		return -1;
	
	if (parse_request(tmpstr, method, protocol, host_port, resource, version) == -1)
		return -1;
	
	get_host_and_port(host_port, host, port);
	
	if (strstr(method, "GET")) {
		strcpy(str, method);
		strcat(str, " ");
		strcat(str, resource);
		strcat(str, " ");
		strcat(str, init_version);
		
		if(strlen(host))
		{
			strcpy(tmpstr, "Host: ");
			strcat(tmpstr, host);
			strcat(tmpstr, ":");
			strcat(tmpstr, port);
			strcat(tmpstr, "\r\n");
			strcat(str, tmpstr);
		}
		
		strcat(str, user_agent_hdr);
		strcat(str, accept_str);
		strcat(str, accept_encoding);
		strcat(str, connection);
		strcat(str, proxy_connection);
		
		while(Rio_readlineb(&rio_client, tmpstr, MAXBUF) > 0) {
			if (!strcmp(tmpstr, "\r\n")){
				strcat(str,"\r\n");
				break;
			}
			else if(strstr(tmpstr, "User-Agent:") || strstr(tmpstr, "Accept:") ||
				strstr(tmpstr, "Accept-Encoding:") || strstr(tmpstr, "Connection:") ||
				strstr(tmpstr, "Proxy Connection:") || strstr(tmpstr, "Cookie:"))
				continue;
			else if (strstr(tmpstr, "Host:")) {
				if (!strlen(host)) {
					sscanf(tmpstr, "Host: %s", host_port);
					get_host_and_port(host_port, host, port);
					strcpy(tmpstr, "Host: ");
					strcat(tmpstr, host);
					strcat(tmpstr, ":");
					strcat(tmpstr, port);
					strcat(tmpstr, "\r\n");
					strcat(str, tmpstr);
				}
			}
			else
				strcat(str, tmpstr);
		}
		
		strcpy(cache_index, host);
		strcat(cache_index, ":");
		strcat(cache_index, port);
		strcat(cache_index, resource);
		
		return 0;
	}
	
	return 1;
}

int forward_to_server(char *host, char *port, int *server_fd, char *request_str) {
	*server_fd = Open_clientfd(host, atoi(port));
	
	if (*server_fd < 0) {
		if (*server_fd == -1)
			return -1;
		else 
			return -2;
	}
	if(Rio_writen(*server_fd, request_str, strlen(request_str)) == -1)
		return -1;
	
	return 0;
}

int forward_content_to_client(int client_fd, char *content, unsigned int len) {
	if (Rio_writen(client_fd, content, len) == -1)
		return -1;
		
	return 0;
}

int read_and_forward_response(int server_fd, int client_fd, 
		char *cache_index, char *content) {
		
	rio_t rio_server;
	char tmp_str[MAXBUF];
	unsigned int size = 0, len = 0, cache_size = 0;
	int valid_size = 1;
	
	content[0] = '\0';
	
	Rio_readinitb(&rio_server, server_fd);
	
	do {
		if(Rio_readlineb(&rio_server, tmp_str, MAXBUF) == -1)
			return -1;
		
		if(valid_size)
			valid_size = append(content, tmp_str, strlen(tmp_str), &cache_size);
			
		if (strstr(tmp_str, "Content-length")) 
			sscanf(tmp_str, "Content-length: %d", &size);

		if (strstr(tmp_str, "Content-Length")) 
			sscanf(tmp_str, "Content-Length: %d", &size);
		
		if (Rio_writen(client_fd, tmp_str, strlen(tmp_str)) == -1)
			return -1;
			
	}while(strcmp(tmp_str, "\r\n") != 0 && strlen(tmp_str));
	
	if(size) {
		while(size > MAXBUF) {
			if((len = Rio_readnb(&rio_server, tmp_str, MAXBUF)) == -1)
				return -1;
				
			if(valid_size)
				valid_size = append(content, tmp_str, MAXBUF, &cache_size); 
			if (Rio_writen(client_fd, tmp_str, len) == -1)
				return -1;
			
			size -= MAXBUF;
		}
		
		if(size) {
			if((len = Rio_readnb(&rio_server, tmp_str, size)) == -1)
				return -1;
				
			if(valid_size)
				valid_size = append(content, tmp_str, size, &cache_size);
				
			if (Rio_writen(client_fd, tmp_str, len) == -1)
				return -1;
		}
	}
	else {
		while((len = Rio_readnb(&rio_server, tmp_str, MAXLINE)) > 0) {
			if(valid_size)
				valid_size = append(content, tmp_str, len, &cache_size);
				
			if (Rio_writen(client_fd, tmp_str, len) == -1)
				return -1;
		}
	}
	
	if (valid_size) {
		if (insert_content_node(list, cache_index, content, cache_size) == -1)
			return -2;
		printf("insert correct!\n");
	}
	
	return 0;
}

int append(char *content, char *str, unsigned int len1, unsigned int *len2) {
	if(len1 + (*len2) > MAX_OBJECT_SIZE)
		return 0;
	
	memcpy(content + (*len2), str, len1);
	*len2 += len1;
	return 1;
}

int parse_request(char *str, char *method, char *protocol, char *host_port, char *resource, char *version){
	char url[MAXBUF];
	
	if((!strstr(str, "/")) || !strlen(str))
		return -1;
	
	strcpy(resource, "/");
	sscanf(str,"%s %s %s", method, url, version);
	
	if (strstr(url, "://")) 
		sscanf(url, "%[^:]://%[^/]%s", protocol, host_port, resource);
	else
		sscanf(url, "%[^/]%s", host_port, resource);
		
	return 0;
}

void get_host_and_port(char *host_port, char *host, char *port){
	char *tmpstr = strstr(host_port,":");
	if (tmpstr) {
		*tmpstr = '\0';
		strcpy(port, tmpstr + 1);
	}
	else
		strcpy(port, "80");
	
	strcpy(host, host_port);
}

void close_fd(int *client_fd, int *server_fd) {
	if(client_fd && *client_fd >=0)
		Close(*client_fd);
		
	if(server_fd && *server_fd >=0)
		Close(*server_fd);
}
