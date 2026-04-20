#ifndef WIN_COMPAT_H
#define WIN_COMPAT_H

/*
 * compat.h — POSIX ↔ Windows compatibility layer
 *
 * This header maps commonly used POSIX functions and macros to their
 * Windows equivalents so that cross-platform source files (e.g.
 * converter.c) can be compiled on both POSIX and Windows without
 * cluttering every call site with #ifdef blocks.
 *
 * Include this header BEFORE any other project headers in files that
 * need cross-platform path and file I/O primitives.
 *
 * Platform guards: all Windows mappings are wrapped in #ifdef _WIN32 so
 * that including this file on Linux or macOS is a no-op.
 */

#ifdef _WIN32

/* -----------------------------------------------------------------------
 * Windows headers required for the types and constants used below.
 * io.h provides _access() / _mkdir().
 * direct.h provides _mkdir() on some MinGW configurations.
 * ----------------------------------------------------------------------- */
#include <io.h>       /* _access */
#include <direct.h>   /* _mkdir  */
#include <sys/stat.h> /* struct stat, _S_IFDIR, _S_IFREG */

/* -----------------------------------------------------------------------
 * access() — POSIX file accessibility check
 *
 * On Windows the CRT provides _access() with the same semantics.
 * The mode constants (R_OK, W_OK, F_OK) have the same numeric values on
 * MSVC and MinGW so we only need to alias the function name.
 *
 * Usage:
 *   if (access(path, F_OK) == 0) { ... }  // path exists
 *   if (access(path, W_OK) == 0) { ... }  // path is writable
 * ----------------------------------------------------------------------- */
#ifndef F_OK
#  define F_OK 0   /* test for existence  */
#endif
#ifndef R_OK
#  define R_OK 4   /* test for read permission  */
#endif
#ifndef W_OK
#  define W_OK 2   /* test for write permission */
#endif
#ifndef X_OK
#  define X_OK 1   /* test for execute permission (not meaningful on Windows — _access() treats it as F_OK) */
#endif

#define access(path, mode)  _access((path), (mode))

/* -----------------------------------------------------------------------
 * mkdir() — POSIX single-directory creation
 *
 * POSIX mkdir() takes two arguments (path, mode); Windows _mkdir() takes
 * only one (path) — the mode argument is ignored on Windows NTFS because
 * permissions are managed through ACLs.
 *
 * The macro discards the mode argument to provide a compatible interface.
 *
 * For recursive creation use win_mkdir_p() from platform.h instead.
 * ----------------------------------------------------------------------- */
#define mkdir(path, mode)   _mkdir(path)

/* -----------------------------------------------------------------------
 * Stat-based type macros (S_ISDIR, S_ISREG)
 *
 * POSIX defines S_ISDIR(m) and S_ISREG(m) as macros that test the file
 * type bits of a stat.st_mode value.  The Windows CRT uses _S_IFDIR and
 * _S_IFREG instead of the standard S_IFDIR/S_IFREG names.
 *
 * We define both the bit masks and the test macros so that code written
 * for POSIX compiles unmodified on Windows.
 *
 * Usage:
 *   struct stat st;
 *   if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) { ... }
 * ----------------------------------------------------------------------- */
#ifndef S_IFDIR
#  define S_IFDIR  _S_IFDIR
#endif
#ifndef S_IFREG
#  define S_IFREG  _S_IFREG
#endif

#ifndef S_ISDIR
#  define S_ISDIR(m)  (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#  define S_ISREG(m)  (((m) & _S_IFMT) == _S_IFREG)
#endif

/* -----------------------------------------------------------------------
 * PATH_MAX — maximum path length constant
 *
 * POSIX defines PATH_MAX in <limits.h>.  On Windows the equivalent is
 * MAX_PATH (260 characters) from <windows.h>. We map to MAX_PATH so that
 * code using PATH_MAX for buffer sizing works on both platforms.
 * ----------------------------------------------------------------------- */
#ifndef PATH_MAX
#  include <windows.h>  /* for MAX_PATH */
#  define PATH_MAX MAX_PATH
#endif

#endif /* _WIN32 */

#endif /* WIN_COMPAT_H */
