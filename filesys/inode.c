#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

#include "filesys/fat.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

// /* On-disk inode.
//  * Must be exactly DISK_SECTOR_SIZE bytes long. */
// struct inode_disk {
// 	disk_sector_t start;                /* First data sector. */
// 	off_t length;                       /* File size in bytes. */
// 	unsigned magic;                     /* Magic number. */
// 	uint32_t unused[125];               /* Not used. */
// };

// /* In-memory inode. */
// struct inode {
// 	struct list_elem elem;              /* Element in inode list. */
// 	disk_sector_t sector;               /* Sector number of disk location. */
// 	int open_cnt;                       /* Number of openers. */
// 	bool removed;                       /* True if deleted, false otherwise. */
// 	int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
// 	struct inode_disk data;             /* Inode content. */
// };


/* Returns the number of sectors to allocate for an inode SIZE
 * bytes long. */
static inline size_t
bytes_to_sectors (off_t size) {
	return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}


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
			for (unsigned i = 0; i < (pos / DISK_SECTOR_SIZE); i++){
				clst = fat_get(clst); // 계속 다음 클러스터를 찾아간다.
				if(clst == 0)  // 아이노드에 데이터가 없다.
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

/* 길이가 LENGTH byte인 아이노드를 만든다. 
   그 후 디스크의 SECTOR 섹터에 이 아이노드를 저장한다. */
bool
inode_create (disk_sector_t sector, off_t length, bool isdir) {
	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT (length >= 0);

	/* If this assertion fails, the inode structure is not exactly
	 * one sector in size, and you should fix that. */
	ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

	/* 디스크 아이노드 초기화 */
	disk_inode = calloc (1, sizeof *disk_inode);
	if (disk_inode != NULL) {
		size_t sectors = bytes_to_sectors (length);  // 해당 아이노드가 차지하게 될 디스크 섹터 개수
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;
		/* ------------------Project 4. File system -------------------- */
		disk_inode->isdir = isdir;
		#ifdef EFILESYS
		cluster_t clst = sector_to_cluster(sector); // 아이노드가 저장될 디스크의 클러스터 번호
		cluster_t new_clst = clst;

		/* disk inode가 디스크에서 차지할 클러스터들의 정보를 메모리에 저장
		   - 디스크에서 시작 섹터 번호 정하기
		   - FAT 테이블 업데이트
		   - 클러스터 체인 만들기 */
		// 디스크에 아이노드를 저장시킬 때 그 클러스터를 시작점으로 하는
	    // 클러스터 체인을 만들고 시작 섹터를 start 필드에 넣는다.
		// 즉 start 필드는 해당 아이노드가 디스크에서 시작하는 섹터 번호이다.
		if (sectors == 0)
			disk_inode->start = cluster_to_sector(fat_create_chain(new_clst));

		// disk inode가 저장될 클러스터들의 정보를 FAT테이블에 업데이트하면서
		// 각각의 클러스터를 클러스터 체인에 저장한다.
		int i;
		for (int i = 0; i < sectors; i++){
			new_clst = fat_create_chain(new_clst);
			if (new_clst == 0){  // chaining 실패하면 다 지워버린다.
				fat_remove_chain(clst, 0);
				free(disk_inode);
				return false;
			}
			// 아이노드의 시작 클러스터를 아이노드 내에 저장한다.
			if (i == 0){
				clst = new_clst;  // 아이노드의 시작점 clst
				disk_inode->start = cluster_to_sector(new_clst); // 시작
			}
		}

		/* disk inode의 내용을 디스크에 저장. */
		disk_write(filesys_disk, sector, disk_inode);
		if (sectors > 0){
			static char zeros[DISK_SECTOR_SIZE];
			for(i = 0; i < sectors; i++){
				ASSERT(clst != 0 || clst != EOChain);
				disk_write(filesys_disk, cluster_to_sector(clst), zeros);
				clst = fat_get(clst);
			}
		}
		success = true;
		/* ------------------Project 4. File system -------------------- */
		#else
		/* 기존에는 아이노드들의 리스트를 비트맵 형태로 관리하고 있었다. */
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
/* 디스크 영역 SECTOR에 있는 disk inode를 디스크에서 읽어와서 
   incore inode로써 메모리에 올린다. */
struct inode *
inode_open (disk_sector_t sector) {
	struct list_elem *e;
	struct inode *inode;

	/* Check whether this inode is already open. */
	/* 열려있는 아이노드들을 리스트로 관리하는데, 이 때 이미 이 아이노드가
	   열려있는지 확인하고, 열려있으면 해당 아이노드를 open한 횟수를 1 늘려준다. */
	for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
			e = list_next (e)) {
		inode = list_entry (e, struct inode, elem);
		if (inode->sector == sector) {
			inode_reopen (inode);
			return inode; 
		}
	}

	/* Allocate memory. */
	/* incore 아이노드를 위한 메모리 공간을 할당한다. */
	inode = malloc (sizeof *inode);
	if (inode == NULL)
		return NULL;

	/* Initialize. */
	/* 열려 있는 아이노드를 관리하는 리스트에 해당 아이노들을 집어넣고
	   필드들을 초기화한 다음, 디스크에서 disk inode 정보들을 읽어온다. */
	list_push_front (&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
	disk_read (filesys_disk, inode->sector, &inode->data);  // 디스크에서 disk inode를 읽어옴
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
/*  */
void
inode_close (struct inode *inode) {
	if (inode == NULL)
		return;

	/* 이 프로세스가 아이노드를 열고 있는 마지막 프로세스라면 자원들을 해제해준다. */
	if (--inode->open_cnt == 0) {    // reference count를 1 낮추고
		list_remove (&inode->elem);  // open inode list에서 지워준다.

		if (inode->removed) {  // 지워져야 할 아이노드라면 할당된 클러스터를 다 반환한다. 
			#ifdef EFILESYS
			fat_remove_chain(sector_to_cluster(inode->sector), 0); // 클러스터 할당 여부 false로.
			#else
			free_map_release (inode->sector, 1);
			free_map_release (inode->data.start, bytes_to_sectors (inode->data.length)); 
		}

		free (inode); // 아이노드 구조체도 메모리에서 반환한다.
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
		disk_sector_t sector_idx = byte_to_sector (inode, offset);  // 해당 오프셋이 어느 sector에 있는지
		int sector_ofs = offset % DISK_SECTOR_SIZE;  
		// sector_idx에서의 오프셋

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;  // 파일 전체에서 offset 뒤에 남겨진 데이터 크기
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;  // sector_idx에서 offset 뒤에 남겨진 데이터 크기
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

	if (inode->deny_write_cnt)
		return 0;

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
	}
	free (bounce);

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
