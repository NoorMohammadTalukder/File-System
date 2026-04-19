#ifndef FAT32_H
#define FAT32_H

#include <stdio.h>
#include <stdint.h>

#define MAX_INPUT 1024
#define MAX_TOKENS 64
#define MAX_NAME 256
#define ATTR_DIRECTORY 0x10
#define FAT32_EOC 0x0FFFFFF8

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

typedef struct {
    FILE *fp;
    BPB_FAT32 bpb;
    uint32_t first_data_sector;
    uint32_t fat_begin_lba;
    uint32_t cwd_cluster;
    char cwd_path[1024];
    int mounted;
} FAT32;

extern FAT32 fs;
extern int shell_running;

/* utils.c */
int mount_fat32(const char *image_path);
void unmount_fat32(void);
void print_info(void);
uint32_t first_sector_of_cluster(uint32_t cluster);
uint64_t cluster_to_offset(uint32_t cluster);
uint32_t get_fat_entry(uint32_t cluster);
void list_directory(uint32_t cluster);
int change_directory(const char *path);
void format_filename(const uint8_t raw[11], char *out);
uint32_t entry_first_cluster(const DirEntry *entry);
int find_entry_in_directory(uint32_t dir_cluster, const char *name, DirEntry *out_entry);
void print_prompt(void);

/* lexer.c */
int tokenize(char *input, char *tokens[]);

/* commands.c */
void execute_command(char *tokens[], int count);

#endif
