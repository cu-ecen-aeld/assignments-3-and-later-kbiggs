#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    int sec_to_wait_to_obtain = thread_func_args->wait_to_obtain_ms / 1000;
    int sec_to_wait_to_release = thread_func_args->wait_to_release_ms / 1000;
    
    // sleep for wait to obtain ms
    sleep(sec_to_wait_to_obtain);

    // obtain mutex
    pthread_mutex_lock(thread_func_args->mutex);
    // hold mutex for wait to release ms
    sleep(sec_to_wait_to_release);

    // release mutex
    pthread_mutex_unlock(thread_func_args->mutex);

    // if completed successfully, populate thread_complete_success with true
    thread_func_args->thread_complete_success = true;
    
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

    // malloc for thread_data param to pass to threadfunc
    struct thread_data *thread_struct = (struct thread_data *) malloc(sizeof(struct thread_data));
    thread_struct->wait_to_obtain_ms = wait_to_obtain_ms;
    thread_struct->wait_to_release_ms = wait_to_release_ms;
    thread_struct->mutex = mutex;

    pthread_attr_t attr;

    // start threadfunc (nonblocking)
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int id = pthread_create(thread, &attr, threadfunc, thread_struct);
    
    // error created thread, return false
    if (id != 0)
    {
        free(thread_struct);
        return false;
    }

    // if thread completed successfully, put thread ID into thread param
    /*if (thread_struct->thread_complete_success)
    {

    }*/

    // free thread_data with pointer returned from threadfunc
    free(thread_struct);
    return true;
}

