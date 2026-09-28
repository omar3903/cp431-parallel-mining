#include "Apriori.h"
#include "dynamic_hash_table.h"
#include "io-processing.h"
#include "itemset_hash_table.h"
#include "rules-aux.h"
#include <mpi.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

// Structure to hold performance metrics
typedef struct {
  double file_split_time;
  double frequent_items_time;
  double triangle_matrix_build_time;
  double pair_generation_time;
  double triangle_prune_time;
  double large_itemset_time;
  double rule_generation_time;
  double total_time;
  int total_transactions;
  int total_frequent_items;
  int total_frequent_pairs;
  int total_frequent_itemsets;
  int total_rules;
} PerformanceMetrics;

int main(int argc, char **argv) {
  setenv("MALLOC_CHECK_", "3", 1);
  int size, rank;
  double start_time, end_time;
  PerformanceMetrics metrics = {0};

  // Record total execution start time
  start_time = MPI_Wtime();

  MPI_Init(&argc, &argv);

  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  if (argc < 4) {
    if (rank == 0)
      fprintf(stderr,
              "Usage: %s <inputfile> <support_threshold> "
              "<confidence_threshold> <text>\n",
              argv[0]);
    return 1;
  }
  float global_support = strtof(argv[2], NULL);
  float confidence_threshold = strtof(argv[3], NULL);
  int text = strtol(argv[4], NULL, 10);

  // Split file into roughly equal sized portions for processing
  double phase_start = MPI_Wtime();
  long *split_points = NULL;
  split_points = (long *)malloc(size * sizeof(long));
  if (split_points == NULL) {
    printf("Memory allocation failed for split points\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  if (rank == 0) {
    printf("#################### \n");
    printf("Begin input processing \n");
    split_file(argv[1], split_points, size);
    printf("Split points generated \n");
    for (int i = 0; i < size; i++) {
      printf("split %i is %li \n", i, split_points[i]);
    }
    printf("#################### \n");
    metrics.file_split_time = MPI_Wtime() - phase_start;
  }

  // Send split points to all processors
  MPI_Bcast(split_points, size, MPI_LONG, 0, MPI_COMM_WORLD);

  // Broadcast file split timing to all processes
  MPI_Bcast(&metrics.file_split_time, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);

  // Each processor finds frequent ints, support and transaction counts
  phase_start = MPI_Wtime();
  HashTable local_table;
  init_table(&local_table, INT_TYPE);
  int local_transaction_count;
  local_transaction_count =
      process_chunk(argv[1], split_points, rank, &local_table, global_support);
  printf("Proc %i has finished processing %i lines in its file chunk \n", rank,
         local_transaction_count);
  metrics.frequent_items_time = MPI_Wtime() - phase_start;
  metrics.total_frequent_items = local_table.count;

  // Get total number of transactions
  // Make available on all procs using all reduce
  int total_transactions;
  MPI_Allreduce(&local_transaction_count, &total_transactions, 1, MPI_INT,
                MPI_SUM, MPI_COMM_WORLD);
  metrics.total_transactions = total_transactions;

  // Gather frequent items metrics
  int total_frequent_items;
  MPI_Reduce(&local_table.count, &total_frequent_items, 1, MPI_INT, MPI_SUM, 0,
             MPI_COMM_WORLD);

  if (rank == 0) {
    printf("#################### \n");
    printf("Total of %i transactions found \n", total_transactions);
    printf("Total of %i frequent items found (not unique)\n",
           total_frequent_items);
    printf("#################### \n");
    printf("Input processing complete \n");
    printf("#################### \n");
    printf("Beginning SON by performing Apriori on each processor \n");
  }

  MPI_Barrier(MPI_COMM_WORLD);

  // Build triangular matrix for storing pairs
  phase_start = MPI_Wtime();
  TriangularMatrix *local_tri = NULL;
  if (local_table.count > 1) { // Only build if we have at least 2 items
    local_tri = build_tri_matrix(&local_table);
    if (local_tri == NULL) {
      printf("Proc %i: Failed to build triangular matrix\n", rank);
      MPI_Abort(MPI_COMM_WORLD, 1);
    }
    metrics.triangle_matrix_build_time = MPI_Wtime() - phase_start;
    printf("Proc %i: Triangular matrix built \n", rank);
  } else {
    metrics.triangle_matrix_build_time = 0;
    printf("Proc %i: Not enough frequent items to build triangular matrix\n",
           rank);
  }

  // Set file buffer for reading the assigned chunk
  FILE *fp = fopen(argv[1], "r");
  if (fp == NULL) {
    printf("Error opening file %s\n", argv[1]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  long read_start = (rank == 0) ? 0 : split_points[rank - 1];
  long read_end = split_points[rank];

  if (read_start > 0) {
    fseek(fp, read_start, SEEK_SET);
  } else {
    rewind(fp);
  }

  // Read each line in the chunk, entering candidate pairs into triangle matrix
  phase_start = MPI_Wtime();
  char buffer[2048];
  memset(buffer, 0, sizeof(buffer));

  int pairs_generated = 0;
  int total_pairs = 0;
  if (local_tri != NULL) {
    while (ftell(fp) < read_end && fgets(buffer, sizeof(buffer), fp) != NULL) {
      int outcount;
      int *frequent_items = extract_frequent(&local_table, buffer, &outcount);

      if (frequent_items != NULL) {
        if (outcount >= 2) {
          total_pairs += check_pairs(local_tri, frequent_items, outcount);
          pairs_generated = 1;
        }

        free(frequent_items);
        frequent_items = NULL;
      }
    }
  }
  metrics.pair_generation_time = MPI_Wtime() - phase_start;

  if (pairs_generated) {
    printf("Proc %i: %d Pairs generated\n", rank, total_pairs);
  } else {
    printf("Proc %i: No pairs were generated\n", rank);
  }

  // Create list of supported pairs from triangle matrix
  phase_start = MPI_Wtime();
  int valid_pairs = 0;
  ItemSet *frequent_pairs = NULL;

  if (local_tri != NULL && pairs_generated) {
    float local_support =
        global_support * ((float)local_transaction_count / total_transactions);
    printf("Proc %i: Using local support threshold of %.6f for pruning\n", rank,
           local_support);
    frequent_pairs = prune_triangle(local_tri, local_support,
                                    local_transaction_count, &valid_pairs);
    if (frequent_pairs == NULL && valid_pairs > 0) {
      printf("Proc %i: Failed to allocate memory for frequent pairs\n", rank);
      valid_pairs = 0;
    }
  }

  metrics.triangle_prune_time = MPI_Wtime() - phase_start;
  metrics.total_frequent_pairs = valid_pairs;

  printf("Proc %i: Triangle pruned, found %d valid pairs\n", rank, valid_pairs);

  // Safely close file after pair generation
  if (fp != NULL) {
    fclose(fp);
    fp = NULL;
  }

  // Initialize array for storing all frequent itemsets (pairs, triples, etc.)
  ItemSet *all_frequent_itemsets = NULL;
  int total_itemsets = 0;
  int max_itemsets = 0;

  // Only allocate and copy if there are valid pairs
  if (valid_pairs > 0) {
    max_itemsets = valid_pairs * 10; // Initial guess
    all_frequent_itemsets = malloc(max_itemsets * sizeof(ItemSet));
    if (all_frequent_itemsets == NULL) {
      printf("Memory allocation failed for all_frequent_itemsets\n");
      // Clean up and continue with empty set
      if (frequent_pairs != NULL) {
        for (int i = 0; i < valid_pairs; i++) {
          if (frequent_pairs[i].elements != NULL) {
            free(frequent_pairs[i].elements);
          }
        }
        free(frequent_pairs);
        frequent_pairs = NULL;
      }
      valid_pairs = 0;
    } else {
      // Copy the frequent pairs to the all_frequent_itemsets array
      for (int i = 0; i < valid_pairs; i++) {
        all_frequent_itemsets[total_itemsets++] = frequent_pairs[i];
      }

      // Debug: Print some of the frequent pairs
      if (rank == 0 && valid_pairs > 0) {
        printf("DEBUG: Sample frequent pairs from Proc %d:\n", rank);
        int pairs_to_print = (valid_pairs < 5) ? valid_pairs : 5;
        for (int i = 0; i < pairs_to_print; i++) {
          printf("  Pair %d: {%d, %d}, support = %.4f\n", i,
                 frequent_pairs[i].elements[0], frequent_pairs[i].elements[1],
                 frequent_pairs[i].support);
        }
      }
    }
  }

  // Find larger itemsets (k > 2)
  phase_start = MPI_Wtime();
  int k = 2; // Current itemset size
  int prev_start_idx = 0;
  int prev_count = valid_pairs;

  // Only proceed if we have valid pairs to begin with
  if (prev_count > 0) {
    // Re-open file for larger itemset discovery
    fp = fopen(argv[1], "r");
    if (fp == NULL) {
      printf("Error opening file %s for larger itemset discovery\n", argv[1]);
      MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (read_start > 0) {
      fseek(fp, read_start, SEEK_SET);
    } else {
      rewind(fp);
    }

    while (prev_count > 0) {
      //      int new_start_idx = total_itemsets;
      int candidates_count = 0;
      ItemSet *candidates = NULL;

      // 1. Generate candidate itemsets of size k+1
      for (int i = prev_start_idx; i < prev_start_idx + prev_count - 1; i++) {
        for (int j = i + 1; j < prev_start_idx + prev_count; j++) {
          ItemSet *candidate = generate_candidate(&all_frequent_itemsets[i],
                                                  &all_frequent_itemsets[j], k);

          if (candidate != NULL) {
            if (has_frequent_subsets(candidate, all_frequent_itemsets,
                                     total_itemsets, k)) {
              // Add the candidate to our list
              ItemSet *temp =
                  realloc(candidates, (candidates_count + 1) * sizeof(ItemSet));
              if (temp == NULL) {
                printf("Memory reallocation failed for candidates\n");
                // Free current candidate since we couldn't add it
                free(candidate->elements);
                free(candidate);
                // Don't abort, just use what we have
                break;
              }
              candidates = temp;
              candidates[candidates_count].size = candidate->size;
              candidates[candidates_count].support = candidate->support;
              candidates[candidates_count].elements = candidate->elements;
              candidates[candidates_count].count = candidate->count;
              candidates_count++;
              free(candidate); // Free the container, but not the elements
                               // (copied above)
            } else {
              // Free candidate that doesn't have frequent subsets
              free(candidate->elements);
              free(candidate);
            }
          }
        }
      }

      if (candidates_count == 0) {
        if (candidates != NULL) {
          free(candidates);
          candidates = NULL;
        }
        break; // No new candidates, we're done
      }

      // Debug: Print number of candidates
      printf("Proc %i: Generated %d candidate itemsets of size %d\n", rank,
             candidates_count, k + 1);

      // 2. Count support for each candidate
      // Reset file pointer to read the chunk again
      fseek(fp, read_start, SEEK_SET);

      while (ftell(fp) < read_end &&
             fgets(buffer, sizeof(buffer), fp) != NULL) {
        int outcount;
        int *frequent_items = extract_frequent(&local_table, buffer, &outcount);

        if (frequent_items != NULL) {
          if (outcount >= k + 1) {
            for (int i = 0; i < candidates_count; i++) {
              if (is_subset(candidates[i].elements, candidates[i].size,
                            frequent_items, outcount)) {
                candidates[i].count += 1;
                candidates[i].support =
                    (float)candidates[i].count / local_transaction_count;
              }
            }
          }

          free(frequent_items);
          frequent_items = NULL;
        }
      }

      // 3. Prune candidates based on support
      int new_count = 0;
      float local_support = global_support * ((float)local_transaction_count /
                                              total_transactions);

      // Create a temporary array to hold the retained itemsets
      ItemSet *retained_itemsets = malloc(candidates_count * sizeof(ItemSet));
      if (retained_itemsets == NULL) {
        printf("Memory allocation failed for retained_itemsets\n");
        // Clean up candidates memory
        for (int i = 0; i < candidates_count; i++) {
          if (candidates[i].elements != NULL) {
            free(candidates[i].elements);
            candidates[i].elements = NULL;
          }
        }
        free(candidates);
        candidates = NULL;
        break;
      }

      for (int i = 0; i < candidates_count; i++) {
        if (candidates[i].support >= local_support) {
          // Copy the item to retained itemsets
          retained_itemsets[new_count].size = candidates[i].size;
          retained_itemsets[new_count].support = candidates[i].support;
          retained_itemsets[new_count].elements = candidates[i].elements;
          retained_itemsets[new_count].count = candidates[i].count;
          new_count++;
        } else {
          // Free memory for non-frequent candidates
          if (candidates[i].elements != NULL) {
            free(candidates[i].elements);
            candidates[i].elements = NULL;
          }
        }
      }

      // Check if we need to resize all_frequent_itemsets
      if (total_itemsets + new_count >= max_itemsets) {
        max_itemsets =
            (total_itemsets + new_count) * 2; // More aggressive resizing
        ItemSet *temp =
            realloc(all_frequent_itemsets, max_itemsets * sizeof(ItemSet));
        if (temp == NULL) {
          printf("Memory reallocation failed for all_frequent_itemsets\n");
          // Clean up retained itemsets
          for (int i = 0; i < new_count; i++) {
            if (retained_itemsets[i].elements != NULL) {
              free(retained_itemsets[i].elements);
              retained_itemsets[i].elements = NULL;
            }
          }
          free(retained_itemsets);
          free(candidates);
          candidates = NULL;
          break;
        }
        all_frequent_itemsets = temp;
      }

      // Copy the retained itemsets to all_frequent_itemsets
      for (int i = 0; i < new_count; i++) {
        all_frequent_itemsets[total_itemsets + i] = retained_itemsets[i];
      }
      total_itemsets += new_count;

      // Update for next iteration
      prev_start_idx = total_itemsets - new_count;
      prev_count = new_count;
      k++;

      // Free temporary arrays (but not the elements that were moved)
      free(retained_itemsets);
      free(candidates);
      candidates = NULL;

      printf("Proc %i: Found %d frequent itemsets of size %d\n", rank,
             new_count, k);
    }

    // Close file after larger itemset discovery
    if (fp != NULL) {
      fclose(fp);
      fp = NULL;
    }
  }

  metrics.large_itemset_time = MPI_Wtime() - phase_start;
  metrics.total_frequent_itemsets = total_itemsets;

  MPI_Barrier(MPI_COMM_WORLD); // Barrier after itemset generation
  printf("Proc %d: Itemset generation complete, %d generated \n", rank,
         total_itemsets);

  // Share frequent itemsets with master process
  int *local_itemset_counts = NULL;
  int *itemset_displacements = NULL;
  int total_all_itemsets = 0;
  ItemSet *global_itemsets = NULL;

  // First, gather the counts from all processes
  if (rank == 0) {
    local_itemset_counts = malloc(size * sizeof(int));
    if (local_itemset_counts == NULL) {
      printf("Memory allocation failed for local_itemset_counts\n");
      MPI_Abort(MPI_COMM_WORLD, 1);
    }
  }

  MPI_Gather(&total_itemsets, 1, MPI_INT, local_itemset_counts, 1, MPI_INT, 0,
             MPI_COMM_WORLD);

  // Master process prepares to receive all itemsets
  if (rank == 0) {
    // Calculate total count and displacements for MPI_Gatherv
    itemset_displacements = malloc(size * sizeof(int));
    if (itemset_displacements == NULL) {
      printf("Memory allocation failed for itemset_displacements\n");
      MPI_Abort(MPI_COMM_WORLD, 1);
    }

    int disp = 0;

    // Sum itemset counts sent by worker procs
    for (int i = 0; i < size; i++) {
      itemset_displacements[i] = disp;
      total_all_itemsets += local_itemset_counts[i];
      disp += local_itemset_counts[i];
    }

    if (total_all_itemsets > 0) {
      // Reuse local_itemsets array as the base of global_itemsets
      global_itemsets =
          realloc(all_frequent_itemsets, total_all_itemsets * sizeof(ItemSet));
      if (global_itemsets == NULL) {
        fprintf(stderr, "Failed to realloc global_itemsets\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
      }
      printf("Master will receive %d total itemsets\n", total_all_itemsets);
    }
  }

  // Note: We can't easily create an MPI datatype for ItemSet because it
  // contains a pointer For simplicity, we'll send each itemset's components
  // individually

  if (rank == 0 && total_all_itemsets > 0) {
    // Master process receives itemsets one by one
    // Merges them into it's local list of itemsets
    int current_idx = total_itemsets;

    for (int src = 1; src < size; src++) {
      printf("Master has %d itemsets, and is expecting to recieve %d from proc "
             "%d \n",
             current_idx, local_itemset_counts[src], src);
      for (int i = 0; i < local_itemset_counts[src]; i++) {
        int size_buf;
        float support_buf;
        int count_buf;

        // Receive size, support and count
        MPI_Recv(&size_buf, 1, MPI_INT, src, 0, MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);
        MPI_Recv(&support_buf, 1, MPI_FLOAT, src, 1, MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);
        MPI_Recv(&count_buf, 1, MPI_INT, src, 2, MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);

        // Check if we already have this itemset in the master array
        global_itemsets[current_idx].size = size_buf;
        global_itemsets[current_idx].support = support_buf;
        int *elements = malloc(size_buf * sizeof(int));
        MPI_Recv(elements, size_buf, MPI_INT, src, 3, MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);

        // Merge into global_itemsets or add new
        bool merged = false;
        for (int j = 0; j < current_idx; j++) {
          if (global_itemsets[j].size != size_buf)
            continue;
          bool match = true;
          for (int k = 0; k < size_buf; k++) {
            if (global_itemsets[j].elements[k] != elements[k]) {
              match = false;
              break;
            }
          }
          if (match) { // We already have this itemset, just add the number of
                       // occurences
            global_itemsets[j].count += count_buf;
            global_itemsets[j].support =
                (float)global_itemsets[j].count /
                total_transactions; // Recalculate support for an itemset
            merged = true;

            // Itemset was already present, reset current slot to accept next
            // new itemset
            global_itemsets[current_idx].size = 0;
            global_itemsets[current_idx].count = 0;
            global_itemsets[current_idx].support = 0.0;

            break;
          }
        }
        if (!merged) { // New itemset, take in all values
          global_itemsets[current_idx].size = size_buf;
          global_itemsets[current_idx].count = count_buf;
          global_itemsets[current_idx].support =
              (float)global_itemsets[current_idx].count / total_transactions;
          global_itemsets[current_idx].elements =
              malloc(size_buf * sizeof(int));
          global_itemsets[current_idx].elements = elements;
          current_idx++;
        } else {
          free(elements);
          elements = NULL;
        }
      }
    }

    // Update the actual count of received itemsets
    total_all_itemsets = current_idx;
    printf("Master has %d unique itemsets after merging from worker procs \n",
           total_all_itemsets);

    // Debug: Print some sample itemsets
    if (total_all_itemsets > 0) {
      printf("DEBUG: Sample of global itemsets:\n");
      int to_print = (total_all_itemsets < 20) ? total_all_itemsets : 20;
      for (int i = 0; i < to_print; i++) {
        printf("  Itemset %d (size %d, support %.4f): {", i,
               global_itemsets[i].size, global_itemsets[i].support);
        for (int j = 0; j < global_itemsets[i].size; j++) {
          printf("%d", global_itemsets[i].elements[j]);
          if (j < global_itemsets[i].size - 1)
            printf(", ");
        }
        printf("}\n");
      }
    }
  } else if (rank == 0) {
    printf("Master has received 0 itemsets\n");
  } else if (total_itemsets > 0) {
    // Worker processes send their itemsets
    for (int i = 0; i < total_itemsets; i++) {
      MPI_Send(&all_frequent_itemsets[i].size, 1, MPI_INT, 0, 0,
               MPI_COMM_WORLD);
      MPI_Send(&all_frequent_itemsets[i].support, 1, MPI_FLOAT, 0, 1,
               MPI_COMM_WORLD);
      MPI_Send(&all_frequent_itemsets[i].count, 1, MPI_INT, 0, 2,
               MPI_COMM_WORLD);
      MPI_Send(all_frequent_itemsets[i].elements, all_frequent_itemsets[i].size,
               MPI_INT, 0, 3, MPI_COMM_WORLD);
    }
  }

  // Place unique itemsets into hashtable to allow for o(1) support lookup
  // when making rules
  if (rank == 0) {
    ItemHashTable unique_itemsets;
    init_item_table(&unique_itemsets);
    for (int i = 0; i < total_all_itemsets; i++) {
      IntArray *key = malloc(sizeof(IntArray));
      key->length = global_itemsets[i].size;
      key->items = malloc(key->length * sizeof(int));

      for (int j = 0; j < global_itemsets[i].size; j++) {
        key->items[j] = global_itemsets[i].elements[j];
      }
      // Place itemset into hashtable, free key and items for next itemset
      insert_itemset(&unique_itemsets, key, global_itemsets[i].support);
      free(key->items);
      free(key);
    }

    // At this point, Master has a hashtable containing all frequent itemsets in
    // the dataset. This contains only unique itemsets, and each itemset has a
    // count and support associated with it. Master process generates
    // association rules from the itemsets
    phase_start = MPI_Wtime();
    int rule_count = 0;
    AssociationRule *rules = NULL;

    if (total_all_itemsets > 0) {
      // Use the fixed rule generation function
      rules = generate_rules_fixed(global_itemsets, total_all_itemsets,
                                   confidence_threshold, &rule_count,
                                   &unique_itemsets);
    }

    metrics.rule_generation_time = MPI_Wtime() - phase_start;
    metrics.total_rules = rule_count;

    printf("#################### \n");
    printf("Generated %d association rules with confidence >= %f\n", rule_count,
           confidence_threshold);
    printf("#################### \n");

    // Print top rules (limited to 10 for brevity)
    if (rule_count > 0 && text == 0) {
      int rules_to_print = (rule_count < 10) ? rule_count : 10;
      printf("Top %d association rules (numeric):\n", rules_to_print);

      for (int i = 0; i < rules_to_print; i++) {
        printf("Rule %d: ", i + 1);
        print_rule(rules[i]);
      }

      // Clean up rules
      if (rules != NULL) {
        free_rules(rules, rule_count);
        rules = NULL;
      }
    } else if (rule_count > 0 && text == 1) {
      int rules_to_print = (rule_count < 10) ? rule_count : 10;
      printf("Top %d association rules:\n", rules_to_print);

      for (int i = 0; i < rules_to_print; i++) {
        printf("Rule %c: ", i + 1);
        print_text_rule(rules[i]);
      }

      // Clean up rules
      if (rules != NULL) {
        free_rules(rules, rule_count);
        rules = NULL;
      }
    } else {
      printf("No association rules found.\n");
    }

    // Clean up global itemsets
    if (global_itemsets != NULL) {
      for (int i = 0; i < total_all_itemsets; i++) {
        if (global_itemsets[i].elements != NULL) {
          free(global_itemsets[i].elements);
          global_itemsets[i].elements = NULL;
        }
      }
      free(global_itemsets);
      global_itemsets = NULL;
    }

    if (local_itemset_counts != NULL) {
      free(local_itemset_counts);
      local_itemset_counts = NULL;
    }

    if (itemset_displacements != NULL) {
      free(itemset_displacements);
      itemset_displacements = NULL;
    }

    // Write metrics to CSV file for further analysis
    FILE *metrics_file = fopen("performance_metrics.csv", "w");
    if (metrics_file != NULL) {
      fprintf(metrics_file, "Metric,Value\n");
      fprintf(metrics_file, "Total execution time (s),%.3f\n",
              metrics.total_time);
      fprintf(metrics_file, "File splitting time (s),%.3f\n",
              metrics.file_split_time);
      fprintf(metrics_file, "Frequent items mining time (s),%.3f\n",
              metrics.frequent_items_time);
      fprintf(metrics_file, "Triangle matrix build time (s),%.3f\n",
              metrics.triangle_matrix_build_time);
      fprintf(metrics_file, "Pair generation time (s),%.3f\n",
              metrics.pair_generation_time);
      fprintf(metrics_file, "Triangle pruning time (s),%.3f\n",
              metrics.triangle_prune_time);
      fprintf(metrics_file, "Large itemset mining time (s),%.3f\n",
              metrics.large_itemset_time);
      fprintf(metrics_file, "Rule generation time (s),%.3f\n",
              metrics.rule_generation_time);
      fprintf(metrics_file, "Total transactions,%d\n",
              metrics.total_transactions);
      fprintf(metrics_file, "Total frequent items,%d\n", total_frequent_items);
      fprintf(metrics_file, "Total frequent pairs,%d\n",
              metrics.total_frequent_pairs);
      fprintf(metrics_file, "Total frequent itemsets,%d\n",
              metrics.total_frequent_itemsets);
      fprintf(metrics_file, "Total association rules,%d\n",
              metrics.total_rules);
      fclose(metrics_file);
    }

    // Calculate total time before cleanup
    end_time = MPI_Wtime();
    metrics.total_time = end_time - start_time;

    // Output performance metrics
    if (rank == 0) {
      printf("\n#################### \n");
      printf("Performance Metrics:\n");
      printf("Total execution time: %.3f seconds\n", metrics.total_time);
      printf("File splitting time: %.3f seconds (%.2f%%)\n",
             metrics.file_split_time,
             (metrics.file_split_time / metrics.total_time) * 100);
      printf("Frequent items mining time: %.3f seconds (%.2f%%)\n",
             metrics.frequent_items_time,
             (metrics.frequent_items_time / metrics.total_time) * 100);
      printf("Triangle matrix build time: %.3f seconds (%.2f%%)\n",
             metrics.triangle_matrix_build_time,
             (metrics.triangle_matrix_build_time / metrics.total_time) * 100);
      printf("Pair generation time: %.3f seconds (%.2f%%)\n",
             metrics.pair_generation_time,
             (metrics.pair_generation_time / metrics.total_time) * 100);
      printf("Triangle pruning time: %.3f seconds (%.2f%%)\n",
             metrics.triangle_prune_time,
             (metrics.triangle_prune_time / metrics.total_time) * 100);
      printf("Large itemset mining time: %.3f seconds (%.2f%%)\n",
             metrics.large_itemset_time,
             (metrics.large_itemset_time / metrics.total_time) * 100);
      printf("Rule generation time: %.3f seconds (%.2f%%)\n",
             metrics.rule_generation_time,
             (metrics.rule_generation_time / metrics.total_time) * 100);
      printf("\n");
      printf("Dataset statistics:\n");
      printf("Total transactions: %d\n", metrics.total_transactions);
      printf("Total frequent items: %d\n", total_frequent_items);
      printf("Total frequent pairs: %d\n", metrics.total_frequent_pairs);
      printf("Total frequent itemsets: %d\n", metrics.total_frequent_itemsets);
      printf("Total association rules: %d\n", metrics.total_rules);
      printf("#################### \n");
    }
  }

  // Clean up all resources before MPI_Finalize
  // First, clean up local resources with thorough null checks
  if (all_frequent_itemsets != NULL) {
    for (int i = 0; i < total_itemsets; i++) {
      if (all_frequent_itemsets[i].elements != NULL) {
        free(all_frequent_itemsets[i].elements);
        all_frequent_itemsets[i].elements = NULL;
      }
    }
    free(all_frequent_itemsets);
    all_frequent_itemsets = NULL;
  }

  // Free frequent_pairs array if it was allocated
  // Note: The elements were already copied to all_frequent_itemsets, so we
  // just free the array
  if (frequent_pairs != NULL) {
    free(frequent_pairs);
    frequent_pairs = NULL;
  }

  // Clean up the triangular matrix
  if (local_tri != NULL) {
    if (local_tri->matrix != NULL) {
      free(local_tri->matrix);
      local_tri->matrix = NULL;
    }
    if (local_tri->item_to_index_map != NULL) {
      free(local_tri->item_to_index_map);
      local_tri->item_to_index_map = NULL;
    }
    if (local_tri->index_to_item_map != NULL) {
      free(local_tri->index_to_item_map);
      local_tri->index_to_item_map = NULL;
    }
    free(local_tri);
    local_tri = NULL;
  }

  // Free the hash table
  free_table(&local_table);

  // Free the split points array
  if (split_points != NULL) {
    free(split_points);
    split_points = NULL;
  }

  // Ensure file is closed
  if (fp != NULL) {
    fclose(fp);
    fp = NULL;
  }

  // Final barrier to ensure all processes are done with cleanup
  MPI_Barrier(MPI_COMM_WORLD);

  MPI_Finalize();
  return 0;
}
