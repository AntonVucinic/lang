#ifndef clox_value_h
#define clox_value_h

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct obj_t obj_t;
typedef struct obj_string obj_string;

#ifdef NAN_BOXING

#define SIGN_BIT ((uint64_t)0x8000000000000000)
#define QNAN ((uint64_t)0x7ffc000000000000)

#define TAG_NIL ((uint64_t)1)
#define TAG_FALSE ((uint64_t)2)
#define TAG_TRUE ((uint64_t)3)

typedef uint64_t value_t;

static inline value_t
nil_val(void);
static inline value_t
false_val(void);
static inline value_t
true_val(void);

static inline bool
is_bool(value_t value)
{
    return value == true_val() || value == false_val();
}

static inline bool
is_nil(value_t value)
{
    return value == nil_val();
}

static inline bool
is_number(value_t value)
{
    return (value & QNAN) != QNAN;
}

static inline bool
is_obj(value_t value)
{
    return (value & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT);
}

static inline bool
as_bool(value_t value)
{
    return value == true_val();
}

static inline double
as_number(value_t value)
{
    double num;
    memcpy(&num, &value, sizeof(value_t));
    return num;
}

static inline obj_t *
as_obj(value_t value)
{
    return (obj_t *)(uintptr_t)(value & ~(SIGN_BIT | QNAN));
}

static inline value_t
bool_val(bool b)
{
    return b ? true_val() : false_val();
}

static inline value_t
false_val(void)
{
    return QNAN | TAG_FALSE;
}

static inline value_t
true_val(void)
{
    return QNAN | TAG_TRUE;
}

static inline value_t
nil_val(void)
{
    return QNAN | TAG_NIL;
}

static inline value_t
number_val(double num)
{
    value_t value;
    memcpy(&value, &num, sizeof(double));
    return value;
}

static inline value_t
obj_val(obj_t *obj)
{
    return SIGN_BIT | QNAN | (uint64_t)(uintptr_t)obj;
}

#else /* NAN_BOXING */

typedef enum
{
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
    VAL_OBJ,
} value_type;

typedef struct
{
    value_type type;
    union
    {
        bool boolean;
        double number;
        obj_t *obj;
    } as;
} value_t;

static inline bool
is_bool(value_t value)
{
    return value.type == VAL_BOOL;
}

static inline bool
is_nil(value_t value)
{
    return value.type == VAL_NIL;
}

static inline bool
is_number(value_t value)
{
    return value.type == VAL_NUMBER;
}

static inline bool
is_obj(value_t value)
{
    return value.type == VAL_OBJ;
}

static inline obj_t *
as_obj(value_t value)
{
    return value.as.obj;
}

static inline bool
as_bool(value_t value)
{
    return value.as.boolean;
}

static inline double
as_number(value_t value)
{
    return value.as.number;
}

static inline value_t
bool_val(bool value)
{
    return (value_t){ VAL_BOOL, { .boolean = value } };
}

static inline value_t
nil_val(void)
{
    return (value_t){ VAL_NIL, { .number = 0 } };
}

static inline value_t
number_val(double value)
{
    return (value_t){ VAL_NUMBER, { .number = value } };
}

static inline value_t
obj_val(obj_t *object)
{
    return (value_t){ VAL_OBJ, { .obj = object } };
}

#endif /* NAN_BOXING */

typedef struct
{
    int capacity;
    int count;
    value_t *values;
} value_array;

bool
values_equal(value_t a, value_t b);
void
init_value_array(value_array *);
void
write_value_array(value_array *, value_t);
void
free_value_array(value_array *);

void print_value(value_t);

#endif /* clox_value_h */
