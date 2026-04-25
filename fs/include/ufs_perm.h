/* ===========================================================================
 * ufs_perm.h — Permission Manager (Plan 9 model)
 * ===========================================================================
 * BSD 3-Clause — Copyright (c) 2025, NeXs Operating System
 * ======================================================================== */

#ifndef UFS_PERM_H
#define UFS_PERM_H

#include "ufs_types.h"

/*
 * Check if 'uid' with group 'gid' has 'access' rights to inode.
 * access: DMREAD, DMWRITE, DMEXEC (or combination).
 */
bool ufs_perm_check(const UFS_Inode *inode,
                     const char *uid, const char *gid, u32 access);

/* Set permission mode + owner/group on an inode. */
void ufs_perm_set(UFS_Inode *inode, u32 mode,
                   const char *owner, const char *group);

/* Create default permission for a new file/directory. */
u32  ufs_perm_default(u32 parent_mode, bool is_dir);

#endif /* UFS_PERM_H */
