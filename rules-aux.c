#include "Apriori.h"
#include "itemset_hash_table.h"
#include "map_lookup.c"
#include "rules-aux.h"
#include <stdlib.h>
#include <string.h>

ItemSet *generate_candidate(ItemSet *itemset1, ItemSet *itemset2, int k) {
  // Ensure the first k-1 elements match
  for (int i = 0; i < k - 1; i++) {
    if (itemset1->elements[i] != itemset2->elements[i]) {
      return NULL;
    }
  }

  // The last elements must be different, and in ascending order
  if (itemset1->elements[k - 1] >= itemset2->elements[k - 1]) {
    return NULL;
  }

  // Create a new candidate itemset - ALLOCATE NEW MEMORY
  ItemSet *candidate = malloc(sizeof(ItemSet));
  if (candidate == NULL) {
    printf("Memory allocation failed for candidate itemset\n");
    return NULL;
  }

  candidate->size = k + 1;
  candidate->elements = malloc((k + 1) * sizeof(int));
  if (candidate->elements == NULL) {
    printf("Memory allocation failed for candidate elements\n");
    free(candidate); // Don't forget to free the already allocated memory
    return NULL;
  }

  candidate->support = 0.0;

  // Copy the k-1 common elements
  for (int i = 0; i < k - 1; i++) {
    candidate->elements[i] = itemset1->elements[i];
  }

  // Add the two different elements at the end
  candidate->elements[k - 1] = itemset1->elements[k - 1];
  candidate->elements[k] = itemset2->elements[k - 1];

  return candidate;
}

// Check if all subsets of the candidate itemset are frequent
int has_frequent_subsets(ItemSet *candidate, ItemSet *frequent_itemsets,
                         int count, int k) {
  if (candidate == NULL || frequent_itemsets == NULL || count <= 0 || k <= 0) {
    return 0;
  }

  int *subset = malloc((k) * sizeof(int));
  if (subset == NULL) {
    printf("Memory allocation failed for subset\n");
    return 0;
  }

  // Generate all subsets of size k by excluding one element at a time
  for (int i = 0; i < k + 1; i++) {
    int pos = 0;
    for (int j = 0; j < k + 1; j++) {
      if (j != i) {
        subset[pos++] = candidate->elements[j];
      }
    }

    // Check if this subset is in the frequent itemsets
    int found = 0;
    for (int j = 0; j < count; j++) {
      if (frequent_itemsets[j].size == k) {
        int match = 1;
        for (int l = 0; l < k; l++) {
          int elem_match = 0;
          for (int m = 0; m < k; m++) {
            if (subset[l] == frequent_itemsets[j].elements[m]) {
              elem_match = 1;
              break;
            }
          }
          if (!elem_match) {
            match = 0;
            break;
          }
        }
        if (match) {
          found = 1;
          break;
        }
      }
    }

    if (!found) {
      free(subset);
      return 0;
    }
  }

  free(subset);
  return 1;
}

// Check if a candidate itemset is present in a transaction
int is_subset(int *candidate, int candidate_size, int *transaction,
              int transaction_size) {
  if (candidate == NULL || transaction == NULL || candidate_size <= 0 ||
      transaction_size <= 0) {
    return 0;
  }

  int matches = 0;

  for (int i = 0; i < candidate_size; i++) {
    for (int j = 0; j < transaction_size; j++) {
      if (candidate[i] == transaction[j]) {
        matches++;
        break;
      }
    }
  }

  return (matches == candidate_size);
}

// Print a single association rule
void print_rule(AssociationRule rule) {
  printf("{");
  for (int i = 0; i < rule.antecedent_size; i++) {
    printf("%d", rule.antecedent[i]);
    if (i < rule.antecedent_size - 1) {
      printf(", ");
    }
  }
  printf("} => {");
  for (int i = 0; i < rule.consequent_size; i++) {
    printf("%d", rule.consequent[i]);
    if (i < rule.consequent_size - 1) {
      printf(", ");
    }
  }

  // Ensure confidence is within a reasonable range
  float confidence = rule.confidence;
  if (confidence > 1.0)
    confidence = 1.0;

  // Ensure support is within a reasonable range
  float support = rule.support;
  if (support > 1.0)
    support = 1.0;

  printf("} (Confidence: %.2f%%, Support: %.2f%%)\n", confidence * 100,
         support * 100);
}

void print_text_rule(AssociationRule rule) {
  load_map("map.txt");
  printf("{");
  for (int i = 0; i < rule.antecedent_size; i++) {
    printf("%s", get_item_name(rule.antecedent[i]));
    if (i < rule.antecedent_size - 1) {
      printf(", ");
    }
  }
  printf("} => {");
  for (int i = 0; i < rule.consequent_size; i++) {
    printf("%s", get_item_name(rule.consequent[i]));
    if (i < rule.consequent_size - 1) {
      printf(", ");
    }
  }

  // Ensure confidence is within a reasonable range
  float confidence = rule.confidence;
  if (confidence > 1.0)
    confidence = 1.0;

  // Ensure support is within a reasonable range
  float support = rule.support;
  if (support > 1.0)
    support = 1.0;

  printf("} (Confidence: %.2f%%, Support: %.2f%%)\n", confidence * 100,
         support * 100);
}

// Fixed version of finding itemset support
float find_itemset_support_fixed(ItemSet *itemsets, int count, int *items,
                                 int size) {
  if (size <= 0 || count <= 0 || itemsets == NULL || items == NULL) {
    return 0.0;
  }

  for (int i = 0; i < count; i++) {
    if (itemsets[i].size == size) {
      // All elements must match exactly
      int matches = 0;

      // For each element in the target itemset
      for (int j = 0; j < size; j++) {
        // Look for it in the candidate itemset
        for (int k = 0; k < itemsets[i].size; k++) {
          if (items[j] == itemsets[i].elements[k]) {
            matches++;
            break;
          }
        }
      }

      // If all elements were found and sizes match
      if (matches == size && size == itemsets[i].size) {
        return itemsets[i].support;
      }
    }
  }

  return 0.0; // Not found
}

// Generate improved association rules from an array of frequent itemsets
AssociationRule *generate_rules_fixed(ItemSet *itemsets, int itemset_count,
                                      float min_confidence, int *rule_count,
                                      ItemHashTable *unique_itemsets) {
  // Initial allocation for rules
  int max_rules = 1000; // We'll reallocate as needed
  AssociationRule *rules =
      (AssociationRule *)malloc(max_rules * sizeof(AssociationRule));
  if (rules == NULL) {
    printf("Memory allocation failed in generate_rules_fixed\n");
    *rule_count = 0;
    return NULL;
  }
  *rule_count = 0;

  // Process each itemset with size >= 2
  for (int i = 0; i < itemset_count; i++) {
    ItemSet itemset = itemsets[i];

    // Skip itemsets of size 1 (can't form rules)
    // Should never recieve a size 1, but can't hurt
    if (itemset.size < 2) {
      continue;
    }

    // Get the support of the full itemset
    float full_support = itemset.support;
    if (full_support <= 0)
      continue; // Skip itemsets with invalid support

    // Generate antecedents
    int antecedent_size = itemset.size - 1;
    // Generate all possible antecedents of this size
    int subset_count;
    int **antecedents = get_all_subsets(itemset.elements, itemset.size,
                                        antecedent_size, &subset_count);
    if (antecedents == NULL)
      continue;

    // For each antecedent, create a rule
    for (int j = 0; j < subset_count; j++) {
      int *antecedent = antecedents[j];
      if (antecedent == NULL)
        continue;

      // Find the complement of the antecedent (the consequent)
      int consequent_size;
      int *consequent =
          get_complement(itemset.elements, itemset.size, antecedent,
                         antecedent_size, &consequent_size);
      if (consequent == NULL)
        continue;

      // Find the support of the antecedent using the hashtable
      IntArray antecedent_array;
      antecedent_array.length = antecedent_size;
      antecedent_array.items = antecedent;
      float antecedent_support =
          get_support_itemset(unique_itemsets, &antecedent_array);

      // Skip if antecedent support is zero or very small to avoid division by
      // zero
      if (antecedent_support < 0.000001) {
        if (consequent != NULL)
          free(consequent);
        continue;
      }

      // Calculate confidence = support(X ∪ Y) / support(X)
      float confidence = full_support / antecedent_support;

      // Clamp confidence to valid range
      if (confidence > 1.0)
        confidence = 1.0;

      // If confidence meets or exceeds threshold, add to rules
      if (confidence >= min_confidence) {
        // Resize if needed
        if (*rule_count >= max_rules) {
          max_rules *= 2;
          AssociationRule *temp = (AssociationRule *)realloc(
              rules, max_rules * sizeof(AssociationRule));
          if (temp == NULL) {
            printf("Memory reallocation failed in generate_rules_fixed\n");
            // Clean up
            free(consequent);
            for (int k = 0; k < *rule_count; k++) {
              if (rules[k].antecedent != NULL)
                free(rules[k].antecedent);
              if (rules[k].consequent != NULL)
                free(rules[k].consequent);
            }
            free(rules);
            *rule_count = 0;
            return NULL;
          }
          rules = temp;
        }

        // Add the rule
        rules[*rule_count].antecedent =
            (int *)malloc(antecedent_size * sizeof(int));
        if (rules[*rule_count].antecedent == NULL) {
          printf("Memory allocation failed for antecedent\n");
          free(consequent);
          continue;
        }
        memcpy(rules[*rule_count].antecedent, antecedent,
               antecedent_size * sizeof(int));
        rules[*rule_count].antecedent_size = antecedent_size;

        rules[*rule_count].consequent =
            (int *)malloc(consequent_size * sizeof(int));
        if (rules[*rule_count].consequent == NULL) {
          printf("Memory allocation failed for consequent\n");
          free(rules[*rule_count].antecedent);
          free(consequent);
          continue;
        }
        memcpy(rules[*rule_count].consequent, consequent,
               consequent_size * sizeof(int));
        rules[*rule_count].consequent_size = consequent_size;

        rules[*rule_count].confidence = confidence;
        rules[*rule_count].support = full_support;

        (*rule_count)++;

        // Debug output for the first few rules
        if (*rule_count <= 5) {
          printf("DEBUG: Created rule with conf=%.4f, sup=%.4f: {", confidence,
                 full_support);
          for (int k = 0; k < antecedent_size; k++) {
            printf("%d", antecedent[k]);
            if (k < antecedent_size - 1)
              printf(", ");
          }
          printf("} => {");
          for (int k = 0; k < consequent_size; k++) {
            printf("%d", consequent[k]);
            if (k < consequent_size - 1)
              printf(", ");
          }
          printf("}\n");
        }
      }

      if (consequent != NULL)
        free(consequent);
    }

    // Free memory for antecedents
    for (int j = 0; j < subset_count; j++) {
      if (antecedents[j] != NULL)
        free(antecedents[j]);
    }
    if (antecedents != NULL)
      free(antecedents);
  }

  // Trim the rules array to the actual size
  if (*rule_count > 0) {
    AssociationRule *temp = (AssociationRule *)realloc(
        rules, (*rule_count) * sizeof(AssociationRule));
    if (temp != NULL) {
      rules = temp;
    }
  } else if (rules != NULL) {
    free(rules);
    rules = NULL;
  }

  return rules;
}
