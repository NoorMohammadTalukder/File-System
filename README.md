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

All commands are fully implemented across all 6 parts of the project spec.

| Command | Usage | Description |
|---|---|---|
| `info` | `info` | Show FAT32 boot sector metadata |
| `ls` | `ls` | List contents of current directory |
| `cd` | `cd <dir>` | Change directory (supports `.` and `..`) |
| `pwd` | `pwd` | Print current working directory path |
| `mkdir` | `mkdir <name>` | Create a new directory |
| `creat` | `creat <name>` | Create a new empty file |
| `open` | `open <name> <mode>` | Open a file (`-r`, `-w`, or `-rw`) |
| `close` | `close <fd>` | Close an open file by file descriptor |
| `lsof` | `lsof` | List all currently open files |
| `lseek` | `lseek <fd> <offset>` | Move file pointer to absolute offset |
| `read` | `read <fd> <size>` | Read bytes from open file |
| `write` | `write <fd> <string>` | Write string to open file |
| `mv` | `mv <src> <dst>` | Rename a file or directory |
| `rm` | `rm <name>` | Delete a file |
| `rmdir` | `rmdir <name>` | Delete an empty directory |
| `help` | `help` | Show all available commands |
| `exit` | `exit` | Unmount image and exit shell |

---

## Example Usage

```bash
./filesys fat32.img
```

### Navigation
```text
fat32:/$ info
fat32:/$ ls
fat32:/$ cd folder
fat32:/folder$ pwd
fat32:/folder$ cd ..
fat32:/$
```

### Creating Files and Directories
```text
fat32:/$ mkdir testdir
Directory 'testdir' created.

fat32:/$ creat hello.txt
File 'hello.txt' created.

fat32:/$ ls
[DIR]  TESTDIR
[FILE] HELLO.TXT            0 bytes
```

### Writing to a File
```text
fat32:/$ open hello.txt -w
Opened 'hello.txt' → fd 0

fat32:/$ write 0 Hello World
Wrote 11 bytes to fd 0.

fat32:/$ close 0
Closed 'hello.txt' (fd 0).
```

### Reading from a File
```text
fat32:/$ open hello.txt -r
Opened 'hello.txt' → fd 0

fat32:/$ read 0 11
Hello World

fat32:/$ lseek 0 6
fd 0: position set to 6.

fat32:/$ read 0 5
World

fat32:/$ close 0
```

### Open File Table
```text
fat32:/$ lsof
FD    Name                  Mode   Size        Offset
----  --------------------  -----  ----------  ------
0     hello.txt             rw     11          0
```

### Rename and Delete
```text
fat32:/$ mv hello.txt renamed.txt
'hello.txt' renamed to 'renamed.txt'.

fat32:/$ rm renamed.txt
'renamed.txt' deleted.

fat32:/$ rmdir testdir
Directory 'testdir' removed.

fat32:/$ exit
Unmounted FAT32 image.
```

---

## Notes

* FAT32 stores all filenames in **uppercase** internally. Commands are case-insensitive — you can type `cd testdir` or `cd TESTDIR` and both will work.
* A file must be opened with `open` before using `read` or `write`.
* `write` requires the file to be opened with `-w` or `-rw` mode.
* `read` requires the file to be opened with `-r` or `-rw` mode.
* `rmdir` will refuse to delete a directory that is not empty.
* The open file table supports a maximum of **10 simultaneously open files**.
