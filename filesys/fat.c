#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>

#define DBG_FAT

/* Should be less than DISK_SECTOR_SIZE */
struct fat_boot {
	unsigned int magic;
	unsigned int sectors_per_cluster; /* Fixed to 1 */
	unsigned int total_sectors; // total number of sectors in disk
	unsigned int fat_start; // start sector in disk to store FAT (fat_open, fat_close)
	unsigned int fat_sectors; /* Size of FAT in sectors. */
	unsigned int root_dir_cluster;
};

/* FAT FS */
struct fat_fs {
	struct fat_boot bs;
	unsigned int *fat; // FAT
	unsigned int fat_length; // how many clusters in the filesystem
	disk_sector_t data_start; // in which sector we can start to store files
	cluster_t last_clst;
	struct lock write_lock;
};

static struct fat_fs *fat_fs;

void fat_boot_create (void);
void fat_fs_init (void);

struct bitmap * fat_bitmap;

cluster_t get_empty_cluster () {
	size_t clst = bitmap_scan_and_flip(fat_bitmap, 0, 1, false) + 1; // index starts with 0, but cluster starts with 1
	if (clst == BITMAP_ERROR)  
		return 0;
	else
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

	fat_bitmap = bitmap_create(fat_fs->fat_length); // #ifdef DBG Q. 0번째는 ROOT_DIR_CLUSTER니까 1로 채워넣어야 하지 않을까?
	#ifdef DBG_FAT
	printf("(fat_create) fat len : %d, sector of last FAT entry : %d\n", fat_fs->fat_length, cluster_to_sector(fat_fs->fat_length));
	#endif
	#ifdef DBG_FAT
	printf("(fat_create) create fat_bitmap - fat_len %d, bitmap size %d\n", fat_fs->fat_length, bitmap_size(fat_bitmap));
	#endif
}

void init_fat_bitmap(void){
	for(int clst = 0; clst < fat_fs->fat_length; clst++){
		// FAT occupied with EOC or any value (next cluster)
		if(fat_fs->fat[clst])
			bitmap_set(fat_bitmap, clst, true);
	}
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
			disk_read (filesys_disk, fat_fs->bs.fat_start + i, buffer + bytes_read);
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
#ifdef DBG_FAT
	printf("\n--fat_create start--\n");
#endif

	// Create FAT boot
	fat_boot_create ();
	fat_fs_init ();

#ifdef DBG_FAT
	//printf("fat_create) fat len : %d, sector of last FAT entry : %d\n", fat_fs->fat_length, cluster_to_sector(fat_fs->fat_length));
#endif

	// Create bitmap for managing FAT entry status (empty/occupied)
	//fat_bitmap = bitmap_create(fat_fs->fat_length); // #ifdef DBG Q. 0번째는 ROOT_DIR_CLUSTER니까 1로 채워넣어야 하지 않을까?

#ifdef DBG_FAT
	//printf("(fat_create) create fat_bitmap - fat_len %d, bitmap size %d\n", fat_fs->fat_length, bitmap_size(fat_bitmap));
#endif

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
	    / (DISK_SECTOR_SIZE / sizeof (cluster_t) * SECTORS_PER_CLUSTER + 1) + 1; // 특별한 의미는 없는 듯. 그냥 FAT 사이즈 미리 결정
	fat_fs->bs = (struct fat_boot){
	    .magic = FAT_MAGIC,
	    .sectors_per_cluster = SECTORS_PER_CLUSTER,
	    .total_sectors = disk_size (filesys_disk),
	    .fat_start = 1, // Sector 0 is BOOT_SECTOR
	    .fat_sectors = fat_sectors,
	    .root_dir_cluster = ROOT_DIR_CLUSTER,
	};
	// ex) total sectors 20160, fat_sectors = 157 -> pintos-mkdisk tmp.dsk 10 값에 따라 달라짐.
	// tmp.dsk 2라면 

#ifdef DBG_FAT
	printf("(fat boot create) fat_sectors: %d, total sectors : %d, disk capa : %d\n", fat_sectors, disk_size (filesys_disk), filesys_disk->capacity);
#endif
}

void
fat_fs_init (void) {
	/* TODO: Your code goes here. */

	// Invariant : SECTORS_PER_CLUSTER == 1
	ASSERT(SECTORS_PER_CLUSTER == 1);

	// how many clusters in the filesystem
	// fat_fs->fat_length = fat_fs->bs.fat_sectors * DISK_SECTOR_SIZE / sizeof (cluster_t); // ex) 157 sectors * 512 bytes/sector % 4 bytes/cluster = 20096 clusters in FAT

	// in which sector we can start to store FAT on the disk
	fat_fs->data_start = fat_fs->bs.fat_start + fat_fs->bs.fat_sectors;
	fat_fs->fat_length = sector_to_cluster(disk_size(filesys_disk))-1;

#ifdef DBG_FAT
	printf("fat len : %d, sector of last FAT entry : %d\n", fat_fs->fat_length, cluster_to_sector(fat_fs->fat_length));
#endif
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
	cluster_t new_clst = get_empty_cluster();
	if (new_clst != 0){
		fat_put(new_clst, EOChain);
		if (clst != 0){
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
	while(clst && clst != EOChain){
		bitmap_set(fat_bitmap, clst - 1, false);
		clst = fat_get(clst);
	}
	if (pclst != 0){
		fat_put(pclst, EOChain);
	}
}

/* Update a value in the FAT table. */
void
fat_put (cluster_t clst, cluster_t val) {
	/* TODO: Your code goes here. */
	ASSERT(clst >= 1);
	if(!bitmap_test(fat_bitmap, clst - 1)) bitmap_mark(fat_bitmap, clst - 1);
	fat_fs->fat[clst - 1] = val;
}

/* Fetch a value in the FAT table. */
cluster_t
fat_get (cluster_t clst) {
	/* TODO: Your code goes here. */
	ASSERT(clst >= 1);

	if (clst > fat_fs->fat_length || !bitmap_test(fat_bitmap, clst - 1))
		return 0; // error handling for fat_get(EOChain) or empty
	return fat_fs->fat[clst - 1];
}

/* Covert a cluster # to a sector number. */
disk_sector_t
cluster_to_sector (cluster_t clst) {
	/* TODO: Your code goes here. */
	ASSERT(clst >= 1);

	// clst 1 -> sector 158 (fat_fs->data_start)
	return fat_fs->data_start + (clst - 1) * SECTORS_PER_CLUSTER;
}

// Prj 4-1 : reverse function for clst_to_sect
cluster_t 
sector_to_cluster (disk_sector_t sector) {
	ASSERT(sector >= fat_fs->data_start);

	// sector 158 -> clst 1
	return sector - fat_fs->data_start + 1;
}
