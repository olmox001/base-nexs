#ifndef KERNEL_STDLIB_H
#define KERNEL_STDLIB_H
#include <stddef.h>
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
int atoi(const char *nptr);
int system(const char *command);
long long atoll(const char *nptr);
double atof(const char *nptr);
void exit(int status) __attribute__((noreturn));
#define EXIT_FAILURE 1
void exit(int status);
#endif
