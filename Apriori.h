#include "dynamic_hash_table.h"

#ifndef MYSTRUCT_H
#define MYSTRUCT_H

typedef struct {
  int *matrix;            // Frequency counts for pairs
  int num_items;          // Total number of frequent items
  int *item_to_index_map; // Maps item IDs to dense matrix indices
  int *index_to_item_map; // Inverse lookup to retrieve items
} TriangularMatrix;

typedef struct {
  int *elements; // Elements of an itemset
  int size;
  float support;
  int count;
} ItemSet;

// Structure to represent an association rule
typedef struct {
  int *antecedent;      // Items in the "if" part of the rule
  int antecedent_size;  // Number of items in the antecedent
  int *consequent;      // Items in the "then" part of the rule
  int consequent_size;  // Number of items in the consequent
  float confidence;     // Confidence of the rule (conditional probability)
  float support;        // Support of the rule (joint probability)
} AssociationRule;

#endif

// Core functions for Apriori algorithm
ItemSet *prune_triangle(TriangularMatrix *matrix, float support, int baskets,
                        int *out_count);
int *extract_frequent(HashTable *frequent_items, char *basket, int *outcount);
TriangularMatrix *build_tri_matrix(HashTable *frequent_items);
int check_pairs(TriangularMatrix *matrix, int *items, int count);
static inline int get_triangle_index(int i, int j, int n);

// Helper functions
int contains_item(int item, int *array, int size);
float find_itemset_support(ItemSet *itemsets, int count, int *items, int size);
float find_itemset_support_fixed(ItemSet *itemsets, int count, int *items, int size);
int** get_all_subsets(int *set, int set_size, int subset_size, int *count);
int* get_complement(int *set, int set_size, int *subset, int subset_size, int *result_size);

// Function to free memory allocated for association rules
void free_rules(AssociationRule *rules, int rule_count);
