/* CU AESD Assignment 2
   Katie Biggs
   January 25, 2025 */

#include <stdio.h>
#include <syslog.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

/* Main function for writer application */
int main(int argc, char* argv[])
{
	/* Set up connection to system loger with AESD prefix for readability */
	/* Calling closelog() at end of program is optional */
	const char * log_prefix = "AESD";
	openlog(log_prefix, 0, LOG_USER);
	
	/* Check to make sure two arguments were specified */
	if (argc != 3)
	{
		syslog(LOG_USER|LOG_ERR, "Expected 2 arguments, not %d. Arg 1 should be a file to write to. Arg 2 should be the string to write.",
				argc-1);
		return 1;
	}

	/* Store off command line params */
	char * writefile = argv[1];
	char * writestr = argv[2];

	/* Create file to write to */
	/* Creat will use O_CREAT|O_WRONLY|O_TRUNC */
	/* User has rwx perms, others has r perms */
	int fd = creat(writefile, S_IRWXU | S_IROTH);

	/* Log error if file couldn't be created */
	if (fd == -1)
	{
		syslog(LOG_USER|LOG_ERR, "%s could not be created/opened successfully", writefile);
		return 1;
	}

	/* Write string to file */
	int write_bytes = write(fd, writestr, strlen(writestr));

	/* Log error if the write operation failed */
	if (write_bytes == -1)
	{
		syslog(LOG_USER|LOG_ERR, "Error writing %s to %s", writestr, writefile);
		return 1;
	}
	else
	{
		syslog(LOG_USER|LOG_DEBUG, "Writing %s to %s", writestr, writefile);
	}

	/* Close file */
	close(fd);
}