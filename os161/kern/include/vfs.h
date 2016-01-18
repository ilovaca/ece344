#ifndef _VFS_H_
#define _VFS_H_

/*
 * Virtual File System layer functions.
 *
 * The VFS layer translates operations on abstract on-disk files or
 * pathnames to operations on specific files on specific filesystems.
 */

struct uio;    /* kernel or userspace I/O buffer (uio.h) */
struct device; /* abstract structure for a device (dev.h) */
struct fs;     /* abstract structure for a filesystem (fs.h) */
struct vnode;  /* abstract structure for an on-disk file (vnode.h) */

/*
 * VFS layer low-level operations. 
 * See vnode.h for direct operations on vnodes.
 * See fs.h for direct operations on filesystems/devices.
 *
 *    vfs_setcurdir - change current directory of current thread by vnode
 *    vfs_clearcurdir - change current directory of current thread to "none"
 *    vfs_getcurdir - retrieve vnode of current directory of current thread
 *    vfs_sync      - force all dirty buffers to disk
 *    vfs_getroot   - get root vnode for the filesystem named DEVNAME
 *    vfs_getdevname - get mounted device name for the filesystem passed in
 */

int vfs_setcurdir(struct vnode *dir);
int vfs_clearcurdir(void);
int vfs_getcurdir(struct vnode **retdir);
int vfs_sync(void);
int vfs_getroot(const char *devname, struct vnode **result);
const char *vfs_getdevname(struct fs *fs);

/*
 * VFS layer mid-level operations.
 *
 *    vfs_lookup     - Like VOP_LOOKUP, but takes a full device:path name,
 *                     or a name relative to the current directory, and
 *                     goes to the correct filesystem.
 *    vfs_lookparent - Likewise, for VOP_LOOKPARENT.
 *
 * Both of these may destroy the path passed in.
 */

int vfs_lookup(char *path, struct vnode **result);
int vfs_lookparent(char *path, struct vnode **result,
		   char *buf, size_t buflen);

/*
 * VFS layer high-level operations on pathnames
 * Because namei may destroy pathnames, these all may too.
 *
 *    vfs_open         - Open or create a file. FLAGS per the syscall. 
 *    vfs_readlink     - Read contents of a symlink into a uio.
 *    vfs_symlink      - Create a symlink PATH containing contents CONTENTS.
 *    vfs_mkdir        - Create a directory.
 *    vfs_link         - Create a hard link to a file.
 *    vfs_remove       - Delete a file.
 *    vfs_rmdir        - Delete a directory.
 *    vfs_rename       - rename a file.
 *
 *    vfs_chdir  - Change current directory of current thread by name.
 *    vfs_getcwd - Retrieve name of current directory of current thread.
 *
 *    vfs_close  - Close a vnode opened with vfs_open. Does not fail.
 *                 (See vfspath.c for a discussion of why.)
 */

int vfs_open(char *path, int openflags, struct vnode **ret);
void vfs_close(struct vnode *vn);
int vfs_readlink(char *path, struct uio *data);
int vfs_symlink(const char *contents, char *path);
int vfs_mkdir(char *path);
int vfs_link(char *oldpath, char *newpath);
int vfs_remove(char *path);
int vfs_rmdir(char *path);
int vfs_rename(char *oldpath, char *newpath);

int vfs_chdir(char *path);
int vfs_getcwd(struct uio *buf);

/*
 * Misc
 *
 *    vfs_bootstrap - Call during system initialization to allocate 
 *                    structures.
 *
 *    vfs_initbootfs - Call during system initialization to allocate
 *                    bootfs-related structures. (Called from 
 *                    vfs_bootstrap.)
 *
 *    vfs_setbootfs - Set the filesystem that paths beginning with a
 *                    slash are sent to. If not set, these paths fail
 *                    with ENOENT. The argument should be the device
 *                    name or volume name for the filesystem (such as
 *                    "lhd0:") but need not have the trailing colon.
 *
 *    vfs_clearbootfs - Clear the bootfs filesystem. This should be
 *                    done during shutdown so that the filesystem in
 *                    question can be unmounted.
 *
 *    vfs_adddev    - Add a device to the VFS named device list. If
 *                    MOUNTABLE is zero, the device will be accessible
 *                    as "DEVNAME:". If the mountable flag is set, the
 *                    device will be accessible as "DEVNAMEraw:" and
 *                    mountable under the name "DEVNAME". Thus, the
 *                    console, added with MOUNTABLE not set, would be
 *                    accessed by pathname as "con:", and lhd0, added
 *                    with mountable set, would be accessed by
 *                    pathname as "lhd0raw:" and mounted by passing
 *                    "lhd0" to vfs_mount.
 *
 *    vfs_addfs     - Add a hardwired filesystem to the VFS named device
 *                    list. It will be accessible as "devname:". This is
 *                    intended for filesystem-devices like emufs, and
 *                    gizmos like Linux procfs or BSD kernfs, not for
 *                    mounting filesystems on disk devices.
 *
 *    vfs_mount     - Attempt to mount a filesystem on a device. The
 *                    device named by DEVNAME will be looked up and 
 *                    passed, along with DATA, to the supplied function
 *                    MOUNTFUNC, which should create a struct fs and
 *                    return it in RESULT.
 *
 *    vfs_unmount   - Unmount the filesystem presently mounted on the
 *                    specified device.
 *
 *    vfs_unmountall - Unmount all mounted filesystems.
 */

void vfs_bootstrap(void);

void vfs_initbootfs(void);
int vfs_setbootfs(const char *fsname);
void vfs_clearbootfs(void);

int vfs_adddev(const char *devname, struct device *dev, int mountable);
int vfs_addfs(const char *devname, struct fs *fs);

int vfs_mount(const char *devname, void *data, 
	      int (*mountfunc)(void *data,
			       struct device *dev, 
			       struct fs **result));
int vfs_unmount(const char *devname);
int vfs_unmountall(void);

#endif /* _VFS_H_ */
