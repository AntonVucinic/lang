#include "chunk.h"

#include <stdint.h>
#include <stdlib.h>

#include "memory.h"
#include "value.h"
#include "vm.h"

void
init_chunk(chunk_t *chunk)
{
    chunk->count = 0;
    chunk->capacity = 8;
    chunk->code = malloc(8 * sizeof(uint8_t));
    chunk->lines = malloc(8 * sizeof(int));
    init_value_array(&chunk->constants);
}

void
free_chunk(chunk_t *chunk)
{
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(int, chunk->lines, chunk->capacity);
    free_value_array(&chunk->constants);
    init_chunk(chunk);
}

void
write_chunk(chunk_t *chunk, uint8_t byte, int line)
{
    if (chunk->capacity < chunk->count + 1) {
        int old_capacity = chunk->capacity;
        chunk->capacity = old_capacity * 2;
        chunk->code =
          GROW_ARRAY(uint8_t, chunk->code, old_capacity, chunk->capacity);
        chunk->lines =
          GROW_ARRAY(int, chunk->lines, old_capacity, chunk->capacity);
    }
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

int
add_constant(chunk_t *chunk, value_t value)
{
    push(value);
    write_value_array(&chunk->constants, value);
    pop();
    return chunk->constants.count - 1;
}
