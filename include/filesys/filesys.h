#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "threads/synch.h"
#include "threads/malloc.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */

/* Disk used for file system. */
extern struct disk *filesys_disk;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size);
struct file *filesys_open (const char *name);
bool filesys_remove (const char *name);

// Project 4-2. Subdirectory
struct path{
	char ** dirnames; //list of directories
	int dircount; //level of directory
	char * filename; //
	char * pathStart_forFreeing; // malloc'ed to copy path input argument to `parse_filepath` - must free
};
// Project 3 (parallel)
struct lock filesys_lock;

struct path *parse_filepath (const char *name);
void free_path(struct path *path);

#endif /* filesys/filesys.h */
