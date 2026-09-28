#ifndef RULES_AUX_H
#define RULES_AUX_H

#include "Apriori.h"
#include "itemset_hash_table.h"

ItemSet *generate_candidate(ItemSet *itemset1, ItemSet *itemset2, int k);
int has_frequent_subsets(ItemSet *candidate, ItemSet *frequent_itemsets,
                         int count, int k);
int is_subset(int *candidate, int candidate_size, int *transaction,
              int transaction_size);
void print_rule(AssociationRule rule);
float find_itemset_support_fixed(ItemSet *itemsets, int count, int *items,
                                 int size);
AssociationRule *generate_rules_fixed(ItemSet *itemsets, int itemset_count,
                                      float min_confidence, int *rule_count,
                                      ItemHashTable *unique_itemsets);
void print_text_rule(AssociationRule rule);

#endif
