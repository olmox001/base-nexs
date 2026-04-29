#ifndef KERNEL_STRING_H
#define KERNEL_STRING_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* memset(void* dest, int ch, size_t count);
void* memcpy(void* dest, const void* src, size_t count);
void* memmove(void* dest, const void* src, size_t count);
int memcmp(const void* lhs, const void* rhs, size_t count);
size_t strlen(const char* str);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t count);
char* strncat(char* dest, const char* src, size_t count);
char* strtok(char* str, const char* delim);
int strcmp(const char* lhs, const char* rhs);
int strncmp(const char* lhs, const char* rhs, size_t count);
char* strchr(const char* str, int ch);
char* strrchr(const char* str, int ch);
char* strstr(const char* haystack, const char* needle);
char* strdup(const char* str);
char* strerror(int errnum);

#ifdef __cplusplus
}
#endif
#endif
