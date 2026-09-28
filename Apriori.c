#include "Apriori.h"
#include "dynamic_hash_table.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ITEM_ID 20000

static inline int get_triangle_index(int i, int j, int n) {
  if (i == j)
    return -1; // Diagonal isn't stored
  if (i > j) {
    int tmp = i;
    i = j;
    j = tmp;
  }
  //   (((n) * (n - 1) - (n - (i)) * (n - (i) + 1)) / 2 + ((j) - (i)))
  return i * (n - 1) - (i * (i + 1)) / 2 + (j - i - 1);
  // (n * (n - 1) - (n - i) * (n - i + 1)) / 2 + (j - i - 1);
}

TriangularMatrix *build_tri_matrix(HashTable *frequent_items) {
  if (frequent_items == NULL || frequent_items->count <= 0) {
    return NULL;
  }

  int n = frequent_items->count;

  // Step 1: Build item-to-index map
  int *item_ids = malloc(frequent_items->count * sizeof(int));
  if (item_ids == NULL) {
    printf("Memory allocation failed for item_ids\n");
    return NULL;
  }

  sort_ids(frequent_items, item_ids);
  int *item_to_index = malloc(MAX_ITEM_ID * sizeof(int));
  if (item_to_index == NULL) {
    printf("Memory allocation failed for item_to_index\n");
    free(item_ids);
    return NULL;
  }

  int *index_to_item = malloc(n * sizeof(int));
  if (index_to_item == NULL) {
    printf("Memory allocation failed for index_to_item\n");
    free(item_ids);
    free(item_to_index);
    return NULL;
  }

  for (int i = 0; i < MAX_ITEM_ID; i++)
    item_to_index[i] = -1;
  for (int i = 0; i < n; i++) {
    item_to_index[item_ids[i]] = i;
    index_to_item[i] = item_ids[i];
  }

  // Step 2: Allocate matrix
  int total_pairs = (n * (n - 1)) / 2;
  int *matrix = calloc(total_pairs, sizeof(int));
  if (matrix == NULL) {
    printf("Memory allocation failed for triangular matrix\n");
    free(item_ids);
    free(item_to_index);
    free(index_to_item);
    return NULL;
  }

  // Step 3: Return wrapped matrix object
  TriangularMatrix *tm = malloc(sizeof(TriangularMatrix));
  if (tm == NULL) {
    printf("Memory allocation failed for triangular matrix structure\n");
    free(item_ids);
    free(item_to_index);
    free(index_to_item);
    free(matrix);
    return NULL;
  }

  tm->matrix = matrix;
  tm->num_items = n;
  tm->item_to_index_map = item_to_index;
  tm->index_to_item_map = index_to_item;

  free(item_ids); // Free temp array after we're done with it
  return tm;
}

int *extract_frequent(HashTable *frequent_items, char *basket, int *outcount) {
  // Extract an array of all the frequent items from a basket
  if (frequent_items == NULL || basket == NULL || outcount == NULL) {
    *outcount = 0;
    return NULL;
  }

  char *token;
  int *items = malloc(100 * sizeof(int)); // Assume max basket size of 100 items
  if (items == NULL) {
    printf("Memory allocation failed for items\n");
    *outcount = 0;
    return NULL;
  }

  int x = 0;

  token = strtok(basket, " \t\r\n");
  while (token != NULL && x < 100) {
    char *endptr;
    long num = strtol(token, &endptr, 10); // Convert string read into a number
    if (*endptr == '\0') {
      if (get_count(frequent_items, (int)num) > 0) {
        items[x] = (int)num;
        x++;
      }
    }
    token = strtok(NULL, " \t\r\n");
  }
  *outcount = x;

  // If no items were found, free the array and return NULL
  if (x == 0) {
    free(items);
    return NULL;
  }

  return items;
}

ItemSet *prune_triangle(TriangularMatrix *matrix, float support, int baskets,
                        int *out_count) {
  // Prune a triangular matrix, returning only supported pairs
  if (matrix == NULL || out_count == NULL || baskets <= 0) {
    *out_count = 0;
    return NULL;
  }

  int *index_to_item = matrix->index_to_item_map;
  int n = matrix->num_items;
  int total_pairs = (n * (n - 1)) / 2;

  ItemSet *results = malloc(total_pairs * sizeof(ItemSet));
  if (results == NULL) {
    printf("Memory allocation failed for results\n");
    *out_count = 0;
    return NULL;
  }

  int count = 0;
  int *triangle = matrix->matrix;

  for (int i = 0; i < n - 1; i++) {
    for (int j = i + 1; j < n; j++) {
      int idx = get_triangle_index(i, j, n);

      assert(idx >= 0 && idx < (n * (n - 1)) / 2);

      float item_support = (float)triangle[idx] / baskets;
      if (item_support >= support) {
        ItemSet is;
        is.elements = malloc(2 * sizeof(int));
        if (is.elements == NULL) {
          printf("Memory allocation failed for itemset elements\n");
          // Free any previously allocated elements
          for (int k = 0; k < count; k++) {
            free(results[k].elements);
          }
          free(results);
          *out_count = 0;
          return NULL;
        }
        is.elements[0] = index_to_item[i];
        is.elements[1] = index_to_item[j];
        is.size = 2;
        is.support = item_support;
        is.count = triangle[idx];

        results[count++] = is;
      }
    }
  }

  *out_count = count;

  // If no pairs met the support threshold, free the array and return NULL
  if (count == 0) {
    free(results);
    return NULL;
  }

  // Resize the results array to save memory
  ItemSet *resized_results = realloc(results, count * sizeof(ItemSet));
  if (resized_results != NULL) {
    return resized_results;
  }

  // If realloc fails, just return the original array
  return results;
}

/**
 * Generate pairs of frequent items
 * @param TriangularMatrix *matrix a triangular matrix to store candidate pairs
 * in
 * @param int *items array of frequent ints from a basket
 * @param int count number of frequent ints in the basket
 * @return number of pairs found in basket. pairs stored in TriangularMatrix
 */
int check_pairs(TriangularMatrix *matrix, int *items, int count) {
  // Generate all pairs from an array of ints
  // Increment index in TriangularMatrix
  // items must be frequent
  if (matrix == NULL || items == NULL || count < 2) {
    return 0;
  }

  int pairs = 0;
  int *item_to_index = matrix->item_to_index_map;
  int num_items = matrix->num_items;
  int *triangle = matrix->matrix;
  int total_pairs = (num_items * (num_items - 1)) / 2;

  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {
      int item_i = items[i];
      int item_j = items[j];

      // Map items to dense indices
      int idx_i =
          (item_i >= 0 && item_i < MAX_ITEM_ID) ? item_to_index[item_i] : -1;
      int idx_j =
          (item_j >= 0 && item_j < MAX_ITEM_ID) ? item_to_index[item_j] : -1;

      if (idx_i == -1 || idx_j == -1)
        continue; // should never happen, but safe check

      // Ensure i < j for triangular matrix
      if (idx_i < idx_j) {
        int index = get_triangle_index(idx_i, idx_j, num_items);
        assert(index >= 0 && index < (num_items * (num_items - 1)) / 2);

        if (index >= 0 && index < (num_items * (num_items - 1) / 2)) {
          triangle[index]++;
          pairs++;
        }
      }
    }
  }
  return pairs;
}

// Helper function to check if item is in an array
int contains_item(int item, int *array, int size) {
  if (array == NULL || size <= 0)
    return 0;

  for (int i = 0; i < size; i++) {
    if (array[i] == item) {
      return 1;
    }
  }
  return 0;
}

// Find a frequent itemset in a list of itemsets
float find_itemset_support(ItemSet *itemsets, int count, int *items, int size) {
  if (itemsets == NULL || count <= 0 || items == NULL || size <= 0) {
    return 0.0;
  }

  for (int i = 0; i < count; i++) {
    if (itemsets[i].size == size) {
      // Check if all elements match
      int match_count = 0;
      for (int j = 0; j < size; j++) {
        if (contains_item(items[j], itemsets[i].elements, itemsets[i].size)) {
          match_count++;
        }
      }

      if (match_count == size && size == itemsets[i].size) {
        return itemsets[i].support;
      }
    }
  }
  return 0.0; // Not found
}

// Generate all possible k-sized subsets of an n-element array
void generate_subsets(int *set, int set_size, int subset_size, int *temp,
                      int temp_index, int start_pos, int ***results,
                      int *result_count) {
  if (set == NULL || temp == NULL || results == NULL || result_count == NULL) {
    return;
  }

  // Base case: subset is complete
  if (temp_index == subset_size) {
    // Add current subset to results
    (*results)[*result_count] = (int *)malloc(subset_size * sizeof(int));
    if ((*results)[*result_count] == NULL) {
      printf("Memory allocation failed in generate_subsets\n");
      return;
    }
    memcpy((*results)[*result_count], temp, subset_size * sizeof(int));
    (*result_count)++;
    return;
  }

  // Try all possible elements for current position
  for (int i = start_pos; i <= set_size - (subset_size - temp_index); i++) {
    temp[temp_index] = set[i];
    generate_subsets(set, set_size, subset_size, temp, temp_index + 1, i + 1,
                     results, result_count);
  }
}

// Generate all possible subsets of an array
int **get_all_subsets(int *set, int set_size, int subset_size, int *count) {
  if (set == NULL || count == NULL || subset_size <= 0 || set_size <= 0) {
    *count = 0;
    return NULL;
  }

  // Maximum number of subsets: C(n,k) = n!/(k!(n-k)!)
  // This is an upper bound, we'll reallocate later
  int max_count = 1;
  for (int i = 0; i < subset_size; i++) {
    max_count *= (set_size - i);
    max_count /= (i + 1);
  }

  int **subsets = (int **)malloc(max_count * sizeof(int *));
  if (subsets == NULL) {
    printf("Memory allocation failed in get_all_subsets\n");
    *count = 0;
    return NULL;
  }

  int *temp = (int *)malloc(subset_size * sizeof(int));
  if (temp == NULL) {
    printf("Memory allocation failed for temp in get_all_subsets\n");
    free(subsets);
    *count = 0;
    return NULL;
  }

  *count = 0;

  generate_subsets(set, set_size, subset_size, temp, 0, 0, &subsets, count);

  free(temp);

  // If no subsets were generated, free the array and return NULL
  if (*count == 0) {
    free(subsets);
    return NULL;
  }

  // Resize the array to save memory
  int **resized_subsets = realloc(subsets, (*count) * sizeof(int *));
  if (resized_subsets != NULL) {
    return resized_subsets;
  }

  // If realloc fails, just return the original array
  return subsets;
}

// Create the complement of a subset within the original set
int *get_complement(int *set, int set_size, int *subset, int subset_size,
                    int *result_size) {
  if (set == NULL || subset == NULL || result_size == NULL || set_size <= 0 ||
      subset_size <= 0 || subset_size >= set_size) {
    *result_size = 0;
    return NULL;
  }

  *result_size = set_size - subset_size;
  int *complement = (int *)malloc(*result_size * sizeof(int));
  if (complement == NULL) {
    printf("Memory allocation failed in get_complement\n");
    *result_size = 0;
    return NULL;
  }

  int index = 0;

  for (int i = 0; i < set_size; i++) {
    int found = 0;
    for (int j = 0; j < subset_size; j++) {
      if (set[i] == subset[j]) {
        found = 1;
        break;
      }
    }

    if (!found) {
      complement[index++] = set[i];
    }
  }

  return complement;
}

// Free memory allocated for association rules
void free_rules(AssociationRule *rules, int rule_count) {
  if (rules == NULL)
    return;

  for (int i = 0; i < rule_count; i++) {
    if (rules[i].antecedent != NULL) {
      free(rules[i].antecedent);
      rules[i].antecedent = NULL;
    }
    if (rules[i].consequent != NULL) {
      free(rules[i].consequent);
      rules[i].consequent = NULL;
    }
  }
  free(rules);
}
