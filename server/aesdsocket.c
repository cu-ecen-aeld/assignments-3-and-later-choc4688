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

#define MAX_PACKET_SIZE 65536 //Buffer size for recv. Needs to be large enough to handle long-string.txt
#define TEMP_FILE "/var/tmp/aesdsocketdata"

//Globals (These variables are global so they can be used in the signal_hander function to avoid memory leaks)
int connfd;
int sockfd;
FILE* fptr;
struct addrinfo hints;
struct addrinfo *servinfo; //Points to results of getaddrinfo() after the function is called.
char* pbuff;
char* outpbuff;


/*
* Complete any open connection operations
* Close any open sockets
* Delete the file /var/tmp/aesdsocketdata
*/
static void signal_handler(int signal_number) { //Static = only visible to other funcs in the same file

    if (signal_number == SIGINT || signal_number == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting\n");

        freeaddrinfo(servinfo);

        close(sockfd);
        
        //Check if already closed (closing in the main loop since I have accept() in the main loop)
        if (connfd != -1) 
            close(connfd);

        //Also avoiding double free / double fclose for memory allocated in the main loop
        if (pbuff) 
            free(pbuff);
        if (outpbuff) 
            free(outpbuff);
        if (fptr) 
            fclose(fptr);
        
        //Deletes the specified file
        if (remove(TEMP_FILE) != 0) {
            syslog(LOG_ERR, "Was unable to delete the file %s\n", TEMP_FILE);
        }

        //Exiting here to ensure all sockets are closed and memory is freed correctly within this function instead of setting a flag variable
        // to handle this in main().
        exit(EXIT_SUCCESS);
    }
}


int main(int argc, char *argv[]) {

    //Set up logging since there is a daemon option for this program.
    openlog(NULL, LOG_CONS, LOG_USER);

    //Truncating file in case the last run had a kill signal and bypassed handling
    fptr = fopen(TEMP_FILE, "w");
    if (!fptr) {
        syslog(LOG_ERR, "Truncating file '%s' failed\n", TEMP_FILE);
        exit(EXIT_FAILURE);
    }
    fclose(fptr);


    //-------------------Signal Setup-----------------------------

    struct sigaction new_action;
    int status = 0;
    memset(&new_action, 0, sizeof(struct sigaction));
    new_action.sa_handler = signal_handler;
    //Below 2 lines AI generated, was having an issue with recv() being blocking and how that interacts with the signal handler and this was part of resolving that.
    new_action.sa_flags = 0; // Don't use SA_RESTART
    sigemptyset(&new_action.sa_mask);

    //Registering SIGTERM and SIGINT
    if (sigaction(SIGTERM, &new_action, NULL) != 0 ) {
        syslog(LOG_ERR, "Unable to register SIGTERM in sigaction()\n");
    }
    if (sigaction(SIGINT, &new_action, NULL)) {
        syslog(LOG_ERR, "Unable to register SIGINT in sigaction()\n");
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
    //Reference: https://stackoverflow.com/questions/24194961/how-do-i-use-setsockoptso-reuseaddr
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
        syslog(LOG_ERR, "Failed to set socket options\n");
        return -1;
    }

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
        return -1;
    }


    //Assigns address of socket to sockfd basically
    status = bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen);
    if (status == -1) {
        //  perror("Failed to bind\n");
         syslog(LOG_ERR, "Failed to bind\n");
         freeaddrinfo(servinfo); //Identified single missing free w/ valgrind and AI
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

                // Close the original file descriptor for /dev/null if it's not one of 0, 1, or 2
                if (dev_null_fd > STDERR_FILENO) {
                    close(dev_null_fd);
                }
                //-------------------
            }
            else { //Parent Process
                exit(EXIT_SUCCESS);
            }
        }
        else { //2 args but the second one is not '-d' case
            syslog(LOG_ERR, "Invalid argument %s for %s\n", argv[1], argv[0]);
        }
    }
    else if (argc > 2) {
        syslog(LOG_ERR, "Invalid number of arguments for %s\n", argv[0]);
    } //Finished daemon code



    //Now listen for connections on the socket
    status = listen(sockfd, 1); //Allows for JUST ONE connection request before refusing the rest*****
    if (status == -1) {
        //  perror("Failed to listen\n");
         syslog(LOG_ERR, "Failed listen()\n");
    }

    //The 2 lines below are AI generated. Was needed to fix my section of code below trying to get the IP address.
    struct sockaddr_storage client_addr; 
    socklen_t addr_size = sizeof(client_addr);

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


    //Main Connection Loop
    //Runs until SIGINT or SIGTERM are called
    while(1) {

        pbuff = malloc(MAX_PACKET_SIZE); //For incoming packets
        if (!pbuff) {
            // perror("Failed to malloc pbuff\n");
            syslog(LOG_ERR, "Failed pbuff malloc\n");
            continue; //Try again on the next loop iteration in case the error is recoverable / temporary (Was suggested by Copilot AI for safe memory handling tips)
        }

        outpbuff = malloc(MAX_PACKET_SIZE); //For outgoing packets
        if (!outpbuff) {
            // perror("Failed to malloc outpbuff\n"); 
            syslog(LOG_ERR, "Failed outpbuff malloc\n");
            continue;
        }

        connfd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_size);
        if (connfd == -1) {
            // perror("Failed to accept\n");
            syslog(LOG_ERR, "Failed accept()\n");
            freeaddrinfo(servinfo);
            return -1;
        }

        //Reference: https://www.w3schools.com/c/c_files_write.php
        //Reference: https://man7.org/linux/man-pages/man3/fopen.3.html
        //'a+' opens in append mode, so subsequent 'fwrite' calls append instead, + means read and write, creates files if DNE
        fptr = fopen(TEMP_FILE, "a+"); 
        //Then use fprintf() to append to the file
        if (!fptr) {
            // perror("Error opening or creating file.\n");
            syslog(LOG_ERR, "Failed fopen()\n");
        }

        //size_t unsigned, ssize_t signed
        size_t totalLen = 0;
        ssize_t numRecvBytes;

        //***********************
        while ((numRecvBytes = recv(connfd, pbuff + totalLen, MAX_PACKET_SIZE - totalLen, 0)) > 0) {
            totalLen += numRecvBytes;

            if (memchr(pbuff, '\n', totalLen)) { //*********** */
                break;
            }

            if (totalLen >= MAX_PACKET_SIZE) {
                syslog(LOG_ERR, "Discarding oversized packet");
                totalLen = 0;
                break;
            }
        }

        
        //******
        if (numRecvBytes > MAX_PACKET_SIZE) {
            //Error
            totalLen = 0;
            memset(pbuff, 0, MAX_PACKET_SIZE);
            continue;
        }

        char* line = NULL;

        //Separate and append each packet (ended w/ '\n') to the file
        size_t startPacket = 0;
        for (size_t i = 0; i < numRecvBytes; i++) {
            if (pbuff[i] == '\n') {
                size_t packetLen = i - startPacket + 1;
            
                fwrite(pbuff + startPacket, 1, packetLen, fptr);
                startPacket = i + 1;

                fflush(fptr); //Needs to occur before rewind****

                //Need to return full file content to client as soon as received data packet completes
                //Need to send each line individually I think. Can't load the full file into RAM b/c of constraints.
                ssize_t lineLength;
                size_t len = 0;
                rewind(fptr);
                while ((lineLength = getline(&line, &len, fptr)) != -1) {
                    send(connfd, line, lineLength, 0); //-------Need error handling------
                }
                
            }

        }

        //Freeing temp memory used to store packets
        free(pbuff);
        free(outpbuff);

        //Closing per connection since the socket server can service multiple clients in a typical application
        close(connfd);

        //Indicates connfd was closed here so the signal handler won't try to close it again
        connfd = -1;

        //Skip if fopen() fails basically
        if (fptr) {
            fclose(fptr);
        }
        
        //******
        free(line);

        syslog(LOG_INFO, "Closed connection from %s\n", ipv4str);

    }
    
    return 0; 
}