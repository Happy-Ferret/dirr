// These settings can be changed by user.

// Set this to 1 if your passwd file is quick to load.
#define PRELOAD_UIDGID 0

// Set this to 1 if you want dot-files always shown.
#define ALWAYS_SHOW_DOTFILES 0

// File containing the settings. Can be changed.
#define SETTINGSFILE "dirrsets.hh"

#define HAVE_TERMIO_H 
#define HAVE_SYS_STAT_H 
#define HAVE_SYS_TYPES_H 
#define HAVE_STATFS_SYS_VFS_H
#define HAVE_STATFS
#undef HAVE_STATFS_SYS_STATFS_H // Untested, redundant
#undef HAVE_STATFS_SYS_MOUNT_H // Untested, redundant
#undef HAVE_STATFS_SYS_STATVFS_H // Untested, redundant
#undef HAVE_IOCTL_UNISTD_H
#define HAVE_IOCTL_SYS_IOCTL_H
#define HAVE_IOCTL
#undef HAVE_IOCTL_SYS_LINUX_IOCTL_H // Untested, redundant
#define HAVE_TCGETATTR_TERMIOS_H
#define HAVE_TCGETATTR
#define HAVE_READDIR_DIRENT_H
#define HAVE_READDIR
#undef HAVE_READDIR_DIRECT_H // Untested, redundant
#undef HAVE_READDIR_DIR_H // Untested, redundant
#define HAVE_GETGRENT_GRP_H
#define HAVE_GETGRENT
#define HAVE_GETPWENT_PWD_H
#define HAVE_GETPWENT
#define HAVE_GETGRGID_GRP_H
#define HAVE_GETGRGID
#define HAVE_GETPWUID_PWD_H
#define HAVE_GETPWUID
#define HAVE_FLOCK_SYS_FILE_H
#define HAVE_FLOCK
#define HAVE_STDIO_FILEBUF
