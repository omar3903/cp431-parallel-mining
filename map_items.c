#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE_LEN 4096
#define MAX_ITEMS 10000
#define HASH_SIZE 10007  // A prime number for better distribution

typedef struct Node {
    char* key;
    int value;
    struct Node* next;
} Node;

Node* hashmap[HASH_SIZE];
char* reverse_map[MAX_ITEMS];
int current_id = 0;

unsigned int hash(const char* s) {
    unsigned int h = 0;
    while (*s) {
        h = (h * 31) + *s++;
    }
    return h % HASH_SIZE;
}

int get_or_assign_id(const char* item) {
    unsigned int index = hash(item);
    Node* node = hashmap[index];

    while (node) {
        if (strcmp(node->key, item) == 0) {
            return node->value;
        }
        node = node->next;
    }

    // Not found, create new entry
    Node* new_node = malloc(sizeof(Node));
    new_node->key = strdup(item);
    new_node->value = current_id;

    new_node->next = hashmap[index];
    hashmap[index] = new_node;

    reverse_map[current_id] = new_node->key;
    return current_id++;
}

void free_hashmap() {
    for (int i = 0; i < HASH_SIZE; i++) {
        Node* node = hashmap[i];
        while (node) {
            Node* temp = node;
            node = node->next;
            free(temp->key);
            free(temp);
        }
    }
}

int main(int argc, char **argv) {
    FILE* in = fopen(argv[1], "r");
    FILE* out = fopen("ds4_numeric.txt", "w");
    FILE* map = fopen("map.txt", "w");

    if (in == NULL) {
        perror("File error");
        return 1;
    }

    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), in)) {
        char* token = strtok(line, " \n");
        while (token) {
            int id = get_or_assign_id(token);
            fprintf(out, "%d ", id);
            token = strtok(NULL, " \n");
        }
        fprintf(out, "\n");
    }

    for (int i = 0; i < current_id; i++) {
        fprintf(map, "%d %s\n", i, reverse_map[i]);
    }

    fclose(in);
    fclose(out);
    fclose(map);
    free_hashmap();
    return 0;
}
