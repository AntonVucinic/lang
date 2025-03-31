#include "memory.h"
#include "compiler.h"
#include "table.h"

#include <stdbool.h>
#ifdef DEBUG_LOG_GC
#include <stdio.h>
#endif
#include <stdlib.h>

#include "chunk.h"
#include "object.h"
#include "value.h"
#include "vm.h"

const int GC_HEAP_GROW_FACTOR = 2;

void *
reallocate(void *pointer, size_t old_size, size_t new_size)
{
    vm.bytes_allocated += new_size - old_size;
    if (new_size > old_size) {
#ifdef DEBUG_STRESS_GC
        garbage_collect();
#endif
        if (vm.bytes_allocated > vm.next_gc)
            garbage_collect();
    }

    if (new_size == 0) {
        free(pointer);
        return NULL;
    }

    void *result = realloc(pointer, new_size);
    if (result == NULL)
        exit(EXIT_FAILURE);
    return result;
}

void
mark_object(obj_t *object)
{
    if (object == NULL || object->is_marked)
        return;
#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void *)object);
    print_value(obj_val(object));
    printf("\n");
#endif
    object->is_marked = true;

    if (vm.gray_capacity < vm.gray_count + 1) {
        vm.gray_capacity = grow_capacity(vm.gray_capacity);
        vm.gray_stack =
          (obj_t **)realloc(vm.gray_stack, sizeof(obj_t *) * vm.gray_capacity);
        if (vm.gray_stack == NULL)
            exit(EXIT_FAILURE);
    }
    vm.gray_stack[vm.gray_count++] = object;
}

void
mark_value(value_t value)
{
    if (is_obj(value))
        mark_object(as_obj(value));
}

static void
mark_array(value_array *array)
{
    for (int i = 0; i < array->count; i++)
        mark_value(array->values[i]);
}

static void
blacken_object(obj_t *object)
{
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void *)object);
    print_value(obj_val(object));
    printf("\n");
#endif

    switch (object->type) {
        case OBJ_BOUND_METHOD: {
            obj_bound_method *bound = (obj_bound_method *)object;
            mark_value(bound->receiver);
            mark_object((obj_t *)bound->method);
            break;
        }
        case OBJ_CLASS: {
            obj_class *class = (obj_class *)object;
            mark_object((obj_t *)class->name);
            table_mark(&class->methods);
            break;
        }
        case OBJ_CLOSURE: {
            obj_closure *closure = (obj_closure *)object;
            mark_object((obj_t *)closure->function);
            for (int i = 0; i < closure->upvalue_count; i++)
                mark_object((obj_t *)closure->upvalues[i]);
            break;
        }
        case OBJ_FUNCTION: {
            obj_function *function = (obj_function *)object;
            mark_object((obj_t *)function->name);
            mark_array(&function->chunk.constants);
            break;
        }
        case OBJ_INSTANCE: {
            obj_instance *instance = (obj_instance *)object;
            mark_object((obj_t *)instance->class);
            table_mark(&instance->fields);
            break;
        }
        case OBJ_UPVALUE:
            mark_value(((obj_upvalue *)object)->closed);
            break;
        case OBJ_NATIVE:
        case OBJ_STRING:
            break;
    }
}

static void
object_free(obj_t *object)
{
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void *)object, object->type);
#endif

    switch (object->type) {
        case OBJ_BOUND_METHOD:
            FREE(obj_bound_method, object);
            break;
        case OBJ_CLASS: {
            obj_class *class = (obj_class *)object;
            table_free(&class->methods);
            FREE(obj_class, object);
            break;
        }
        case OBJ_CLOSURE: {
            obj_closure *closure = (obj_closure *)object;
            FREE_ARRAY(
              obj_upvalue *, closure->upvalues, closure->upvalue_count);
            FREE(obj_closure, object);
            break;
        }
        case OBJ_FUNCTION: {
            obj_function *function = (obj_function *)object;
            free_chunk(&function->chunk);
            FREE(obj_function, object);
            break;
        }
        case OBJ_INSTANCE: {
            obj_instance *instance = (obj_instance *)object;
            table_free(&instance->fields);
            FREE(obj_instance, object);
            break;
        }
        case OBJ_NATIVE:
            FREE(obj_native, object);
            break;
        case OBJ_STRING: {
            obj_string *string = (obj_string *)object;
            FREE_ARRAY(char, string->chars, string->length + 1);
            FREE(obj_string, object);
            break;
        }
        case OBJ_UPVALUE:
            FREE(obj_upvalue, object);
            break;
    }
}

static void
mark_roots(void)
{
    for (value_t *slot = vm.stack; slot < vm.stack_top; slot++)
        mark_value(*slot);
    for (int i = 0; i < vm.frame_count; i++)
        mark_object((obj_t *)vm.frames[i].closure);
    for (obj_upvalue *upvalue = vm.open_upvalues; upvalue != NULL;
         upvalue = upvalue->next)
        mark_object((obj_t *)upvalue);
    table_mark(&vm.globals);
    mark_compiler_roots();
    mark_object((obj_t *)vm.init_string);
}

static void
trace_references(void)
{
    while (vm.gray_count > 0) {
        obj_t *object = vm.gray_stack[--vm.gray_count];
        blacken_object(object);
    }
}

static void
sweep(void)
{
    obj_t *previous = NULL;
    obj_t *object = vm.objects;
    while (object != NULL) {
        if (object->is_marked) {
            object->is_marked = false;
            previous = object;
            object = object->next;
            continue;
        }

        obj_t *unreached = object;
        object = object->next;
        if (previous != NULL)
            previous->next = object;
        else
            vm.objects = object;
        object_free(unreached);
    }
}

void
garbage_collect(void)
{
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
    size_t before = vm.bytes_allocated;
#endif

    mark_roots();
    trace_references();
    table_remove_white(&vm.strings);
    sweep();

    vm.next_gc = vm.bytes_allocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
           before - vm.bytes_allocated,
           before,
           vm.bytes_allocated,
           vm.next_gc);
#endif
}

void
free_objects(void)
{
    obj_t *object = vm.objects;
    while (object != NULL) {
        obj_t *next = object->next;
        object_free(object);
        object = next;
    }
    free(vm.gray_stack);
}
