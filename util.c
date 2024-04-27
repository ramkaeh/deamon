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

#define LOG_FILE "/var/log/syncdaemon.log"
#define PATH_MAX 4096

void logMessage(const char *message) {
    openlog("SyncDaemon", LOG_PID | LOG_CONS | LOG_NOWAIT, LOG_USER);
    syslog(LOG_INFO, "%s", message);
    closelog();


    
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

                if (access(destPath, F_OK) == -1 || difftime(statbuf.st_mtime, destStatbuf.st_mtime) > 0 ){
                    // Source file is newer, copy it to destination
                    copyFile(sourcePath, destPath, statbuf.st_size, mmapThreshold);
                    logMessage("Copied file from source to destination");
                } else if (access(destPath, F_OK) == -1 || difftime(statbuf.st_mtime, destStatbuf.st_mtime) > 0) {
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


