# FAT32 File System Utility

## Project Overview

This project is a user-space shell utility that mounts and manipulates FAT32 disk images without corrupting them. It supports navigation, file and directory creation, reading, writing, moving, and deletion while maintaining full FAT32 file system structure integrity.

The main goal of this project is to simulate how an operating system interacts with a FAT32 file system by directly managing disk structures such as the Boot Sector, FAT tables, clusters, and directory entries.

This utility provides a custom shell-like interface where users can execute commands similar to Linux shell commands such as `ls`, `cd`, `mkdir`, `creat`, `read`, `write`, `rm`, and more.

---

## Features

### Part 1: Mounting the Image

* Parse FAT32 Boot Sector
* Load FAT32 metadata
* Map FAT tables into memory
* Initialize root directory
* Implement `info` and `exit` commands
* Set up shell loop and prompt

### Part 2: Navigation

* Implement directory traversal
* Current working directory tracking
* Implement `cd` command
* Implement `ls` command
* Support `.` and `..` directories
* Error checking and validation

### Part 3: Create

* Implement `mkdir` command
* Implement `creat` command
* Directory entry creation
* File creation with proper FAT32 structure handling

### Part 4: Read

* Open file table management (maximum 10 files)
* Implement `open`
* Implement `close`
* Implement `lsof`
* Implement `lseek`
* Implement `read`
* Proper offset management
* Read permission validation

### Part 5: Update

* Implement `write`
* File size extension
* Cluster allocation
* Implement `mv`
* Rename files and directories
* Cross-directory move support

### Part 6: Delete

* Implement `rm`
* Implement `rmdir`
* Proper FAT entry zeroing
* Directory entry removal
* Empty directory validation

---

## Project Structure

```text
filesys/
│
├── src/
│   ├── main.c
│   ├── lexer.c
│   ├── commands.c
│   └── utils.c
│
├── include/
│   ├── lexer.h
│   └── fat32.h
│
├── fat32.img
├── README.md
└── Makefile
```

---

## Technologies Used

* Language: C
* Compiler: GCC
* Operating System: Linux / Ubuntu
* Platform: DigitalOcean Droplet
* File System: FAT32

---

## Compilation

Make sure GCC and Make are installed:

```bash
sudo apt update
sudo apt install build-essential make -y
```

Compile the project using:

```bash
make
```

---


## Supported Commands

### Currently Implemented

* `info`
* `ls`
* `cd`
* `pwd`
* `help`
* `exit`

### Planned Commands

* `mkdir`
* `creat`
* `open`
* `close`
* `lsof`
* `lseek`
* `read`
* `write`
* `mv`
* `rm`
* `rmdir`

---

## Example Usage

```bash
./filesys fat32.img
```

```text
fat32:/$ info
fat32:/$ ls
fat32:/$ cd folder
fat32:/folder$ pwd
fat32:/folder$ exit
```

---


