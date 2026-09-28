#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ITEMS 10000

// Global array to store item names
char* reverse_map[MAX_ITEMS];

// Load the ID-to-name map from "map.txt"
void load_map(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        perror("Error opening map file");
        exit(1);
    }

    int id;
    char item[256];
    while (fscanf(f, "%d %s", &id, item) == 2) {
        reverse_map[id] = strdup(item);
    }

    fclose(f);
}

// Get the item name for a given ID
const char* get_item_name(int id) {
    if (id < 0 || id >= MAX_ITEMS || reverse_map[id] == NULL) {
        return "UNKNOWN";
    }
    return reverse_map[id];
}

// Format an association rule for output: e.g. {39, 200} -> {20}
//void print_rule(int* lhs, int lhs_count, int* rhs, int rhs_count) {
//    printf("{");
//    for (int i = 0; i < lhs_count; i++) {
//        printf("%s", get_item_name(lhs[i]));
//        if (i < lhs_count - 1) printf(", ");
//    }
//    printf("} -> {");
//    for (int i = 0; i < rhs_count; i++) {
//        printf("%s", get_item_name(rhs[i]));
//        if (i < rhs_count - 1) printf(", ");
//    }
//    printf("}\n");
//}
