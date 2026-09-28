#include "itemset_hash_table.h"
#include <string.h> // for memcpy

void init_item_table(ItemHashTable *table) {
  if (table == NULL) {
    return;
  }

  table->size = ITEM_INITIAL_TABLE_SIZE;
  table->count = 0;
  table->entries = calloc(table->size, sizeof(ItemHashEntry));
  if (table->entries == NULL) {
    printf("Memory allocation failed for hash table entries\n");
    return;
  }
}

unsigned long hash_int_array(const IntArray *key) {
  // Need to sort the input array so that every possible order of the set gives
  // the same hash value Copy the array so we can sort it without affecting the
  // original
  int *sorted = malloc(key->length * sizeof(int));
  if (!sorted) {
    fprintf(stderr, "Memory allocation failed in hash_int_array\n");
    exit(EXIT_FAILURE);
  }
  memcpy(sorted, key->items, key->length * sizeof(int));
  qsort(sorted, key->length, sizeof(int), compare_ints);

  // Hash function
  unsigned long hash = 5381;
  for (size_t i = 0; i < key->length; i++) {
    hash = ((hash << 5) + hash) + sorted[i]; // hash * 33 + item
  }

  free(sorted);
  return hash;
}

void resize_itemset_table(ItemHashTable *table) {
  if (table == NULL || table->entries == NULL) {
    return;
  }

  int old_size = table->size;
  ItemHashEntry *old_entries = table->entries;

  // Double the size
  table->size = old_size * 2;
  table->entries = calloc(table->size, sizeof(ItemHashEntry));
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
      insert_itemset(table, &old_entries[i].key, old_entries[i].support);
      free_int_array(&old_entries[i].key);
    }
  }

  // Ensure count is correct if insertion failed for some reason
  if (table->count != old_count) {
    printf("Warning: Count mismatch after resize (%d vs %d)\n", table->count,
           old_count);
  }
  free(old_entries);
}

void insert_itemset(ItemHashTable *table, const IntArray *key, float support) {
  // Early return for invalid table
  if (table == NULL || table->entries == NULL) {
    return;
  }

  // Resize the table if needed
  if ((float)table->count / table->size >= LOAD_FACTOR) {
    printf("Resizing local table from %d to %d entries\n", table->size,
           table->size * 2);
    resize_itemset_table(table);
    // Check if resize was successful
    if ((float)table->count / table->size >= LOAD_FACTOR) {
      printf("Warning: Table resize may have failed\n");
    }
  }

  // Hash the key
  unsigned int index = hash_int_array(key) % table->size;
  int start_index = index; // Remember starting point to detect cycles

  // After hashing, do some linear probing until we get to a free slot
  while (table->entries[index].occupied) {
    if (compare_int_arrays(&table->entries[index].key, key)) {
      printf("Duplicate itemset detected, fatal error in merge \n");
      return;
    }
    index = (index + 1) % table->size;

    // If we've checked the entire table (unlikely but possible with failed
    // resize)
    if (index == start_index) {
      printf("Warning: Hash table is full, can't insert key\n");
      return;
    }
  }

  // Insert itemset into table after finding free slot
  table->entries[index].occupied = 1;
  table->entries[index].key = copy_int_array(key);
  table->entries[index].support = support;
  table->count++;
}

float get_support_itemset(ItemHashTable *table, IntArray *key) {
  if (table == NULL || table->entries == NULL) {
    return 0.0;
  }

  unsigned int index = hash_int_array(key) % table->size;
  int start_index = index;

  while (table->entries[index].occupied) {
    if (compare_int_arrays(&table->entries[index].key, key)) {
      return table->entries[index].support;
    }
    index = (index + 1) % table->size;

    // Prevent infinite loop
    if (index == start_index) {
      break;
    }
  }
  return 0.0;
}

IntArray copy_int_array(const IntArray *src) {
  IntArray copy;
  copy.length = src->length;
  copy.items = malloc(src->length * sizeof(int));
  if (copy.items == NULL) {
    fprintf(stderr, "Memory allocation failed in copy_int_array\n");
    exit(EXIT_FAILURE);
  }
  for (size_t i = 0; i < src->length; i++) {
    copy.items[i] = src->items[i];
  }
  return copy;
}

void free_itemset_table(ItemHashTable *table) {
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
void free_int_array(IntArray *arr) {
  free(arr->items);
  arr->items = NULL;
  arr->length = 0;
}

int compare_int_arrays(const IntArray *a, const IntArray *b) {
  if (a->length != b->length)
    return 0;

  // Create a temporary copy of a's items
  int *temp_a = malloc(a->length * sizeof(int));
  if (!temp_a) {
    fprintf(stderr, "Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }
  memcpy(temp_a, a->items, a->length * sizeof(int));

  // Sort both arrays
  qsort(temp_a, a->length, sizeof(int), (__compar_fn_t)compare_ints);
  qsort(b->items, b->length, sizeof(int), (__compar_fn_t)compare_ints);

  // Compare element by element
  for (size_t i = 0; i < a->length; i++) {
    if (temp_a[i] != b->items[i]) {
      free(temp_a);
      return 0;
    }
  }

  free(temp_a);
  return 1;
}

static int compare_ints(const void *a, const void *b) {
  return (*(int *)a - *(int *)b);
}
