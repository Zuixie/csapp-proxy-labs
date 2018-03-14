#include "csapp.h"
#include <stdio.h>

#ifndef __DEBUG_H__
#define __DEBUG_H__

#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
# define LOG(str) (printf("%s (%d) - <%s> %s\n", __FILE__ , __LINE__ ,__FUNCTION__, str))
#else
# define dbg_printf(...)
# define LOG(str) 
#endif

#endif

#define SUCCESS 0

#define ERROR_ADD_FIELD_MALLOC 0x01
#define ERROR_NO_HOST_FIELD 0x02
#define ERROR_REQUEST_LINE 0x03
#define ERROR_MALLOC_FAIL 0x04
#define ERROR_RESPONSE_LINE 0x05

#define HEAD_CONTENT_LENGTH "Content-Length"
#define HEAD_HOST "Host"
#define HEAD_CONNECTION "Connection"
#define HEAD_USER_AGENT "User-Agent"
#define HEAD_PROXY_CONNECTION "Proxy-Connection"

#define HTTP_VERSION "HTTP/1.0"
#define DEFAULT_PORT "80"

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3";

struct field
{
    char *name;
    char *value;
    struct field *next;
    struct field *prev;
};

typedef struct
{
    char *method;
    char *uri;
    char *version;
    struct field field_list;
    char *content;
    int content_length;
} request;

typedef struct 
{
    char *version;
    unsigned int status_code;
    char *status_msg;
    struct field field_list;
    char *content;
    int content_length;
} response;

struct field *findfields_by_key(struct field *field_list, char *key);
int addfield(struct field *field_list, const char *key, const char *value);
int remove_fields_by_key(struct field *field_list, char *key);
int remove_fields_all(struct field *head);
char *strip_right(char *buf);   

int get_host_port(request *req, char *hostbuf, char *portbuf);
int parse_host_port(char *buf, char *host, char *port);

int read_request_header_fields(rio_t *rio, request *req);
int read_request_line(rio_t *rio, request *req);
int read_response(rio_t *rio, response *resp);
int read_response_line(rio_t *rio, response *resp);
int read_response_header_fields(rio_t *rio, response *resp);
int read_response_body(rio_t *rio, response *resp);

int fix_response(response *resp);
int fix_request(request *req);

size_t count_request_header_length(request *req);
size_t count_response_header_length(response *resp);
size_t count_fields_length(struct field *field_list);
int generate_fields_str(struct field *field_list, char *buf, size_t length);
int generate_request_str(request *req, char *buf, size_t length);
int generate_response_str(response *resp, char *buf, size_t length);

int write_to_client(int connfd, response *resp);
int write_to_server(int connfd, request *req);

void release_response(response *resp);
void release_request(request *req);

int service_with_https(int connfd, rio_t *rio, request *req);

void printf_request(request *req);

int init();
void service(int connfd);
int doit(int connfd);

void sigpipe_handler(int sig);
void *thread(void *vargp);

int set_nonblocking(int fd)
{
    int flags;
    if ((flags = fcntl(fd, F_GETFL, 0)) == -1)
        flags = 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char **argv)
{

    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    int ret;

    /* Check command line args */
    if (argc != 2) {
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
	    exit(1);
    }

    if ((ret = init()) != SUCCESS) 
    {
        fprintf(stderr, "init failed %d\n", ret);
        exit(1);
    }
    dbg_printf("init success\n");

    listenfd = Open_listenfd(argv[1]);
    while (1) {
	    clientlen = sizeof(clientaddr);
	    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        dbg_printf("Accepted connection from (%s, %s)\n", hostname, port);  
        service(connfd);
    }

    return 0;
}

void sigpipe_handler(int sig)
{
    dbg_printf("sigpipe !!!!! \n");
}

int init()
{
    Signal(SIGPIPE,  sigpipe_handler);
    return SUCCESS;
}

void *thread(void *vargp)
{
    int connfd = (int)(vargp);
    int ret;

    Pthread_detach(Pthread_self());

	ret = doit(connfd);
    if (ret != SUCCESS)
    {
        fprintf(stderr, "service failed ret:%d\n", ret);
    }

    Close(connfd); 
    return NULL;
}

void service(int connfd)
{
    pthread_t tid;
    Pthread_create(&tid, NULL, thread, (void *)(connfd));
    // int ret;
	// ret = doit(connfd);
    // if (ret != SUCCESS)
    // {
    //     fprintf(stderr, "service failed ret:%d\n", ret);
    // }

    // Close(connfd);   
}

int doit(int connfd)
{
    rio_t rio;
    rio_t rioserver;
    int server_fd = -1;
    int ret = SUCCESS;
    request req;
    response resp;
    char host[MAXLINE];
    char port[256];

    memset(&req, 0, sizeof(request));
    memset(&resp, 0, sizeof(response));

    // read connfd  
    Rio_readinitb(&rio, connfd);

    ret = read_request_line(&rio, &req);
    if (ret != SUCCESS) 
    {
        goto _exit;
    }

    // HTTPS
    if (!strcmp(req.method, "CONNECT")) 
    {
        // handle the https
        dbg_printf("https request %s!!!\n", req.uri);
        ret = service_with_https(connfd, &rio, &req);
        goto _exit;
    }

    if ((ret = read_request_header_fields(&rio, &req)) != SUCCESS)
    {
        goto _exit;
    }
    
    // get host:port
    if((ret = get_host_port(&req, host, port)))
    {
        goto _exit;
    }
    // dbg_printf("host:%s port:%s\n", host, port);

    // modify content of the request to meet the expreimental requirements 
    if ((ret = fix_request(&req)) != SUCCESS)
    {
        goto _exit;
    }
    printf_request(&req);

    // connect to server
    dbg_printf("start connect: %s:%s\n", host, port);
    server_fd = open_clientfd(host, port);
    if (server_fd < 0)
    {
        fprintf(stderr, "open_clientd error.\n");
        goto _exit;
    }

    Rio_readinitb(&rioserver, server_fd);

    // write to server
    ret = write_to_server(server_fd, &req);
    if (ret != SUCCESS)
    {
        goto _exit;
    }

    // read response
    if ((ret = read_response(&rioserver, &resp)) != SUCCESS) 
    {
        goto _exit;
    }
    
    // write to client;
    fix_response(&resp);
    ret = write_to_client(connfd, &resp);

_exit:

    if (server_fd >= 0)
        Close(server_fd);

    release_request(&req);
    release_response(&resp);
    return ret;

}

// TODO beat
int service_with_https(int connfd, rio_t *rio, request *req)
{
    char host[256];
    char port[256];
    char buf[MAXLINE];
    int server_fd = -1;
    rio_t rioserver;
    int ret;
    size_t size;
    fd_set read_set, ready_set;

    if ((ret = parse_host_port(req->uri, host, port)) != SUCCESS)
    {
        return ret;
    }
    dbg_printf("host:%s, port:%s\n", host, port);

    server_fd = open_clientfd(host, port);

    if (server_fd < 0)
    {
        fprintf(stderr, "open_clientd error.\n");
        goto _quit;
    }
    Rio_writen(connfd, "HTTP/1.1 200 Connection Established\r\n\r\n", strlen("HTTP/1.1 200 Connection Established\r\n\r\n"));
    
    Rio_readinitb(&rioserver, server_fd);
    
    FD_ZERO(&read_set);
    FD_SET(connfd, &read_set);
    FD_SET(server_fd, &read_set);

    set_nonblocking(server_fd);
    set_nonblocking(connfd);

    while(1) 
    {
        ready_set = read_set;
        dbg_printf("select ... ...\n");
        Select(server_fd + 1, &ready_set, NULL, NULL, NULL);
        if (FD_ISSET(server_fd, &ready_set))
        {
            dbg_printf("server_fd ...\n");
            // size = rio_readn(server_fd, buf, MAXLINE);
            size = read(server_fd, buf, MAXLINE);
            dbg_printf("%s-p: %d\n", __FUNCTION__, (int)size);
            Rio_writen(connfd, buf, size);
            if (size == 0)
            {
                break;
            }
        } 

        if (FD_ISSET(connfd, &ready_set)) 
        {
            dbg_printf("connfd ...\n");
            size = read(connfd, buf, MAXLINE);
            dbg_printf("%s-r: %d\n", __FUNCTION__, (int)size);
            Rio_writen(server_fd, buf, size);
            if (size == 0)
            {
                break;
            }
        }

    }
    
_quit:
    if (server_fd >= 0)
        Close(server_fd);

    return SUCCESS;
}

int read_response(rio_t *rio, response *resp)
{
    LOG("start ...");
    int ret;
    if ((ret = read_response_line(rio, resp)) != SUCCESS)
    {
        return ret;
    }

    if ((ret = read_response_header_fields(rio, resp)) != SUCCESS)
    {
        return ret;
    }

    if ((ret = read_response_body(rio, resp)) != SUCCESS)
    {
        return ret;
    }

    LOG("end...");
    return SUCCESS;
}

int read_response_line(rio_t *rio, response *resp)
{
    char buf[MAXLINE];
    char *sp1, *sp2;

    Rio_readlineb(rio, buf, MAXLINE);
    dbg_printf("%s:%s", __FUNCTION__, buf);
    strip_right(buf);

    sp1 = strchr(buf, ' ');
    if (sp1 == NULL)
    {
        fprintf(stderr, "the format of response is error.\n");
        return ERROR_RESPONSE_LINE;
    }
    *sp1 = 0;
    sp1++;

    sp2 = strchr(sp1, ' ');
    if (sp2 == NULL)
    {
        fprintf(stderr, "the format of response is error.\n");
        return ERROR_RESPONSE_LINE;
    }
    *sp2 = 0;
    sp2++;

    resp->version = (char *)malloc(strlen(buf) + 1);
    if (resp->version == NULL)
    {
        fprintf(stderr, "malloc response version failed.\n");
        return ERROR_MALLOC_FAIL;
    }    
    memcpy(resp->version, buf, strlen(buf) + 1);
    
    resp->status_code = atoi(sp1);
    
    resp->status_msg = (char *)malloc(strlen(sp2) + 1);  
    if (resp->status_msg == NULL)
    {
        fprintf(stderr, "malloc response status msg failed.\n");
        return ERROR_MALLOC_FAIL;
    }
    memcpy(resp->status_msg, sp2, strlen(sp2) + 1);

    return SUCCESS;
}

int read_response_header_fields(rio_t *rio, response *resp)
{
    char buf[MAXLINE];
    char *value, *name;
    int ret;
    size_t readsize = 0;

    readsize = Rio_readlineb(rio, buf, MAXLINE);

    while (strcmp(buf, "\r\n") && readsize > 0) 
    {
        dbg_printf("%s: %s", __FUNCTION__, buf);
        // delete "\r\n" in the end of line
        strip_right(buf);

        // split by ':'
        if ((value = strchr(buf, ':')) != NULL) 
        {
            name = buf;
            *value = 0;
            value += 2;

            ret = addfield(&(resp->field_list), name, value);
            if (ret) 
            {
                return ret;
            }
        }

        readsize = Rio_readlineb(rio, buf, MAXLINE);
    }

    return SUCCESS;
}

int read_response_body(rio_t *rio, response *resp)
{
    struct field *f;
    int length;

    f = findfields_by_key(&resp->field_list, HEAD_CONTENT_LENGTH);
    if (f == NULL) 
    {
        resp->content_length = 0;
        dbg_printf("%s %s is null\n", __FUNCTION__, HEAD_CONTENT_LENGTH);
        return SUCCESS;
    }

    length = atoi(f->value);

    resp->content = (char *)malloc(length);
    if (resp->content == NULL)
    {
        fprintf(stderr, "malloc response content is failed.\n");
        return ERROR_MALLOC_FAIL;
    }

    resp->content_length = length;
    
    Rio_readnb(rio, resp->content, length);
    
    return SUCCESS;
}

// read method + uri + version of request
int read_request_line(rio_t *rio, request *req)
{
    LOG("start...");
    char buf[MAXLINE];
    char method[64];
    char uri[MAXLINE];
    char version[256];
    int ret;

    Rio_readlineb(rio, buf, MAXLINE);
    dbg_printf("first line: %s", buf);
    ret = sscanf(buf, "%s %s %s", method, uri, version);
    if (ret < 3) 
    {
        fprintf(stderr, "%s the format of the request is error.\n", __FUNCTION__);
        return ERROR_REQUEST_LINE;
    }

    req->method = (char *)malloc(strlen(method) + 1);
    if (req->method == NULL) 
    {
        fprintf(stderr, "%s malloc method failed.\n", __FUNCTION__);
        return ERROR_MALLOC_FAIL;
    }
    memcpy(req->method, method, strlen(method) + 1);

    req->version = (char *)malloc(strlen(version) + 1);    
    if (req->version == NULL) 
    {
        fprintf(stderr, "%s malloc version failed.\n", __FUNCTION__);
        return ERROR_MALLOC_FAIL;
    }
    memcpy(req->version, version, strlen(version) + 1);
    
    
    req->uri = (char *)malloc(strlen(uri) + 1);    
    if (req->uri == NULL) 
    {
        fprintf(stderr, "%s malloc uri failed.\n", __FUNCTION__);
        return ERROR_MALLOC_FAIL;
    }
    memcpy(req->uri, uri, strlen(uri) + 1);

    dbg_printf("method:%s uri:%s version:%s\n", req->method,
                req->uri, req->version);
    LOG("end...");
    return SUCCESS;
}

int read_request_header_fields(rio_t *rio, request *req)
{
    LOG("start...");
    char buf[MAXLINE];
    char *key, *value;
    int ret;
    size_t readsize = 0;

    readsize = Rio_readlineb(rio, buf, MAXLINE);
    while (strcmp(buf, "\r\n") && readsize > 0) 
    {
        dbg_printf("buf %s", buf);

        // delete "\r\n" in the end of line
        strip_right(buf);

        // split by ':'
        if ((value = strchr(buf, ':')) != NULL) 
        {
            key = buf;
            *value = 0;
            value += 2;
            // dbg_printf("key:%s; value:%s\n", key, value);
            ret = addfield(&(req->field_list), key, value);
            if (ret) 
            {
                return ret;
            }
        }
        readsize = Rio_readlineb(rio, buf, MAXLINE);
    }
    LOG("end...");
    return SUCCESS;
}

// delete "\r\n" in the end of line
char *strip_right(char *buf)
{
    char *ptr;
    if (buf == NULL)
        return buf;

    ptr = buf + strlen(buf);
    if (*(ptr - 1) == '\n')
    {
        ptr--;
        *ptr = 0;
    }

    if (*(ptr - 1) == '\r')
    {
        ptr--;
        *ptr = 0;
    }
    return buf;
}

int addfield(struct field *field_list, const char *key, const char *value)
{
    int keylen = strlen(key);
    int valuelen = strlen(value);

    struct field *newfd = (struct field *)calloc(1, sizeof(struct field));
    if (newfd == NULL)
    {
        fprintf(stderr, "%s, calloc struct field failed\n", __FUNCTION__);
        return ERROR_MALLOC_FAIL;
    }

    newfd->name = (char*)malloc(keylen + 1);
    if (newfd->name == NULL)
    {
        fprintf(stderr, "%s, malloc name failed\n", __FUNCTION__);
        return ERROR_MALLOC_FAIL;
    }
    memcpy(newfd->name, key, keylen + 1);
    
    newfd->value = (char*)malloc(valuelen + 1);
    if (newfd->value == NULL)
    {
        fprintf(stderr, "%s, malloc value failed\n", __FUNCTION__);
        return ERROR_MALLOC_FAIL;
    }
    memcpy(newfd->value, value, valuelen + 1);

    // set link
    newfd->next = field_list->next;
    newfd->prev = field_list;
    if (field_list->next) 
        field_list->next->prev = newfd;
    field_list->next = newfd;

    return 0;
};

struct field* findfields_by_key(struct field *field_list, char *key)
{
    struct field *ptr = field_list->next;
    while(ptr) 
    {
        if (!strcasecmp(ptr->name, key)) 
        {
            return ptr;
        }
        ptr = ptr->next;
    }

    return NULL;
}

int remove_fields_by_key(struct field *field_list, char *key)
{
    struct field *rfd = findfields_by_key(field_list, key);
    if (rfd == NULL)
        return SUCCESS;

    rfd->prev->next = rfd->next;
    if (rfd->next)
        rfd->next->prev = rfd->prev;

    if (rfd->name)
        free(rfd->name);
    if (rfd->value)
        free(rfd->value);
    free(rfd); 

    return SUCCESS;
}

int remove_fields_all(struct field *head)
{
    struct field *ptr = head->next;
    struct field *temp;
    
    while (ptr) 
    {
        temp = ptr;
        ptr = ptr->next;

        if (temp->name)
            free(temp->name);
        if (temp->value)
            free(temp->value);
        free(temp);
    }

    head->next = 0;
    head->prev = 0; 
    return SUCCESS;
}

void release_request(request *req)
{
    LOG("start");
    if (req == NULL)
        return;

    if (req->method)
    {
        free(req->method);
        req->method = NULL;
    }

    if (req->uri)
    {
        free(req->uri);
        req->uri = NULL;
    }

    if (req->version)
    {
        free(req->version);
        req->version = NULL;
    }

    if (req->content)
    {
        free(req->content);
        req->content = NULL;
    }
    req->content_length = 0;

    remove_fields_all(&(req->field_list));
    LOG("end");
}

void release_response(response *resp)
{
    LOG("start...");
    if (resp == NULL)
        return;
    
    if (resp->version)
    {
        free(resp->version);
        resp->version = NULL;
    }

    if (resp->status_msg)
    {
        free(resp->status_msg);
        resp->status_msg = NULL;
    }

    resp->status_code = 0;

    if (resp->content)
    {
        free(resp->content);
        resp->content = NULL;
    }
    resp->content_length = 0;
    
    remove_fields_all(&(resp->field_list));
    LOG("end...");
}

// print request content 
void printf_request(request *req)
{
    LOG("start");
    struct field *ptr = req->field_list.next;
    // struct field *last = &req->field_list;

    dbg_printf("%s %s %s\n", req->method, req->uri, req->version);

    while(ptr) 
    {
        // last = ptr;
        dbg_printf("%s: %s\n", ptr->name, ptr->value);
        ptr = ptr->next;
    }

    // check prev link
    // while(last != (&req->field_list)) 
    // {
    //     printf("%d, %s: %s\n", --count, last->name, last->value);
    //     last = last->prev;
    // }
    LOG("end...");
}

int parse_host_port(char *buf, char *host, char *port)
{
    LOG("start...");
    char temp[MAXLINE];
    char *sp;

    memcpy(temp, buf, strlen(buf) + 1);
    dbg_printf("temp:%s\n", buf);
    sp = strchr(temp, ':');
    
    // set port
    if (sp == NULL)
    {
        strncpy(port, DEFAULT_PORT, strlen(DEFAULT_PORT) + 1);
    }
    else
    {
        *sp = '\0';
        sp++;
        strncpy(port, sp, strlen(sp) + 1);
    }


    // set host
    strncpy(host, temp, strlen(temp));
    LOG("end..");
    return SUCCESS;
}

int get_host_port(request *req, char *host, char *port)
{
    // get Host:
    struct field *hostfd = findfields_by_key(&req->field_list, HEAD_HOST);
    if (hostfd == NULL || hostfd->value == NULL) 
    {
        fprintf(stderr, "can't find Host.\n");
        return ERROR_NO_HOST_FIELD;
    }
    
    return parse_host_port(hostfd->value, host, port);    
}

int fix_request(request *req)
{
    LOG("start...");
    struct field *list = &req->field_list;
    int ret;

    if (req->version)
    {
        free(req->version);
        req->version = NULL;
    }

    req->version = (char *)malloc(strlen(HTTP_VERSION) + 1);  
    if (req->version == NULL)
    {
        fprintf(stderr, "%s malloc version failed.\n", __FUNCTION__);
        return ERROR_MALLOC_FAIL;
    }

    memcpy(req->version, HTTP_VERSION, strlen(HTTP_VERSION) + 1);

    remove_fields_by_key(list, HEAD_USER_AGENT);
    if ((ret = addfield(list, HEAD_USER_AGENT, user_agent_hdr)) != SUCCESS)
    {
        return ret;
    }

    remove_fields_by_key(list, HEAD_CONNECTION);
    if ((ret = addfield(list, HEAD_CONNECTION, "close")) != SUCCESS)
    {
        return ret;
    }

    remove_fields_by_key(list, HEAD_PROXY_CONNECTION);
    if ((ret = addfield(list, HEAD_PROXY_CONNECTION, "close")) != SUCCESS)
    {
        return ret;
    }

    // TODO
    // http://localhost:8080/tiny/home.html -> tiny/home.html
    // change uri
    char *uri = req->uri;
    uri = strstr(uri, "http://");

    if (uri != NULL && uri == req->uri)
    {
        uri += strlen("http://");
    } 
    else 
    {
        uri = req->uri;
    }
    
    char *sp = strchr(uri, '/');
    if (sp == NULL)
    {
        sp = "/";
    }
    // dbg_printf("uri:%s\n", uri);

    uri = (char *)malloc(strlen(sp) + 1);
    if (uri == NULL)
    {
        fprintf(stderr, "%s malloc uri failed.\n", __FUNCTION__);
        return ERROR_MALLOC_FAIL;
    }
    memcpy(uri, sp, strlen(sp) + 1);
    free(req->uri);
    req->uri = uri;
    // dbg_printf("uri:%s\n", uri);
    LOG("end...");
    return SUCCESS;
}

int fix_response(response *resp)
{
    return SUCCESS;
}

size_t count_request_header_length(request *req)
{
    size_t len = 0;
    len = len + strlen(req->method) + 1;
    len = len + strlen(req->uri) + 1;
    len = len + strlen(req->version) + 2;
    return len;
}

size_t count_response_header_length(response *resp)
{
    char status_code_buf[256];
    sprintf(status_code_buf, "%d", resp->status_code);

    size_t len = 0;
    len = len + strlen(resp->version) + 1;
    len = len + strlen(status_code_buf) + 1;
    len = len + strlen(resp->status_msg) + 2;

    return len;
}

size_t count_fields_length(struct field *field_list)
{
    struct field *ptr = field_list->next;
    size_t len = 2; // "\r\n"
    while (ptr)
    {
        // +4 -> ": " + "\r\n"     
        len = len + strlen(ptr->value) + strlen(ptr->name) + 4; 
        ptr = ptr->next;
    }

    return len;
}

int generate_fields_str(struct field *field_list, char *buf, size_t length)
{
    struct field *ptr = field_list->next;
    
    while(ptr) 
    {
        buf = strcat(buf, ptr->name);
        buf = strcat(buf, ": ");
        buf = strcat(buf, ptr->value);
        buf = strcat(buf, "\r\n");

        ptr = ptr->next;
    }
    
    buf = strcat(buf, "\r\n");

    return SUCCESS;
}

int generate_request_str(request *req, char *buf, size_t length)
{
    sprintf(buf, "%s %s %s\r\n", req->method, req->uri, req->version);
    
    return SUCCESS;
}

int generate_response_str(response *resp, char *buf, size_t length)
{
    sprintf(buf, "%s %d %s\r\n", resp->version, resp->status_code, resp->status_msg);

    return SUCCESS;
}

int write_to_client(int connfd, response *resp)
{
    LOG("start...");
    // calcute the length;
    char *buf = NULL;
    int ret = SUCCESS;
    size_t length, length_header, length_fields;

    length_header = count_response_header_length(resp);
    length_fields = count_fields_length(&resp->field_list);
    length = length_header + length_fields;

    buf = (char *)malloc(length + 1);
    if (buf == NULL)
    {
        fprintf(stderr, "%s malloc buf failed.\n", __FUNCTION__);
        ret = ERROR_MALLOC_FAIL;
        goto _quit;
    }

    ret = generate_response_str(resp, buf, length_header);
    if (ret != SUCCESS)
    {
        goto _quit;
    }
    
    ret = generate_fields_str(&resp->field_list, buf + length_header, length_fields);
    if (ret != SUCCESS)
    {
        goto _quit;
    }

    Rio_writen(connfd, buf, strlen(buf));
    Rio_writen(connfd, resp->content, resp->content_length);
    
_quit:
    if (buf)
    {
        free(buf);
    }

    LOG("end");
    return SUCCESS;
}

int write_to_server(int connfd, request *req)
{
    LOG("start...");
    char *buf = NULL;
    int ret = SUCCESS;
    size_t length, length_header, length_fields;

    length_header = count_request_header_length(req);
    length_fields = count_fields_length(&req->field_list);
    length = length_header + length_fields;

    buf = (char *)malloc(length + 1);
    if (buf == NULL)
    {
        fprintf(stderr, "%s malloc buf failed.\n", __FUNCTION__);
        ret = ERROR_MALLOC_FAIL;
        goto _quit;
    }

    ret = generate_request_str(req, buf, length_header);
    if (ret != SUCCESS)
    {
        goto _quit;
    }
    
    ret = generate_fields_str(&req->field_list, buf + length_header, length_fields);
    if (ret != SUCCESS)
    {
        goto _quit;
    }

    // TODO
    Rio_writen(connfd, buf, length);

_quit:
    if (buf) 
    {
        free(buf);
    }

    LOG("end...");
    return ret;
}