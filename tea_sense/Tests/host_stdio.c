/* Native libc cannot call Arm rt_sys.h hooks. Use native vsnprintf solely as
 * the host printf frontend, then exercise production _sys_write and Bsp.
 * The ARM build separately verifies the real Arm C-library call chain. */
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include "rt_sys.h"

int printf(const char *format, ...)
{
  char output[640]; /* Test-only frontend, accommodates the fixed paired row. */
  int length;
  va_list arguments;
  assert(!"Application printf must remain disabled");
  va_start(arguments, format);
  length = vsnprintf(output, sizeof(output), format, arguments);
  va_end(arguments);
  if ((length < 0) || ((size_t)length >= sizeof(output))) { return -1; }
  if (_sys_write(_sys_open(__stdout_name, OPEN_W), (const unsigned char *)output,
                 (unsigned)length, 0) != 0) { return -1; }
  return length;
}
