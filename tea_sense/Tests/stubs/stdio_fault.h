#ifndef TEST_STDIO_FAULT_H
#define TEST_STDIO_FAULT_H
#include <stdio.h>
/* Test-only interception after the native stdio declarations/inline bodies. */
int TestSnprintf(char *buffer, size_t capacity, const char *format, ...);
#define snprintf TestSnprintf
#endif
