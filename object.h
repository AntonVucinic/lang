#ifndef clox_object_h
#define clox_object_h

#include <stdbool.h>
#include <stdint.h>

#include "chunk.h"
#include "table.h"
#include "value.h"

typedef enum
{
    OBJ_BOUND_METHOD,
    OBJ_CLASS,
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_INSTANCE,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_UPVALUE,
} obj_type_t;

struct obj_t
{
    obj_type_t type;
    bool is_marked;
    struct obj_t *next;
};

typedef struct
{
    obj_t obj;
    int arity;
    int upvalue_count;
    chunk_t chunk;
    obj_string *name;
} obj_function;

typedef value_t (*native_fn)(int arg_count, value_t *args);

typedef struct
{
    obj_t obj;
    native_fn function;
} obj_native;

struct obj_string
{
    obj_t obj;
    int length;
    char *chars;
    uint32_t hash;
};

typedef struct obj_upvalue
{
    obj_t obj;
    value_t *location;
    value_t closed;
    struct obj_upvalue *next;
} obj_upvalue;

typedef struct
{
    obj_t obj;
    obj_function *function;
    obj_upvalue **upvalues;
    /* if a function gets GC'd before the closure we need to know how large the
     * upvalues array is so we store a redundant count. How can the funciton
     * get GC'd if a closure still has a reference to it? */
    int upvalue_count;
} obj_closure;

typedef struct
{
    obj_t obj;
    obj_string *name;
    table_t methods;
} obj_class;

typedef struct
{
    obj_t obj;
    obj_class *class;
    table_t fields;
} obj_instance;

typedef struct
{
    obj_t obj;
    value_t receiver;
    obj_closure *method;
} obj_bound_method;

obj_bound_method *
newbound_method(value_t receiver, obj_closure *method);

obj_class *
newclass(obj_string *name);

obj_closure *
newclosure(obj_function *function);

obj_function *
newfunction(void);

obj_instance *
newinstance(obj_class *class);

obj_native *
newnative(native_fn function);

obj_string *
take_string(char *chars, int length);
obj_string *
copy_string(const char *chars, int length);
obj_upvalue *
newupvalue(value_t *slot);
void
print_object(value_t value);

static inline obj_type_t
obj_type(value_t value)
{

    return as_obj(value)->type;
}

static inline bool
is_bound_method(value_t value)
{
    return is_obj(value) && as_obj(value)->type == OBJ_CLASS;
}

static inline bool
is_class(value_t value)
{
    return is_obj(value) && as_obj(value)->type == OBJ_CLASS;
}

static inline bool
is_closure(value_t value)
{
    return is_obj(value) && as_obj(value)->type == OBJ_CLOSURE;
}

static inline bool
is_function(value_t value)
{
    return is_obj(value) && as_obj(value)->type == OBJ_FUNCTION;
}

static inline bool
is_instance(value_t value)
{
    return is_obj(value) && as_obj(value)->type == OBJ_INSTANCE;
}

static inline bool
is_native(value_t value)
{
    return is_obj(value) && as_obj(value)->type == OBJ_NATIVE;
}

static inline bool
is_string(value_t value)
{
    return is_obj(value) && as_obj(value)->type == OBJ_STRING;
}

static inline obj_bound_method *
as_bound_method(value_t value)
{
    return (obj_bound_method *)as_obj(value);
}

static inline obj_class *
as_class(value_t value)
{
    return (obj_class *)as_obj(value);
}

static inline obj_closure *
as_closure(value_t value)
{
    return (obj_closure *)as_obj(value);
}

static inline obj_function *
as_function(value_t value)
{
    return (obj_function *)as_obj(value);
}

static inline obj_instance *
as_instance(value_t value)
{
    return (obj_instance *)as_obj(value);
}

static inline native_fn
as_native(value_t value)
{
    return ((obj_native *)as_obj(value))->function;
}

static inline obj_string *
as_string(value_t value)
{
    return (obj_string *)as_obj(value);
}

static inline char *
as_cstring(value_t value)
{
    return ((obj_string *)as_obj(value))->chars;
}

#endif /* clox_object_h */
