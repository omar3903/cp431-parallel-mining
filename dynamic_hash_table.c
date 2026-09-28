#include "dynamic_hash_table.h"

unsigned int hash(int key, int size) { 
    return (unsigned int)key % size; 
}

void init_table(HashTable *table, ValueType type) {
    if (table == NULL) {
        return;
    }
    
    table->size = INITIAL_TABLE_SIZE;
    table->count = 0;
    table->entries = calloc(table->size, sizeof(HashEntry));
    if (table->entries == NULL) {
        printf("Memory allocation failed for hash table entries\n");
        return;
    }
    table->type = type;
}

void resize_table(HashTable *table) {
    if (table == NULL || table->entries == NULL) {
        return;
    }
    
    int old_size = table->size;
    HashEntry *old_entries = table->entries;

    // Double the size
    table->size = old_size * 2;
    table->entries = calloc(table->size, sizeof(HashEntry));
    if (table->entries == NULL) {
        printf("Memory allocation failed during hash table resize\n");
        // Restore old state
        table->size = old_size;
        table->entries = old_entries;
        return;
    }
    
    // Reset count as we'll increment it during insertion
    int old_count = table->count;
    table->count = 0;

    // Rehash all entries
    for (int i = 0; i < old_size; i++) {
        if (old_entries[i].occupied) {
            insert(table, old_entries[i].key);
            table->entries[hash(old_entries[i].key, table->size)].value =
                old_entries[i].value;
        }
    }

    // Ensure count is correct if insertion failed for some reason
    if (table->count != old_count) {
        printf("Warning: Count mismatch after resize (%d vs %d)\n", 
               table->count, old_count);
    }

    free(old_entries);
}

void print_sample(const HashTable *table, int max_items, int rank) {
    if (table == NULL || table->entries == NULL) {
        printf("Rank %d: Hash table is NULL or empty\n", rank);
        return;
    }
    
    printf("Rank %d: printing up to %d hash table entries from the size 1 "
           "itemsets: \n",
           rank, max_items);
    int printed = 0;

    for (int i = 0; i < table->size && printed < max_items; i++) {
        if (table->entries[i].occupied) {
            printf("  key = %d, count = %i \n", table->entries[i].key,
                   table->entries[i].value.i);
            printed++;
        }
    }

    if (printed == 0) {
        printf("  (No entries)\n");
    }

    fflush(stdout); // Ensure output appears in MPI jobs
}

void insert(HashTable *table, int key) {
    // Early return for invalid table
    if (table == NULL || table->entries == NULL) {
        return;
    }
    
    // Insert is only ever called by int type tables
    if ((float)table->count / table->size >= LOAD_FACTOR) {
        printf("Resizing local table from %d to %d entries\n", 
               table->size, table->size * 2);
        resize_table(table);
        // Check if resize was successful
        if ((float)table->count / table->size >= LOAD_FACTOR) {
            printf("Warning: Table resize may have failed\n");
        }
    }

    unsigned int index = hash(key, table->size);
    int start_index = index; // Remember starting point to detect cycles
    
    while (table->entries[index].occupied) {
        if (table->entries[index].key == key) {
            table->entries[index].value.i++;
            return;
        }
        index = (index + 1) % table->size;
        
        // If we've checked the entire table (unlikely but possible with failed resize)
        if (index == start_index) {
            printf("Warning: Hash table is full, can't insert key %d\n", key);
            return;
        }
    }

    table->entries[index].occupied = 1;
    table->entries[index].key = key;
    table->entries[index].value.i = 1;
    table->count++;
}

int get_count(HashTable *table, int key) {
    if (table == NULL || table->entries == NULL) {
        return 0;
    }
    
    unsigned int index = hash(key, table->size);
    int start_index = index;
    
    while (table->entries[index].occupied) {
        if (table->entries[index].key == key) {
            return table->entries[index].value.i;
        }
        index = (index + 1) % table->size;
        
        // Prevent infinite loop
        if (index == start_index) {
            break;
        }
    }
    return 0;
}

float get_support(HashTable *table, int key) {
    if (table == NULL || table->entries == NULL) {
        return 0.0;
    }
    
    unsigned int index = hash(key, table->size);
    int start_index = index;
    
    while (table->entries[index].occupied) {
        if (table->entries[index].key == key) {
            return table->entries[index].value.f;
        }
        index = (index + 1) % table->size;
        
        // Prevent infinite loop
        if (index == start_index) {
            break;
        }
    }
    return 0.0;
}

void free_table(HashTable *table) {
    if (table == NULL) {
        return;
    }
    
    if (table->entries != NULL) {
        free(table->entries);
        table->entries = NULL;
    }
    table->size = 0;
    table->count = 0;
}

void merge_exact(HashTable *table, int key, HashValue value) {
    if (table == NULL || table->entries == NULL) {
        return;
    }
    
    if ((float)table->count / table->size >= LOAD_FACTOR) {
        resize_table(table);
    }

    unsigned int index = hash(key, table->size);
    int start_index = index;
    
    while (table->entries[index].occupied) {
        if (table->entries[index].key == key) {
            table->entries[index].value.i += value.i;
            return;
        }
        index = (index + 1) % table->size;
        
        // Prevent infinite loop if table is full
        if (index == start_index) {
            printf("Warning: Hash table is full in merge_exact\n");
            return;
        }
    }

    table->entries[index].occupied = 1;
    table->entries[index].key = key;
    table->entries[index].value = value;
    table->count++;
}

Pair *convert_table(HashTable *table, int *count) {
    // Turns a hashtable into an array of Pair objects
    // Outputs count through pointer to calling code
    if (table == NULL || table->entries == NULL || count == NULL) {
        *count = 0;
        return NULL;
    }

    Pair *pairs = malloc(sizeof(Pair) * table->count);
    if (pairs == NULL) {
        printf("Memory allocation failed in convert_table\n");
        *count = 0;
        return NULL;
    }
    
    int index = 0;

    for (int i = 0; i < table->size; i++) {
        if (table->entries[i].occupied) {
            pairs[index].key = table->entries[i].key;
            pairs[index].value = table->entries[i].value;
            index++;
        }
    }

    *count = index;
    return pairs;
}

void clone_table(HashTable *dest, const HashTable *src) {
    if (dest == NULL || src == NULL || src->entries == NULL) {
        return;
    }
    
    init_table(dest, src->type);
    if (dest->entries == NULL) {
        return;
    }
    
    for (int i = 0; i < src->size; i++) {
        if (src->entries[i].occupied) {
            merge_exact(dest, src->entries[i].key, src->entries[i].value);
        }
    }
}

int compare(const void *a, const void *b) {
    int int_a = *((int *)a);
    int int_b = *((int *)b);

    // an easy expression for comparing
    return (int_a > int_b) - (int_a < int_b);
}

void sort_ids(HashTable* table, int* ids) {
    if (table == NULL || table->entries == NULL || ids == NULL) {
        return;
    }
    
    int count = 0;
    for (int i = 0; i < table->size; i++) {
        if (table->entries[i].occupied) {
            ids[count++] = table->entries[i].key;
        }
    }
    qsort(ids, count, sizeof(int), compare);
}