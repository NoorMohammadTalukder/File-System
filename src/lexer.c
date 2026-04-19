#include <string.h>
#include "lexer.h"
#include "fat32.h"

int tokenize(char *input, char *tokens[]) {
    int count = 0;
    char *token = strtok(input, " \t\n");

    while (token != NULL && count < MAX_TOKENS - 1) {
        tokens[count++] = token;
        token = strtok(NULL, " \t\n");
    }

    tokens[count] = NULL;
    return count;
}
