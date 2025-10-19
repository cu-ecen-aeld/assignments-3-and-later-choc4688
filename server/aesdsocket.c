#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <syslog.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "queue.h"

#define USE_AESD_CHAR_DEVICE 1
#ifdef USE_AESD_CHAR_DEVICE
    #define TEMP_FILE "/dev/aesdchar"
    #define USE_TIMESTAMP false
#else
    #define TEMP_FILE "/var/tmp/aesdsocketdata"
    #define USE_TIMESTAMP true
#endif

#define MAX_PACKET_SIZE 65536 //Buffer size for recv. Needs to be large enough to handle long-string.txt
#define RFC2822_FORMAT "timestamp:%a, %d %b %Y %T %z\n"

//Global flag for signal handling
bool signalCaughtFlag = false;


struct thread_data{

    pthread_mutex_t* fileMutex;
    char* pbuffPtr;
    char* outpbuffPtr;
    char* outLine;
    int* connfd;
    char* ipaddrStr;
    bool completeFlag;
};

struct timer_thread_data 
{
    pthread_mutex_t* fileMutex;
};

typedef struct slist_data_s slist_data_t;
struct slist_data_s {
    struct thread_data* value;
    pthread_t* threadPtr;
    SLIST_ENTRY(slist_data_s) entries;
};


/*
* Complete any open connection operations
* Close any open sockets
* Delete the file /var/tmp/aesdsocketdata
*/
static void signal_handler(int signal_number) { //Static = only visible to other funcs in the same file

    if (signal_number == SIGINT || signal_number == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, flagging for cleanup\n");
        signalCaughtFlag = true;
    }
}


void* threadfunc(void* thread_param) {

    // bool threadSuccess = true;

    struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    //size_t unsigned, ssize_t signed
    size_t totalLen = 0;
    ssize_t numRecvBytes;

    while ((numRecvBytes = recv(*thread_func_args->connfd, thread_func_args->pbuffPtr + totalLen, MAX_PACKET_SIZE - totalLen, 0)) > 0) {
        totalLen += numRecvBytes;
        if (memchr(thread_func_args->pbuffPtr, '\n', totalLen)) { //********
            break;
        }
        if (totalLen >= MAX_PACKET_SIZE) {
            syslog(LOG_ERR, "Discarding oversized packet");
            totalLen = 0;
            break;
        }
    }
    if (totalLen > MAX_PACKET_SIZE) { //Over buffer size error
        totalLen = 0;
        memset(thread_func_args->pbuffPtr, 0, MAX_PACKET_SIZE);
        perror("Received more bytes than available in receive buffer.");
        syslog(LOG_ERR, "Received more bytes than available in receive buffer.");
    }
    else if (numRecvBytes == -1 || numRecvBytes == 0) {
        perror("Error on recv, either closed connection or recv error");
    }
    else {
        //---------------------MUTEX LOCK-----------------------
        int status = pthread_mutex_lock(thread_func_args->fileMutex);
        if (status != 0) {
            perror("Obtaining mutex lock failed.");
            syslog(LOG_ERR, "Obtaining mutex lock failed.");
        }
        else {
            FILE* fptr = fopen(TEMP_FILE, "a+"); 
            if (!fptr) {
                perror("Error opening or creating file.\n");
                syslog(LOG_ERR, "Failed fopen()\n");
            }
            else {
                //Separate and append each packet (ended w/ '\n') to the file
                size_t startPacket = 0;
                for (size_t i = 0; i < totalLen; i++) {
                    if (thread_func_args->pbuffPtr[i] == '\n') {
                        size_t packetLen = i - startPacket + 1;
                        status = fwrite(thread_func_args->pbuffPtr + startPacket, 1, packetLen, fptr);
                        if (status == 0) {
                            perror("Failed fwrite()\n");
                            syslog(LOG_ERR, "Failed fwrite()\n");
                            break;
                        }
                        startPacket = i + 1;
                        status = fflush(fptr); //Needs to occur before rewind according to Copilot AI
                        if (status != 0) {
                            perror("Failed fflush()\n");
                            syslog(LOG_ERR, "Failed fflush()\n");
                            break;
                        }
                        //Need to return full file content to client as soon as received data packet completes
                        //Need to send each line individually. Can't load the full file into RAM b/c of constraints.
                        ssize_t lineLength;
                        size_t len = 0;
                        rewind(fptr); //Returns nothing
                        while ((lineLength = getline(&thread_func_args->outLine, &len, fptr)) != -1) {
                            status = send(*thread_func_args->connfd, thread_func_args->outLine, lineLength, 0); 
                            if (status == -1) {
                                syslog(LOG_ERR, "Failed send()\n");
                            }
                        } 
                    }
                }
            }

            fclose(fptr);

            status = pthread_mutex_unlock(thread_func_args->fileMutex);
            if (status != 0) {
                perror("Releasing mutex lock failed.");
            }


            // if (!threadSuccess) {
            //     free(thread_func_args->pbuffPtr);
            //     free(thread_func_args->outpbuffPtr);
            //     free(thread_func_args->outLine);
            //     free(thread_func_args->connfd);
            //     free(thread_func_args);
            // }

            //------------------END MUTEX LOCK-----------------------
        }
    }

    close(*thread_func_args->connfd); 
    syslog(LOG_INFO, "Closed connection from %s\n", thread_func_args->ipaddrStr);

    thread_func_args->completeFlag = true;
    return thread_param;
}

//For setting timestamps in the file as per the assignment description.
static void timer_thread ( union sigval sigval )
{

    char outStr[200];
    time_t t;
    struct tm* tmp;

    struct timer_thread_data *td = (struct timer_thread_data*) sigval.sival_ptr;

    //---------------------MUTEX LOCK-----------------------
    int status = pthread_mutex_lock(td->fileMutex);
    if (status != 0) {
        perror("Obtaining mutex lock failed.");
        syslog(LOG_ERR, "Obtaining mutex lock failed.");
    }
    else {

        //Append timestamp in form "timestamp:time"
        //'time' format: ********\n
        //Includes year, month, day, hour, minute, and second representing system wall clock time
        t= time(NULL);
        tmp = localtime(&t);
        if (tmp == NULL) {
            perror("localtime");
            //Error************
        }
        if (strftime(outStr, sizeof(outStr), RFC2822_FORMAT, tmp) == 0) {
            fprintf(stderr, "strftime returned 0");
        }

        FILE* fptr = fopen(TEMP_FILE, "a+"); 
        if (!fptr) {
            perror("Error opening or creating file.\n");
            syslog(LOG_ERR, "Failed fopen() in timer thread\n");
        }

        //Now append to file
        //Need to find '\n' char to determine size
        for (size_t i = 0; i < sizeof(outStr); i++) {
                if (outStr[i] == '\n') {
                    size_t lineLen = i +  1;
                    fwrite(outStr, 1, lineLen, fptr);
                    break;
                }
            }

        
        fclose(fptr);

        if ( pthread_mutex_unlock(td->fileMutex) != 0 ) {
            printf("Error %d (%s) unlocking thread data!\n",errno,strerror(errno));
        }
        //------------------END MUTEX LOCK-----------------------
    }
}



int main(int argc, char *argv[]) {

    //Set up logging since there is a daemon option for this program.
    openlog(NULL, LOG_CONS, LOG_USER);

    //Moved
    // //Truncating file in case the last run had a kill signal and bypassed handling
    // FILE* fptr = fopen(TEMP_FILE, "w");
    // if (!fptr) {
    //     syslog(LOG_ERR, "Truncating file '%s' failed\n", TEMP_FILE);
    //     return -1;
    // }
    // fclose(fptr);


    //---------THREADING STUFF------------

    pthread_mutex_t* fileMutex = malloc(sizeof(pthread_mutex_t));
    if (fileMutex == NULL) {
        //Error
        perror("Error malloc'ing for fileMutex");
    }

    int status = pthread_mutex_init(fileMutex, NULL);
    if (status != 0) {
        //Error
        perror("Error initializing fileMutex");
        free(fileMutex);
        return -1;
    }

    slist_data_t* datap = NULL;
    SLIST_HEAD(slisthead, slist_data_s) head;
    SLIST_INIT(&head);
    int threadCount = 0;

    //------------------------------



    struct timer_thread_data td;
    struct sigevent sev;
    timer_t timerid;

        


    //-------------------Signal Setup-----------------------------

    struct sigaction new_action;
    memset(&new_action, 0, sizeof(struct sigaction));
    new_action.sa_handler = signal_handler;
    //Below 2 lines AI generated, was having an issue with recv() being blocking and how that interacts with the signal handler and this was part of resolving that.
    new_action.sa_flags = 0; // Don't use SA_RESTART
    sigemptyset(&new_action.sa_mask);

    //Registering SIGTERM and SIGINT
    if (sigaction(SIGTERM, &new_action, NULL) != 0 ) {
        syslog(LOG_ERR, "Unable to register SIGTERM in sigaction()\n");
        free(fileMutex);
        return -1;
    }
    if (sigaction(SIGINT, &new_action, NULL)) {
        syslog(LOG_ERR, "Unable to register SIGINT in sigaction()\n");
        free(fileMutex);
        return -1;
    }

    //-------------------Socket Setup-----------------------------
    
    //Open a stream socket bound to port 9000
    //Fail and return -1 if any socket connection steps fail
    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Failed to create socket\n");
        syslog(LOG_ERR, "Failed to create socket\n");
        return -1;
    }

    //Setting REUSEADDR to avoid bind issues:
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
        syslog(LOG_ERR, "Failed to set socket options\n");
        return -1;
    }

    struct addrinfo hints;
    struct addrinfo *servinfo;

    //Setup for getaddrinfo()
    memset(&hints, 0, sizeof(hints)); //Ensures struct is empty
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC; 

    //NULL for first param sets identity to the program name.
    status = getaddrinfo(NULL, "9000", &hints, &servinfo); 
    if (status != 0) {
        // perror("Failed to getaddrinfo\n");
        syslog(LOG_ERR, "getaddrinfo() failed\n");
        free(fileMutex);
        return -1;
    }


    //Assigns address of socket to sockfd basically
    status = bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen);
    if (status == -1) {
        //  perror("Failed to bind\n");
         syslog(LOG_ERR, "Failed to bind\n");
         freeaddrinfo(servinfo); //Identified single missing free w/ valgrind and AI
         free(fileMutex);
         return -1;
    }


    //Forking after bind() for daemon mode
    if (argc == 2) {
        if (!strcmp(argv[1], "-d")) {

            //Creating Daemon:
            //Fork > Exit in Parent > Setsid > Chdir > Close fds > Redirect stdin, stdout, stderr to /dev/null
            pid_t child_pid = fork();
            if (child_pid == -1) {
                syslog(LOG_ERR, "Failed fork()\n");
            }
            else if (child_pid == 0) { //Child Process
                setsid(); //Want to not have a controlling terminal

                //No chdir needed because not deleting any directories 
                //Don't wan't to close FDs since part of this program is storing something in a file
                
                //Redirect stdin, out, and err to /dev/null
                //Reference: Searched on Google for how to redirect these streams, received AI example and modified to add syslog calls
            //-------------------
                int dev_null_fd = open("/dev/null", O_RDWR);

                if (dev_null_fd == -1) {
                    // perror("open /dev/null");
                    syslog(LOG_ERR, "Failed to open /dev/null\n");
                }
                // Redirect stdin (file descriptor 0) to /dev/null
                if (dup2(dev_null_fd, STDIN_FILENO) == -1) {
                    // perror("dup2 STDIN_FILENO");
                    syslog(LOG_ERR, "Failed dup2 STDIN_FILENO\n");
                    close(dev_null_fd);
                }
                // Redirect stdout (file descriptor 1) to /dev/null
                if (dup2(dev_null_fd, STDOUT_FILENO) == -1) {
                    // perror("dup2 STDOUT_FILENO");
                    syslog(LOG_ERR, "Failed dup2 STDOUT_FILENO\n");
                    close(dev_null_fd);
                }
                // Redirect stderr (file descriptor 2) to /dev/null
                if (dup2(dev_null_fd, STDERR_FILENO) == -1) {
                    // perror("dup2 STDERR_FILENO");
                    syslog(LOG_ERR, "Failed dup2 STDERR_FILENO\n");
                    close(dev_null_fd);
                }



                //---Trying file init here in child daemon mode to see if it fixies file content issue between runs
                   
                //Truncating file in case the last run had a kill signal and bypassed handling
                FILE* fptr = fopen(TEMP_FILE, "w");
                if (!fptr) {
                    syslog(LOG_ERR, "Truncating file '%s' failed\n", TEMP_FILE);
                    return -1;
                }
                fclose(fptr);



                //TIMER HAS TO BE IN THE CHILD PROCESS (learned through a long time of debugging.....)
                //---------------------------------------------------------------

                if (USE_TIMESTAMP) {
                        
                    //Reference: https://github.com/cu-ecen-aeld/aesd-lectures/blob/master/lecture9/timer_thread.c
                    memset(&td,0,sizeof(struct timer_thread_data));
                    //Don't need to initialize mutex because we are using the one for the file
                    //Setting up timer to be used for required timestamps in the output file
                    int clock_id = CLOCK_MONOTONIC;
                    memset(&sev, 0, sizeof(struct sigevent));
                    //Setup call to timer_thread passing in td structure as the sigev_value arg
                    sev.sigev_notify = SIGEV_THREAD;
                    sev.sigev_notify_function = timer_thread;
                    td.fileMutex = fileMutex;
                    sev.sigev_value.sival_ptr = &td;

                    if ( timer_create(clock_id,&sev,&timerid) != 0 ) {
                        printf("Error %d (%s) creating timer!\n",errno,strerror(errno));
                    } else {
                        struct itimerspec sleep_time;
                        
                        sleep_time.it_value.tv_sec = 10;
                        sleep_time.it_value.tv_nsec = 0;
                        sleep_time.it_interval.tv_sec = 10;
                        sleep_time.it_interval.tv_nsec = 0;

                        //****************NEED TO ADD STUFF HERE*************
                        //Reference: If statement below from asking Copilot AI "posix interval timer example"
                            //Also had to change timespec sleep_time to itimerspec to work with POSIX timer
                        if (timer_settime(timerid, 0, &sleep_time, NULL) == -1) {
                            perror("Failed timer_settime()");
                            //Error
                            free(fileMutex);
                            return -1;
                        }
                    }
                }

                //---------------------------------------------------------------



                // Close the original file descriptor for /dev/null if it's not one of 0, 1, or 2
                if (dev_null_fd > STDERR_FILENO) {
                    close(dev_null_fd);
                }
                //-------------------
            }
            else { //Parent Process

                //Need to free all data that was associated with the parent process


                freeaddrinfo(servinfo);
                free(fileMutex);


                exit(EXIT_SUCCESS);
            }
        }
        else { //2 args but the second one is not '-d' case
            syslog(LOG_ERR, "Invalid argument %s for %s\n", argv[1], argv[0]);
        }
    }
    else if (argc > 2) {
        syslog(LOG_ERR, "Invalid number of arguments for %s\n", argv[0]);
    } else { //Finished daemon code


        //Truncating file in case the last run had a kill signal and bypassed handling
        FILE* fptr = fopen(TEMP_FILE, "w");
        if (!fptr) {
            syslog(LOG_ERR, "Truncating file '%s' failed\n", TEMP_FILE);
            return -1;
        }
        fclose(fptr);

        if (USE_TIMESTAMP) {
            //Reference: https://github.com/cu-ecen-aeld/aesd-lectures/blob/master/lecture9/timer_thread.c
            memset(&td,0,sizeof(struct timer_thread_data));
            //Don't need to initialize mutex because we are using the one for the file
            //Setting up timer to be used for required timestamps in the output file
            int clock_id = CLOCK_MONOTONIC;
            memset(&sev, 0, sizeof(struct sigevent));
            //Setup call to timer_thread passing in td structure as the sigev_value arg
            sev.sigev_notify = SIGEV_THREAD;
            sev.sigev_notify_function = timer_thread;
            td.fileMutex = fileMutex;
            sev.sigev_value.sival_ptr = &td;
            
            if ( timer_create(clock_id,&sev,&timerid) != 0 ) {
                printf("Error %d (%s) creating timer!\n",errno,strerror(errno));
            } else {
                struct itimerspec sleep_time;
                
                sleep_time.it_value.tv_sec = 10;
                sleep_time.it_value.tv_nsec = 0;
                sleep_time.it_interval.tv_sec = 10;
                sleep_time.it_interval.tv_nsec = 0;

                //****************NEED TO ADD STUFF HERE*************
                //Reference: If statement below from asking Copilot AI "posix interval timer example"
                    //Also had to change timespec sleep_time to itimerspec to work with POSIX timer
                if (timer_settime(timerid, 0, &sleep_time, NULL) == -1) {
                    perror("Failed timer_settime()");
                    //Error
                    free(fileMutex);
                    return -1;
                }
            }
        }
        
    } 



    //Now listen for connections on the socket
    status = listen(sockfd, 1); //Allows for JUST ONE connection request before refusing the rest*****
    if (status == -1) {
         perror("Failed to listen\n");
         syslog(LOG_ERR, "Failed listen()\n");
    }


    //Main Connection Loop
    //Runs until SIGINT or SIGTERM are called
    while(!signalCaughtFlag) {

        char* pbuff = malloc(MAX_PACKET_SIZE); //For incoming packets
        if (!pbuff) {
            perror("Failed to malloc pbuff\n");
            syslog(LOG_ERR, "Failed pbuff malloc\n");
            continue; //Try again on the next loop iteration in case the error is recoverable / temporary (Was suggested by Copilot AI for safe memory handling tips)
        }

        char* outpbuff = malloc(MAX_PACKET_SIZE); //For outgoing packets
        if (!outpbuff) {
            perror("Failed to malloc outpbuff\n"); 
            syslog(LOG_ERR, "Failed outpbuff malloc\n");
            continue;
        }


        //The 2 lines below are AI generated. Was needed to fix my section of code trying to get the IP address.
        struct sockaddr_storage client_addr; 
        socklen_t addr_size = sizeof(client_addr);

        int connfd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_size);
        if (connfd == -1) {
            perror("Failed to accept\n");
            syslog(LOG_ERR, "Failed accept()\n");
            freeaddrinfo(servinfo);
            free(pbuff);
            free(outpbuff);
            return -1;
        }

        //Log connection + get IP addr
        //Reference: https://stackoverflow.com/questions/3060950/how-to-get-ip-address-from-sock-structure-in-c
        struct sockaddr_in* pV4Addr = (struct sockaddr_in*)&client_addr;
        struct in_addr ipAddr = pV4Addr->sin_addr;
        char ipv4str[INET_ADDRSTRLEN];
        const char* temp = inet_ntop( AF_INET, &ipAddr, ipv4str, INET_ADDRSTRLEN );
        if (!temp) {
            perror("Error with inet_ntop\n");
            syslog(LOG_ERR, "Failed inet_ntop()\n");
        }

        syslog(LOG_INFO, "Accepted connection from %s\n", ipv4str);


        //---------INSERT THREADING FUNCTIONALITY HERE------------

        //Have to malloc b/c using threads
        int* memConnFd = malloc(sizeof(connfd));
        if (!memConnFd) {
            perror("Failed to malloc memConnFd\n"); 
            syslog(LOG_ERR, "Failed memConnFd malloc\n");
            continue;
        }
        *memConnFd = connfd;


        //Create struct
        struct thread_data* thread_func_args = (struct thread_data*)malloc(sizeof(struct thread_data));

        *thread_func_args = \
            (struct thread_data){.completeFlag = false, \
            .fileMutex = fileMutex, \
            .pbuffPtr = pbuff, \
            .outpbuffPtr = outpbuff, \
            .outLine = NULL, \
            .connfd = memConnFd, \
            .ipaddrStr = strdup(ipv4str)}; //From Copilot AI debugging, needed to prevent stale pointer


            //Add node for thread info to LL
            //Note that there is an empty first node for init.
            datap = malloc(sizeof(slist_data_t));
            datap->value = thread_func_args;
            datap->threadPtr = malloc(sizeof(pthread_t));
            SLIST_INSERT_HEAD(&head, datap, entries);
            threadCount++;
        


        //Thread param in pthread_create() should be either tail or head of the linked list
        //LL inside or outside while loop? It's a stack, so head at least.


        int status = pthread_create(datap->threadPtr, NULL, threadfunc, thread_func_args);
        if (status != 0) {
            perror("Failed to create thread");
            free(fileMutex);
            free(pbuff);
            free(outpbuff);
            free(datap);
            free(datap->threadPtr);
            free(thread_func_args);
            free(memConnFd);
            return -1;
        } 



        //Put loop here checking status of all threads, joining ones complete
        //Also free data associated with that thread
        //Reference: Asked Copilot AI for an example of using SLIST_FOREACH_SAFE and SLIST_REMOVE together
            //Used no references for the joining and freeing of data.
        struct slist_data_s *currNodePtr, *tmpNodePtr;
        SLIST_FOREACH_SAFE(currNodePtr, &head, entries, tmpNodePtr) {
            //Join thread
            //Free pbuff, outpbuff, etc.

            //Finished, need to join
            if(currNodePtr->value->completeFlag) {
                pthread_join(*currNodePtr->threadPtr, NULL);

                //Free data associated with that thread.
                free(currNodePtr->threadPtr);
                free(currNodePtr->value->pbuffPtr);
                free(currNodePtr->value->outpbuffPtr);
                free(currNodePtr->value->outLine);
                free(currNodePtr->value->connfd); //Earlier malloc'd space for this integer

                //After freeing components, can free thread_func_args
                free(currNodePtr->value);

                SLIST_REMOVE(&head, currNodePtr, slist_data_s, entries);
                free(currNodePtr);
            }
        }
    } //End main connection while-loop


    //Arrive at this section of code once the signal flag is set and the current
    // connection loop completes.

    //Busy-wait until all threads finish for 'graceful' exit...?
    struct slist_data_s *currNodePtr, *tmpNodePtr;
    while (!SLIST_EMPTY(&head)) {
        SLIST_FOREACH_SAFE(currNodePtr, &head, entries, tmpNodePtr) {
            //Join thread
            //Free pbuff, outpbuff, etc.

            //Finished, need to join
            if(currNodePtr->value->completeFlag) {
                pthread_join(*currNodePtr->threadPtr, NULL);

                //Free data associated with that thread.
                free(currNodePtr->threadPtr);
                free(currNodePtr->value->pbuffPtr);
                free(currNodePtr->value->outpbuffPtr);
                free(currNodePtr->value->outLine);
                free(currNodePtr->value->connfd); //Earlier malloc'd space for this integer

                //After freeing components, can free thread_func_args
                free(currNodePtr->value);

                SLIST_REMOVE(&head, currNodePtr, slist_data_s, entries);
                free(currNodePtr);
            }
        }
    }

    if (timer_delete(timerid) != 0) {
        perror("Error deleting timer");
        //Error
    }

    free(fileMutex);
    freeaddrinfo(servinfo);


    if (!USE_AESD_CHAR_DEVICE) {
        //Deletes the specified file
        if (remove(TEMP_FILE) != 0) {
            perror("Was unable to delete the file");
            syslog(LOG_ERR, "Was unable to delete the file %s\n", TEMP_FILE);
        }
    }

    return 0; 
}