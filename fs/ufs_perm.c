/* ===========================================================================
 * ufs_perm.c — Permission Manager (Plan 9 model)
 * ===========================================================================
 * BSD 3-Clause — Copyright (c) 2025, NeXs Operating System
 * ======================================================================== */

#include "include/ufs_perm.h"

/* ---- String compare (local, avoids circular dep) ----------------------- */
static bool streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* ---- Permission Check -------------------------------------------------- */

bool ufs_perm_check(const UFS_Inode *inode,
                     const char *uid, const char *gid, u32 access) {
    u32 mode = inode->mode;

    /* None user "none" always denied (Plan 9 convention) */
    if (streq(uid, "none")) return false;

    /* Owner check (bits 8-6) */
    if (streq(uid, inode->owner)) {
        u32 owner_perm = (mode >> 6) & 7;
        return (owner_perm & access) == access;
    }

    /* Group check (bits 5-3) */
    if (streq(gid, inode->group)) {
        u32 group_perm = (mode >> 3) & 7;
        return (group_perm & access) == access;
    }

    /* Other (bits 2-0) */
    u32 other_perm = mode & 7;
    return (other_perm & access) == access;
}

/* ---- Permission Set ---------------------------------------------------- */

void ufs_perm_set(UFS_Inode *inode, u32 mode,
                   const char *owner, const char *group) {
    /* Preserve type flags (high bits), update permission bits */
    inode->mode = (inode->mode & 0xFFFFFE00) | (mode & 0x1FF);

    /* Copy owner/group strings */
    u32 i;
    for (i = 0; owner[i] && i < 31; i++) inode->owner[i] = owner[i];
    inode->owner[i] = '\0';
    for (i = 0; group[i] && i < 31; i++) inode->group[i] = group[i];
    inode->group[i] = '\0';
}

/* ---- Default Permission ------------------------------------------------ */

u32 ufs_perm_default(u32 parent_mode, bool is_dir) {
    /*
     * Plan 9: new file inherits parent's group permissions.
     * Default: owner=rwx, group=rx, other=rx for dirs
     *          owner=rw,  group=r,  other=r  for files
     */
    u32 base = is_dir ? (DMDIR | 0755) : 0644;

    /* Inherit parent's DMAPPEND and DMEXCL flags */
    base |= (parent_mode & (DMAPPEND | DMEXCL));

    return base;
}
