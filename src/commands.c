#include <stdio.h>
#include <string.h>
#include "fat32.h"

int shell_running = 1;

static void print_help(void) {
    printf("Supported commands:\n");
    printf("  info           Show FAT32 metadata\n");
    printf("  ls             List current directory\n");
    printf("  cd <dir>       Change directory\n");
    printf("  pwd            Print current path\n");
    printf("  help           Show commands\n");
    printf("  exit           Exit shell\n");
    printf("\n");
    printf("Placeholders for later:\n");
    printf("  mkdir creat open close lsof lseek read write mv rm rmdir\n");
}

void execute_command(char *tokens[], int count) {
    if (count == 0) {
        return;
    }

    if (strcmp(tokens[0], "info") == 0) {
        print_info();
    }
    else if (strcmp(tokens[0], "ls") == 0) {
        list_directory(fs.cwd_cluster);
    }
    else if (strcmp(tokens[0], "cd") == 0) {
        if (count < 2) {
            printf("Usage: cd <directory>\n");
            return;
        }
        change_directory(tokens[1]);
    }
    else if (strcmp(tokens[0], "pwd") == 0) {
        printf("%s\n", fs.cwd_path);
    }
    else if (strcmp(tokens[0], "help") == 0) {
        print_help();
    }
    else if (strcmp(tokens[0], "mkdir") == 0 ||
             strcmp(tokens[0], "creat") == 0 ||
             strcmp(tokens[0], "open") == 0 ||
             strcmp(tokens[0], "close") == 0 ||
             strcmp(tokens[0], "lsof") == 0 ||
             strcmp(tokens[0], "lseek") == 0 ||
             strcmp(tokens[0], "read") == 0 ||
             strcmp(tokens[0], "write") == 0 ||
             strcmp(tokens[0], "mv") == 0 ||
             strcmp(tokens[0], "rm") == 0 ||
             strcmp(tokens[0], "rmdir") == 0) {
        printf("%s: not implemented yet\n", tokens[0]);
    }
    else if (strcmp(tokens[0], "exit") == 0) {
        shell_running = 0;
    }
    else {
        printf("Unknown command: %s\n", tokens[0]);
    }
}
