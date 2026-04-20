#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "fat32.h"

FAT32 fs = {0};

/* ================================================================
   Internal helpers (not exposed in header)
   ================================================================ */

static int is_end_of_directory(const DirEntry *entry) {
    return entry->DIR_Name[0] == 0x00;
}

static int is_deleted(const DirEntry *entry) {
    return entry->DIR_Name[0] == 0xE5;
}

static int is_lfn(const DirEntry *entry) {
    return (entry->DIR_Attr & 0x0F) == 0x0F;
}

/* ================================================================
   Cluster / offset arithmetic
   ================================================================ */

uint32_t first_sector_of_cluster(uint32_t cluster) {
    return ((cluster - 2) * fs.bpb.SecPerClus) + fs.first_data_sector;
}

uint64_t cluster_to_offset(uint32_t cluster) {
    return (uint64_t)first_sector_of_cluster(cluster) * fs.bpb.BytsPerSec;
}

/* ================================================================
   FAT table read / write
   ================================================================ */

uint32_t get_fat_entry(uint32_t cluster) {
    uint64_t fat_offset = (uint64_t)fs.fat_begin_lba * fs.bpb.BytsPerSec
                        + (uint64_t)cluster * 4;
    uint32_t value = 0;
    fseek(fs.fp, fat_offset, SEEK_SET);
    fread(&value, sizeof(uint32_t), 1, fs.fp);
    return value & 0x0FFFFFFF;
}

/*
 * Write 'value' into every FAT copy for 'cluster'.
 * Preserves the top 4 bits of the existing FAT entry (Microsoft spec).
 */
void set_fat_entry(uint32_t cluster, uint32_t value) {
    value &= 0x0FFFFFFF;
    for (uint8_t f = 0; f < fs.bpb.NumFATs; f++) {
        uint64_t offset = (uint64_t)(fs.fat_begin_lba + (uint32_t)f * fs.bpb.FATSz32)
                        * fs.bpb.BytsPerSec
                        + (uint64_t)cluster * 4;
        uint32_t current = 0;
        fseek(fs.fp, offset, SEEK_SET);
        fread(&current, 4, 1, fs.fp);
        current = (current & 0xF0000000) | value;
        fseek(fs.fp, offset, SEEK_SET);
        fwrite(&current, 4, 1, fs.fp);
    }
    fflush(fs.fp);
}

/*
 * Find a free cluster (FAT value == 0), mark it as end-of-chain,
 * link it from prev_cluster (if non-zero), zero its data, return it.
 * Returns 0 on failure.
 */
uint32_t allocate_cluster(uint32_t prev_cluster) {
    uint32_t total_clusters = (fs.bpb.TotSec32 - fs.first_data_sector)
                            / fs.bpb.SecPerClus;

    for (uint32_t c = 2; c < total_clusters + 2; c++) {
        if (get_fat_entry(c) == FAT32_FREE) {
            /* Mark as end-of-chain */
            set_fat_entry(c, 0x0FFFFFFF);

            /* Link from previous cluster in chain */
            if (prev_cluster != 0) {
                set_fat_entry(prev_cluster, c);
            }

            /* Zero the cluster's data area */
            uint32_t cluster_bytes = (uint32_t)fs.bpb.BytsPerSec * fs.bpb.SecPerClus;
            uint8_t *zeros = calloc(cluster_bytes, 1);
            if (zeros) {
                fseek(fs.fp, cluster_to_offset(c), SEEK_SET);
                fwrite(zeros, 1, cluster_bytes, fs.fp);
                free(zeros);
                fflush(fs.fp);
            }
            return c;
        }
    }
    return 0; /* disk full */
}

/*
 * Walk the cluster chain starting at start_cluster, setting every
 * entry back to 0 (free).
 */
void free_cluster_chain(uint32_t start_cluster) {
    uint32_t current = start_cluster;
    while (current >= 2 && current < FAT32_EOC) {
        uint32_t next = get_fat_entry(current);
        set_fat_entry(current, FAT32_FREE);
        current = next;
    }
}

/* ================================================================
   Filename helpers
   ================================================================ */

/*
 * Convert an 11-byte FAT "8.3" name to a printable NUL-terminated string.
 */
void format_filename(const uint8_t raw[11], char *out) {
    char name[9];
    char ext[4];

    memcpy(name, raw, 8);
    memcpy(ext, raw + 8, 3);
    name[8] = '\0';
    ext[3]  = '\0';

    for (int i = 7; i >= 0 && name[i] == ' '; i--) name[i] = '\0';
    for (int i = 2; i >= 0 && ext[i]  == ' '; i--) ext[i]  = '\0';

    if (ext[0] != '\0') {
        snprintf(out, MAX_NAME, "%s.%s", name, ext);
    } else {
        snprintf(out, MAX_NAME, "%s", name);
    }
}

/*
 * Convert a user-supplied name to an 11-byte FAT 8.3 field (space-padded,
 * upper-case).  Exposed in the header so commands.c can call it too.
 */
void to_fat_name(const char *input, char fatname[11]) {
    memset(fatname, ' ', 11);
    const char *dot = strchr(input, '.');
    if (dot) {
        int name_len = (int)(dot - input);
        if (name_len > 8) name_len = 8;
        for (int i = 0; i < name_len; i++)
            fatname[i] = (char)toupper((unsigned char)input[i]);

        dot++;
        int ext_len = (int)strlen(dot);
        if (ext_len > 3) ext_len = 3;
        for (int i = 0; i < ext_len; i++)
            fatname[8 + i] = (char)toupper((unsigned char)dot[i]);
    } else {
        int len = (int)strlen(input);
        if (len > 8) len = 8;
        for (int i = 0; i < len; i++)
            fatname[i] = (char)toupper((unsigned char)input[i]);
    }
}

uint32_t entry_first_cluster(const DirEntry *entry) {
    return ((uint32_t)entry->DIR_FstClusHI << 16) | entry->DIR_FstClusLO;
}

/* ================================================================
   FAT timestamps
   ================================================================ */

/* Return current date packed into FAT32 format: bits[15:9]=year-1980,
   [8:5]=month, [4:0]=day. */
uint16_t fat_date(void) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    return (uint16_t)(((tm->tm_year - 80) << 9)
                    | ((tm->tm_mon + 1)   << 5)
                    |   tm->tm_mday);
}

/* Return current time packed into FAT32 format: bits[15:11]=hour,
   [10:5]=minute, [4:0]=second/2. */
uint16_t fat_time(void) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    return (uint16_t)((tm->tm_hour << 11)
                    | (tm->tm_min  <<  5)
                    | (tm->tm_sec  /   2));
}

/* ================================================================
   Directory scanning
   ================================================================ */

/*
 * Print every valid entry under 'cluster'.
 */
void list_directory(uint32_t cluster) {
    if (!fs.mounted) { printf("No image mounted.\n"); return; }

    uint32_t current = cluster;
    while (current < FAT32_EOC) {
        uint64_t offset = cluster_to_offset(current);
        uint32_t epc = ((uint32_t)fs.bpb.BytsPerSec * fs.bpb.SecPerClus)
                      / sizeof(DirEntry);
        fseek(fs.fp, offset, SEEK_SET);

        for (uint32_t i = 0; i < epc; i++) {
            DirEntry entry;
            fread(&entry, sizeof(DirEntry), 1, fs.fp);

            if (is_end_of_directory(&entry)) return;
            if (is_deleted(&entry) || is_lfn(&entry))   continue;
            if (entry.DIR_Attr == ATTR_VOLUME_ID)        continue;

            char name[MAX_NAME];
            format_filename(entry.DIR_Name, name);

            if (entry.DIR_Attr & ATTR_DIRECTORY)
                printf("[DIR]  %s\n", name);
            else
                printf("[FILE] %-20s %u bytes\n", name, entry.DIR_FileSize);
        }
        current = get_fat_entry(current);
    }
}

/*
 * Search dir_cluster for 'name' (case-insensitive 8.3).
 * Fills *out_entry if found.  Returns 1 on success, 0 on failure.
 */
int find_entry_in_directory(uint32_t dir_cluster, const char *name,
                             DirEntry *out_entry) {
    char fatname[11];
    to_fat_name(name, fatname);

    uint32_t current = dir_cluster;
    while (current < FAT32_EOC) {
        uint64_t base = cluster_to_offset(current);
        uint32_t epc  = ((uint32_t)fs.bpb.BytsPerSec * fs.bpb.SecPerClus)
                       / sizeof(DirEntry);
        fseek(fs.fp, base, SEEK_SET);

        for (uint32_t i = 0; i < epc; i++) {
            DirEntry e;
            fread(&e, sizeof(DirEntry), 1, fs.fp);

            if (is_end_of_directory(&e)) return 0;
            if (is_deleted(&e) || is_lfn(&e)) continue;

            if (memcmp(e.DIR_Name, fatname, 11) == 0) {
                if (out_entry) *out_entry = e;
                return 1;
            }
        }
        current = get_fat_entry(current);
    }
    return 0;
}

/*
 * Like find_entry_in_directory, but also returns the exact byte offset
 * of the DirEntry in the image (needed to update / delete it later).
 */
int find_entry_with_offset(uint32_t dir_cluster, const char *name,
                            DirEntry *out_entry, uint64_t *out_offset) {
    char fatname[11];
    to_fat_name(name, fatname);

    uint32_t current = dir_cluster;
    while (current < FAT32_EOC) {
        uint64_t base = cluster_to_offset(current);
        uint32_t epc  = ((uint32_t)fs.bpb.BytsPerSec * fs.bpb.SecPerClus)
                       / sizeof(DirEntry);
        fseek(fs.fp, base, SEEK_SET);

        for (uint32_t i = 0; i < epc; i++) {
            uint64_t entry_off = base + (uint64_t)i * sizeof(DirEntry);
            DirEntry e;
            fread(&e, sizeof(DirEntry), 1, fs.fp);

            if (is_end_of_directory(&e)) return 0;
            if (is_deleted(&e) || is_lfn(&e)) continue;

            if (memcmp(e.DIR_Name, fatname, 11) == 0) {
                if (out_entry)  *out_entry  = e;
                if (out_offset) *out_offset = entry_off;
                return 1;
            }
        }
        current = get_fat_entry(current);
    }
    return 0;
}

/*
 * Find the first free directory slot (0x00 or 0xE5 first byte) in
 * dir_cluster, allocating a new cluster for the directory if needed.
 * Stores the byte offset of the free slot in *out_offset.
 * Returns 1 on success, 0 on failure.
 */
int find_free_dir_entry(uint32_t dir_cluster, uint64_t *out_offset) {
    uint32_t current = dir_cluster;
    uint32_t prev    = 0;
    uint32_t epc     = ((uint32_t)fs.bpb.BytsPerSec * fs.bpb.SecPerClus)
                      / sizeof(DirEntry);

    while (current < FAT32_EOC) {
        uint64_t base = cluster_to_offset(current);

        for (uint32_t i = 0; i < epc; i++) {
            uint64_t off = base + (uint64_t)i * sizeof(DirEntry);
            uint8_t first_byte;
            fseek(fs.fp, off, SEEK_SET);
            fread(&first_byte, 1, 1, fs.fp);

            if (first_byte == 0x00 || first_byte == 0xE5) {
                *out_offset = off;
                return 1;
            }
        }
        prev    = current;
        current = get_fat_entry(current);
    }

    /* No free slot found — grow the directory by one cluster */
    uint32_t new_clus = allocate_cluster(prev);
    if (new_clus == 0) return 0;

    *out_offset = cluster_to_offset(new_clus);
    return 1;
}

/* ================================================================
   Navigation
   ================================================================ */

int change_directory(const char *path) {
    if (!fs.mounted) { printf("No image mounted.\n"); return 0; }

    if (strcmp(path, "/") == 0) {
        fs.cwd_cluster = fs.bpb.RootClus;
        strcpy(fs.cwd_path, "/");
        return 1;
    }
    if (strcmp(path, ".") == 0) return 1;

    if (strcmp(path, "..") == 0) {
        if (strcmp(fs.cwd_path, "/") == 0) return 1;

        DirEntry parent;
        if (!find_entry_in_directory(fs.cwd_cluster, "..", &parent)) {
            fs.cwd_cluster = fs.bpb.RootClus;
            strcpy(fs.cwd_path, "/");
            return 1;
        }
        uint32_t pc = entry_first_cluster(&parent);
        if (pc == 0) pc = fs.bpb.RootClus;
        fs.cwd_cluster = pc;

        char *last = strrchr(fs.cwd_path, '/');
        if (last && last != fs.cwd_path) *last = '\0';
        else strcpy(fs.cwd_path, "/");
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

    uint32_t next = entry_first_cluster(&entry);
    if (next == 0) next = fs.bpb.RootClus;
    fs.cwd_cluster = next;

    if (strcmp(fs.cwd_path, "/") == 0)
        snprintf(fs.cwd_path, sizeof(fs.cwd_path), "/%s", path);
    else {
        strncat(fs.cwd_path, "/",  sizeof(fs.cwd_path) - strlen(fs.cwd_path) - 1);
        strncat(fs.cwd_path, path, sizeof(fs.cwd_path) - strlen(fs.cwd_path) - 1);
    }
    return 1;
}

/* ================================================================
   Mount / unmount / info / prompt
   ================================================================ */

int mount_fat32(const char *image_path) {
    fs.fp = fopen(image_path, "rb+");
    if (!fs.fp) { perror("Failed to open FAT32 image"); return 0; }

    fseek(fs.fp, 0, SEEK_SET);
    if (fread(&fs.bpb, sizeof(BPB_FAT32), 1, fs.fp) != 1) {
        perror("Failed to read boot sector");
        fclose(fs.fp); fs.fp = NULL;
        return 0;
    }

    fs.fat_begin_lba    = fs.bpb.RsvdSecCnt;
    fs.first_data_sector= fs.bpb.RsvdSecCnt + (fs.bpb.NumFATs * fs.bpb.FATSz32);
    fs.cwd_cluster      = fs.bpb.RootClus;
    strcpy(fs.cwd_path, "/");
    fs.mounted = 1;

    memset(fs.open_files, 0, sizeof(fs.open_files));
    return 1;
}

void unmount_fat32(void) {
    if (fs.fp) { fclose(fs.fp); fs.fp = NULL; }
    fs.mounted = 0;
}

void print_info(void) {
    if (!fs.mounted) { printf("No image mounted.\n"); return; }
    printf("FAT32 Info:\n");
    printf("  Bytes Per Sector      : %u\n",  fs.bpb.BytsPerSec);
    printf("  Sectors Per Cluster   : %u\n",  fs.bpb.SecPerClus);
    printf("  Reserved Sector Count : %u\n",  fs.bpb.RsvdSecCnt);
    printf("  Number of FATs        : %u\n",  fs.bpb.NumFATs);
    printf("  FAT Size 32           : %u\n",  fs.bpb.FATSz32);
    printf("  Root Cluster          : %u\n",  fs.bpb.RootClus);
    printf("  First Data Sector     : %u\n",  fs.first_data_sector);
}

void print_prompt(void) {
    printf("fat32:%s$ ", fs.cwd_path);
}
