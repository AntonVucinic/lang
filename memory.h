#ifndef clox_memory_h
#define clox_memory_h

#include <stddef.h>

#include "value.h"

#define ALLOCATE(type, count)                                                 \
    (type *)reallocate(NULL, 0, sizeof(type) * (count))

#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

#define GROW_ARRAY(type, pointer, old_count, new_count)                       \
    (type *)reallocate(                                                       \
      pointer, sizeof(type) * (old_count), sizeof(type) * (new_count))

#define FREE_ARRAY(type, pointer, old_count)                                  \
    reallocate(pointer, sizeof(type) * (old_count), 0);

static inline int
grow_capacity(int old_capacity)
{
    return old_capacity == 0 ? 8 : 2 * old_capacity;
}

void *
reallocate(void *pointer, size_t old_size, size_t new_size);
void
mark_object(obj_t *object);
void
mark_value(value_t value);
void
garbage_collect(void);
void
free_objects(void);

#endif /* clox_memory_h */
