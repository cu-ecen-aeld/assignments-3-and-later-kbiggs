/* CU AESD Assignment 6
   Katie Biggs
   February 25, 2024   */

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
#include <sys/queue.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

const char * LOG_FILE = "/var/tmp/aesdsocketdata";
const char * PORT = "9000";

int sock_fd = -1;
bool timer_fired = false;
bool signal_caught = false;

struct thread_data {
    bool        thread_complete;
    int         client_fd;
    pthread_t   thread_id;
    SLIST_ENTRY(thread_data) next_thread;
};

pthread_mutex_t log_mutex;

static void signal_handler(int sig_num)
{
    syslog(LOG_USER, "Caught signal, exiting");
    signal_caught = true;
    shutdown(sock_fd, SHUT_RDWR);
}

static void timer_handler(int sig, siginfo_t *si, void *uc)
{
    timer_fired = true;
}

int register_signals(void)
{
    int retval = 0;
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

    return retval;
}

// Below function to get address info was utilized from the BGNet guide
// https://github.com/thlorenz/beejs-guide-to-network-samples/blob/master/lib/get_in_addr.c
void *get_in_addr(struct sockaddr *sa)
{
  return sa->sa_family == AF_INET
    ? (void *) &(((struct sockaddr_in*)sa)->sin_addr)
    : (void *) &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Thread function (move receive/send here, set complete flag)
// mutex lock/unlock around writing to /var/tmp/aesdsocketdata
// exit when connection closed or error during send/receive
int handle_client(struct thread_data* thread_func_args)
{
    int retval = 0;
    int client_fd = thread_func_args->client_fd;
    FILE *fp;

    // Create /var/tmp/aesdsocketdata & append data received
    char buf[512] = {0};
    int  bytes_recv, total_bytes_recv = 0;
    bool more_bytes_to_read = true;

    char *final_buffer = malloc(1);
    if (!final_buffer)
    {
        syslog(LOG_ERR, "Malloc failure");
        retval = -1;
    }
    *final_buffer = '\0';

    while (more_bytes_to_read)
    {
        bytes_recv = recv(client_fd, buf, 512, 0);
        syslog(LOG_INFO, "Received %d bytes", bytes_recv);
        if (bytes_recv == -1)
        {
            syslog(LOG_ERR, "Error receiving data");
            more_bytes_to_read = false;
            retval = -1;
            continue;
        }
        else if (bytes_recv == 0)
        {
            more_bytes_to_read = false;
        }
        else
        {
            int new_buf_len = strlen(final_buffer) + strlen(buf) + 1;
            char *tmp_buf = realloc(final_buffer, new_buf_len);
            if (!tmp_buf)
            {
                syslog(LOG_ERR, "Realloc failure");
                retval = -1;
                continue;
            }
            final_buffer = tmp_buf;
            total_bytes_recv += bytes_recv;
            strcpy(final_buffer, buf);

            char * new_line_found = memchr(final_buffer, '\n', 512);
            if (new_line_found != NULL)
            {
                more_bytes_to_read = false;
            }
        }
    }

    pthread_mutex_lock(&log_mutex);
    fp = fopen(LOG_FILE, "a+");
    fwrite(final_buffer, total_bytes_recv, 1, fp);
    syslog(LOG_INFO, "Completed file write");
    fclose(fp);
    pthread_mutex_unlock(&log_mutex);

    // Once received data packet completes, return full content of /var/tmp/aesdsocketdata to client
    char read_buf[512] = {0};
    int  bytes_read;
    fp = fopen(LOG_FILE, "r+");
    while (!feof(fp))
    {
        bytes_read = fread(read_buf, 1, 512, fp);
        char *msg_to_send = read_buf;
        int bytes_sent = send(client_fd, msg_to_send, bytes_read, 0);
        if (bytes_sent == -1)
        {
            syslog(LOG_ERR, "Error sending bytes");
            retval = -1;
        }
        syslog(LOG_INFO, "Read %d bytes, sent %d bytes", bytes_read, bytes_sent);
    }
    fclose(fp);

    // Log message to syslog when connection closes
    if (close(client_fd) != 0)
    {
        syslog(LOG_ERR, "Error closing client socket");
        retval = -1;
    }
    else
    {
        syslog(LOG_USER, "Closed connection from client");
    }

    return retval;
}

void* thread_func(void* thread_params)
{
    struct thread_data* thread_func_args = (struct thread_data *) thread_params;
    handle_client(thread_func_args);
    thread_func_args->thread_complete = true;
    return thread_params;
}

int main(int argc, char* argv[])
{
    int    retval = 0, client_fd;
    struct addrinfo hints, *serv_info, *p;
    struct sockaddr_storage client_addr;
    pthread_t thread;

    timer_t timer_id;
    sigset_t mask;
    struct sigaction sa;
    struct sigevent  sev;
    struct itimerspec its;

    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = timer_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGRTMIN, &sa, NULL);

    sigemptyset(&mask);
    sigaddset(&mask, SIGRTMIN);
    sigprocmask(SIG_SETMASK, &mask, NULL);

    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGRTMIN;
    sev.sigev_value.sival_ptr = &timer_id;
    timer_create(CLOCK_REALTIME, &sev, &timer_id);

    its.it_value.tv_sec = 10;
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;

    timer_settime(timer_id, 0, &its, NULL);

    sigprocmask(SIG_UNBLOCK, &mask, NULL);
    
    // Check for daemon
    bool run_daemon = false;
    if (argc > 1 && (strcmp(argv[1], "-d") == 0))
    {
        run_daemon = true;
    }

    // Register for signals
    retval = register_signals();

    // Open syslog
    openlog("AESD Socket", 0, LOG_USER);

    // Initialize mutex
    if (pthread_mutex_init(&log_mutex, NULL) != 0)
    {
        syslog(LOG_ERR, "Error initializing mutex");
        retval = -1;
    }

    // Initialize SLIST
    SLIST_HEAD(slist_head, thread_data) head;
    SLIST_INIT(&head);

    // open stream socket with port 9000
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &serv_info) != 0)
    {
        syslog(LOG_ERR, "Error getting address info");
        retval = -1;
    }

    for (p = serv_info; p != NULL; p = p->ai_next)
    {
        sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock_fd == -1)
        {
            syslog(LOG_ERR, "Error getting socket file descriptor");
            retval = -1;
            break;
        }

        int yes = 1;
        if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0)
        {
            syslog(LOG_ERR, "Error setting socket options");
            retval = -1;
            break;
        }

        if (bind(sock_fd, p->ai_addr, p->ai_addrlen) != 0)
        {
            syslog(LOG_ERR, "Error binding");
            retval = -1;
            break;
        }
    }

    freeaddrinfo(serv_info);

    // Check for daemon, initializing if option was specified
    if (run_daemon)
    {
        pid_t pid = fork();

        if (pid < 0)
        {
            close(sock_fd);
            exit(EXIT_FAILURE);
        }
        else if (pid > 0)
        {
            exit(EXIT_SUCCESS);
        }

        // Create new session
        if (setsid() == -1)
        {
            close(sock_fd);
            exit(EXIT_FAILURE);
        }

        // Change working directory
        if (chdir("/"))
        {
            close(sock_fd);
            exit(EXIT_FAILURE);
        }

        // Close file descriptors
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        // Redirect stdin/out/err to /dev/null
        open("/dev/null", O_RDWR);
        if (dup(0) == -1)
        {
            close(sock_fd);
            exit(EXIT_FAILURE);
        }
        if (dup(0) == -1)
        {
            close(sock_fd);
            exit(EXIT_FAILURE);
        }
    }

    // listen for connection
    if (listen(sock_fd, 5) != 0)
    {
        syslog(LOG_ERR, "Error listening");
        retval = -1;
    }
    
    // Accept connections until SIGINT or SIGTERM received
    while (!signal_caught && (retval != -1))
    {
        if (timer_fired)
        {
            char buf[200] = {0};
            time_t t;
            struct tm *tmp;

            t = time(NULL);
            tmp = localtime(&t);
            if (tmp == NULL)
            {
                syslog(LOG_ERR, "Error getting localtime");
                exit(EXIT_FAILURE);
            }

            // Write timestamp:time with newline
            // year, month, day, hour (24 hr), minute, second
            if (strftime(buf, sizeof(buf), "timestamp: %Y, %m, %d, %H, %M, %S\n", tmp) == 0)
            {
                syslog(LOG_ERR, "Strftime returned 0");
                exit(EXIT_FAILURE);
            }
            printf("%s", buf);
            
            if (pthread_mutex_lock(&log_mutex) != 0)
            {
                syslog(LOG_ERR, "Error locking mutex during timer call");
            }
            else
            {
                FILE *fp = fopen(LOG_FILE, "a+");     
                fputs(buf, fp);
                fclose(fp);
                if (pthread_mutex_unlock(&log_mutex) != 0)
                {
                    syslog(LOG_ERR, "Error unlocking thread");
                }
            }
            timer_fired = false;
        }
        
        // accept connection
        socklen_t client_addr_size = sizeof(client_addr);
        client_fd = accept(sock_fd, (struct sockaddr *)&client_addr, &client_addr_size);
        if (client_fd == -1)
        {
            syslog(LOG_ERR, "Issue accepting connection");
            continue;
        }

        // log message to syslog when client connects
        char ip_addr[INET6_ADDRSTRLEN];
        inet_ntop(client_addr.ss_family,
                  get_in_addr((struct sockaddr *)&client_addr),
                  ip_addr, sizeof(ip_addr));

        syslog(LOG_USER, "Accepted connection from %s", ip_addr);

        // Now that we've accepted connection, declare/init/insert element at head
        // instantiate thread
        struct thread_data *thread_struct = (struct thread_data *) malloc(sizeof(struct thread_data));
        if (thread_struct == NULL)
        {
            syslog(LOG_ERR, "Malloc failure");
            retval = -1;
        }
        thread_struct->client_fd = client_fd;
        thread_struct->thread_complete = false;        
        int id = pthread_create(&thread, NULL, thread_func, thread_struct);        
        thread_struct->thread_id = thread;
        SLIST_INSERT_HEAD(&head, thread_struct, next_thread);

        if (id != 0)
        {
            syslog(LOG_ERR, "Error creating new thread");
            retval = -1;
        }

        // iterate over linked list, remove from list if flag is set
        // also call pthread join 
        SLIST_FOREACH(thread_struct, &head, next_thread)
        {
            if (thread_struct->thread_complete)
            {
                syslog(LOG_INFO, "Thread complete, joining");
                pthread_join(thread_struct->thread_id, NULL);
                SLIST_REMOVE_HEAD(&head, next_thread);
                //free(thread_struct);
            }
        }      
    }
    
    if (pthread_mutex_destroy(&log_mutex) != 0)
    {
        syslog(LOG_ERR, "Error destroying mutex");
        retval = -1;
    }
    
    //TODO - request exit from each thread and wait for complete
    syslog(LOG_INFO, "Closing aesdsocket application");
    pthread_mutex_destroy(&log_mutex);
    closelog();
    close(sock_fd);
    remove(LOG_FILE);

    return retval;
}
