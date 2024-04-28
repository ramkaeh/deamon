#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/mman.h>
#include <syslog.h>
#include "util.h"


#define THRESHOLD_SIZE 1048576 // Próg rozmiaru pliku (1 MB)




int main(int argc, char *argv[]) {
    logMessage("--SyncDaemon started--");
    // Check arguments
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <source_directory> <destination_directory> [options]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Parse optional arguments
    int recursive = 0;
    int sleepTime = 300; // Default sleep time is 5 minutes
    int mmapThreshold = THRESHOLD_SIZE; // Default mmap threshold is 1 MB
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-R") == 0) {
            recursive = 1;
            logMessage("Option :Recursive");
        } else if (strcmp(argv[i], "-t") == 0 && i+1 < argc) {
            sleepTime = atoi(argv[++i]);
            
        } else if (strcmp(argv[i], "-m") == 0 && i+1 < argc) {
            mmapThreshold = atoi(argv[++i]);
            
        }
    }

    // Check if source and destination directories exist and are directories
    struct stat sourceStat, destStat;
    if (stat(argv[1], &sourceStat) == -1 || !S_ISDIR(sourceStat.st_mode)) {
        fprintf(stderr, "Error: %s is not a directory\n", argv[1]);
        exit(EXIT_FAILURE);
    }
    if (stat(argv[2], &destStat) == -1 || !S_ISDIR(destStat.st_mode)) {
        fprintf(stderr, "Error: %s is not a directory\n", argv[2]);
        exit(EXIT_FAILURE);
    }

    // Daemonize the process
    pid_t pid = fork();
    if (pid == -1) {
        perror("Error forking process");
        exit(EXIT_FAILURE);
    }

    if (pid != 0) {
        // Parent process exits
        exit(EXIT_SUCCESS);
        
    }
    logMessage("Process forked");
    
    // Create new session and detach from controlling terminal
    if (setsid() == -1) {
        perror("Error creating new session");
        exit(EXIT_FAILURE);
    }
    logMessage("SyncDaemon running");

    // Change working directory to root
   if (chdir("/") == -1) {
        perror("Error changing working directory to root");
        exit(EXIT_FAILURE);
    }

    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Set signal handler for SIGUSR1
    signal(SIGUSR1, handleSignal);

    // Main daemon logic: sleep for specified time, then synchronize directories
    while (1) {
        logMessage("Going to sleep");
        sleep(10);
        logMessage("Waking up");
        synchronizeDirectories(argv[1], argv[2], recursive, mmapThreshold);
        logMessage("Directories synchronized");
    }

    return 0;
}