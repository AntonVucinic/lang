#include "vm.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "chunk.h"
#include "compiler.h"
#ifdef DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

vm_t vm;

static value_t
native_clock(int arg_count, value_t *args)
{
    return number_val((double)clock() / CLOCKS_PER_SEC);
}

static void
reset_stack(void)
{
    vm.stack_top = vm.stack;
    vm.frame_count = 0;
    vm.open_upvalues = NULL;
}

static void
runtime_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = vm.frame_count - 1; i >= 0; i--) {
        call_frame_t *frame = &vm.frames[i];
        obj_function *function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
        if (function->name == NULL)
            fprintf(stderr, "script\n");
        else
            fprintf(stderr, "%s()\n", function->name->chars);
    }

    reset_stack();
}

static void
define_native(const char *name, native_fn function)
{
    push(obj_val((obj_t *)copy_string(name, (int)strlen(name))));
    push(obj_val((obj_t *)newnative(function)));
    table_set(&vm.globals, as_string(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

void
vm_init(void)
{
    reset_stack();
    vm.objects = NULL;
    vm.bytes_allocated = 0;
    vm.next_gc = 1024 * 1024;

    vm.gray_count = 0;
    vm.gray_capacity = 0;
    vm.gray_stack = 0;

    table_init(&vm.globals);
    table_init(&vm.strings);

    vm.init_string = NULL;
    vm.init_string = copy_string("init", 4);

    define_native("clock", native_clock);
}

void
free_vm(void)
{
    table_free(&vm.globals);
    table_free(&vm.strings);
    vm.init_string = NULL;
    free_objects();
}

void
push(value_t value)
{
    *vm.stack_top++ = value;
}

value_t
pop(void)
{
    return *--vm.stack_top;
}

static value_t
peek(int distance)
{
    return vm.stack_top[-1 - distance];
}

static bool
call(obj_closure *closure, int arg_count)
{
    if (arg_count != closure->function->arity) {
        runtime_error("Expected %d arguments but got %d.",
                      closure->function->arity,
                      arg_count);
        return false;
    }
    if (vm.frame_count == FRAMES_MAX) {
        runtime_error("Stack overflow.");
        return false;
    }

    call_frame_t *frame = &vm.frames[vm.frame_count++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stack_top - arg_count - 1;
    return true;
}

static bool
invoke_from_class(obj_class *class, obj_string *name, int arg_count)
{
    value_t method;
    if (!table_get(&class->methods, name, &method)) {
        runtime_error("Undefined property '%s'.", name->chars);
        return false;
    }
    return call(as_closure(method), arg_count);
}

static bool
call_value(value_t callee, int arg_count)
{
    if (!is_obj(callee))
        goto call_err;

    switch (obj_type(callee)) {
        case OBJ_BOUND_METHOD: {
            obj_bound_method *bound = as_bound_method(callee);
            vm.stack_top[-arg_count - 1] = bound->receiver;
            return call(bound->method, arg_count);
        }
        case OBJ_CLASS: {
            obj_class *class = as_class(callee);
            vm.stack_top[-arg_count - 1] =
              obj_val((obj_t *)newinstance(class));
            value_t initializer;
            if (table_get(&class->methods, vm.init_string, &initializer)) {
                return call(as_closure(initializer), arg_count);
            } else if (arg_count != 0) {
                runtime_error("Expect - arguments but got %d.", arg_count);
                return false;
            }
            return true;
        }
        case OBJ_CLOSURE:
            return call(as_closure(callee), arg_count);
        case OBJ_NATIVE: {
            native_fn native = as_native(callee);
            value_t result = native(arg_count, vm.stack_top - arg_count);
            vm.stack_top -= arg_count + 1;
            push(result);
            return true;
        }
        default:
            break; /* Non-callable object type. */
    }

call_err:
    runtime_error("Can only call functions and classes.");
    return false;
}

static bool
invoke(obj_string *name, int arg_count)
{
    value_t receiver = peek(arg_count);
    if (!is_instance(receiver)) {
        runtime_error("Only instances have methods.");
        return false;
    }
    obj_instance *instance = as_instance(receiver);
    return invoke_from_class(instance->class, name, arg_count);
}

static bool
bind_method(obj_class *class, obj_string *name)
{
    value_t method;
    if (!table_get(&class->methods, name, &method)) {
        runtime_error("Undefined property '%s'.", name->chars);
        return false;
    }

    obj_bound_method *bound = newbound_method(peek(0), as_closure(method));
    pop();
    push(obj_val((obj_t *)bound));
    return true;
}

static obj_upvalue *
capture_upvalue(value_t *local)
{
    obj_upvalue *prev_upvalue = NULL;
    obj_upvalue *upvalue = vm.open_upvalues;
    while (upvalue != NULL && upvalue->location > local) {
        prev_upvalue = upvalue;
        upvalue = upvalue->next;
    }
    if (upvalue != NULL && upvalue->location == local)
        return upvalue;
    obj_upvalue *created_upvalue = newupvalue(local);
    created_upvalue->next = upvalue;
    if (prev_upvalue == NULL)
        vm.open_upvalues = created_upvalue;
    else
        prev_upvalue->next = created_upvalue;
    return created_upvalue;
}

static void
close_upvalues(value_t *last)
{
    while (vm.open_upvalues != NULL && vm.open_upvalues->location >= last) {
        obj_upvalue *upvalue = vm.open_upvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.open_upvalues = upvalue->next;
    }
}

static void
method_define(obj_string *name)
{
    value_t method = peek(0);
    obj_class *class = as_class(peek(1));
    table_set(&class->methods, name, method);
    pop();
}

static bool
is_falsey(value_t value)
{
    return is_nil(value) || (is_bool(value) && !as_bool(value));
}

static void
concatenate(void)
{
    obj_string *b = as_string(peek(0));
    obj_string *a = as_string(peek(1));

    int length = a->length + b->length;
    char *chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    obj_string *result = take_string(chars, length);
    pop();
    pop();
    push(obj_val((obj_t *)result));
}

static inline uint8_t
read_byte(call_frame_t *frame)
{
    return *frame->ip++;
}

static inline uint16_t
read_short(call_frame_t *frame)
{
    frame->ip += 2;
    return (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]);
}

static inline value_t
read_constant(call_frame_t *frame)
{
    return frame->closure->function->chunk.constants.values[read_byte(frame)];
}

static inline obj_string *
read_string(call_frame_t *frame)
{
    return as_string(read_constant(frame));
}

static interpret_result
run(void)
{
    call_frame_t *frame = &vm.frames[vm.frame_count - 1];
#define BINARY_OP(value_type, op)                                             \
    do {                                                                      \
        if (!is_number(peek(0)) || !is_number(peek(1))) {                     \
            runtime_error("Operands must be numbers.");                       \
            return INTERPRET_RUNTIME_ERROR;                                   \
        }                                                                     \
        double b = as_number(pop());                                          \
        double a = as_number(pop());                                          \
        push(value_type(a op b));                                             \
    } while (0)

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
        for (value_t *slot = vm.stack; slot < vm.stack_top; slot++) {
            printf("[ ");
            print_value(*slot);
            printf(" ]");
        }
        printf("\n");
        disassemble_instruction(
          &frame->closure->function->chunk,
          (int)(frame->ip - frame->closure->function->chunk.code));
#endif
        op_code instruction;
        switch (instruction = read_byte(frame)) {
            case OP_CONSTANT: {
                value_t constant = read_constant(frame);
                push(constant);
                break;
            }
            case OP_NIL:
                push(nil_val());
                break;
            case OP_TRUE:
                push(bool_val(true));
                break;
            case OP_FALSE:
                push(bool_val(false));
                break;
            case OP_POP:
                pop();
                break;
            case OP_GET_LOCAL: {
                uint8_t slot = read_byte(frame);
                push(frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = read_byte(frame);
                frame->slots[slot] = peek(0);
                break;
            }
            case OP_GET_GLOBAL: {
                obj_string *name = read_string(frame);
                value_t value;
                if (!table_get(&vm.globals, name, &value)) {
                    runtime_error("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_DEFINE_GLOBAL: {
                obj_string *name = read_string(frame);
                /* don't pop until after adding the value to the hash table
                 * this ensures the value doesn't get garbage collected
                 */
                table_set(&vm.globals, name, peek(0));
                pop();
                break;
            }
            case OP_SET_GLOBAL: {
                obj_string *name = read_string(frame);
                if (table_set(&vm.globals, name, peek(0))) {
                    table_delete(&vm.globals, name);
                    runtime_error("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_GET_UPVALUE: {
                uint8_t slot = read_byte(frame);
                push(*frame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = read_byte(frame);
                *frame->closure->upvalues[slot]->location = peek(0);
                break;
            }
            case OP_GET_PROPERTY: {
                if (!is_instance(peek(0))) {
                    runtime_error("Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                obj_instance *instance = as_instance(peek(0));
                obj_string *name = read_string(frame);

                value_t value;
                if (table_get(&instance->fields, name, &value)) {
                    pop();
                    push(value);
                    break;
                }

                if (!bind_method(instance->class, name))
                    return INTERPRET_RUNTIME_ERROR;
                break;
            }
            case OP_SET_PROPERTY: {
                if (!is_instance(peek(1))) {
                    runtime_error("Only instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                obj_instance *instance = as_instance(peek(1));
                table_set(&instance->fields, read_string(frame), peek(0));
                value_t value = pop();
                pop();
                push(value);
                break;
            }
            case OP_GET_SUPER: {
                obj_string *name = read_string(frame);
                obj_class *superclass = as_class(pop());

                if (!bind_method(superclass, name))
                    return INTERPRET_RUNTIME_ERROR;
                break;
            }
            case OP_EQUAL: {
                value_t b = pop();
                value_t a = pop();
                push(bool_val(values_equal(a, b)));
                break;
            }
            case OP_GREATER:
                BINARY_OP(bool_val, >);
                break;
            case OP_LESS:
                BINARY_OP(bool_val, <);
                break;
            case OP_ADD: {
                if (is_string(peek(0)) && is_string(peek(1)))
                    concatenate();
                else if (is_number(peek(0)) && is_number(peek(1))) {
                    double b = as_number(pop());
                    double a = as_number(pop());
                    push(number_val(a + b));
                } else {
                    runtime_error(
                      "Operands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT:
                BINARY_OP(number_val, -);
                break;
            case OP_MULTIPLY:
                BINARY_OP(number_val, *);
                break;
            case OP_DIVIDE:
                BINARY_OP(number_val, /);
                break;
            case OP_NOT:
                push(bool_val(is_falsey(pop())));
                break;
            case OP_NEGATE:
                if (!is_number(peek(0))) {
                    runtime_error("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(number_val(-as_number(pop())));
                break;
            case OP_PRINT:
                print_value(pop());
                printf("\n");
                break;
            case OP_JUMP:
                frame->ip += read_short(frame);
                break;
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = read_short(frame);
                if (is_falsey(peek(0)))
                    frame->ip += offset;
                break;
            }
            case OP_LOOP:
                frame->ip -= read_short(frame);
                break;
            case OP_CALL: {
                int arg_count = read_byte(frame);
                if (!call_value(peek(arg_count), arg_count))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frame_count - 1];
                break;
            }
            case OP_INVOKE: {
                obj_string *method = read_string(frame);
                int arg_count = read_byte(frame);
                if (!invoke(method, arg_count))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frame_count - 1];
                break;
            }
            case OP_SUPER_INVOKE: {
                obj_string *method = read_string(frame);
                int arg_count = read_byte(frame);
                obj_class *superclass = as_class(pop());
                if (!invoke_from_class(superclass, method, arg_count))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frame_count - 1];
                break;
            }
            case OP_CLOSURE: {
                obj_function *function = as_function(read_constant(frame));
                obj_closure *closure = newclosure(function);
                push(obj_val((obj_t *)closure));
                for (int i = 0; i < closure->upvalue_count; i++) {
                    uint8_t is_local = read_byte(frame);
                    uint8_t index = read_byte(frame);
                    if (is_local)
                        closure->upvalues[i] =
                          capture_upvalue(frame->slots + index);
                    else
                        closure->upvalues[i] = frame->closure->upvalues[index];
                }
                break;
            }
            case OP_CLOSE_UPVALUE:
                close_upvalues(vm.stack_top - 1);
                pop();
                break;
            case OP_RETURN: {
                value_t result = pop();
                close_upvalues(frame->slots);
                if (--vm.frame_count == 0) {
                    pop();
                    return INTERPRET_OK;
                }
                vm.stack_top = frame->slots;
                push(result);
                frame = &vm.frames[vm.frame_count - 1];
                break;
            }
            case OP_CLASS:
                push(obj_val((obj_t *)newclass(read_string(frame))));
                break;
            case OP_INHERIT: {
                value_t superclass = peek(1);
                if (!is_class(superclass)) {
                    runtime_error("Superclass must be a class.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                obj_class *subclass = as_class(peek(0));
                table_add_all(&as_class(superclass)->methods,
                              &subclass->methods);
                pop();
                break;
            }
            case OP_METHOD:
                method_define(read_string(frame));
                break;
        }
    }
#undef BINARY_OP
}

interpret_result
interpret(const char *source)
{
    obj_function *function = compile(source);
    if (function == NULL)
        return INTERPRET_COMPILE_ERROR;

    push(obj_val((obj_t *)function));
    obj_closure *closure = newclosure(function);
    pop();
    push(obj_val((obj_t *)closure));
    call(closure, 0);

    return run();
}
