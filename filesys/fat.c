#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "lib/kernel/bitmap.h"
#include <stdio.h>
#include <string.h>

/* Should be less than DISK_SECTOR_SIZE */
struct fat_boot {
	unsigned int magic;
	unsigned int sectors_per_cluster; /* Fixed to 1 */
	unsigned int total_sectors;
	unsigned int fat_start;
	unsigned int fat_sectors; /* Size of FAT in sectors. */
	unsigned int root_dir_cluster;
};

/* FAT FS */
struct fat_fs {
	struct fat_boot bs;
	unsigned int *fat;
	unsigned int fat_length;
	disk_sector_t data_start;
	cluster_t last_clst; 
	struct lock write_lock;
};

static struct fat_fs *fat_fs;

void fat_boot_create (void);
void fat_fs_init (void);

struct bitmap * fat_bitmap;

/* Project 4-1: FAT */
// 빈 클러스터를 가져온다.
cluster_t get_empty_cluster () {
	// fat_bitmap을 
	size_t clst = bitmap_scan_and_flip(fat_bitmap, 0, 1, false) + 1; // 인덱스는 0부터 시작하나 cluster는 1부터 시작
	if (clst == BITMAP_ERROR)
		return 0;
	else
		return (cluster_t) clst;
}

/* FAT 테이블 초기화하는 함수 */
void
fat_init (void) {
	// FAT 담을 공간을 힙에 할당 -> 구조체 자체는 위에 선언해둠.
	fat_fs = calloc (1, sizeof (struct fat_fs));
	if (fat_fs == NULL)
		PANIC ("FAT init failed");

	// Read boot sector from the disk: 디스크에서 FAT 읽어서 calloc담은 공간에 저장
	unsigned int *bounce = malloc (DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT init failed");
	disk_read (filesys_disk, FAT_BOOT_SECTOR, bounce);
	memcpy (&fat_fs->bs, bounce, sizeof (fat_fs->bs));
	free (bounce);

	// Extract FAT info
	if (fat_fs->bs.magic != FAT_MAGIC)
		fat_boot_create ();
	fat_fs_init ();

	/* Project 4-1: FAT */
	fat_bitmap = bitmap_create(fat_fs->fat_length); // 
}

void
fat_open (void) {
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT load failed");

	// Load FAT directly from the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_read = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_read;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_read (filesys_disk, fat_fs->bs.fat_start + i,
			           buffer + bytes_read);
			bytes_read += DISK_SECTOR_SIZE;
		} else {
			uint8_t *bounce = malloc (DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT load failed");
			disk_read (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			memcpy (buffer + bytes_read, bounce, bytes_left);
			bytes_read += bytes_left;
			free (bounce);
		}
	}
}

void
fat_close (void) {
	// Write FAT boot sector
	uint8_t *bounce = calloc (1, DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT close failed");
	memcpy (bounce, &fat_fs->bs, sizeof (fat_fs->bs));
	disk_write (filesys_disk, FAT_BOOT_SECTOR, bounce);
	free (bounce);

	// Write FAT directly to the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_wrote = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_wrote;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_write (filesys_disk, fat_fs->bs.fat_start + i,
			            buffer + bytes_wrote);
			bytes_wrote += DISK_SECTOR_SIZE;
		} else {
			bounce = calloc (1, DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT close failed");
			memcpy (bounce, buffer + bytes_wrote, bytes_left);
			disk_write (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			bytes_wrote += bytes_left;
			free (bounce);
		}
	}
}

void
fat_create (void) {
	// Create FAT boot
	fat_boot_create ();
	fat_fs_init ();

	// Create FAT table
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT creation failed");

	// Set up ROOT_DIR_CLST
	fat_put (ROOT_DIR_CLUSTER, EOChain);

	// Fill up ROOT_DIR_CLUSTER region with 0
	uint8_t *buf = calloc (1, DISK_SECTOR_SIZE);
	if (buf == NULL)
		PANIC ("FAT create failed due to OOM");
	disk_write (filesys_disk, cluster_to_sector (ROOT_DIR_CLUSTER), buf);
	free (buf);
}

void
fat_boot_create (void) {
	unsigned int fat_sectors =
	    (disk_size (filesys_disk) - 1)
	    / (DISK_SECTOR_SIZE / sizeof (cluster_t) * SECTORS_PER_CLUSTER + 1) + 1;
	fat_fs->bs = (struct fat_boot){
	    .magic = FAT_MAGIC,
	    .sectors_per_cluster = SECTORS_PER_CLUSTER,
	    .total_sectors = disk_size (filesys_disk),
	    .fat_start = 1,
	    .fat_sectors = fat_sectors,
	    .root_dir_cluster = ROOT_DIR_CLUSTER,
	};
}


void
fat_fs_init (void) {
	/* TODO: Your code goes here. */
	/* Project 4-1: FAT */
	ASSERT(SECTORS_PER_CLUSTER==1);
	/*fat_length: 파일 시스템에 얼마나 클러스터가 많은 지를 저장*/
	fat_fs->fat_length = fat_fs->bs.total_sectors/SECTORS_PER_CLUSTER;
	//fat_fs->fat_length = sector_to_cluster(disk_size(filesys_disk)) -1;
	
	/*data_start: 파일 저장 시작할 수 있는 섹터 위치 저장 => DATA Sector 시작 지점*/
	fat_fs->data_start = fat_fs->bs.fat_start + fat_fs->bs.fat_sectors; // data_start = 158
	
	// 이외 값들 초기화 -> 이건 아닌듯
	// fat_fs->fat = 0;
	// fat_fs->last_clst = 0;
	// lock_init(&fat_fs->write_lock);
}

/*----------------------------------------------------------------------------*/
/* FAT handling                                                               */
/*----------------------------------------------------------------------------*/

/* Add a cluster to the chain.
 * If CLST is 0, start a new chain.
 * Returns 0 if fails to allocate a new cluster. */
cluster_t
fat_create_chain (cluster_t clst) {
	/* TODO: Your code goes here. */
	/* Project 4-1: FAT */
	cluster_t new_clst = get_empty_cluster(); // 빈 클러스터를 bitmap에서 가져온다.
	if (new_clst != 0) {
		fat_put(new_clst, EOChain);
		if (clst != 0) {
			fat_put(clst, new_clst);
		}
	}
	return new_clst;
}

/* Remove the chain of clusters starting from CLST.
 * If PCLST is 0, assume CLST as the start of the chain. */
void
fat_remove_chain (cluster_t clst, cluster_t pclst) {
	/* TODO: Your code goes here. */
	/* Project 4-1: FAT */
	while (clst != EOChain) { // EOChain: End of cluster chain
		bitmap_set(fat_bitmap, clst-1, false);
		clst = fat_get(clst);
	}
	if (pclst != 0) {
		fat_put(pclst, EOChain);
	}
}

/* 
Update a value in the FAT table. 
fat 테이블에 해당 인덱스 값을 업데이트
*/
void
fat_put (cluster_t clst, cluster_t val) {
	/* TODO: Your code goes here. */
	ASSERT(clst >= 1);
	if(!bitmap_test(fat_bitmap, clst-1)) { // 클러스터 값은 1부터 시작. 해당 인덱스가 fat_bitmap에 들어있는지 체크.
		bitmap_mark(fat_bitmap, clst-1); // 인덱스가 없다면 비트맵에서 해당 인덱스에 true로 변경
	}
	fat_fs->fat[clst-1] = val-1; // fat
}

/* Fetch a value in the FAT table. */
cluster_t
fat_get (cluster_t clst) {
	/* TODO: Your code goes here. */
	ASSERT(clst >=1);
	/* 만약 클러스터 번호가 fat_length를 넘어가거나 fat 테이블에 들어있지 않다면 */
	if (clst > fat_fs->fat_length || !bitmap_test(fat_bitmap, clst-1))
		return 0;
	/* fat 테이블에서 해당 인덱스에 들어있는 값을 반환*/
	return fat_fs->fat[clst-1];
}

/* Covert a cluster # to a sector number. */
disk_sector_t
cluster_to_sector (cluster_t clst) {
	/* TODO: Your code goes here. */
	ASSERT(clst >= 1);

	return fat_fs->data_start + (clst-1) * SECTORS_PER_CLUSTER; // 데이터 시작 위치부터 클러스터당 섹터 수 * 인자로 받은 클러스터 번호를 곱한다.
}


/* Project 4-1: FAT */
cluster_t sector_to_cluster (disk_sector_t sector) {
	ASSERT(sector >= fat_fs->data_start);

	return sector - fat_fs->data_start +1;
}