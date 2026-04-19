#include <stdio.h>
#include <string.h>
#include "fat32.h"
#include "lexer.h"

int main(int argc, char *argv[]) {
    const char *image_path = "fat32.img";

    if (argc >= 2) {
        image_path = argv[1];
    }

    if (!mount_fat32(image_path)) {
        return 1;
    }

    printf("Mounted FAT32 image: %s\n", image_path);
    printf("Type 'help' for available commands.\n");

    char input[MAX_INPUT];
    char *tokens[MAX_TOKENS];

    while (shell_running) {
        print_prompt();

        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }

        int count = tokenize(input, tokens);
        execute_command(tokens, count);
    }

    unmount_fat32();
    printf("Unmounted FAT32 image.\n");
    return 0;
}
