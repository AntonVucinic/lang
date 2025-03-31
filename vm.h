#ifndef clox_vm_h
#define clox_vm_h

#include <stddef.h>
#include <stdint.h>

#include "common.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct
{
    obj_closure *closure;
    uint8_t *ip;
    value_t *slots;
} call_frame_t;

typedef struct
{
    call_frame_t frames[FRAMES_MAX];
    int frame_count;
    value_t stack[STACK_MAX];
    value_t *stack_top;
    table_t globals;
    table_t strings;
    obj_string *init_string;
    obj_upvalue *open_upvalues;
    size_t bytes_allocated;
    size_t next_gc;
    obj_t *objects;
    int gray_count;
    int gray_capacity;
    obj_t **gray_stack;
} vm_t;

typedef enum
{
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} interpret_result;

extern vm_t vm;

void
vm_init(void);
void
free_vm(void);

interpret_result
interpret(const char *chunk);

void push(value_t);
value_t
pop(void);

#endif
