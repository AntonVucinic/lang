#include "table.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void
table_init(table_t *table)
{
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void
table_free(table_t *table)
{
    FREE_ARRAY(entry_t, table->entries, table->capacity);
    table_init(table);
}

static entry_t *
find_entry(entry_t *entries, int capacity, obj_string *key)
{
    entry_t *tombstone = NULL;
    for (uint32_t index = key->hash & (capacity - 1);;
         index = (index + 1) & (capacity - 1)) {
        entry_t *entry = &entries[index];
        if (entry->key == key)
            return entry;
        if (entry->key != NULL)
            continue;
        if (is_nil(entry->value))
            return tombstone != NULL ? tombstone : entry;
        if (tombstone == NULL)
            tombstone = entry;
    }
}

bool
table_get(table_t *table, obj_string *key, value_t *value)
{
    if (table->count == 0)
        return false;

    entry_t *entry = find_entry(table->entries, table->capacity, key);
    if (entry->key == NULL)
        return false;

    *value = entry->value;
    return true;
}

static void
adjust_capacity(table_t *table, int capacity)
{
    entry_t *entries = ALLOCATE(entry_t, capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].value = nil_val();
    }

    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        entry_t *entry = &table->entries[i];
        if (entry->key == NULL)
            continue;

        entry_t *dest = find_entry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    FREE_ARRAY(entry_t, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

bool
table_set(table_t *table, obj_string *key, value_t value)
{
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = grow_capacity(table->capacity);
        adjust_capacity(table, capacity);
    }

    entry_t *entry = find_entry(table->entries, table->capacity, key);
    bool is_new_key = entry->key == NULL;
    if (is_new_key && is_nil(entry->value))
        table->count++;

    entry->key = key;
    entry->value = value;
    return is_new_key;
}

bool
table_delete(table_t *table, obj_string *key)
{
    if (table->count == 0)
        return false;

    entry_t *entry = find_entry(table->entries, table->capacity, key);
    if (entry->key == NULL)
        return false;

    entry->key = NULL;
    entry->value = bool_val(true);
    return true;
}

void
table_add_all(table_t *from, table_t *to)
{
    for (int i = 0; i < from->capacity; i++) {
        entry_t *entry = &from->entries[i];
        if (entry->key != NULL) {
            table_set(to, entry->key, entry->value);
        }
    }
}

obj_string *
table_find_string(table_t *table, const char *chars, int length, uint32_t hash)
{
    if (table->count == 0)
        return NULL;

    for (uint32_t index = hash & (table->capacity - 1);;
         index = (index + 1) & (table->capacity - 1)) {
        entry_t *entry = &table->entries[index];
        if (entry->key == NULL) {
            if (is_nil(entry->value))
                return NULL;
            else
                continue;
        }
        if (entry->key->length == length && entry->key->hash == hash &&
            memcmp(entry->key->chars, chars, length) == 0)
            return entry->key;
    }
}

void
table_remove_white(table_t *table)
{
    for (int i = 0; i < table->capacity; i++) {
        entry_t *entry = &table->entries[i];
        if (entry->key != NULL && !entry->key->obj.is_marked)
            table_delete(table, entry->key);
    }
}

void
table_mark(table_t *table)
{
    for (int i = 0; i < table->capacity; i++) {
        entry_t *entry = &table->entries[i];
        mark_object((obj_t *)entry->key);
        mark_value(entry->value);
    }
}
