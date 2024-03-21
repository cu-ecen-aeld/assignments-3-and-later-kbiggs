/* CU AESD Assignment 6
   Katie Biggs
   March 2, 2024   */

#include "queue.h"
#include <pthread.h>
#include <time.h>
#include <unistd.h>

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
#include <sys/ioctl.h>
#include "../aesd-char-driver/aesd_ioctl.h"

#define USE_AESD_CHAR_DEVICE 1

#ifdef USE_AESD_CHAR_DEVICE
    const char * LOG_FILE = "/dev/aesdchar";
    const char * AESD_CHAR_IOCTL_CMD = "AESDCHAR_IOCSEEKTO:";
#else
    const char * LOG_FILE = "/var/tmp/aesdsocketdata";
#endif

const char * PORT = "9000";
const int buf_size = 512;
const int timer_dur_s = 10;

int sock_fd = -1;
bool timer_fired = false;
bool signal_caught = false;
timer_t timer_id;

typedef struct thread_data_t thread_data_t;
struct thread_data_s {
    bool        thread_complete;
    int         client_fd;
    pthread_t   thread_id;
    SLIST_ENTRY(thread_data_s) entries;
};

pthread_mutex_t log_mutex;

/* Signal handler for program terminating signals */
static void signal_handler(int sig_num)
{
    signal_caught = true;
    shutdown(sock_fd, SHUT_RDWR);
}

/* Timer signal handler */
static void timer_handler(int sig, siginfo_t *si, void *uc)
{
    timer_fired = true;
}

/* Register for all necessary signals */
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

/* Initialize the timer.
   This functionality was based off of example provided at
   https://man7.org/linux/man-pages/man2/timer_create.2.html */
int init_timer(void)
{
    int retval = 0;
    sigset_t mask;
    struct sigaction sa;
    struct sigevent  sev;
    struct itimerspec its;

    // Setup timer handler
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = timer_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGRTMIN, &sa, NULL) != 0)
    {
        retval = -1;
    }

    // Temporarily disable all signals while initializing timer
    sigemptyset(&mask);
    sigaddset(&mask, SIGRTMIN);
    if (sigprocmask(SIG_SETMASK, &mask, NULL) != 0)
    {
        retval = -1;
    }

    // Create timer
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGRTMIN;
    sev.sigev_value.sival_ptr = &timer_id;
    if (timer_create(CLOCK_REALTIME, &sev, &timer_id) != 0)
    {
        retval = -1;
    }

    // Start time
    its.it_value.tv_sec = timer_dur_s;
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;

    if (timer_settime(timer_id, 0, &its, NULL) != 0)
    {
        retval = -1;
    }

    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) != 0)
    {
        retval = -1;
    }

    return retval;
}

/* Handle printing the timestamp.
   This functionality was based off of example provided at 
   https://man7.org/linux/man-pages/man3/strftime.3.html */
int print_timestamp(void)
{
    int retval = 0;
    char buf[200] = {0};

    time_t t = time(NULL);
    struct tm *tmp = localtime(&t);
    if (tmp == NULL)
    {
        syslog(LOG_ERR, "Error getting localtime");
        retval = -1;
    }

    // Write timestamp:time with newline
    // year, month, day, hour (24 hr), minute, second
    if (strftime(buf, sizeof(buf), "timestamp: %Y, %m, %d, %H, %M, %S\n", tmp) == 0)
    {
        syslog(LOG_ERR, "Strftime returned 0");
        retval = -1;
    }
    //printf("%s\n", buf);
    
    if (pthread_mutex_lock(&log_mutex) != 0)
    {
        syslog(LOG_ERR, "Error locking mutex during timer call");
        retval = -1;
    }
    else
    {
        FILE *fp = fopen(LOG_FILE, "a+");     
        fputs(buf, fp);
        fclose(fp);
        if (pthread_mutex_unlock(&log_mutex) != 0)
        {
            syslog(LOG_ERR, "Error unlocking thread");
            retval = -1;
        }
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
int handle_client(struct thread_data_s* thread_func_args)
{
    int retval = 0;
    int client_fd = thread_func_args->client_fd;
    FILE *fp;
    char buf[512] = {0};
    int  bytes_recv, total_bytes_recv = 0;
    bool more_bytes_to_read = true;
    char * new_line_found = NULL;
    int ioctl_cmd_found = -1;

    // Create a buffer to store the final packet
    char *final_buffer = malloc(1);
    if (!final_buffer)
    {
        syslog(LOG_ERR, "Malloc failure");
        retval = -1;
    }
    *final_buffer = '\0';

    //fp = fopen(LOG_FILE, "a+");

    // Receive all available bytes from the client
    while (more_bytes_to_read && !(retval == -1))
    {
        bytes_recv = recv(client_fd, buf, 512, 0);
        syslog(LOG_INFO, "Received %d bytes", bytes_recv);
        if (bytes_recv == -1)
        {
            syslog(LOG_ERR, "Error receiving data");
            more_bytes_to_read = false;
            retval = -1;
        }
        else if (bytes_recv == 0)
        {
            more_bytes_to_read = false;
        }
        else
        {
            // Calculate the new buffer size needed to store packet and realloc
            int new_buf_len = strlen(final_buffer) + strlen(buf) + 1;
            char *tmp_buf = realloc(final_buffer, new_buf_len);
            if (!tmp_buf)
            {
                syslog(LOG_ERR, "Realloc failure");
                retval = -1;
                continue;
            }

            // Move contents of most recent recv into final buffer
            final_buffer = tmp_buf;
            total_bytes_recv += bytes_recv;
            strcpy(final_buffer, buf);

            // Check to see if we've gotten new line and are finished receiving
            new_line_found = memchr(final_buffer, '\n', 512);
            if (new_line_found != NULL)
            {
                more_bytes_to_read = false;
            }

            #ifdef USE_AESD_CHAR_DEVICE
            // Check to see if we've gotten the ioctl command
            // Check to see if we've gotten the cmd itself (minus newline) and the other arguments
            const size_t ioctl_cmd_len = 23;

            ioctl_cmd_found = strncmp(final_buffer, AESD_CHAR_IOCTL_CMD, 19);
            syslog(LOG_INFO, "IOCTL cmd length %lu, ioctl_cmd_found %d", ioctl_cmd_len, ioctl_cmd_found);
            if ((total_bytes_recv >= ioctl_cmd_len) && (ioctl_cmd_found == 0))
            {
                syslog(LOG_INFO, "Received ioctl cmd");
                more_bytes_to_read = false;
            }
            #endif
        }
    }

    // Open file and write the new packet contents
    if (pthread_mutex_lock(&log_mutex) != 0)
    {
        syslog(LOG_ERR, "Error locking mutex for file write");
        retval = -1;
    }
    else
    {
        if (ioctl_cmd_found == 0)
        {
            #ifdef USE_AESD_CHAR_DEVICE
            struct aesd_seekto seekto;
            syslog(LOG_INFO, "Performing ioctl");
            // find the command portions from the input string
            char * separator = memchr(final_buffer, ',', 23);
            seekto.write_cmd = strtoul(final_buffer, &separator, 10);
            seekto.write_cmd_offset = strtoul(separator+1, NULL, 10);
            syslog(LOG_INFO, "Write cmd %u write cmd offset %u", seekto.write_cmd, seekto.write_cmd_offset);
            fp = fopen(LOG_FILE, "a+");
            ioctl(fileno(fp), AESDCHAR_IOCSEEKTO, &seekto);
            #endif
        }
        else
        {
            fp = fopen(LOG_FILE, "a+");
            fwrite(final_buffer, total_bytes_recv, 1, fp);
            syslog(LOG_INFO, "Completed file write");
            fclose(fp);
            pthread_mutex_unlock(&log_mutex);
        }        
    }
    free(final_buffer);

    // Once write completes, return full content of /var/tmp/aesdsocketdata to client
    char read_buf[512] = {0};
    int  bytes_read;
    if (ioctl_cmd_found)
    {
        fp = fopen(LOG_FILE, "r+");
    }
    
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
    struct thread_data_s* thread_func_args = (struct thread_data_s *) thread_params;
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
    
    // Check for daemon
    bool run_daemon = false;
    if (argc > 1 && (strcmp(argv[1], "-d") == 0))
    {
        run_daemon = true;
    }

    // Register for signals
    if (register_signals() != 0)
    {
        exit(EXIT_FAILURE);
    }

    // Open syslog
    openlog("AESD Socket", 0, LOG_USER);

    // Initialize mutex
    if (pthread_mutex_init(&log_mutex, NULL) != 0)
    {
        syslog(LOG_ERR, "Error initializing mutex");
        retval = -1;
    }

    // Initialize SLIST
    SLIST_HEAD(slist_head, thread_data_s) head;
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

    #ifndef USE_AESD_CHAR_DEVICE
        // Initialize timer - needs to be called after fork
        if (init_timer() != 0)
        {
            retval = -1;
        }
    #endif
    
    // listen for connection
    if (listen(sock_fd, 5) != 0)
    {
        syslog(LOG_ERR, "Error listening");
        retval = -1;
    }
    
    // Accept connections until SIGINT or SIGTERM received
    while (!signal_caught && (retval != -1))
    {
        // accept connection
        socklen_t client_addr_size = sizeof(client_addr);
        client_fd = accept(sock_fd, (struct sockaddr *)&client_addr, &client_addr_size);
        if (client_fd != -1)
        {
            // log message to syslog when client connects
            char ip_addr[INET6_ADDRSTRLEN];
            inet_ntop(client_addr.ss_family,
                    get_in_addr((struct sockaddr *)&client_addr),
                    ip_addr, sizeof(ip_addr));

            syslog(LOG_USER, "Accepted connection from %s", ip_addr);

            // Now that we've accepted connection, declare/init/insert element at head
            // instantiate thread
            struct thread_data_s *thread_struct = (struct thread_data_s *) malloc(sizeof(struct thread_data_s));
            if (!thread_struct)
            {
                syslog(LOG_ERR, "Malloc failure");
                retval = -1;
                continue;
            }
            thread_struct->client_fd = client_fd;
            thread_struct->thread_complete = false;        
            int id = pthread_create(&thread, NULL, thread_func, thread_struct);        
            if (id != 0)
            {
                syslog(LOG_ERR, "Error creating new thread");
                free(thread_struct);
                retval = -1;
                continue;
            }
            thread_struct->thread_id = thread;
            SLIST_INSERT_HEAD(&head, thread_struct, entries);
        }        

        #ifndef USE_AESD_CHAR_DEVICE
            if (timer_fired)
            {
                if (print_timestamp() != 0)
                {
                    retval = -1;
                    continue;
                }
                timer_fired = false;
            }
        #endif

        // iterate over linked list, remove from list if flag is set
        // also call pthread join 
        // Following code segments were based off of examples provided at:
        // https://github.com/stockrt/queue.h/blob/master/sample.c
        // https://man.freebsd.org/cgi/man.cgi?query=SLIST_HEAD&sektion=3&manpath=FreeBSD%2010.2-RELEASE
        struct thread_data_s *thread_ptr = NULL;
        struct thread_data_s *next_thread = NULL;
        SLIST_FOREACH_SAFE(thread_ptr, &head, entries, next_thread)
        {
            if (thread_ptr->thread_complete)
            {
                syslog(LOG_INFO, "Thread complete, joining");
                int id = pthread_join(thread_ptr->thread_id, NULL);
                if (id != 0)
                {
                    syslog(LOG_ERR, "Failure joining thread");
                    retval = -1;
                }
                SLIST_REMOVE(&head, thread_ptr, thread_data_s, entries);
                free(thread_ptr);
            }
        }
    }

    syslog(LOG_USER, "Caught signal, exiting");
    
    // Request exit from each thread and wait for complete
    while (!SLIST_EMPTY(&head))
    {
        struct thread_data_s *thread_rm = SLIST_FIRST(&head);
        pthread_join(thread_rm->thread_id, NULL);
        SLIST_REMOVE_HEAD(&head, entries);
        free(thread_rm);
    }

    #ifndef USE_AESD_CHAR_DEVICE
        timer_delete(timer_id);
        remove(LOG_FILE);
    #endif

    syslog(LOG_INFO, "Closing aesdsocket application");
    pthread_mutex_destroy(&log_mutex);
    closelog();
    close(sock_fd);

    return retval;
}