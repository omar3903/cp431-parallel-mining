#ifndef IO_PROCESSING_H
#define IO_PROCESSING_H

#include <stdio.h>
#include "dynamic_hash_table.h"

/**
 * Get the size of a file in bytes.
 * @param filename The path to the file
 * @return The size of the file in bytes, or -1 on error
 */
long get_file_size(const char *filename);

/**
 * Read a line from a file within the specified bounds.
 * @param fp File pointer to read from
 * @param rank Current processor rank
 * @param split_points Array of file split points
 * @return Pointer to the line read, or NULL on error
 */
char *get_line(FILE *fp, int rank, long split_points[]);

/**
 * Split a file into roughly equal chunks for parallel processing.
 * @param filename The path to the file
 * @param split_points Array to store the byte positions of split points
 * @param p Number of processes
 */
void split_file(const char *filename, long split_points[], int p);

/**
 * Process a chunk of a file to find frequent items.
 * @param filename The path to the file
 * @param split_points Array of file split points
 * @param rank Current processor rank
 * @param frequent_table Hash table to store frequent items
 * @param global_support Global minimum support threshold
 * @return Number of lines processed
 */
int process_chunk(const char *filename, long split_points[], int rank,
                  HashTable *frequent_table, float global_support);

#endif /* IO_PROCESSING_H */