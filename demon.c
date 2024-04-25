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


#define THRESHOLD_SIZE 1048576 // Próg rozmiaru pliku (1 MB)
#define LOG_FILE "/var/log/syncdaemon.log"
#define PATH_MAX 4096

void logMessage(const char *message) {
    openlog("SyncDaemon", LOG_PID | LOG_CONS | LOG_NOWAIT, LOG_USER);
    syslog(LOG_INFO, "%s", message);
    closelog();

    FILE *logFile = fopen(LOG_FILE,"a");
    if(logFile == NULL){
        perror("Error: Could not open log file");
        exit(EXIT_FAILURE);
    }
        // Pobranie aktualnej daty i czasu
    time_t currentTime;
    struct tm *localTime;
    time(&currentTime);
    localTime = localtime(&currentTime);

    // Zapisanie komunikatu i daty do pliku logu
    fprintf(logFile, "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
            localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday,
            localTime->tm_hour, localTime->tm_min, localTime->tm_sec, message);

    // Zamknięcie pliku logu
    fclose(logFile);

    
}

void copyFile(const char *sourcePath, const char *destPath, off_t fileSize, int mmapThreshold) {
    int srcFile = open(sourcePath, O_RDONLY);
    if (srcFile == -1) {
        perror("Error opening source file");
        return;
    }

    // Determine if mmap/write or read/write should be used based on file size
    if (fileSize <= mmapThreshold) {
        // Use read/write for small files
        int destFile = open(destPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (destFile == -1) {
            perror("Error opening destination file");
            close(srcFile);
            return;
        }

        char buffer[BUFSIZ];
        ssize_t bytesRead;
        while ((bytesRead = read(srcFile, buffer, BUFSIZ)) > 0) {
            if (write(destFile, buffer, bytesRead) != bytesRead) {
                perror("Error writing to destination file");
                close(srcFile);
                close(destFile);
                return;
            }
        }

        close(destFile);
    } else {
        // Use mmap/write for large files
        void *srcData = mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, srcFile, 0);
        if (srcData == MAP_FAILED) {
            perror("Error mapping source file");
            close(srcFile);
            return;
        }

        int destFile = open(destPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (destFile == -1) {
            perror("Error opening destination file");
            munmap(srcData, fileSize);
            close(srcFile);
            return;
        }

        if (write(destFile, srcData, fileSize) != fileSize) {
            perror("Error writing to destination file");
            munmap(srcData, fileSize);
            close(srcFile);
            close(destFile);
            return;
        }

        munmap(srcData, fileSize);
        close(destFile);
    }

    close(srcFile);
}

void synchronizeDirectories(const char *source, const char *dest, int recursive, int mmapThreshold) {
    // Open source directory
    DIR *dir = opendir(source);
    if (dir == NULL) {
        perror("Error opening source directory");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue; // Skip current and parent directory entries
        }

        // Construct full paths for source and destination files
        char sourcePath[PATH_MAX];
        char destPath[PATH_MAX];
        snprintf(sourcePath, PATH_MAX, "%s/%s", source, entry->d_name);
        snprintf(destPath, PATH_MAX, "%s/%s", dest, entry->d_name);

        // Check if the entry is a directory
        struct stat statbuf;
        if (stat(sourcePath, &statbuf) == -1) {
            perror("Error getting file status");
            continue;
        }
        if (S_ISDIR(statbuf.st_mode)) {
            // Recursively synchronize directories if recursive option is enabled
            if (recursive) {
                synchronizeDirectories(sourcePath, destPath, recursive, mmapThreshold);
            }
        } else if (S_ISREG(statbuf.st_mode)) {
            // Only synchronize regular files

            // Check if the file exists in the destination directory
            if (access(destPath, F_OK) == -1) {
                // File does not exist in destination directory, copy it
                copyFile(sourcePath, destPath, statbuf.st_size, mmapThreshold);
                logMessage("Copied file from source to destination");
            } else {
                // File exists in destination directory, compare modification times
                struct stat destStatbuf;
                if (stat(destPath, &destStatbuf) == -1) {
                    perror("Error getting file status");
                    continue;
                }

                if (difftime(statbuf.st_mtime, destStatbuf.st_mtime) > 0) {
                    // Source file is newer, copy it to destination
                    copyFile(sourcePath, destPath, statbuf.st_size, mmapThreshold);
                    logMessage("Copied file from source to destination");
                } else if (difftime(destStatbuf.st_mtime, statbuf.st_mtime) > 0) {
                    // Destination file is newer, remove it
                    if (remove(destPath) == 0) {
                        logMessage("Removed file from destination");
                    } else {
                        perror("Error removing file from destination");
                    }
                }
            }
        }
    }

    closedir(dir);
}

void handleSignal(int signal) {
    if (signal == SIGUSR1) {
        logMessage("Received SIGUSR1 signal, waking up daemon");
    }
}

int main(int argc, char *argv[]) {
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

    // Create new session and detach from controlling terminal
    if (setsid() == -1) {
        perror("Error creating new session");
        exit(EXIT_FAILURE);
    }

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
        sleep(sleepTime);
        synchronizeDirectories(argv[1], argv[2], recursive, mmapThreshold);
        logMessage("Directories synchronized");
    }

    return 0;
}