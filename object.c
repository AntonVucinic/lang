#include "object.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "memory.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, object_type)                                       \
    (type *)object_allocate(sizeof(type), object_type)

static obj_t *
object_allocate(size_t size, obj_type_t type)
{
    obj_t *object = (obj_t *)reallocate(NULL, 0, size);
    object->type = type;
    object->is_marked = false;
    object->next = vm.objects;
    vm.objects = object;

#ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void *)object, size, type);
#endif

    return object;
}

obj_bound_method *
newbound_method(value_t receiver, obj_closure *method)
{
    obj_bound_method *bound = ALLOCATE_OBJ(obj_bound_method, OBJ_BOUND_METHOD);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

obj_class *
newclass(obj_string *name)
{
    obj_class *class = ALLOCATE_OBJ(obj_class, OBJ_CLASS);
    class->name = name;
    table_init(&class->methods);
    return class;
}

obj_closure *
newclosure(obj_function *function)
{
    obj_upvalue **upvalues = ALLOCATE(obj_upvalue *, function->upvalue_count);
    for (int i = 0; i < function->upvalue_count; i++)
        upvalues[i] = NULL;
    obj_closure *closure = ALLOCATE_OBJ(obj_closure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalue_count = function->upvalue_count;
    return closure;
}

obj_function *
newfunction(void)
{
    obj_function *function = ALLOCATE_OBJ(obj_function, OBJ_FUNCTION);
    function->arity = 0;
    function->upvalue_count = 0;
    function->name = NULL;
    init_chunk(&function->chunk);
    return function;
}

obj_instance *
newinstance(obj_class *class)
{
    obj_instance *instance = ALLOCATE_OBJ(obj_instance, OBJ_INSTANCE);
    instance->class = class;
    table_init(&instance->fields);
    return instance;
}

obj_native *
newnative(native_fn function)
{
    obj_native *native = ALLOCATE_OBJ(obj_native, OBJ_NATIVE);
    native->function = function;
    return native;
}

static obj_string *
allocate_string(char *chars, int length, uint32_t hash)
{
    obj_string *string = ALLOCATE_OBJ(obj_string, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    push(obj_val((obj_t *)string));
    table_set(&vm.strings, string, nil_val());
    pop();
    return string;
}

static uint32_t
hash_string(const char *key, int length)
{
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

obj_string *
take_string(char *chars, int length)
{
    uint32_t hash = hash_string(chars, length);
    obj_string *interned = table_find_string(&vm.strings, chars, length, hash);
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }
    return allocate_string(chars, length, hash);
}

obj_string *
copy_string(const char *chars, int length)
{
    uint32_t hash = hash_string(chars, length);
    obj_string *interned = table_find_string(&vm.strings, chars, length, hash);
    if (interned != NULL)
        return interned;
    char *heap_chars = ALLOCATE(char, length + 1);

    memcpy(heap_chars, chars, length);
    heap_chars[length] = '\0';
    return allocate_string(heap_chars, length, hash);
}

obj_upvalue *
newupvalue(value_t *slot)
{
    obj_upvalue *upvalue = ALLOCATE_OBJ(obj_upvalue, OBJ_UPVALUE);
    upvalue->closed = nil_val();
    upvalue->location = slot;
    upvalue->next = NULL;
    return upvalue;
}

static void
print_function(obj_function *function)
{
    if (function->name == NULL) {
        printf("<script>");
        return;
    }
    printf("<fn %s>", function->name->chars);
}

void
print_object(value_t value)
{
    switch (obj_type(value)) {
        case OBJ_BOUND_METHOD:
            print_function(as_bound_method(value)->method->function);
            break;
        case OBJ_CLASS:
            printf("%s", as_class(value)->name->chars);
            break;
        case OBJ_CLOSURE:
            print_function(as_closure(value)->function);
            break;
        case OBJ_FUNCTION:
            print_function(as_function(value));
            break;
        case OBJ_INSTANCE:
            printf("%s instance", as_instance(value)->class->name->chars);
            break;
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
        case OBJ_STRING:
            printf("%s", as_cstring(value));
            break;
        case OBJ_UPVALUE:
            printf("upvalue");
            break;
    }
}
