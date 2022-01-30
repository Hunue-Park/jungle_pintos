#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>

/* 
FAT Filesystem(디스크)

fat_start = 1
total_sectors = 8
fat_sectors = 3
data_start = 4
fat_length = 4 -> FAT table의 인덱스 개수

		+-------+-------+-------+-------+-------+-------+-------+-------+
		|Boot   |       FAT Table       |          Data Blocks          |
		|Sector |       |       |       |       |       |       |       |
		+-------+-------+-------+-------+-------+-------+-------+-------+
sector :    0       1       2       3       4       5       6       7     
cluster:                                    1       2       3       4
*/

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

/* 
	FAT Bitmap
		+------+------+------+------+------+
		|  0   |  1   |	 1   |	0   |  1   |
		+------+------+------+------+------+
index   :  0      1      2      3      4
cluster :  1      2      3      4      5
 */
struct bitmap* fat_bitmap;

cluster_t get_empty_cluster(void){
	/* fat_bitmap에서 칸 하나가 false인 인덱스를 true로 바꿔주고 인덱스를 리턴한다.
	   비트맵 맨 처음부터 찾아나간다. */
	size_t clst = bitmap_scan_and_flip(fat_bitmap, 0, 1, false);
	
	if (clst == BITMAP_ERROR)
		return 0;  // 실패
	
	return (cluster_t) clst;
}

void
fat_init (void) {
	fat_fs = calloc (1, sizeof (struct fat_fs));
	if (fat_fs == NULL)
		PANIC ("FAT init failed");

	// Read boot sector from the disk
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
	/* FAT 파일시스템의 boot block 내용 및 필드들을 초기화한다. */
	fat_boot_create ();
	fat_fs_init ();

	/* FAT 테이블을 메모리에 할당한다. */
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT creation failed");


	/* FAT 테이블에 루트 디렉토리에 대한 정보를 집어넣는다.
	   FAT 테이블에 루트 디렉토리 데이터 블록의 다음 데이터 블록 주소로 EOChain을 넣는다.
	   FAT[ROOT_DIR_CLUSTER] = EOChain(0x0FFFFFFF) */
	fat_put (ROOT_DIR_CLUSTER, EOChain);

	/* 루트 디렉토리를 디스크에 만들어준다. 클러스터 번호는 ROOT_DIR_CLUSTER이고,
	   디스크 내의 모든 데이터는 0으로 설정된다. */
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
	fat_fs->data_start = fat_fs->bs.fat_start + fat_fs->bs.fat_sectors;
	fat_fs->fat_length = sector_to_cluster(disk_size(filesys_disk)) - 1;
}

/*----------------------------------------------------------------------------*/
/* FAT handling                                                               */
/*----------------------------------------------------------------------------*/

/* Add a cluster to the chain.
 * If CLST is 0, start a new chain.
 * Returns 0 if fails to allocate a new cluster. */
cluster_t
fat_create_chain (cluster_t clst) {
	cluster_t new_clst = get_empty_cluster();  // fat bitmap의 처음부터 비어 있는 클러스터 찾기

	if (new_clst != 0){  // get_empty_cluster 리턴값이 0이면 실패.
		fat_put(new_clst, EOChain);
		if (clst != 0){  // 원래 클러스터가 0이면 새로 클러스터 체인을 만들어준다.
			fat_put(clst, new_clst);
		}
	}
	return new_clst;  // 실패하면 0 리턴
}

/* Remove the chain of clusters starting from CLST.
 * If PCLST is 0, assume CLST as the start of the chain. */
void
fat_remove_chain (cluster_t clst, cluster_t pclst) {
	/* TODO: Your code goes here. */

	/* 클러스터 체인 안의 모든 클러스터에 대해
	   (clst가 0이 아니고 끝(EOChain)도 아니면)
	   클러스터의 할당 여부를 false로 만든다. */
	// clst == 0 : 클러스터 체인이 없다
	// clst == EOChain : 클러스터 체인이 끝났다
	while(clst && clst != EOChain){
		bitmap_set(fat_bitmap, clst - 1, false);
		clst = fat_get(clst); // 다음 클러스터
	}

	if (pclst != 0){ 
		fat_put(pclst, EOChain);
	}

}

/* Update a value in the FAT table. */
/* FAT테이블의 각 인덱스(클러스터 번호)에 다음 클러스터 번호를 넣는다.
   ex. FAT[clst] = val */
void
fat_put (cluster_t clst, cluster_t val) {
	ASSERT(clst >= 1);

	/* FAT bitmap에서 해당 클러스터 할당 여부 True로 바꿔주기 */
	if (!bitmap_test(fat_bitmap, clst - 1))
		bitmap_mark(fat_bitmap, clst - 1);

	fat_fs->fat[clst - 1] = val;
}

/* Fetch a value in the FAT table. */
cluster_t
fat_get (cluster_t clst) {
	ASSERT(clst >= 1);

	/* 클러스터 번호 clst이 FAT 테이블 인덱스 범위를 넘어가거나
	   or FAT Bitmap에 해당 클러스터가 할당이 안 되어 있으면 실패. */
	if(clst > fat_fs->fat_length || !bitmap_test(fat_bitmap, clst-1))
		return 0;

	/* 클러스터 번호는 1부터 시작하지만 FAT 인덱스는 0부터 시작한다. */
	return fat_fs->fat[clst - 1];
}

/* Covert a cluster # to a sector number. */
disk_sector_t
cluster_to_sector (cluster_t clst) {
	ASSERT(clst >= 1);

	return fat_fs->data_start + (clst - 1) * SECTORS_PER_CLUSTER;
}

 

cluster_t
sector_to_cluster (disk_sector_t sector) {
	ASSERT(sector >= fat_fs->data_start);

	return sector - fat_fs->data_start + 1;
}