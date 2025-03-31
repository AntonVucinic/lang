#ifndef clox_table_h
#define clox_table_h

#include <stdint.h>

#include "value.h"

typedef struct
{
    obj_string *key;
    value_t value;
} entry_t;

typedef struct
{
    int count;
    int capacity;
    entry_t *entries;
} table_t;

void
table_init(table_t *table);
void
table_free(table_t *table);
bool
table_get(table_t *table, obj_string *key, value_t *value);
bool
table_set(table_t *table, obj_string *key, value_t value);
bool
table_delete(table_t *table, obj_string *key);
void
table_add_all(table_t *from, table_t *to);
obj_string *
table_find_string(table_t *table,
                  const char *chars,
                  int length,
                  uint32_t hash);
void
table_remove_white(table_t *table);
void
table_mark(table_t *table);

#endif /* clox_table_h */
