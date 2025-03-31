#ifndef clox_compiler_h
#define clox_compiler_h

#include <stdbool.h>

#include "object.h"

obj_function *
compile(const char *source);
void
mark_compiler_roots(void);

#endif /* clox_compiler_h */
