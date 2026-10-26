#ifndef A733_ACTUAL_NTFS_WRITE_TARGET_H
#define A733_ACTUAL_NTFS_WRITE_TARGET_H

/* Actual placement, not the H639 bounds model's example address.
 * analysis/h640_ntfs_stage.json SHA256:
 * 3769FE1CF0DD41B7BA0A8292B3D8053627E788DD5B1BD8C6C3145A438D2BB0C3
 * Postallocation baseline SHA256:
 * 030D5372EF53C06A342105058372D6EFBDBE910744E8C92E1D5D4D71C5F21F09
 * Owned UMS serial<ums-serial>, NTFS0640.IMG, one complete allocation.
 * Image SHA256111D446EEC93B40E7811B4C0462DDBDFA2CE4B399C3996BB9B951DC052D20712.
 * NTFS serial<ntfs-serial>. Golden NTFS0636 ends at this extent's start.
 * The stage independently verifies non-overlap with the FULL boot.wim allocation.
 */
#define A733_WRITE_TARGET_PHYSICAL_LBA 0x002D5008U
#define A733_WRITE_TARGET_SECTORS 0x0003F000U
#define A733_WRITE_TARGET_END_LBA 0x00314008U

#if A733_WRITE_TARGET_PHYSICAL_LBA < (0x00296008U + 0x0003F000U)
#error Disposable write extent overlaps protected golden NTFS
#endif
#if (A733_WRITE_TARGET_PHYSICAL_LBA + A733_WRITE_TARGET_SECTORS) != A733_WRITE_TARGET_END_LBA
#error Disposable write extent geometry mismatch
#endif
#if A733_WRITE_TARGET_PHYSICAL_LBA < 65536U || A733_WRITE_TARGET_END_LBA > 8454144U
#error Disposable write extent outside owned ESP
#endif

#endif
