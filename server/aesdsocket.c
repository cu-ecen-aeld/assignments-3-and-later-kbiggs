/* CU AESD Assignment 5
   Katie Biggs
   February 10, 2024    */

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/stat.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <netdb.h>

const char * LOG_FILE = "/var/tmp/aesdsocketdata";
const char * PORT = "9000";

bool sigint_found = false;
bool sigterm_found = false;

static void signal_handler(int sig_num)
{
    if (sig_num == SIGINT)
    {
        sigint_found = true;
    }
    else if (sig_num == SIGTERM)
    {
        sigterm_found = true;
    }
}

// https://github.com/thlorenz/beejs-guide-to-network-samples/blob/master/lib/get_in_addr.c
void *get_in_addr(struct sockaddr *sa) {
  return sa->sa_family == AF_INET
    ? (void *) &(((struct sockaddr_in*)sa)->sin_addr)
    : (void *) &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char* argv[])
{
    int    retval = 0, sock_fd = -1, client_fd;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage client_addr;

    // Register for signals
    struct sigaction new_action;
    memset(&new_action, 0, sizeof(struct sigaction));
    new_action.sa_handler = signal_handler;
    if (sigaction(SIGTERM, &new_action, NULL) != 0)
    {
        retval = -1;
    }
    if (sigaction(SIGINT, &new_action, NULL))
    {
        retval = -1;
    }

    // Open log
    openlog("AESD Socket", 0, LOG_USER);

    // Open file for writing
    //int fd = open(LOG_FILE, O_CREAT|O_APPEND|O_WRONLY|O_DSYNC, S_IRWXU | S_IROTH);
    FILE *fp;

    // open stream socket with port 9000 (fail and return -1 if any socket connection steps fail)
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &servinfo) != 0)
    {
        printf("Error getting address info\n");
        retval = -1;
    }

    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock_fd == -1)
        {
            printf("Error getting socket file descriptor\n");
            retval = -1;
            break;
        }

        int yes = 1;
        if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0)
        {
            retval = -1;
            break;
        }

        if (bind(sock_fd, p->ai_addr, p->ai_addrlen) != 0)
        {
            printf("Error binding\n");
            retval = -1;
            break;
        }
    }

    freeaddrinfo(servinfo);

    // listen for connection
    if (listen(sock_fd, 5) != 0)
    {
        retval = -1;
    }
    
    // Accept connections until SIGINT or SIGTERM received
    while (!sigint_found && !sigterm_found && (retval != -1))
    {
        printf("in while\n");        

        // accept connection
        // log message to syslog when client connects "Accepted connection from XXXX" <-x is IP address
        socklen_t client_addr_size = sizeof(client_addr);
        client_fd = accept(sock_fd, (struct sockaddr *)&client_addr, &client_addr_size);
        if (client_fd == -1)
        {
            printf("Issue accepting connection\n");
            continue;
        }

        char s[INET6_ADDRSTRLEN];
        inet_ntop(client_addr.ss_family,
                  get_in_addr((struct sockaddr *)&client_addr),
                  s, sizeof(s));

        printf("Accepted connection from %s\n", s);
        syslog(LOG_USER, "Accepted connection from %s", s);

        // Receive data over connection
        // Create /var/tmp/aesdsocketdata & append data received
        // Each packet is complete when newline character is received
        // Packet should be shorter than available heap, but handle malloc failures to discard over-length packets
        char buf[256] = {0};
        int bytes_recv;
        fp = fopen(LOG_FILE, "w+");
        while ((bytes_recv = recv(client_fd, buf, 256, 0)) > 0)
        {
            if (bytes_recv == -1)
            {
                printf("Error receiving\n");
                retval = -1;
                continue;
            }
            
            printf("bytes recv %d\n", bytes_recv);
            printf("bytes %s\n", buf);

            /*char * new_line_found = memchr(buf, '\n', 256);

            if (new_line_found != NULL)
            {
                bytes_recv -= 1;
            }*/

            fwrite(buf, bytes_recv, 1, fp);
        }

        fclose(fp);

        // Once received data packet completes, return full content of /var/tmp/aesdsocketdata to client
        fp = fopen(LOG_FILE, "r+");
        char read_buf[256] = {0};
        int bytes_read;
        while (!feof(fp))
        {
            bytes_read = fread(read_buf, 1, 256, fp);
            printf("bytes read %d\n", bytes_read);
            printf("buffer %s\n", read_buf);
            char *msg_to_send = read_buf;
            int bytes_sent = send(client_fd, msg_to_send, bytes_read, 0);
            printf("bytes sent %d\n", bytes_sent);
        }
        fclose(fp);      

        // Log message to syslog when connection closes "Closed connection of XXX"
        close(client_fd);
        syslog(LOG_USER, "Closed connection from %s", s);
        printf("closed\n");
    }
    
    // When SIGINT or SIGTERM received, complete open connection operations, close open sockets, delete /var/tmp/aesdsocketdata
    // Also log message to syslog "Caught signal, exiting"
    
    printf("got signal\n");
    syslog(LOG_USER, "Caught signal, exiting");
    closelog();
    close(sock_fd);
    //fclose(fp);
    remove(LOG_FILE);

    return retval;
}
