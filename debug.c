#include "debug.h"

#include <stdint.h>
#include <stdio.h>

#include "chunk.h"
#include "object.h"
#include "value.h"

void
disassemble_chunk(chunk_t *chunk, const char *name)
{
    printf("== %s == \n", name);
    for (int offset = 0; offset < chunk->count;) {
        offset = disassemble_instruction(chunk, offset);
    }
}

static int
constant_instruction(const char *name, chunk_t *chunk, int offset)
{
    uint8_t constant = chunk->code[offset + 1];
    printf("%-16s %4d '", name, constant);
    print_value(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 2;
}

static int
invoke_instruction(const char *name, chunk_t *chunk, int offset)
{
    uint8_t constant = chunk->code[offset + 1];
    uint8_t arg_count = chunk->code[offset + 2];
    printf("%-16s (%d args) %4d '", name, arg_count, constant);
    print_value(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 3;
}

static int
simple_instruction(const char *name, int offset)
{
    printf("%s\n", name);
    return offset + 1;
}

static int
byte_instruction(const char *name, chunk_t *chunk, int offset)
{
    uint8_t slot = chunk->code[offset + 1];
    printf("%-16s %4d\n", name, slot);
    return offset + 2;
}

static int
jump_instruction(const char *name, int sign, chunk_t *chunk, int offset)
{
    uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
    jump |= chunk->code[offset + 2];
    printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

int
disassemble_instruction(chunk_t *chunk, int offset)
{
#define SIMPLE_INSTRUCTION(OP)                                                \
    case OP:                                                                  \
        return simple_instruction(#OP, offset)

    printf("%04d ", offset);
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1])
        printf("   | ");
    else
        printf("%4d ", chunk->lines[offset]);
    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_CONSTANT:
            return constant_instruction("OP_CONSTANT", chunk, offset);
            SIMPLE_INSTRUCTION(OP_NIL);
            SIMPLE_INSTRUCTION(OP_TRUE);
            SIMPLE_INSTRUCTION(OP_FALSE);
        case OP_GET_LOCAL:
            return byte_instruction("OP_GET_LOCAL", chunk, offset);
        case OP_SET_LOCAL:
            return byte_instruction("OP_SET_LOCAL", chunk, offset);
        case OP_GET_GLOBAL:
            return constant_instruction("OP_GET_GLOBAL", chunk, offset);
        case OP_DEFINE_GLOBAL:
            return constant_instruction("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL:
            return constant_instruction("OP_SET_GLOBAL", chunk, offset);
        case OP_GET_UPVALUE:
            return byte_instruction("OP_GET_UPVALUE", chunk, offset);
        case OP_SET_UPVALUE:
            return byte_instruction("OP_SET_UPVALUE", chunk, offset);
        case OP_GET_PROPERTY:
            return constant_instruction("OP_GET_PROPERTY", chunk, offset);
        case OP_SET_PROPERTY:
            return constant_instruction("OP_SET_PROPERTY", chunk, offset);
        case OP_GET_SUPER:
            return constant_instruction("OP_GET_SUPER", chunk, offset);
            SIMPLE_INSTRUCTION(OP_EQUAL);
            SIMPLE_INSTRUCTION(OP_POP);
            SIMPLE_INSTRUCTION(OP_GREATER);
            SIMPLE_INSTRUCTION(OP_LESS);
            SIMPLE_INSTRUCTION(OP_ADD);
            SIMPLE_INSTRUCTION(OP_SUBTRACT);
            SIMPLE_INSTRUCTION(OP_MULTIPLY);
            SIMPLE_INSTRUCTION(OP_DIVIDE);
            SIMPLE_INSTRUCTION(OP_NOT);
            SIMPLE_INSTRUCTION(OP_NEGATE);
            SIMPLE_INSTRUCTION(OP_PRINT);
        case OP_JUMP:
            return jump_instruction("OP_JUMP", 1, chunk, offset);
        case OP_JUMP_IF_FALSE:
            return jump_instruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_LOOP:
            return jump_instruction("OP_LOOP", 1, chunk, offset);
        case OP_CALL:
            return byte_instruction("OP_CALL", chunk, offset);
        case OP_INVOKE:
            return invoke_instruction("OP_INVOKE", chunk, offset);
        case OP_SUPER_INVOKE:
            return invoke_instruction("OP_SUPER_INVOKE", chunk, offset);
        case OP_CLOSURE: {
            offset++;
            uint8_t constant = chunk->code[offset++];
            printf("%-16s %4d ", "OP_CLOSURE", constant);
            print_value(chunk->constants.values[constant]);
            printf("\n");

            obj_function *function =
              as_function(chunk->constants.values[constant]);
            for (int j = 0; j < function->upvalue_count; j++) {
                int is_local = chunk->code[offset++];
                int index = chunk->code[offset++];
                printf("%04d      |                     %s %d\n",
                       offset - 2,
                       is_local ? "local" : "upvalue",
                       index);
            }

            return offset;
        }
            SIMPLE_INSTRUCTION(OP_CLOSE_UPVALUE);
            SIMPLE_INSTRUCTION(OP_RETURN);
        case OP_CLASS:
            return constant_instruction("OP_CLASS", chunk, offset);
            SIMPLE_INSTRUCTION(OP_INHERIT);
        case OP_METHOD:
            return constant_instruction("OP_METHOD", chunk, offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
#undef SIMPLE_INSTRUCTION
}
