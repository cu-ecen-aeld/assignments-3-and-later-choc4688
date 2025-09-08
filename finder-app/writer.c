#include <string.h>
#include <syslog.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <stdio.h>

int main(int argc, char *argv[]) {

    //References were LSP Ch2 and syslog man pages.

    //NULL as first param sets identity to the program name apparently.
    openlog(NULL, LOG_CONS, LOG_USER);

    //arg 0 = file name
    //arg 1 = writefile + path
    //arg 2 = filestr

    //argc includes the filename in the count
    if (argc != 3) {
        syslog(LOG_ERR | LOG_USER, "Expected 2 arguments\n");
        printf("Debug 2\n");
        return 1;
    }

    //Can assume a directory is created by the caller

    char* writefile = argv[1];
    char* writestr = argv[2];

    //Need to split writefile string into path and file 
    //Then create / open the new file, deleting all previous contents

    int fd;

    //Create file at specified path with all user, group, and world permissions enabled
    //According to LSP Ch2, this function does truncate the file if it already exists.
    //Also opens the file for write only by default.
    fd = creat(writefile, 0777);

    if (fd == -1) {
        syslog(LOG_ERR | LOG_USER, "Unable to create file at the specified directory '%s'\n", writefile);
        return 1;
    }

    //Then write the string to the file and then close it
    syslog(LOG_DEBUG | LOG_USER, "Writing %s to %s\n", writestr, writefile);

    ssize_t nr;
    nr = write(fd, writestr, strlen(writestr));
    if (nr == -1) {
        syslog(LOG_ERR | LOG_USER, "Error writing '%s' to the specified file '%s'", writestr, writefile);
        return 1;
    }
    else if (nr != strlen(writestr)) {
        syslog(LOG_ERR | LOG_USER, "Partial write error of message '%s' to the specified file '%s'", writestr, writefile);
        return 1;
    }

    if (close(fd) == -1) {
        syslog(LOG_ERR | LOG_USER, "Error closing file '%s'", writefile);
        return 1;
    }

    return 0;
}

