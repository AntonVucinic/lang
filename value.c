#include "value.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"

void
init_value_array(value_array *array)
{
    array->count = 0;
    array->capacity = 0;
    array->values = NULL;
}

void
write_value_array(value_array *array, value_t value)
{
    if (array->capacity < array->count + 1) {
        int old_capacity = array->capacity;
        array->capacity = old_capacity ? old_capacity * 8 : 8;
        array->values =
          GROW_ARRAY(value_t, array->values, old_capacity, array->capacity);
    }
    array->values[array->count++] = value;
}

void
free_value_array(value_array *array)
{
    FREE_ARRAY(value_t, array->values, array->capacity);
    init_value_array(array);
}

void
print_value(value_t value)
{
#ifdef NAN_BOXING
    if (is_bool(value))
        printf(as_bool(value) ? "true" : "false");
    else if (is_nil(value))
        printf("nil");
    else if (is_number(value))
        printf("%g", as_number(value));
    else if (is_obj(value))
        print_object(value);
#else  /* NAN_BOXING */
    switch (value.type) {
        case VAL_BOOL:
            printf(as_bool(value) ? "true" : "false");
            break;
        case VAL_NIL:
            printf("nil");
            break;
        case VAL_NUMBER:
            printf("%g", as_number(value));
            break;
        case VAL_OBJ:
            print_object(value);
            break;
    }
#endif /* NAN_BOXING */
}

bool
values_equal(value_t a, value_t b)
{
#ifdef NAN_BOXING
    if (is_number(a) && is_number(b))
        return as_number(a) == as_number(b);
    return a == b;
#else  /* NAN_BOXING */
    if (a.type != b.type)
        return false;
    switch (a.type) {
        case VAL_BOOL:
            return as_bool(a) == as_bool(b);
        case VAL_NIL:
            return true;
        case VAL_NUMBER:
            return as_number(a) == as_number(b);
        case VAL_OBJ:
            return as_obj(a) == as_obj(b);
    }
#endif /* NAN_BOXING */
}
