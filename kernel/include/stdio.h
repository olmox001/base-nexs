#ifndef KERNEL_STDIO_H
#define KERNEL_STDIO_H
#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FILE FILE;

extern FILE* stdout;
extern FILE* stderr;
extern FILE* stdin;

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int printf(const char* format, ...);
int snprintf(char* buffer, size_t count, const char* format, ...);
int sprintf(char* buffer, const char* format, ...);
int vprintf(const char* format, va_list va);
int vfprintf(FILE *stream, const char *format, va_list ap);

int fprintf(FILE* stream, const char* format, ...);
int fputc(int c, FILE* stream);
int fputs(const char* s, FILE* stream);
int fflush(FILE* stream);

FILE *fopen(const char *filename, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int remove(const char *filename);
FILE *fdopen(int fd, const char *mode);
char *fgets(char *str, int n, FILE *stream);

#ifdef __cplusplus
}
#endif
#endif
