/* CU AESD Assignment 5
   Katie Biggs
   February 10, 2024    */

#include <stdio.h>
#include <syslog.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>

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

int main(int argc, char* argv[])
{
    int retval = 0;

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
    
    while (!sigint_found && !sigterm_found)
    {
        // open stream socket with port 9000 (fail and return -1 if any socket connection steps fail)

        // listen for connection

        // accept connection
        // log message to syslog when client connects "Accepted connection from XXXX" <-x is IP address

        // Receive data over connection
        // Create /var/tmp/aesdsocketdata & append data received
        // Each packet is complete when newline character is received
        // Packet should be shorter than available heap, but handle malloc failures to discard over-length packets

        // Once received data packet completes, return full content of /var/tmp/aesdsocketdata to client

        // Log message to syslog when connection closes "Closed connection of XXX"

        // Restart accepting connections in loop until SIGINT or SIGTERM received
    }
    
    // When SIGINT or SIGTERM received, complete open connection operations, close open sockets, delete /var/tmp/aesdsocketdata
    // Also log message to syslog "Caught signal, exiting"

    return retval;
}