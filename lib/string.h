#pragma once

#include "types.h"

void* memset(void* dest, int val, size_t count);
void* memcpy(void* dest, const void* src, size_t count);
void* memmove(void* dest, const void* src, size_t count);
int   memcmp(const void* a, const void* b, size_t count);
int   strlen(const char* str);
int   strcmp(const char* a, const char* b);
int   strncmp(const char* a, const char* b, int n);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, int n);
char* strcat(char* dest, const char* src);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
int   toupper(int c);
int   tolower(int c);
int   isdigit(int c);
int   isalpha(int c);
int   isspace(int c);
int   abs(int x);
