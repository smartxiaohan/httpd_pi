#include <string>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/select.h>
#include <signal.h>

using namespace std;

int tcp_listen(const char *host, const char *serv, socklen_t *len)
{
    struct addrinfo *res, *saved, hints;
    int n, listenfd;
    const int on = 1;

    bzero(&hints, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    n = getaddrinfo(host, serv, &hints, &res);
    if(n != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(n));
        return -1;
    }
    saved = res;
    while(res)
    {
        listenfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if(listenfd >= 0)
        {
            if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1)
                perror("setsockopt");
            if(bind(listenfd, res->ai_addr, res->ai_addrlen) == 0)
            {
                if(listen(listenfd, 128) == 0)
                    break;
            }
            close(listenfd);
        }
        res = res->ai_next;
    }
    if(res == NULL)
    {
        perror("tcp_listen");
        freeaddrinfo(saved);
        return -1;
    }
    else
    {
        if(len)
            *len = res->ai_addrlen;
        freeaddrinfo(saved);
        return listenfd;
    }
}

void send_http501(int clientfd)
{
    const char s[] = "HTTP/1.1 501 Not Implemented\r\n\r\n";
    write(clientfd, s, strlen(s));
    shutdown(clientfd, SHUT_WR);

    fprintf(stderr, "[HTTP 501/]\n");
}

void send_http404(int clientfd)
{
    const char s[] = "HTTP/1.1 404 Not Found\r\n\r\n";
    write(clientfd, s, strlen(s));
    shutdown(clientfd, SHUT_WR);

    fprintf(stderr, "[HTTP 404/]\n");
}

int send_file(int clientfd, const char *path)
{
    char buffer[2048];
    size_t ret;
    const char s[] = "HTTP/1.1 200 OK\r\n\r\n";
    FILE *fp;

    fp = fopen(path, "rb");
    if(fp == NULL)
        return 0;

    write(clientfd, s, strlen(s));
    while( (ret = fread(buffer, 1, sizeof(buffer), fp)) > 0)
        write(clientfd, buffer, ret);
    shutdown(clientfd, SHUT_WR);
    fclose(fp);

    fprintf(stderr, "[HTTP 200] sent file %s\n", path);
    return 1;
}

bool is_path_secure(const string &path)
{
    if(path.size() > 0 && path.at(0) == '/')
        return false;
    if(path.find('\0') != string::npos)
        return false;
    if(path.find("..") != string::npos)
        return false;
    return true;
}

void process_http_request(int clientfd)
{
    char buffer[2048];
    ssize_t ret;
    string http_header;
    bool ok;

    ok = false;
    while( (ret = read( clientfd, buffer, sizeof(buffer) )) > 0)
    {
        http_header.append(buffer, ret);
        if(http_header.find("\r\n\r\n") != string::npos)
        {
            ok = true;
            break;
        }
    }
    if(ok)
    {
        size_t pos;
        string line;
        string path;

        fprintf(stderr, "[HTTP Request Begin]\n%s\n[HTTP Request End]\n", http_header.c_str());
        pos = http_header.find("\r\n");
        if(pos == string::npos)
            return;
        line = http_header.substr(0, pos);
        if(line.find("GET /") == string::npos)
        {
            send_http501(clientfd);
            return;
        }
        path = line.substr(5);
        pos = path.find("\x20");
        path = path.substr(0, pos);

        if( !is_path_secure(path) || !send_file(clientfd, path.c_str()) )
            send_http404(clientfd);
    }
}

void http_loop(int listenfd)
{
    while(1)
    {
        pid_t pid;
        int clientfd;

        clientfd = accept(listenfd, NULL, NULL);
        if(clientfd == -1)
        {
            perror("accept");
            continue;
        }

        pid = fork();
        if(pid == -1)
            perror("fork");
        else if(pid == 0)
        {
            //child process
            close(listenfd);
            process_http_request(clientfd);
            exit(0);
        }
        else
        {
            //parent process
            close(clientfd);
        }
    }
}

int main(int argc, char *argv[])
{
    int listenfd;
    const char *host = "127.0.0.1";
    const char *port = "8080";

    switch(argc)
    {
        case 4:
            port = argv[3];
        case 3:
            host = argv[2];
        case 2:
            if(chdir(argv[1]) == -1)
            {
                perror("chdir");
                exit(EXIT_FAILURE);
            }
            break;
        default:
            fprintf(stderr, "usage: httpd_pi webdir [ host [ port ] ]\n");
            exit(EXIT_FAILURE);
    }

    signal(SIGCHLD, SIG_IGN);

    listenfd = tcp_listen(host, port, NULL);
    if(listenfd == -1)
        exit(EXIT_FAILURE);

    http_loop(listenfd);

    return 0;
}
