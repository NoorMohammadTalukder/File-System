#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fat32.h"

int shell_running = 1;

/* ================================================================
   Help
   ================================================================ */

static void print_help(void) {
    printf("Available commands:\n");
    printf("  info                    Show FAT32 metadata\n");
    printf("  ls                      List current directory\n");
    printf("  cd <dir>                Change directory\n");
    printf("  pwd                     Print working directory\n");
    printf("  mkdir <name>            Create directory\n");
    printf("  creat <name>            Create empty file\n");
    printf("  open  <name> <mode>     Open file  (mode: -r | -w | -rw)\n");
    printf("  close <fd>              Close open file\n");
    printf("  lsof                    List open files\n");
    printf("  lseek <fd> <offset>     Move file pointer (from start)\n");
    printf("  read  <fd> <size>       Read bytes from file\n");
    printf("  write <fd> <string>     Write string to file\n");
    printf("  mv    <src> <dst>       Rename / move file or directory\n");
    printf("  rm    <name>            Delete file\n");
    printf("  rmdir <name>            Delete empty directory\n");
    printf("  help                    Show this help\n");
    printf("  exit                    Exit shell\n");
}

/* ================================================================
   Part 3 – CREATE
   ================================================================ */

/*
 * mkdir <name>
 * Allocates a cluster for the new directory, writes a DirEntry into
 * the current directory, and initialises '.' and '..' entries.
 */
static void cmd_mkdir(const char *name) {
    /* name must not already exist */
    DirEntry existing;
    if (find_entry_in_directory(fs.cwd_cluster, name, &existing)) {
        printf("mkdir: '%s' already exists\n", name);
        return;
    }

    /* Allocate a cluster for the new directory */
    uint32_t new_cluster = allocate_cluster(0);
    if (new_cluster == 0) {
        printf("mkdir: no free clusters on disk\n");
        return;
    }

    /* Find a free slot in the current directory */
    uint64_t slot_offset;
    if (!find_free_dir_entry(fs.cwd_cluster, &slot_offset)) {
        printf("mkdir: no space in directory\n");
        free_cluster_chain(new_cluster);
        return;
    }

    /* Build and write the DirEntry in the parent directory */
    DirEntry entry;
    memset(&entry, 0, sizeof(DirEntry));

    char fatname[11];
    to_fat_name(name, fatname);
    memcpy(entry.DIR_Name, fatname, 11);

    entry.DIR_Attr     = ATTR_DIRECTORY;
    entry.DIR_FstClusHI= (uint16_t)((new_cluster >> 16) & 0xFFFF);
    entry.DIR_FstClusLO= (uint16_t)( new_cluster        & 0xFFFF);
    entry.DIR_FileSize  = 0;
    entry.DIR_CrtDate   = entry.DIR_WrtDate = entry.DIR_LstAccDate = fat_date();
    entry.DIR_CrtTime   = entry.DIR_WrtTime = fat_time();

    fseek(fs.fp, slot_offset, SEEK_SET);
    fwrite(&entry, sizeof(DirEntry), 1, fs.fp);
    fflush(fs.fp);

    /* Write '.' and '..' inside the new directory cluster */
    uint64_t new_dir_base = cluster_to_offset(new_cluster);

    /* '.' – points to itself */
    DirEntry dot;
    memset(&dot, 0, sizeof(DirEntry));
    memset(dot.DIR_Name, ' ', 11);
    dot.DIR_Name[0]  = '.';
    dot.DIR_Attr     = ATTR_DIRECTORY;
    dot.DIR_FstClusHI= entry.DIR_FstClusHI;
    dot.DIR_FstClusLO= entry.DIR_FstClusLO;
    dot.DIR_CrtDate  = dot.DIR_WrtDate = dot.DIR_LstAccDate = fat_date();
    dot.DIR_CrtTime  = dot.DIR_WrtTime = fat_time();

    /* '..' – points to the current (parent) directory.
       The FAT32 spec stores 0 for the root cluster in '..'. */
    DirEntry dotdot;
    memset(&dotdot, 0, sizeof(DirEntry));
    memset(dotdot.DIR_Name, ' ', 11);
    dotdot.DIR_Name[0] = '.';
    dotdot.DIR_Name[1] = '.';
    dotdot.DIR_Attr    = ATTR_DIRECTORY;
    uint32_t parent_cluster = fs.cwd_cluster;
    if (parent_cluster == fs.bpb.RootClus) parent_cluster = 0; /* spec says 0 for root */
    dotdot.DIR_FstClusHI = (uint16_t)((parent_cluster >> 16) & 0xFFFF);
    dotdot.DIR_FstClusLO = (uint16_t)( parent_cluster        & 0xFFFF);
    dotdot.DIR_CrtDate   = dotdot.DIR_WrtDate = dotdot.DIR_LstAccDate = fat_date();
    dotdot.DIR_CrtTime   = dotdot.DIR_WrtTime = fat_time();

    fseek(fs.fp, new_dir_base, SEEK_SET);
    fwrite(&dot,    sizeof(DirEntry), 1, fs.fp);
    fwrite(&dotdot, sizeof(DirEntry), 1, fs.fp);
    fflush(fs.fp);

    printf("Directory '%s' created.\n", name);
}

/*
 * creat <name>
 * Creates a zero-length file (no cluster allocated yet).
 */
static void cmd_creat(const char *name) {
    DirEntry existing;
    if (find_entry_in_directory(fs.cwd_cluster, name, &existing)) {
        printf("creat: '%s' already exists\n", name);
        return;
    }

    uint64_t slot_offset;
    if (!find_free_dir_entry(fs.cwd_cluster, &slot_offset)) {
        printf("creat: no space in directory\n");
        return;
    }

    DirEntry entry;
    memset(&entry, 0, sizeof(DirEntry));

    char fatname[11];
    to_fat_name(name, fatname);
    memcpy(entry.DIR_Name, fatname, 11);

    entry.DIR_Attr      = ATTR_ARCHIVE;
    entry.DIR_FstClusHI = 0;
    entry.DIR_FstClusLO = 0;
    entry.DIR_FileSize  = 0;
    entry.DIR_CrtDate   = entry.DIR_WrtDate = entry.DIR_LstAccDate = fat_date();
    entry.DIR_CrtTime   = entry.DIR_WrtTime = fat_time();

    fseek(fs.fp, slot_offset, SEEK_SET);
    fwrite(&entry, sizeof(DirEntry), 1, fs.fp);
    fflush(fs.fp);

    printf("File '%s' created.\n", name);
}

/* ================================================================
   Part 4 – READ  (open / close / lsof / lseek / read)
   ================================================================ */

/*
 * open <name> <mode>
 * Mode flags: -r (read), -w (write), -rw or -wr (read+write).
 * Up to MAX_OPEN_FILES files can be open simultaneously.
 */
static void cmd_open(const char *name, const char *mode_str) {
    /* Parse mode */
    uint8_t mode = 0;
    if      (strcmp(mode_str, "-r")  == 0) mode = MODE_READ;
    else if (strcmp(mode_str, "-w")  == 0) mode = MODE_WRITE;
    else if (strcmp(mode_str, "-rw") == 0 ||
             strcmp(mode_str, "-wr") == 0) mode = MODE_READ | MODE_WRITE;
    else {
        printf("open: invalid mode '%s'. Use -r, -w, or -rw\n", mode_str);
        return;
    }

    /* Locate the file */
    DirEntry entry;
    uint64_t entry_off;
    if (!find_entry_with_offset(fs.cwd_cluster, name, &entry, &entry_off)) {
        printf("open: '%s' not found\n", name);
        return;
    }
    if (entry.DIR_Attr & ATTR_DIRECTORY) {
        printf("open: '%s' is a directory\n", name);
        return;
    }

    /* Find a free slot in the open-file table */
    int fd = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!fs.open_files[i].used) { fd = i; break; }
    }
    if (fd == -1) {
        printf("open: too many open files (max %d)\n", MAX_OPEN_FILES);
        return;
    }

    OpenFile *of    = &fs.open_files[fd];
    of->used        = 1;
    strncpy(of->name, name, MAX_NAME - 1);
    of->first_cluster   = entry_first_cluster(&entry);
    of->file_size       = entry.DIR_FileSize;
    of->offset          = 0;
    of->mode            = mode;
    of->dir_entry_offset= entry_off;

    printf("Opened '%s' → fd %d  (mode: %s%s)\n",
           name, fd,
           (mode & MODE_READ)  ? "r" : "",
           (mode & MODE_WRITE) ? "w" : "");
}

/*
 * close <fd>
 */
static void cmd_close(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !fs.open_files[fd].used) {
        printf("close: invalid fd %d\n", fd);
        return;
    }
    printf("Closed '%s' (fd %d).\n", fs.open_files[fd].name, fd);
    memset(&fs.open_files[fd], 0, sizeof(OpenFile));
}

/*
 * lsof
 * List all currently open files.
 */
static void cmd_lsof(void) {
    int any = 0;
    printf("%-4s  %-20s  %-5s  %-10s  %s\n",
           "FD", "Name", "Mode", "Size", "Offset");
    printf("%-4s  %-20s  %-5s  %-10s  %s\n",
           "----", "--------------------", "-----", "----------", "------");

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        OpenFile *of = &fs.open_files[i];
        if (!of->used) continue;
        any = 1;

        char mode_str[4] = {0};
        int mi = 0;
        if (of->mode & MODE_READ)  mode_str[mi++] = 'r';
        if (of->mode & MODE_WRITE) mode_str[mi++] = 'w';

        printf("%-4d  %-20s  %-5s  %-10u  %u\n",
               i, of->name, mode_str, of->file_size, of->offset);
    }
    if (!any) printf("(no open files)\n");
}

/*
 * lseek <fd> <offset>
 * Sets the file position to an absolute byte offset.
 */
static void cmd_lseek(int fd, uint32_t offset) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !fs.open_files[fd].used) {
        printf("lseek: invalid fd %d\n", fd);
        return;
    }
    OpenFile *of = &fs.open_files[fd];

    if (offset > of->file_size) {
        printf("lseek: offset %u exceeds file size %u\n", offset, of->file_size);
        return;
    }
    of->offset = offset;
    printf("fd %d: position set to %u.\n", fd, offset);
}

/*
 * read <fd> <size>
 * Reads up to 'size' bytes from the current offset, then prints them.
 */
static void cmd_read(int fd, uint32_t size) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !fs.open_files[fd].used) {
        printf("read: invalid fd %d\n", fd);
        return;
    }
    OpenFile *of = &fs.open_files[fd];

    if (!(of->mode & MODE_READ)) {
        printf("read: fd %d not opened for reading\n", fd);
        return;
    }
    if (of->offset >= of->file_size) {
        printf("read: end of file\n");
        return;
    }

    /* Clamp to remaining bytes */
    uint32_t remaining = of->file_size - of->offset;
    if (size > remaining) size = remaining;
    if (size == 0) return;

    uint32_t cluster_size = (uint32_t)fs.bpb.BytsPerSec * fs.bpb.SecPerClus;

    /* Navigate to the cluster that contains of->offset */
    uint32_t cluster_idx = of->offset / cluster_size;
    uint32_t intra_off   = of->offset % cluster_size;

    uint32_t current = of->first_cluster;
    for (uint32_t i = 0; i < cluster_idx && current < FAT32_EOC; i++)
        current = get_fat_entry(current);

    if (current >= FAT32_EOC) {
        printf("read: cluster chain ended unexpectedly\n");
        return;
    }

    /* Read data, crossing cluster boundaries as needed */
    uint8_t *buf = malloc(size + 1);
    if (!buf) { printf("read: out of memory\n"); return; }

    uint32_t total_read = 0;
    while (total_read < size && current < FAT32_EOC) {
        uint32_t avail    = cluster_size - intra_off;
        uint32_t to_read  = size - total_read;
        if (to_read > avail) to_read = avail;

        fseek(fs.fp, cluster_to_offset(current) + intra_off, SEEK_SET);
        fread(buf + total_read, 1, to_read, fs.fp);

        total_read += to_read;
        intra_off   = 0;          /* subsequent clusters start from byte 0 */
        current     = get_fat_entry(current);
    }

    of->offset += total_read;

    /* Print the data (null-terminate for safety) */
    buf[total_read] = '\0';
    printf("%.*s\n", (int)total_read, buf);
    free(buf);

    printf("(%u bytes read)\n", total_read);
}

/* ================================================================
   Part 5 – UPDATE  (write / mv)
   ================================================================ */

/*
 * write <fd> <string>
 * Writes the string at the current file offset, extending the file
 * and allocating new clusters as required.  Updates the DirEntry
 * with the new file size and first-cluster pointer.
 */
static void cmd_write(int fd, const char *data) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !fs.open_files[fd].used) {
        printf("write: invalid fd %d\n", fd);
        return;
    }
    OpenFile *of = &fs.open_files[fd];

    if (!(of->mode & MODE_WRITE)) {
        printf("write: fd %d not opened for writing\n", fd);
        return;
    }

    uint32_t data_len = (uint32_t)strlen(data);
    if (data_len == 0) return;

    uint32_t cluster_size = (uint32_t)fs.bpb.BytsPerSec * fs.bpb.SecPerClus;

    /* If the file has no cluster yet, allocate the first one */
    if (of->first_cluster == 0) {
        uint32_t c = allocate_cluster(0);
        if (c == 0) { printf("write: disk is full\n"); return; }
        of->first_cluster = c;
    }

    /* Navigate to the cluster that contains of->offset,
       allocating clusters on the way if the offset is past the end */
    uint32_t cluster_idx = of->offset / cluster_size;
    uint32_t intra_off   = of->offset % cluster_size;

    uint32_t current = of->first_cluster;
    uint32_t prev    = 0;

    for (uint32_t i = 0; i < cluster_idx; i++) {
        uint32_t next = get_fat_entry(current);
        if (next >= FAT32_EOC) {
            /* Extend the chain */
            next = allocate_cluster(current);
            if (next == 0) { printf("write: disk is full\n"); return; }
        }
        prev    = current;
        current = next;
    }
    (void)prev; /* silence unused-variable warning */

    /* Write data, crossing cluster boundaries as needed */
    uint32_t written = 0;
    while (written < data_len) {
        uint32_t avail    = cluster_size - intra_off;
        uint32_t to_write = data_len - written;
        if (to_write > avail) to_write = avail;

        fseek(fs.fp, cluster_to_offset(current) + intra_off, SEEK_SET);
        fwrite(data + written, 1, to_write, fs.fp);

        written    += to_write;
        of->offset += to_write;
        intra_off   = 0;

        if (written < data_len) {
            /* Need the next cluster */
            uint32_t next = get_fat_entry(current);
            if (next >= FAT32_EOC) {
                next = allocate_cluster(current);
                if (next == 0) {
                    printf("write: disk is full (partial write of %u/%u bytes)\n",
                           written, data_len);
                    break;
                }
            }
            current = next;
        }
    }
    fflush(fs.fp);

    /* Extend the logical file size if we wrote past the old end */
    if (of->offset > of->file_size)
        of->file_size = of->offset;

    /* Persist the updated DirEntry (size + first-cluster + timestamps) */
    DirEntry entry;
    fseek(fs.fp, of->dir_entry_offset, SEEK_SET);
    fread(&entry, sizeof(DirEntry), 1, fs.fp);

    entry.DIR_FileSize   = of->file_size;
    entry.DIR_FstClusHI  = (uint16_t)((of->first_cluster >> 16) & 0xFFFF);
    entry.DIR_FstClusLO  = (uint16_t)( of->first_cluster        & 0xFFFF);
    entry.DIR_WrtDate    = fat_date();
    entry.DIR_WrtTime    = fat_time();
    entry.DIR_LstAccDate = fat_date();

    fseek(fs.fp, of->dir_entry_offset, SEEK_SET);
    fwrite(&entry, sizeof(DirEntry), 1, fs.fp);
    fflush(fs.fp);

    printf("Wrote %u bytes to fd %d.\n", written, fd);
}

/*
 * mv <src> <dst>
 * Renames src to dst within the current directory.
 * (Cross-directory moves are not part of this project spec.)
 */
static void cmd_mv(const char *src, const char *dst) {
    DirEntry entry;
    uint64_t entry_off;

    if (!find_entry_with_offset(fs.cwd_cluster, src, &entry, &entry_off)) {
        printf("mv: '%s' not found\n", src);
        return;
    }

    /* Destination must not already exist */
    DirEntry existing;
    if (find_entry_in_directory(fs.cwd_cluster, dst, &existing)) {
        printf("mv: '%s' already exists\n", dst);
        return;
    }

    /* Re-encode the name and patch the DirEntry in-place */
    char fatname[11];
    to_fat_name(dst, fatname);
    memcpy(entry.DIR_Name, fatname, 11);
    entry.DIR_WrtDate    = fat_date();
    entry.DIR_WrtTime    = fat_time();
    entry.DIR_LstAccDate = fat_date();

    fseek(fs.fp, entry_off, SEEK_SET);
    fwrite(&entry, sizeof(DirEntry), 1, fs.fp);
    fflush(fs.fp);

    /* Update the name in the open-file table if the file is open */
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (fs.open_files[i].used &&
            fs.open_files[i].dir_entry_offset == entry_off) {
            strncpy(fs.open_files[i].name, dst, MAX_NAME - 1);
        }
    }

    printf("'%s' renamed to '%s'.\n", src, dst);
}

/* ================================================================
   Part 6 – DELETE  (rm / rmdir)
   ================================================================ */

/*
 * rm <name>
 * Marks the directory entry deleted (0xE5) and frees its cluster chain.
 */
static void cmd_rm(const char *name) {
    DirEntry entry;
    uint64_t entry_off;

    if (!find_entry_with_offset(fs.cwd_cluster, name, &entry, &entry_off)) {
        printf("rm: '%s' not found\n", name);
        return;
    }
    if (entry.DIR_Attr & ATTR_DIRECTORY) {
        printf("rm: '%s' is a directory — use rmdir\n", name);
        return;
    }

    /* Close the file if it is currently open */
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (fs.open_files[i].used &&
            fs.open_files[i].dir_entry_offset == entry_off) {
            printf("rm: closing fd %d before deletion\n", i);
            memset(&fs.open_files[i], 0, sizeof(OpenFile));
        }
    }

    /* Mark directory entry as deleted */
    uint8_t del = 0xE5;
    fseek(fs.fp, entry_off, SEEK_SET);
    fwrite(&del, 1, 1, fs.fp);
    fflush(fs.fp);

    /* Free the cluster chain */
    uint32_t first = entry_first_cluster(&entry);
    if (first >= 2) free_cluster_chain(first);

    printf("'%s' deleted.\n", name);
}

/*
 * rmdir <name>
 * Deletes an empty directory (only '.' and '..' entries allowed inside).
 */
static void cmd_rmdir(const char *name) {
    DirEntry entry;
    uint64_t entry_off;

    if (!find_entry_with_offset(fs.cwd_cluster, name, &entry, &entry_off)) {
        printf("rmdir: '%s' not found\n", name);
        return;
    }
    if (!(entry.DIR_Attr & ATTR_DIRECTORY)) {
        printf("rmdir: '%s' is not a directory — use rm\n", name);
        return;
    }

    /* Refuse to delete the current working directory */
    if (entry_first_cluster(&entry) == fs.cwd_cluster) {
        printf("rmdir: cannot remove the current working directory\n");
        return;
    }

    /* Check that the directory contains only '.' and '..' */
    uint32_t dir_clus = entry_first_cluster(&entry);
    uint32_t current  = dir_clus;
    uint32_t epc      = ((uint32_t)fs.bpb.BytsPerSec * fs.bpb.SecPerClus)
                       / sizeof(DirEntry);
    int has_entries = 0;

    while (current < FAT32_EOC && !has_entries) {
        uint64_t base = cluster_to_offset(current);
        fseek(fs.fp, base, SEEK_SET);
        for (uint32_t i = 0; i < epc; i++) {
            DirEntry e;
            fread(&e, sizeof(DirEntry), 1, fs.fp);

            if (e.DIR_Name[0] == 0x00) goto scan_done;   /* end-of-dir mark */
            if (e.DIR_Name[0] == 0xE5) continue;          /* deleted */
            if ((e.DIR_Attr & 0x0F) == 0x0F) continue;    /* LFN */

            /* Skip '.' and '..' */
            if (e.DIR_Name[0] == '.' &&
                (e.DIR_Name[1] == ' ' || e.DIR_Name[1] == '.'))
                continue;

            has_entries = 1;
            break;
        }
        current = get_fat_entry(current);
    }
    scan_done:

    if (has_entries) {
        printf("rmdir: '%s' is not empty\n", name);
        return;
    }

    /* Mark the directory entry as deleted */
    uint8_t del = 0xE5;
    fseek(fs.fp, entry_off, SEEK_SET);
    fwrite(&del, 1, 1, fs.fp);
    fflush(fs.fp);

    /* Free the cluster chain */
    if (dir_clus >= 2) free_cluster_chain(dir_clus);

    printf("Directory '%s' removed.\n", name);
}

/* ================================================================
   Command dispatcher
   ================================================================ */

void execute_command(char *tokens[], int count) {
    if (count == 0) return;

    /* ── Navigation / info ───────────────────────────────────────── */
    if (strcmp(tokens[0], "info") == 0) {
        print_info();
    }
    else if (strcmp(tokens[0], "ls") == 0) {
        list_directory(fs.cwd_cluster);
    }
    else if (strcmp(tokens[0], "cd") == 0) {
        if (count < 2) { printf("Usage: cd <directory>\n"); return; }
        change_directory(tokens[1]);
    }
    else if (strcmp(tokens[0], "pwd") == 0) {
        printf("%s\n", fs.cwd_path);
    }
    else if (strcmp(tokens[0], "help") == 0) {
        print_help();
    }

    /* ── Part 3: Create ──────────────────────────────────────────── */
    else if (strcmp(tokens[0], "mkdir") == 0) {
        if (count < 2) { printf("Usage: mkdir <name>\n"); return; }
        cmd_mkdir(tokens[1]);
    }
    else if (strcmp(tokens[0], "creat") == 0) {
        if (count < 2) { printf("Usage: creat <name>\n"); return; }
        cmd_creat(tokens[1]);
    }

    /* ── Part 4: Read (open / close / lsof / lseek / read) ───────── */
    else if (strcmp(tokens[0], "open") == 0) {
        if (count < 3) { printf("Usage: open <name> <-r|-w|-rw>\n"); return; }
        cmd_open(tokens[1], tokens[2]);
    }
    else if (strcmp(tokens[0], "close") == 0) {
        if (count < 2) { printf("Usage: close <fd>\n"); return; }
        cmd_close(atoi(tokens[1]));
    }
    else if (strcmp(tokens[0], "lsof") == 0) {
        cmd_lsof();
    }
    else if (strcmp(tokens[0], "lseek") == 0) {
        if (count < 3) { printf("Usage: lseek <fd> <offset>\n"); return; }
        cmd_lseek(atoi(tokens[1]), (uint32_t)atoi(tokens[2]));
    }
    else if (strcmp(tokens[0], "read") == 0) {
        if (count < 3) { printf("Usage: read <fd> <size>\n"); return; }
        cmd_read(atoi(tokens[1]), (uint32_t)atoi(tokens[2]));
    }

    /* ── Part 5: Update (write / mv) ─────────────────────────────── */
    else if (strcmp(tokens[0], "write") == 0) {
        if (count < 3) { printf("Usage: write <fd> <string>\n"); return; }
        /* Join tokens[2..count-1] back into a single string */
        char data[MAX_INPUT] = {0};
        for (int i = 2; i < count; i++) {
            if (i > 2) strcat(data, " ");
            strcat(data, tokens[i]);
        }
        cmd_write(atoi(tokens[1]), data);
    }
    else if (strcmp(tokens[0], "mv") == 0) {
        if (count < 3) { printf("Usage: mv <src> <dst>\n"); return; }
        cmd_mv(tokens[1], tokens[2]);
    }

    /* ── Part 6: Delete (rm / rmdir) ─────────────────────────────── */
    else if (strcmp(tokens[0], "rm") == 0) {
        if (count < 2) { printf("Usage: rm <name>\n"); return; }
        cmd_rm(tokens[1]);
    }
    else if (strcmp(tokens[0], "rmdir") == 0) {
        if (count < 2) { printf("Usage: rmdir <name>\n"); return; }
        cmd_rmdir(tokens[1]);
    }

    /* ── Exit ────────────────────────────────────────────────────── */
    else if (strcmp(tokens[0], "exit") == 0) {
        shell_running = 0;
    }

    /* ── Unknown ─────────────────────────────────────────────────── */
    else {
        printf("Unknown command: %s  (type 'help' for a list)\n", tokens[0]);
    }
}
