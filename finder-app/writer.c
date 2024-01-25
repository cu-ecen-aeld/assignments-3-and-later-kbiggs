#include <stdio.h>
#include <syslog.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char* argv[])
{
	syslog(LOG_USER|LOG_DEBUG, "testing logging\n");
	
	/* Check to make sure two arguments were specified */
	if (argc != 3)
	{
		printf("Expected 2 arguments, not %d.\nArg 1 should be a filepath. Arg 2 should be the string to write.\n",
			argc-1);
		syslog(LOG_USER|LOG_ERR, "Expected 2 arguments, not %d.\nArg 1 should be a filepath. Arg 2 should be the string to write\n",
			argc-1);
		return 1;
	}

	/* Store off command line params */
	char * writefile = argv[1];
	char * writestr = argv[2];

	/* Create/open file to write to */
	int fd = creat(writefile, S_IRWXU | S_IROTH);

	/* Print error if file couldn't be created */
	if (fd == -1)
	{
		printf("%s could not be created successfully\n", writefile);
		syslog(LOG_USER|LOG_ERR, "%s could not be created successfully\n", writefile);
		return 1;
	}

	/* Write string to file */
	int write_bytes = write(fd, writestr, strlen(writestr));
	if (write_bytes == -1)
	{
		printf("Error writing %s to %s\n", writestr, writefile);
		syslog(LOG_USER|LOG_ERR, "Error writing %s to %s\n", writestr, writefile);
		return 1;
	}
	else
	{
		printf("Writing %s to %s\n", writestr, writefile);
		syslog(LOG_USER|LOG_DEBUG, "Writing %s to %s\n", writestr, writefile);
	}
}
