#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "fat32.h"

FAT32 fs = {0};

static int is_end_of_directory(const DirEntry *entry) {
    return entry->DIR_Name[0] == 0x00;
}

static int is_deleted(const DirEntry *entry) {
    return entry->DIR_Name[0] == 0xE5;
}

static int is_lfn(const DirEntry *entry) {
    return (entry->DIR_Attr & 0x0F) == 0x0F;
}

uint32_t entry_first_cluster(const DirEntry *entry) {
    return ((uint32_t)entry->DIR_FstClusHI << 16) | entry->DIR_FstClusLO;
}

uint32_t first_sector_of_cluster(uint32_t cluster) {
    return ((cluster - 2) * fs.bpb.SecPerClus) + fs.first_data_sector;
}

uint64_t cluster_to_offset(uint32_t cluster) {
    uint32_t sector = first_sector_of_cluster(cluster);
    return (uint64_t)sector * fs.bpb.BytsPerSec;
}

uint32_t get_fat_entry(uint32_t cluster) {
    uint64_t fat_offset = (uint64_t)fs.fat_begin_lba * fs.bpb.BytsPerSec + (cluster * 4);
    uint32_t value = 0;

    fseek(fs.fp, fat_offset, SEEK_SET);
    fread(&value, sizeof(uint32_t), 1, fs.fp);

    value &= 0x0FFFFFFF;
    return value;
}

void format_filename(const uint8_t raw[11], char *out) {
    char name[9];
    char ext[4];

    memcpy(name, raw, 8);
    memcpy(ext, raw + 8, 3);
    name[8] = '\0';
    ext[3] = '\0';

    for (int i = 7; i >= 0 && name[i] == ' '; i--) {
        name[i] = '\0';
    }
    for (int i = 2; i >= 0 && ext[i] == ' '; i--) {
        ext[i] = '\0';
    }

    if (ext[0] != '\0') {
        snprintf(out, MAX_NAME, "%s.%s", name, ext);
    } else {
        snprintf(out, MAX_NAME, "%s", name);
    }
}

static void to_fat_name(const char *input, char fatname[11]) {
    memset(fatname, ' ', 11);

    const char *dot = strchr(input, '.');
    if (dot) {
        int name_len = (int)(dot - input);
        if (name_len > 8) name_len = 8;
        for (int i = 0; i < name_len; i++) {
            fatname[i] = (char)toupper((unsigned char)input[i]);
        }

        dot++;
        int ext_len = (int)strlen(dot);
        if (ext_len > 3) ext_len = 3;
        for (int i = 0; i < ext_len; i++) {
            fatname[8 + i] = (char)toupper((unsigned char)dot[i]);
        }
    } else {
        int len = (int)strlen(input);
        if (len > 8) len = 8;
        for (int i = 0; i < len; i++) {
            fatname[i] = (char)toupper((unsigned char)input[i]);
        }
    }
}

int mount_fat32(const char *image_path) {
    fs.fp = fopen(image_path, "rb+");
    if (!fs.fp) {
        perror("Failed to open FAT32 image");
        return 0;
    }

    fseek(fs.fp, 0, SEEK_SET);
    if (fread(&fs.bpb, sizeof(BPB_FAT32), 1, fs.fp) != 1) {
        perror("Failed to read boot sector");
        fclose(fs.fp);
        fs.fp = NULL;
        return 0;
    }

    fs.fat_begin_lba = fs.bpb.RsvdSecCnt;
    fs.first_data_sector = fs.bpb.RsvdSecCnt + (fs.bpb.NumFATs * fs.bpb.FATSz32);
    fs.cwd_cluster = fs.bpb.RootClus;
    strcpy(fs.cwd_path, "/");
    fs.mounted = 1;

    return 1;
}

void unmount_fat32(void) {
    if (fs.fp) {
        fclose(fs.fp);
        fs.fp = NULL;
    }
    fs.mounted = 0;
}

void print_info(void) {
    if (!fs.mounted) {
        printf("No image mounted.\n");
        return;
    }

    printf("FAT32 Info:\n");
    printf("Bytes Per Sector      : %u\n", fs.bpb.BytsPerSec);
    printf("Sectors Per Cluster   : %u\n", fs.bpb.SecPerClus);
    printf("Reserved Sector Count : %u\n", fs.bpb.RsvdSecCnt);
    printf("Number of FATs        : %u\n", fs.bpb.NumFATs);
    printf("FAT Size 32           : %u\n", fs.bpb.FATSz32);
    printf("Root Cluster          : %u\n", fs.bpb.RootClus);
    printf("First Data Sector     : %u\n", fs.first_data_sector);
}

void list_directory(uint32_t cluster) {
    if (!fs.mounted) {
        printf("No image mounted.\n");
        return;
    }

    uint32_t current = cluster;

    while (current < FAT32_EOC) {
        uint64_t offset = cluster_to_offset(current);
        uint32_t entries_per_cluster = (fs.bpb.BytsPerSec * fs.bpb.SecPerClus) / sizeof(DirEntry);

        fseek(fs.fp, offset, SEEK_SET);

        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            DirEntry entry;
            fread(&entry, sizeof(DirEntry), 1, fs.fp);

            if (is_end_of_directory(&entry)) {
                return;
            }
            if (is_deleted(&entry) || is_lfn(&entry)) {
                continue;
            }
            if (entry.DIR_Attr == 0x08) {
                continue;
            }

            char name[MAX_NAME];
            format_filename(entry.DIR_Name, name);

            if (entry.DIR_Attr & ATTR_DIRECTORY) {
                printf("[DIR]  %s\n", name);
            } else {
                printf("[FILE] %-20s %u bytes\n", name, entry.DIR_FileSize);
            }
        }

        current = get_fat_entry(current);
    }
}

int find_entry_in_directory(uint32_t dir_cluster, const char *name, DirEntry *out_entry) {
    char fatname[11];
    to_fat_name(name, fatname);

    uint32_t current = dir_cluster;

    while (current < FAT32_EOC) {
        uint64_t offset = cluster_to_offset(current);
        uint32_t entries_per_cluster = (fs.bpb.BytsPerSec * fs.bpb.SecPerClus) / sizeof(DirEntry);

        fseek(fs.fp, offset, SEEK_SET);

        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            DirEntry entry;
            fread(&entry, sizeof(DirEntry), 1, fs.fp);

            if (is_end_of_directory(&entry)) {
                return 0;
            }
            if (is_deleted(&entry) || is_lfn(&entry)) {
                continue;
            }

            if (memcmp(entry.DIR_Name, fatname, 11) == 0) {
                if (out_entry) {
                    *out_entry = entry;
                }
                return 1;
            }
        }

        current = get_fat_entry(current);
    }

    return 0;
}

int change_directory(const char *path) {
    if (!fs.mounted) {
        printf("No image mounted.\n");
        return 0;
    }

    if (strcmp(path, "/") == 0) {
        fs.cwd_cluster = fs.bpb.RootClus;
        strcpy(fs.cwd_path, "/");
        return 1;
    }

    if (strcmp(path, ".") == 0) {
        return 1;
    }

    if (strcmp(path, "..") == 0) {
        if (strcmp(fs.cwd_path, "/") == 0) {
            return 1;
        }

        DirEntry parent;
        if (!find_entry_in_directory(fs.cwd_cluster, "..", &parent)) {
            fs.cwd_cluster = fs.bpb.RootClus;
            strcpy(fs.cwd_path, "/");
            return 1;
        }

        uint32_t parent_cluster = entry_first_cluster(&parent);
        if (parent_cluster == 0) {
            parent_cluster = fs.bpb.RootClus;
        }
        fs.cwd_cluster = parent_cluster;

        char *last_slash = strrchr(fs.cwd_path, '/');
        if (last_slash && last_slash != fs.cwd_path) {
            *last_slash = '\0';
        } else {
            strcpy(fs.cwd_path, "/");
        }

        return 1;
    }

    DirEntry entry;
    if (!find_entry_in_directory(fs.cwd_cluster, path, &entry)) {
        printf("cd: directory not found: %s\n", path);
        return 0;
    }

    if (!(entry.DIR_Attr & ATTR_DIRECTORY)) {
        printf("cd: not a directory: %s\n", path);
        return 0;
    }

    uint32_t next_cluster = entry_first_cluster(&entry);
    if (next_cluster == 0) {
        next_cluster = fs.bpb.RootClus;
    }

    fs.cwd_cluster = next_cluster;

    if (strcmp(fs.cwd_path, "/") == 0) {
        snprintf(fs.cwd_path, sizeof(fs.cwd_path), "/%s", path);
    } else {
        strncat(fs.cwd_path, "/", sizeof(fs.cwd_path) - strlen(fs.cwd_path) - 1);
        strncat(fs.cwd_path, path, sizeof(fs.cwd_path) - strlen(fs.cwd_path) - 1);
    }

    return 1;
}

void print_prompt(void) {
    printf("fat32:%s$ ", fs.cwd_path);
}
