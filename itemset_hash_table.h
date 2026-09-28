#ifndef ITEMSET_HASH_TABLE_H
#define ITEMSET_HASH_TABLE_H

#include <stdio.h>
#include <stdlib.h>

#define ITEM_INITIAL_TABLE_SIZE 1000
#define LOAD_FACTOR 0.75

// A reimplimenrtation of dynamic_hash_table.c
// For use with itemsets, to allow for o(1) lookup on itemset support

typedef struct {
  int *items;    // Items
  size_t length; // Length of itemset
} IntArray;

typedef struct {
  IntArray key;  // Item array used as key
  float support; // Support of the itemset
  int occupied;  // 1 if used, 0 otherwise
} ItemHashEntry;

typedef struct {
  ItemHashEntry *entries;
  int size;  // Current table size
  int count; // Number of used slots
} ItemHashTable;

void init_item_table(ItemHashTable *table);
void insert_itemset(ItemHashTable *table, const IntArray *key, float support);
float get_support_itemset(ItemHashTable *table, IntArray *key);
void resize_itemset_table(ItemHashTable *table);
void free_itemset_table(ItemHashTable *table);
unsigned long hash_int_array(const IntArray *key);
int compare_int_arrays(const IntArray *a, const IntArray *b);
IntArray copy_int_array(const IntArray *src);
void free_int_array(IntArray *arr);
static int compare_ints(const void *a, const void *b);

#endif
