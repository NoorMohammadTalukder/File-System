#ifndef FAT32_H
#define FAT32_H

#include <stdio.h>
#include <stdint.h>

/* ── Shell / tokeniser limits ─────────────────────────────────────── */
#define MAX_INPUT    1024
#define MAX_TOKENS   64
#define MAX_NAME     256

/* ── FAT32 directory-entry attribute flags ────────────────────────── */
#define ATTR_READ_ONLY  0x01
#define ATTR_HIDDEN     0x02
#define ATTR_SYSTEM     0x04
#define ATTR_VOLUME_ID  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVE    0x20

/* ── FAT32 special values ─────────────────────────────────────────── */
#define FAT32_EOC       0x0FFFFFF8   /* end-of-chain threshold          */
#define FAT32_FREE      0x00000000   /* free cluster marker             */
#define FAT32_BAD       0x0FFFFFF7   /* bad cluster marker              */

/* ── Open-file table ─────────────────────────────────────────────── */
#define MAX_OPEN_FILES  10
#define MODE_READ       0x01
#define MODE_WRITE      0x02

/* One slot in the open-file table */
typedef struct {
    int      used;               /* 1 = slot occupied                  */
    char     name[MAX_NAME];     /* filename (display only)            */
    uint32_t first_cluster;      /* first data cluster (0 = empty)     */
    uint32_t file_size;          /* current byte size                  */
    uint32_t offset;             /* current read/write position        */
    uint8_t  mode;               /* MODE_READ | MODE_WRITE             */
    uint64_t dir_entry_offset;   /* byte offset of DirEntry in image   */
} OpenFile;

/* ── On-disk structures ───────────────────────────────────────────── */
#pragma pack(push, 1)

typedef struct {
    uint8_t  jmpBoot[3];
    uint8_t  OEMName[8];
    uint16_t BytsPerSec;
    uint8_t  SecPerClus;
    uint16_t RsvdSecCnt;
    uint8_t  NumFATs;
    uint16_t RootEntCnt;
    uint16_t TotSec16;
    uint8_t  Media;
    uint16_t FATSz16;
    uint16_t SecPerTrk;
    uint16_t NumHeads;
    uint32_t HiddSec;
    uint32_t TotSec32;
    uint32_t FATSz32;
    uint16_t ExtFlags;
    uint16_t FSVer;
    uint32_t RootClus;
    uint16_t FSInfo;
    uint16_t BkBootSec;
    uint8_t  Reserved[12];
    uint8_t  DrvNum;
    uint8_t  Reserved1;
    uint8_t  BootSig;
    uint32_t VolID;
    uint8_t  VolLab[11];
    uint8_t  FilSysType[8];
} BPB_FAT32;

typedef struct {
    uint8_t  DIR_Name[11];
    uint8_t  DIR_Attr;
    uint8_t  DIR_NTRes;
    uint8_t  DIR_CrtTimeTenth;
    uint16_t DIR_CrtTime;
    uint16_t DIR_CrtDate;
    uint16_t DIR_LstAccDate;
    uint16_t DIR_FstClusHI;
    uint16_t DIR_WrtTime;
    uint16_t DIR_WrtDate;
    uint16_t DIR_FstClusLO;
    uint32_t DIR_FileSize;
} DirEntry;

#pragma pack(pop)

/* ── In-memory filesystem state ──────────────────────────────────── */
typedef struct {
    FILE      *fp;
    BPB_FAT32  bpb;
    uint32_t   first_data_sector;
    uint32_t   fat_begin_lba;
    uint32_t   cwd_cluster;
    char       cwd_path[1024];
    int        mounted;
    OpenFile   open_files[MAX_OPEN_FILES];  /* open-file table */
} FAT32;

extern FAT32 fs;
extern int   shell_running;

/* ================================================================
   Function prototypes
   ================================================================ */

/* ── utils.c ─────────────────────────────────────────────────────── */

/* Mount / unmount */
int      mount_fat32(const char *image_path);
void     unmount_fat32(void);

/* Info / prompt */
void     print_info(void);
void     print_prompt(void);

/* Cluster / FAT helpers */
uint32_t first_sector_of_cluster(uint32_t cluster);
uint64_t cluster_to_offset(uint32_t cluster);
uint32_t get_fat_entry(uint32_t cluster);
void     set_fat_entry(uint32_t cluster, uint32_t value);
uint32_t allocate_cluster(uint32_t prev_cluster);
void     free_cluster_chain(uint32_t start_cluster);

/* Directory helpers */
void     format_filename(const uint8_t raw[11], char *out);
void     to_fat_name(const char *input, char fatname[11]);
uint32_t entry_first_cluster(const DirEntry *entry);
void     list_directory(uint32_t cluster);
int      find_entry_in_directory(uint32_t dir_cluster, const char *name, DirEntry *out_entry);
int      find_entry_with_offset(uint32_t dir_cluster, const char *name,
                                DirEntry *out_entry, uint64_t *out_offset);
int      find_free_dir_entry(uint32_t dir_cluster, uint64_t *out_offset);

/* Navigation */
int      change_directory(const char *path);

/* Timestamps */
uint16_t fat_date(void);
uint16_t fat_time(void);

/* ── lexer.c ──────────────────────────────────────────────────────── */
int      tokenize(char *input, char *tokens[]);

/* ── commands.c ───────────────────────────────────────────────────── */
void     execute_command(char *tokens[], int count);

#endif /* FAT32_H */
