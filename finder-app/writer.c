#include <stdio.h>
#include <syslog.h>

int main(int argc, char* argv[])
{
	syslog(LOG_USER|LOG_DEBUG, "testing logging");
	
	if (argc != 3)
	{
		printf("Expected 2 arguments, not %d.\nArg 1 should be a filepath. Arg 2 should be the string to write",
			argc-1);
		syslog(LOG_USER|LOG_ERR, "Expected 2 arguments, not %d.\nArg 1 should be a filepath. Arg 2 should be the string to write",
			argc-1);
		return 1;
	}
}
