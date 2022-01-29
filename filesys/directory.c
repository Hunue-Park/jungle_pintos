#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"

#include "filesys/fat.h"
// moved to directory.h
// /* A directory. */
// struct dir {
// 	struct inode *inode;                /* Backing store. */
// 	off_t pos;                          /* Current position. */
// };

// /* A single directory entry. */
// struct dir_entry {
// 	disk_sector_t inode_sector;         /* Sector number of header. */
// 	char name[NAME_MAX + 1];            /* Null terminated file name. */
// 	bool in_use;                        /* In use or free? */
// };

//Project 4-2
// find current working directory
struct dir *current_directory(){
	return thread_current()->wd;
}

void set_current_directory(struct dir *dir){
	// dir_close(current_directory());
	thread_current()->wd = dir;
}

// find subdirectory that contains last file/subdirectory in the path
// ex) a/b/c/d/e -> returns inode (dir_entry table) of directory 'd'
// returns NULL if path is invalid (ex. some subdirectory missing - a/b/c/d/e 중 c가 없다거나)
struct dir *find_subdir(char ** dirnames, int dircount){
	int i;
	struct inode *inode_even = NULL; 
	struct inode *inode_odd = NULL;
	struct inode *inode = NULL; // inode of subdirectory or file

	struct dir *cwd = current_directory();
	if (cwd == NULL) return NULL;
	struct dir *subdir = dir_reopen(cwd); // prevent working directory from being closed
	for(i = 0; i < dircount; i++){
		struct dir *olddir = subdir;
		if (i == 0 && (strcmp(dirnames[i],"root") == 0)){ // path from root dir
			subdir = dir_open_root();
			dir_close(olddir);
			continue;
		}
		if (i % 2 == 0) 
		{
			dir_lookup(olddir, dirnames[i], &inode_even);
			inode = inode_even;
		}
		else 
		{
			dir_lookup(olddir, dirnames[i], &inode_odd);
			inode = inode_odd;
		}
		
		if(inode == NULL) return NULL;
		
		subdir = dir_open(inode);
		dir_close(olddir);
	}
	return subdir;
}

/* Creates a directory with space for ENTRY_CNT entries in the
 * given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (disk_sector_t sector, size_t entry_cnt) {
	return inode_create (sector, entry_cnt * sizeof (struct dir_entry), true);
}

/* Opens and returns the directory for the given INODE, of which
 * it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) {
	struct dir *dir = calloc (1, sizeof *dir);
	if (inode != NULL && dir != NULL) {
		dir->inode = inode;
		dir->pos = 0;
		
		dir->deny_write = false;
		dir->dupCount = 0;
		return dir;
	} else {
		inode_close (inode);
		free (dir);
		return NULL;
	}
}

/* Opens the root directory and returns a directory for it.
 * Return true if successful, false on failure. */
struct dir *
dir_open_root (void) {
	return dir_open (inode_open (cluster_to_sector(ROOT_DIR_CLUSTER)));
}

/* Opens and returns a new directory for the same inode as DIR.
 * Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) {
	return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) {
	if (dir != NULL) {
		inode_close (dir->inode);
		free (dir);
	}
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) {
	return dir->inode;
}

/* Searches DIR for a file with the given NAME.
 * If successful, returns true, sets *EP to the directory entry
 * if EP is non-null, and sets *OFSP to the byte offset of the
 * directory entry if OFSP is non-null.
 * otherwise, returns false and ignores EP and OFSP. */
bool
lookup (const struct dir *dir, const char *name,
		struct dir_entry *ep, off_t *ofsp) {
	struct dir_entry e;
	size_t ofs;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
			ofs += sizeof e)
		if (e.in_use && !strcmp (name, e.name)) {
			if (ep != NULL)
				*ep = e;
			if (ofsp != NULL)
				*ofsp = ofs;
			return true;
		}
	return false;
}

/* Searches DIR for a file with the given NAME
 * and returns true if one exists, false otherwise.
 * On success, sets *INODE to an inode for the file, otherwise to
 * a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
		struct inode **inode) {
	struct dir_entry e;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	if (lookup (dir, name, &e, NULL)){
		if (strcmp("lazy", e.lazy)) //lazy symlink update
		{
			dir_lookup(dir_open(inode_open(e.inode_sector)), e.lazy, inode);
			if(*inode != NULL){
				e.inode_sector = inode_get_inumber(*inode);
				strlcpy (e.lazy, "lazy", sizeof e.lazy);
				return *inode != NULL;
			}
		}
		*inode = inode_open (e.inode_sector);
	}
	else
		*inode = NULL;

	return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
 * file by that name.  The file's inode is in sector
 * INODE_SECTOR.
 * Returns true if successful, false on failure.
 * Fails if NAME is invalid (i.e. too long) or a disk or memory
 * error occurs. */
bool
dir_add (struct dir *dir, const char *name, disk_sector_t inode_sector) {
	struct dir_entry e;
	off_t ofs;
	bool success = false;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	/* Check NAME for validity. */
	if (*name == '\0' || strlen (name) > NAME_MAX)
		return false;

	/* Check that NAME is not in use. */
	if (lookup (dir, name, NULL, NULL))
		goto done;

	/* Set OFS to offset of free slot.
	 * If there are no free slots, then it will be set to the
	 * current end-of-file.

	 * inode_read_at() will only return a short read at end of file.
	 * Otherwise, we'd need to verify that we didn't get a short
	 * read due to something intermittent such as low memory. */
	for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
			ofs += sizeof e)
		if (!e.in_use)
			break;

	/* Write slot. */
	e.in_use = true;
	e.is_sym = false;
	strlcpy (e.lazy, "lazy", sizeof e.lazy);
	strlcpy (e.name, name, sizeof e.name);
	e.inode_sector = inode_sector;
	success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

done:
	return success;
}

/* Removes any entry for NAME in DIR.
 * Returns true if successful, false on failure,
 * which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) {
	struct dir_entry e;
	struct inode *inode = NULL;
	bool success = false;
	off_t ofs;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	/* Find directory entry. */
	if (!lookup (dir, name, &e, &ofs))
		goto done;

	//project 4-2 : remove symlink
	if (e.is_sym){
		e.in_use = false;
		return (inode_write_at(dir->inode, &e, sizeof e, ofs) == sizeof e);
	}

	/* Open inode. */
	inode = inode_open (e.inode_sector);
	if (inode == NULL)
		goto done;

	//project 4-2 : remove subdirectory
	if (inode_isdir(inode)){
		char temp[NAME_MAX + 1];
		struct dir *tar = dir_open(inode);
		if (dir_readdir(tar, temp)){ // dir not empty
			dir->pos -= sizeof(struct dir_entry); // restore original pos.
			dir_close(tar);
			goto done;
		}
		dir_close(tar);
	}

	/* Erase directory entry. */
	e.in_use = false;
	if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
		goto done;

	/* Remove inode. */
	inode_remove (inode);
	success = true;

done:
	inode_close (inode);
	return success;
}

/* Reads the next directory entry in DIR and stores the name in
 * NAME.  Returns true if successful, false if the directory
 * contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1]) {
	struct dir_entry e;

	while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) {
		dir->pos += sizeof e;
		if (e.in_use) {
			strlcpy (name, e.name, NAME_MAX + 1);
			return true;
		}
	}
	return false;
}

//project 4-2 : set dir entry's issym flag
void set_entry_symlink(struct dir* dir, const char *name, bool issym){
	struct dir_entry e;
	off_t ofs;
	for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e){
		if (e.in_use && !strcmp (name, e.name)){
			break;
		}
	}
	e.is_sym = issym;
	inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
}
// set dir entry's lazy symlink target info
void set_entry_lazytar(struct dir* dir, const char *name, const char *tar){
	struct dir_entry e;
	off_t ofs;
	for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e){
		if (e.in_use && !strcmp (name, e.name)){
			break;
		}
	}
	strlcpy(e.lazy, tar, sizeof e.lazy);
	inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
}