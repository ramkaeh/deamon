#ifndef UTIL_H
#define UTIL_H

#include <unistd.h>

void logMessage(const char *message);

void copyFile(const char *sourcePath, const char *destPath, off_t fileSize, int mmapThreshold);

int removeDirectory(const char *target);

void synchronizeDirectories(const char *source, const char *dest, int recursive, int mmapThreshold);

void handleSignal(int signal);

#endif