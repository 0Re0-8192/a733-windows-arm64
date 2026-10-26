#ifndef A733_ACTUAL_FAT_WRITE_TARGET_H
#define A733_ACTUAL_FAT_WRITE_TARGET_H

/* H641 fixed FAT0641.IMG allocation, recorded by h641_fat32_stage.json.
 * The backing file is a single complete ESP extent and is separate from the
 * boot WIM, golden NTFS image, GPT, and physical partition-2 FAT32 metadata.
 */
#define A733_FAT_WRITE_TARGET_PHYSICAL_LBA 0x004D49B0U
#define A733_FAT_WRITE_TARGET_SECTORS 0x0003F000U
#define A733_FAT_WRITE_TARGET_END_LBA 0x005139B0U

#if A733_FAT_WRITE_TARGET_PHYSICAL_LBA < 65536U || \
    A733_FAT_WRITE_TARGET_END_LBA > 8454144U
#error Disposable FAT32 write extent is outside the owned ESP
#endif
#if (A733_FAT_WRITE_TARGET_PHYSICAL_LBA + A733_FAT_WRITE_TARGET_SECTORS) != \
    A733_FAT_WRITE_TARGET_END_LBA
#error Disposable FAT32 write extent geometry mismatch
#endif

#endif
