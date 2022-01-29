#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#ifdef EFILESYS
	#include "filesys/fat.h"
#endif
/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
 * Must be exactly DISK_SECTOR_SIZE bytes long. */
// -> moved to inode.h


/* Returns the number of sectors to allocate for an inode SIZE
 * bytes long. */
static inline size_t
bytes_to_sectors (off_t size) {
	return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

// /* In-memory inode. */
// struct inode {
// 	struct list_elem elem;              /* Element in inode list. */
// 	disk_sector_t sector;               /* Sector number of disk location. */
// 	int open_cnt;                       /* Number of openers. */
// 	bool removed;                       /* True if deleted, false otherwise. */
// 	int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
// 	struct inode_disk data;             /* Inode content. */
// };   => moved to inode.h

/* Returns the disk sector that contains byte offset POS within
 * INODE.
 * Returns -1 if INODE does not contain data for a byte at offset
 * POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) {
	ASSERT (inode != NULL);
	if (pos < inode->data.length){
		#ifdef EFILESYS
			cluster_t clst = sector_to_cluster(inode->data.start);
			for (unsigned i = 0; i < (pos / DISK_SECTOR_SIZE); i++) {
					clst = fat_get(clst);
					if (clst == 0)
						return -1;
			}
			return cluster_to_sector(clst);
		#else
			return inode->data.start + pos / DISK_SECTOR_SIZE;
		#endif
	}
	else
		return -1;
}

/* List of open inodes, so that opening a single inode twice
 * returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) {
	list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
 * writes the new inode to sector SECTOR on the file system
 * disk.
 * Returns true if successful.
 * Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length, bool isdir) {
	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT (length >= 0);

	/* If this assertion fails, the inode structure is not exactly
	 * one sector in size, and you should fix that. */
	ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

	disk_inode = calloc (1, sizeof *disk_inode);
	if (disk_inode != NULL) {
		size_t sectors = bytes_to_sectors (length);
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;
		disk_inode->isdir = isdir;
		#ifdef EFILESYS
		cluster_t clst = sector_to_cluster(sector);
		cluster_t newclst = clst; // save clst in case of chaining failure
		if (sectors == 0) disk_inode->start = cluster_to_sector(fat_create_chain(newclst));

		int i;
		for (i = 0; i < sectors; i++){
			newclst = fat_create_chain(newclst);
			if (newclst == 0) {
				fat_remove_chain(clst, 0);
				free(disk_inode);
				return false;
			}
			if (i == 0){
				clst = newclst;
				disk_inode->start = cluster_to_sector(newclst); // set start point of the file
			}
		}
		disk_write (filesys_disk, sector, disk_inode);
		if (sectors > 0) {
			static char zeros[DISK_SECTOR_SIZE];
			for (i = 0; i < sectors; i++){
				ASSERT(clst != 0 || clst != EOChain);
				disk_write (filesys_disk, cluster_to_sector(clst), zeros); // non-contiguous sectors 
				clst = fat_get(clst); // find next cluster(=sector) in FAT
			}
		}
		success = true;
		#else
		if (free_map_allocate (sectors, &disk_inode->start)) {
			disk_write (filesys_disk, sector, disk_inode);
			if (sectors > 0) {
				static char zeros[DISK_SECTOR_SIZE];
				size_t i;

				for (i = 0; i < sectors; i++) 
					disk_write (filesys_disk, disk_inode->start + i, zeros); 
			}
			success = true; 
		} 
		#endif
		free (disk_inode);
	}
	return success;
}

/* Reads an inode from SECTOR
 * and returns a `struct inode' that contains it.
 * Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) {
	struct list_elem *e;
	struct inode *inode;

	/* Check whether this inode is already open. */
	for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
			e = list_next (e)) {
		inode = list_entry (e, struct inode, elem);
		if (inode->sector == sector) {
			inode_reopen (inode);
			return inode; 
		}
	}

	/* Allocate memory. */
	inode = malloc (sizeof *inode);
	if (inode == NULL)
		return NULL;

	/* Initialize. */
	list_push_front (&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
	disk_read (filesys_disk, inode->sector, &inode->data);
	return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode) {
	if (inode != NULL)
		inode->open_cnt++;
	return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode) {
	return inode->sector;
}

/* Closes INODE and writes it to disk.
 * If this was the last reference to INODE, frees its memory.
 * If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) {
	/* Ignore null pointer. */
	if (inode == NULL)
		return;

	/* Release resources if this was the last opener. */
	if (--inode->open_cnt == 0) {
		/* Remove from inode list and release lock. */
		list_remove (&inode->elem);

		/* Deallocate blocks if removed. */
		if (inode->removed) {
			#ifdef EFILESYS
			fat_remove_chain(sector_to_cluster(inode->sector), 0);
			#else
			free_map_release (inode->sector, 1);
			free_map_release (inode->data.start,
					bytes_to_sectors (inode->data.length));
			#endif 
		}

		free (inode); 
	}
}

/* Marks INODE to be deleted when it is closed by the last caller who
 * has it open. */
void
inode_remove (struct inode *inode) {
	ASSERT (inode != NULL);
	inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
 * Returns the number of bytes actually read, which may be less
 * than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) {
	uint8_t *buffer = buffer_;
	off_t bytes_read = 0;
	uint8_t *bounce = NULL;

	while (size > 0) {
		/* Disk sector to read, starting byte offset within sector. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually copy out of this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* Read full sector directly into caller's buffer. */
			disk_read (filesys_disk, sector_idx, buffer + bytes_read); 
		} else {
			/* Read sector into bounce buffer, then partially copy
			 * into caller's buffer. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}
			disk_read (filesys_disk, sector_idx, bounce);
			memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}
	free (bounce);

	return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
 * Returns the number of bytes actually written, which may be
 * less than SIZE if end of file is reached or an error occurs.
 * (Normally a write at end of file would extend the inode, but
 * growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
		off_t offset) {
	const uint8_t *buffer = buffer_;
	off_t bytes_written = 0;
	uint8_t *bounce = NULL;

	bool grow = false; // extend marker
	uint8_t zero[DISK_SECTOR_SIZE]; // buffer for zero padding


	if (inode->deny_write_cnt)
		return 0;

	/* Sector to write, starting byte offset within sector. */
	// check if there is enough memory for writing 'size' from 'offset'
	disk_sector_t sector_idx = byte_to_sector (inode, offset + size);
	#ifdef EFILESYS
	while (sector_idx == -1){
		grow = true; // mark that the extend occured
		off_t inode_len = inode_length(inode);

		// if(((offset + size - inode_len) <= DISK_SECTOR_SIZE) && (offset + size) / DISK_SECTOR_SIZE == inode_len / DISK_SECTOR_SIZE){ 
		// 	// in-sector growth(bytes to be written <= sector size, last byte after write lies within current last sector)
		// 	off_t inode_ofs = inode_len % DISK_SECTOR_SIZE;
		// 	inode->data.length += DISK_SECTOR_SIZE - inode_ofs;
		// 	// sector_idx = byte_to_sector(inode, offset + size);
		// 	break;
		// }
		
		// Extending file
		cluster_t endclst = sector_to_cluster(byte_to_sector(inode, inode_len - 1));
		cluster_t newclst = inode_len == 0 ? endclst : fat_create_chain(endclst);
		if (newclst == 0){
			break; //no more space in disk
			// #ifdef DBG Q. return 안하고, 그냥 있는 공간 가지고 write 진행? 근데 이런 상황은 테스트 안할 것 같기도
		}

		// Zero padding
		memset (zero, 0, DISK_SECTOR_SIZE);

		off_t inode_ofs = inode_len % DISK_SECTOR_SIZE;
		if(inode_ofs != 0)
			inode->data.length += DISK_SECTOR_SIZE - inode_ofs; // round up to DISK_SECTOR_SIZE for convinience
		// #ifdef Q. What if inode_ofs == 0? Unnecessary sector added -> unnecessary가 아님. extend 중이니까! 

		disk_write (filesys_disk, cluster_to_sector(newclst), zero); // zero padding for new cluster
		if (inode_ofs != 0){
			disk_read (filesys_disk, cluster_to_sector(endclst), zero);
			memset (zero + inode_ofs + 1 , 0, DISK_SECTOR_SIZE - inode_ofs);
			disk_write (filesys_disk, cluster_to_sector(endclst), zero); // zero padding for current cluster
			/*
					endclst          newclst (extended)
				 ---------------     ------------
				| data  0 0 0 0 | - | 0 0 0 0 0 |
				 ---------------     -----------
						↑ zero padding here!
			*/
		}

		inode->data.length += DISK_SECTOR_SIZE; // update file length
		sector_idx = byte_to_sector (inode, offset + size);
	}	
	#endif
	sector_idx = byte_to_sector(inode, offset); // start writing from offset
	while (size > 0) {
		/* Sector to write, starting byte offset within sector. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually write into this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* Write full sector directly to disk. */
			disk_write (filesys_disk, sector_idx, buffer + bytes_written); 
		} else {
			/* We need a bounce buffer. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}

			/* If the sector contains data before or after the chunk
			   we're writing, then we need to read in the sector
			   first.  Otherwise we start with a sector of all zeros. */
			if (sector_ofs > 0 || chunk_size < sector_left) 
				disk_read (filesys_disk, sector_idx, bounce);
			else
				memset (bounce, 0, DISK_SECTOR_SIZE);
			memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
			disk_write (filesys_disk, sector_idx, bounce); 
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
		sector_idx = byte_to_sector(inode, offset);
	}
	#ifdef EFILESYS
		if (grow == true) {
			inode->data.length = offset; // correct inode length
		}
	#endif
	free (bounce);
	disk_write(filesys_disk, inode->sector, &inode->data);

	return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
	void
inode_deny_write (struct inode *inode) 
{
	inode->deny_write_cnt++;
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
 * Must be called once by each inode opener who has called
 * inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) {
	ASSERT (inode->deny_write_cnt > 0);
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
	inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode) {
	return inode->data.length;
}

bool inode_isdir(struct inode *inode) {
	return inode->data.isdir;
}
