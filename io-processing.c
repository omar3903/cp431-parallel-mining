#include "dynamic_hash_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define BUF_SIZE 65536

long get_file_size(const char *filename) {
  // Uses the stat() system call to return the size of filename in bytes
  if (filename == NULL) {
    return -1;
  }
  
  struct stat st;

  if (stat(filename, &st) == 0) {
    return st.st_size;
  } else {
    perror("Failed to get filesize");
    return -1;
  }
}

char *get_line(FILE *fp, int rank, long split_points[]) {
  if (fp == NULL || split_points == NULL) {
    return NULL;
  }
  
  static char buffer[2048];
  
  if (ftell(fp) < split_points[rank]) {
    return fgets(buffer, sizeof(buffer), fp);
  } else {
    perror("file reading too far");
    return NULL;
  }
}

void split_file(const char *filename, long split_points[], int p) {
  // Fill array split_points with byte values
  // Creates split points based on the number of processes
  if (filename == NULL || split_points == NULL || p <= 0) {
    printf("Invalid arguments to split_file\n");
    return;
  }

  // Get an estimate of each chunk size
  long fsize = get_file_size(filename);
  if (fsize <= 0) {
    printf("Error: Invalid file size %ld\n", fsize);
    // Set all split points to 0 to prevent segfaults
    for (int i = 0; i < p; i++) {
      split_points[i] = 0;
    }
    return;
  }
  
  int chunk_size = fsize / p;
  if (chunk_size <= 0) {
    printf("Warning: Very small file or too many processes - chunk size is %d bytes\n", chunk_size);
    // Handle tiny files or many processes by giving all data to rank 0
    for (int i = 0; i < p - 1; i++) {
      split_points[i] = fsize;
    }
    split_points[p - 1] = fsize;
    return;
  }

  // Create split points
  FILE *fp = fopen(filename, "r");
  if (fp == NULL) {
    printf("Error opening file %s in split_file\n", filename);
    // Set all split points to prevent segfaults
    for (int i = 0; i < p; i++) {
      split_points[i] = fsize;
    }
    return;
  }
  
  for (int i = 0; i < p - 1; i++) {
    long pos = (i + 1) * chunk_size;
    if (fseek(fp, pos, SEEK_SET) != 0) {
      printf("Error seeking in file at position %ld\n", pos);
      // For safety, set remaining split points to file size
      for (int j = i; j < p; j++) {
        split_points[j] = fsize;
      }
      fclose(fp);
      return;
    }

    // Skip to start of next line to ensure we don't split mid-line
    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
      // Reached EOF
      split_points[i] = ftell(fp);
    } else {
      split_points[i] = ftell(fp);
    }
  }
  split_points[p - 1] = fsize;

  fclose(fp);
  printf("filesize of %li determined \n", fsize);
}

int process_chunk(const char *filename, long split_points[], int rank,
                  HashTable *frequent_table, float global_support) {
  // Process filename from split_points[rank] to split_points[rank + 1]
  // Returns the number of lines processed
  // Fills hashtable with frequent and unique integers as well as their support
  if (filename == NULL || split_points == NULL || frequent_table == NULL || rank < 0) {
    return 0;
  }

  int lines = 0;
  HashTable local_table;
  init_table(&local_table, INT_TYPE);
  if (local_table.entries == NULL) {
    printf("Failed to initialize local hash table\n");
    return 0;
  }

  // Cluster file system allows concurrent reads
  FILE *fp = fopen(filename, "r");
  if (fp == NULL) {
    printf("Error opening file %s in process_chunk\n", filename);
    free_table(&local_table);
    return 0;
  }
  
  long fstart = 0;
  if (rank != 0) {
    fstart = split_points[rank - 1];
    if (fseek(fp, fstart, SEEK_SET) != 0) {
      printf("Error seeking to position %ld in file\n", fstart);
      fclose(fp);
      free_table(&local_table);
      return 0;
    }
  }

  long fend = split_points[rank];
  printf("Proc %i reading from %li to %li \n", rank, fstart, fend);
  fflush(stdout);

  // Process file chunk to extract unique integers
  // Fills local_table with all unique integers and their frequency
  char buffer[2048];
  while (ftell(fp) < fend && fgets(buffer, sizeof(buffer), fp) != NULL) {
    lines++;

    // Tokenize line for hashing
    char *token;
    token = strtok(buffer, " \t\r\n");

    while (token != NULL) {
      char *endptr;
      long num = strtol(token, &endptr, 10); // Convert string read into a number
      if (*endptr == '\0') {
        insert(&local_table, (int)num); // Insert number into hashtable
      }
      token = strtok(NULL, " \t\r\n");
    }
    memset(buffer, 0, sizeof(buffer));
  }

  // Local support threshold is based on the percentage of the file that this
  // proc is responsible for.
  long filesize = get_file_size(filename);
  if (filesize <= 0) {
    printf("Error: Invalid file size %ld in process_chunk\n", filesize);
    fclose(fp);
    free_table(&local_table);
    return 0;
  }
  
  float local_support = global_support * ((float)(fend - fstart) / filesize);
  printf("Proc %i has a local support of %f \n", rank, local_support);

  /* Find support for each unique int, adding it to the output table if high
   * enough. We calculate local support based on how many bytes of the file
   * this proc is handling. Ever so slightly less accurate than line numbers
   * but easier for us to do, and more efficient
   */
  for (int i = 0; i < local_table.size; i++) {
    if (local_table.entries[i].occupied) {
      float support = (float)local_table.entries[i].value.i / lines;
      if (support >= local_support) {
        merge_exact(frequent_table, local_table.entries[i].key,
                   local_table.entries[i].value);
      }
    }
  }
  
  // Clean up
  fclose(fp);
  free_table(&local_table);
  
  return lines; // Return # of transactions processed in local chunk
}