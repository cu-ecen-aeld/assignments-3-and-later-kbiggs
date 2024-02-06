#include "systemcalls.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{
    int sys_ret = system(cmd);
    bool ret_val =  (sys_ret == -1) ? false : true;
    return ret_val;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    /* Confirm that path provided is absolute */
    if (command[0][0] != '/')
    {
        return false;
    }

    /* Call fork, returning false if there is an error in invocation */
    pid_t pid = fork();    
    if (pid == -1)
    {
        return false;
    }    
    /* If fork was successful (pid of 0 indicates in the child), try calling execv */
    else if (pid == 0)
    {
        /* Child calls execv, returning false if there is an error.
           Exec should only return if there is an error. */
        int ret = execv(command[0], command);
        if (ret == -1)
        {
            return false;
        }
    }
    
    /* Wait for the particular pid child process we created, 
       which means the parent will wait until this child has executed.
       Return false if there is an error. */
    int status;
    pid_t w_pid = waitpid(pid, &status, 0);
    if (w_pid == -1)
    {
        return false;
    }

    /* If child terminated normally, but the exit status of
       the child is non-zero, return false */
    if (WIFEXITED(status) && WEXITSTATUS(status))
    {
        return false;
    }

    va_end(args);
    return true;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    /* Confirm that path provided is absolute */
    if (command[0][0] != '/')
    {
        return false;
    }

    /* Open file to redirect standard out */
    /* Content inspired by https://stackoverflow.com/questions/13784269/redirection-inside-call-to-execvp-not-working/13784315#13784315 */
    int fd = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT, 0644);
    if (fd < 0)
    {
        return false;
    }

    /* Call fork, returning false if there is an error in invocation */
    pid_t pid = fork();    
    if (pid == -1)
    {
        close(fd);
        return false;
    }    
    /* If fork was successful (pid of 0 indicates in the child), try calling execv */
    else if (pid == 0)
    {
        if (dup2(fd, 1) < 0)
        {
            close(fd);
            return false;
        }
        /* Child calls execv, returning false if there is an error.
           Exec should only return if there is an error. */
        int ret = execv(command[0], command);
        if (ret == -1)
        {
            close(fd);
            return false;
        }
    }
    
    /* Wait for the particular pid child process we created, 
       which means the parent will wait until this child has executed.
       Return false if there is an error. */
    int status;
    pid_t w_pid = waitpid(pid, &status, 0);
    if (w_pid == -1)
    {
        close(fd);
        return false;
    }

    /* If child terminated normally, but the exit status of
        the child is non-zero, return false */
    if (WIFEXITED(status) && WEXITSTATUS(status))
    {
        close(fd);
        return false;
    }

    /* Close file */
    close(fd);
    va_end(args);
    return true;
}
