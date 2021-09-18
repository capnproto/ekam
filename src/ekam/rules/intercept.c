/* Ekam Build System
 * Author: Kenton Varda (kenton@sandstorm.io)
 * Copyright (c) 2010-2015 Kenton Varda, Google Inc., and contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define _GNU_SOURCE
#define _DARWIN_NO_64_BIT_INODE  /* See stat madness, below. */
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdbool.h>

#if __linux__
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <sys/prctl.h>
#include <syscall.h>
#endif

static const int EKAM_DEBUG = 0;

#define likely(x)       __builtin_expect((x),1)

typedef int open_t(const char * pathname, int flags, ...);
void __attribute__((constructor)) start_interceptor() {
  if (EKAM_DEBUG) {
    static open_t* real_open;

    if (real_open == NULL) {
      real_open = (open_t*) dlsym(RTLD_NEXT, "open");
      assert(real_open != NULL);
    }

    int fd = real_open("/proc/self/cmdline", O_RDONLY);
    char buffer[1024];
    int n = read(fd, buffer, 1024);
    close(fd);
    write(STDERR_FILENO, buffer, n);
    write(STDERR_FILENO, "\n", 1);
  }

#if __linux__
  /* Block the statx syscall which node 12 uses which we're unable to intercept because it doesn't
   * actually have a glibc wrapper, ugh. */

#if 0  /* source of the seccomp filter below */
  ld [0]                  /* offsetof(struct seccomp_data, nr) */
  jeq #332, nosys         /* __NR_statx */
  jmp good

nosys:  ret #0x00050026  /* SECCOMP_RET_ERRNO | ENOSYS */
good:   ret #0x7fff0000  /* SECCOMP_RET_ALLOW */
#endif

  static struct sock_filter filter[] = {
    { 0x20,  0,  0, 0000000000 },
    { 0x15,  1,  0, 0x0000014c },
    { 0x05,  0,  0, 0x00000001 },
    { 0x06,  0,  0, 0x00050026 },
    { 0x06,  0,  0, 0x7fff0000 },
  };
  static struct sock_fprog prog = { sizeof(filter) / sizeof(filter[0]), filter };

  prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
  syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog);
#endif
}

/****************************************************************************************/
/* Bootstrap pthreads without linking agaist libpthread. */

typedef int pthread_once_func(pthread_once_t*, void (*)(void));
typedef int pthread_mutex_lock_func(pthread_mutex_t*);
typedef int pthread_mutex_unlock_func(pthread_mutex_t*);

static pthread_once_func* dynamic_pthread_once = NULL;
static pthread_mutex_lock_func* dynamic_pthread_mutex_lock = NULL;
static pthread_mutex_unlock_func* dynamic_pthread_mutex_unlock = NULL;

int fake_pthread_once(pthread_once_t* once_control, void (*init_func)(void)) {
  static pthread_mutex_t fake_once_mutex = PTHREAD_MUTEX_INITIALIZER;
  static int ONCE_INPROGRESS = 1;
  static int ONCE_DONE = 2;

  int initialized = __atomic_load_n(once_control, __ATOMIC_ACQUIRE);
  if (likely(initialized & ONCE_DONE)) return 0;

  // TODO(soon): Remove the dynamic_pthread_mutex usage once dynamic_pthread_once isn't forced to
  // unconditionally use fake_pthread_once. See init_pthreads.

  // The real implementation is typically more complex to try to make sure that concurrent
  // initialization of different pthread_once doesn't have a false dependency on a global mutex.
  // However, we only have a single such usage here & that would be overkill to try to implement on
  // MacOS and Linux. Use a single mutex anyway because it would have largely the same effect. It
  // also has additional complexities to handle things like forking and that too isn't a concern
  // here.
  dynamic_pthread_mutex_lock(&fake_once_mutex);
  pthread_once_t expected = PTHREAD_ONCE_INIT;
  bool updated =__atomic_compare_exchange_n(once_control, &expected, ONCE_INPROGRESS, false,
      __ATOMIC_RELEASE, __ATOMIC_RELAXED);
  if (likely(!updated)) {
    assert(__atomic_load_n(&expected, __ATOMIC_RELAXED) == ONCE_DONE);
    dynamic_pthread_mutex_unlock(&fake_once_mutex);
    return 0;
  }
  init_func();
  __atomic_store_n(once_control, ONCE_DONE, __ATOMIC_RELEASE);
  dynamic_pthread_mutex_unlock(&fake_once_mutex);
  return 0;
}
int fake_pthread_mutex_lock(pthread_mutex_t* mutex) { return 0; }
int fake_pthread_mutex_unlock(pthread_mutex_t* mutex) { return 0; }

void init_pthreads() {
  if (dynamic_pthread_once == NULL) {
    dynamic_pthread_once =
        (pthread_once_func*) dlsym(RTLD_DEFAULT, "pthread_once");
  }
  /* TODO:  For some reason the pthread_once returned by dlsym() doesn't do anything? */
  // Update 7/13/2021 - on Arch Linux pthread_once seems to work fine. Not tested on Buster so
  // unclear if this was a glibc bug that had been fixed at some point or there's something subtle
  // about when this breaks. There's an assert added in init_streams that ensures the functor
  // invoked.
  if (dynamic_pthread_once == NULL || 1) {
    dynamic_pthread_once = &fake_pthread_once;
  }

  if (dynamic_pthread_mutex_lock == NULL) {
    dynamic_pthread_mutex_lock =
        (pthread_mutex_lock_func*) dlsym(RTLD_DEFAULT, "pthread_mutex_lock");
  }
  if (dynamic_pthread_mutex_lock == NULL) {
    dynamic_pthread_mutex_lock = &fake_pthread_mutex_lock;
  }

  if (dynamic_pthread_mutex_unlock == NULL) {
    dynamic_pthread_mutex_unlock =
        (pthread_mutex_unlock_func*) dlsym(RTLD_DEFAULT, "pthread_mutex_unlock");
  }
  if (dynamic_pthread_mutex_lock == NULL) {
    dynamic_pthread_mutex_lock = &fake_pthread_mutex_unlock;
  }
}

/****************************************************************************************/

#define EKAM_CALL_FILENO 3
#define EKAM_RETURN_FILENO 4
static FILE* ekam_call_stream;
static FILE* ekam_return_stream;

static char current_dir[PATH_MAX + 1];

static pthread_once_t init_once_control = PTHREAD_ONCE_INIT;

typedef ssize_t readlink_t(const char *path, char *buf, size_t bufsiz);
static readlink_t* real_readlink = NULL;

static bool bypass_interceptor = false;

static void init_bypass_interceptor() {
  static const char* sanitizer_env_vars[] = {
    "ASAN_SYMBOLIZER_PATH",
    "MSAN_SYMBOLIZER_PATH",
    "LLVM_SYMBOLIZER_PATH",
    NULL,
  };

  char real_exe[PATH_MAX + 1];
  bool real_exe_initialized = false;

  assert(real_readlink != NULL);

  for (size_t i = 0; sanitizer_env_vars[i]; i++) {
    const char* sanitizer_path = getenv(sanitizer_env_vars[i]);
    if (sanitizer_path != NULL) {
      if (!real_exe_initialized) {
        ssize_t result = real_readlink("/proc/self/exe", real_exe, PATH_MAX);
        if (result == -1) {
          fprintf(stderr, "Failed to readlink(/proc/self/exe): %s (%d)\n", strerror(errno), errno);
          abort();
        }

        real_exe_initialized = true;
      }

      if (strcmp(real_exe, sanitizer_path) == 0) {
        bypass_interceptor = true;
        break;
      }
    }
  }
}

static void init_streams_once() {
  static bool initialized = false;
  if (__atomic_exchange_n(&initialized, true, __ATOMIC_RELEASE)) {
    fprintf(stderr, "pthread_once is broken\n");
    abort();
  }

  assert(ekam_call_stream == NULL);
  assert(ekam_return_stream == NULL);

  assert(real_readlink == NULL);
  real_readlink = (readlink_t*) dlsym(RTLD_NEXT, "readlink");
  assert(real_readlink != NULL);

  init_bypass_interceptor();

  if (bypass_interceptor) {
    /* Bypass any further initialization as it will fail. */
    return;
  }

  ekam_call_stream = fdopen(EKAM_CALL_FILENO, "w");
  if (ekam_call_stream == NULL) {
    fprintf(stderr, "fdopen(EKAM_CALL_FILENO): error %d\n", errno);
    abort();
  }
  ekam_return_stream = fdopen(EKAM_RETURN_FILENO, "r");
  if (ekam_return_stream == NULL) {
    fprintf(stderr, "fdopen(EKAM_CALL_FILENO): error %d\n", errno);
    abort();
  }
  if (getcwd(current_dir, PATH_MAX) == NULL) {
    fprintf(stderr, "getcwd(): error %d\n", errno);
    abort();
  }
  strcat(current_dir, "/");

}

static void init_streams() {
  init_pthreads();
  dynamic_pthread_once(&init_once_control, &init_streams_once);
  // This assert makes sure the pthread_once call actually invokes the initializer.
  assert(bypass_interceptor || (ekam_call_stream != NULL && ekam_return_stream != NULL));
}

/****************************************************************************************/

typedef enum usage {
  READ,
  WRITE
} usage_t;

static const char TAG_PROVIDER_PREFIX[] = "/ekam-provider/";
static const char TMP[] = "/tmp";
static const char VAR_TMP[] = "/var/tmp";
static const char TMP_PREFIX[] = "/tmp/";
static const char VAR_TMP_PREFIX[] = "/var/tmp/";
static const char PROC_PREFIX[] = "/proc/";

static usage_t last_usage = READ;
static char last_path[PATH_MAX] = "";
static char last_result[PATH_MAX] = "";
pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

static void cache_result(const char* input, const char* output, usage_t usage) {
  dynamic_pthread_mutex_lock(&cache_mutex);
  last_usage = usage;
  strcpy(last_path, input);
  strcpy(last_result, output);
  dynamic_pthread_mutex_unlock(&cache_mutex);
}

static int get_cached_result(const char* pathname, char* buffer, usage_t usage) {
  int result = 0;
  dynamic_pthread_mutex_lock(&cache_mutex);
  if (usage == last_usage && strcmp(pathname, last_path) == 0) {
    strcpy(buffer, last_result);
    result = 1;
  }
  dynamic_pthread_mutex_unlock(&cache_mutex);
  return result;
}

static void canonicalizePath(char* path) {
  /* Preconditions:
   * - path has already been determined to be relative, perhaps because the pointer actually points
   *   into the middle of some larger path string, in which case it must point to the character
   *   immediately after a '/'.
   */

  /* Invariants:
   * - src points to the beginning of a path component.
   * - dst points to the location where the path component should end up, if it is not special.
   * - src == path or src[-1] == '/'.
   * - dst == path or dst[-1] == '/'.
   */

  char* src = path;
  char* dst = path;
  char* locked = dst;  /* dst cannot backtrack past this */
  char* partEnd;
  int hasMore;

  for (;;) {
    while (*src == '/') {
      /* Skip duplicate slash. */
      ++src;
    }

    partEnd = strchr(src, '/');
    hasMore = partEnd != NULL;
    if (hasMore) {
      *partEnd = '\0';
    } else {
      partEnd = src + strlen(src);
    }

    if (strcmp(src, ".") == 0) {
      /* Skip it. */
    } else if (strcmp(src, "..") == 0) {
      if (dst > locked) {
        /* Backtrack over last path component. */
        --dst;
        while (dst > locked && dst[-1] != '/') --dst;
      } else {
        locked += 3;
        goto copy;
      }
    } else {
      /* Copy if needed. */
    copy:
      if (dst < src) {
        memmove(dst, src, partEnd - src);
        dst += partEnd - src;
      } else {
        dst = partEnd;
      }
      *dst++ = '/';
    }

    if (hasMore) {
      src = partEnd + 1;
    } else {
      /* Oops, we have to remove the trailing '/'. */
      if (dst == path) {
        /* Oops, there is no trailing '/'.  We have to return ".". */
        strcpy(path, ".");
      } else {
        /* Remove the trailing '/'.  Note that this means that opening the file will work even
         * if it is not a directory, where normally it should fail on non-directories when a
         * trailing '/' is present.  If this is a problem, we need to add some sort of special
         * handling for this case where we stat() it separately to check if it is a directory,
         * because Ekam findInput will not accept a trailing '/'.
         */
        --dst;
        *dst = '\0';
      }
      return;
    }
  }
}

static bool path_has_prefix(const char* pathname, const char* prefix, size_t prefix_length) {
  // Returns true if pathname is a child within prefix or if it matches prefix directly. Prefix must
  // always end with a trailing '/'.
  assert(prefix_length > 1);
  assert(prefix_length <= PATH_MAX);
  assert(prefix[prefix_length - 1] == '/');
  if (strncmp(pathname, prefix, prefix_length - 1) == 0) {
    return pathname[prefix_length - 1] == '\0' || pathname[prefix_length - 1] == '/';
  }

  return false;
}

static bool bypass_remap(const char *pathname) {
  int debug = EKAM_DEBUG;

  if (pathname[0] != '/') {
    // Only absolute paths are allowed to bypass the remap.
    return false;
  }

  const char* bypass_dirs = getenv("EKAM_REMAP_BYPASS_DIRS");
  if (bypass_dirs == NULL) {
    return false;
  }

  bool found = false;

  while (bypass_dirs && *bypass_dirs) {
    char* separator_location = strchr(bypass_dirs, ':');
    size_t bypass_dir_length;
    if (separator_location) {
      bypass_dir_length = separator_location - bypass_dirs;
    } else {
      bypass_dir_length = strlen(bypass_dirs);
    }

    if (bypass_dirs[0] != '/') {
      fprintf(stderr, "Bypass directory '%s' isn't an absolute path. Skipping.\n", bypass_dirs);
    } else if (bypass_dirs[bypass_dir_length - 1] != '/') {
      fprintf(stderr, "Bypass directory '%s' doesn't end in trailing '/'. Skipping.\n", bypass_dirs);
    } else if (path_has_prefix(pathname, bypass_dirs, bypass_dir_length)) {
      if (debug) {
        fprintf(stderr, "Bypass found for %s\n", pathname);
      }
      found = true;
      break;
    }

    if (separator_location) {
      bypass_dirs = separator_location + 1;
    } else {
      bypass_dirs = NULL;
    }
  }

  return found;
}

static bool is_temporary_dir(const char *pathname) {
  size_t cwd_len = strlen(current_dir) - 1;
  if (strncmp(pathname, current_dir, cwd_len) == 0
      && (pathname[cwd_len] == '/' || pathname[cwd_len] == '\0')) {
    // Somewhere under our working directory. If our working directory happens
    // to be inside one of the temporary dirs, then we need to be careful to
    // *not* treat this as a temporary file, otherwise this will apply to basicaly
    // everything, and strange things will happen.
    return false;
  }

  // TODO(soon): Simplify the logic to use `path_has_prefix`.
  if (strcmp(pathname, TMP) == 0 ||
      strcmp(pathname, VAR_TMP) == 0 ||
      strncmp(pathname, TMP_PREFIX, strlen(TMP_PREFIX)) == 0 ||
      strncmp(pathname, VAR_TMP_PREFIX, strlen(VAR_TMP_PREFIX)) == 0 ||
      strncmp(pathname, PROC_PREFIX, strlen(PROC_PREFIX)) == 0) {
    return true;
  }

#if defined(__linux__)
#define SYSTEMD_TMPDIR_PREFIX "/run/user/"
  if (path_has_prefix(pathname, SYSTEMD_TMPDIR_PREFIX, strlen(SYSTEMD_TMPDIR_PREFIX))) {
    return true;
  }
#else
  (void) pathname;
#endif

  return false;
}

static const char* remap_file(const char* syscall_name, const char* pathname,
                              char* buffer, usage_t usage) {
  char* pos;
  int debug = EKAM_DEBUG;

  /* Ad-hoc debugging can be accomplished by setting debug = 1 when a particular file pattern
   * is matched. */

  if (debug) {
    fprintf(stderr, "remap for %s (%s): %s\n",
            syscall_name, (usage == READ ? "read" : "write"), pathname);
  }

  init_streams();

  if (bypass_interceptor) {
    if (debug) fprintf(stderr, "Bypassing interceptor for %s on %s\n", syscall_name, pathname);
    return pathname;
  }

  if (strlen(pathname) >= PATH_MAX) {
    /* Too long. */
    if (debug) fprintf(stderr, "  name too long\n");
    errno = ENAMETOOLONG;
    return NULL;
  }

  if (get_cached_result(pathname, buffer, usage)) {
    if (debug) fprintf(stderr, "  cached: %s\n", buffer);
    return buffer;
  }

  flockfile(ekam_call_stream);

  if (strncmp(pathname, TAG_PROVIDER_PREFIX, strlen(TAG_PROVIDER_PREFIX)) == 0) {
    /* A tag reference.  Construct the tag name in |buffer|. */
    strcpy(buffer, pathname + strlen(TAG_PROVIDER_PREFIX));

    if (usage == READ) {
      /* Change first slash to a colon to form a tag.  E.g. "header/foo.h" becomes
       * "header:foo.h". */
      pos = strchr(buffer, '/');
      if (pos == NULL) {
        /* This appears to be a tag type without a name, so it should look like a directory.
         * We can use the current directory.  TODO:  Return some fake empty directory instead. */
        funlockfile(ekam_call_stream);
        strcpy(buffer, ".");
        if (debug) fprintf(stderr, "  is directory\n");
        return buffer;
      }
      *pos = ':';
      canonicalizePath(pos + 1);

      if (strcmp(buffer, "canonical:.") == 0) {
        /* HACK:  Don't try to remap top directory. */
        funlockfile(ekam_call_stream);
        if (debug) fprintf(stderr, "  current directory\n");
        return "src";
      }
    }

    /* Ask ekam to remap the file name. */
    fputs(usage == READ ? "findProvider " : "newProvider ", ekam_call_stream);
    fputs(buffer, ekam_call_stream);
    fputs("\n", ekam_call_stream);
  } else if (is_temporary_dir(pathname)) {
    /* Temp file or /proc.  Ignore. */
    funlockfile(ekam_call_stream);
    if (debug) fprintf(stderr, "  temp file: %s\n", pathname);
    return pathname;
  } else if (bypass_remap(pathname)) {
    funlockfile(ekam_call_stream);
    if (debug) fprintf(stderr, "  bypassed file: %s\n", pathname);
    return pathname;
  } else {
    if (strncmp(pathname, current_dir, strlen(current_dir)) == 0 &&
        strncmp(pathname + strlen(current_dir), "deps/", 5) != 0) {
      /* The app is trying to open files in the current directory by absolute path.  Treat it
       * exactly as if it had used a relative path.  We make a special exception for the directory
       * `deps`, to allow e.g. executing binaries found there without mapping them into the
       * common source tree. */
      pathname = pathname + strlen(current_dir);
    } else if (pathname[0] == '/' ||
               strncmp(pathname, "deps/", 5) == 0) {
      /* Absolute path or under `deps`.  Note the access but don't remap. */
      if (usage == WRITE) {
        /* Cannot write to absolute paths. */
        funlockfile(ekam_call_stream);
        errno = EACCES;
        if (debug) fprintf(stderr, "  absolute path, can't write\n");
        return NULL;
      }

      fputs("noteInput ", ekam_call_stream);
      fputs(pathname, ekam_call_stream);
      fputs("\n", ekam_call_stream);
      fflush(ekam_call_stream);
      if (ferror_unlocked(ekam_call_stream)) {
        funlockfile(ekam_call_stream);
        fprintf(stderr, "error: Ekam call stream broken.\n");
        abort();
      }
      cache_result(pathname, pathname, usage);
      funlockfile(ekam_call_stream);
      if (debug) fprintf(stderr, "  absolute path: %s\n", pathname);
      return pathname;
    }

    /* Path in current directory. */
    strcpy(buffer, pathname);
    canonicalizePath(buffer);
    if (strcmp(buffer, ".") == 0) {
      /* HACK:  Don't try to remap current directory. */
      funlockfile(ekam_call_stream);
      if (debug) fprintf(stderr, "  current directory\n");
      return ".";
    } else {
      /* Ask ekam to remap the file name. */
      fputs(usage == READ ? "findInput " : "newOutput ", ekam_call_stream);
      fputs(buffer, ekam_call_stream);
      fputs("\n", ekam_call_stream);
    }
  }

  fflush(ekam_call_stream);
  if (ferror_unlocked(ekam_call_stream)) {
    funlockfile(ekam_call_stream);
    fprintf(stderr, "error: Ekam call stream broken.\n");
    abort();
  }

  /* Carefully lock the return stream then unlock the call stream, so that we know that
   * responses will be received in the correct order. */
  flockfile(ekam_return_stream);
  funlockfile(ekam_call_stream);

  /* Read response from Ekam. */
  if (fgets(buffer, PATH_MAX, ekam_return_stream) == NULL) {
    funlockfile(ekam_return_stream);
    fprintf(stderr, "error: Ekam return stream broken.\n");
    abort();
  }

  /* Done reading. */
  funlockfile(ekam_return_stream);

  /* Remove the trailing newline. */
  pos = strchr(buffer, '\n');
  if (pos == NULL) {
    fprintf(stderr, "error: Path returned from Ekam was too long.\n");
    abort();
  }
  *pos = '\0';

  if (*buffer == '\0') {
    /* Not found. */
    errno = ENOENT;
    if (debug) fprintf(stderr, "  ekam says no such file\n");
    return NULL;
  }

  cache_result(pathname, buffer, usage);

  if (debug) fprintf(stderr, "  remapped to: %s\n", buffer);
  return buffer;
}

/****************************************************************************************/

#define WRAP(RETURNTYPE, NAME, PARAMTYPES, PARAMS, USAGE, ERROR_RESULT)     \
  typedef RETURNTYPE NAME##_t PARAMTYPES;                                   \
  RETURNTYPE NAME PARAMTYPES {                                              \
    static NAME##_t* real_##NAME = NULL;                                    \
    char buffer[PATH_MAX];                                                  \
                                                                            \
    if (real_##NAME == NULL) {                                              \
      real_##NAME = (NAME##_t*) dlsym(RTLD_NEXT, #NAME);                    \
      assert(real_##NAME != NULL);                                          \
    }                                                                       \
                                                                            \
    path = remap_file(#NAME, path, buffer, USAGE);                          \
    if (path == NULL) return ERROR_RESULT;                                  \
    return real_##NAME PARAMS;                                              \
  }                                                                         \
  RETURNTYPE _##NAME PARAMTYPES {                                           \
    return NAME PARAMS;                                                     \
  }

WRAP(int, chdir, (const char* path), (path), READ, -1)
WRAP(int, chmod, (const char* path, mode_t mode), (path, mode), WRITE, -1)
WRAP(int, lchmod, (const char* path, mode_t mode), (path, mode), WRITE, -1)
WRAP(int, unlink, (const char* path), (path), WRITE, -1)
WRAP(int, link, (const char* target, const char* path), (target, path), WRITE, -1)
WRAP(int, symlink, (const char* target, const char* path), (target, path), WRITE, -1)
WRAP(int, execve, (const char* path, char* const argv[], char* const envp[]),
                  (path, argv, envp), READ, -1);

#ifdef __APPLE__
WRAP(int, stat, (const char* path, struct stat* sb), (path, sb), READ, -1)
WRAP(int, lstat, (const char* path, struct stat* sb), (path, sb), READ, -1)

/* OSX defines an alternate version of stat with 64-bit inodes. */
WRAP(int, stat64, (const char* path, struct stat64* sb), (path, sb), READ, -1)

/* In some crazy attempt to transition the regular "stat" call to use 64-bit inodes, Apple
 * resorted to some sort of linker magic in which calls to stat() in newly-compiled code
 * actually go to _stat$INODE64(), which appears to be identical to stat64().  We disabled
 * this by defining _DARWIN_NO_64_BIT_INODE, above, but we need to intercept all versions
 * of stat, including the $INODE64 version.  Let's avoid any dependency on stat64, though,
 * since it is "deprecated".  So, we just make the stat buf pointer opaque. */
typedef int stat_inode64_t(const char* path, void* sb);
int stat_inode64(const char* path, void* sb) __asm("_stat$INODE64");
int stat_inode64(const char* path, void* sb) {
  static stat_inode64_t* real_stat_inode64 = NULL;
  char buffer[PATH_MAX];

  if (real_stat_inode64 == NULL) {
    real_stat_inode64 = (stat_inode64_t*) dlsym(RTLD_NEXT, "stat$INODE64");
    assert(real_stat_inode64 != NULL);
  }

  path = remap_file("_stat$INODE64", path, buffer, READ);
  if (path == NULL) return -1;
  return real_stat_inode64(path, sb);
}

/* Cannot intercept intra-libc function calls on OSX, so we must intercept fopen() as well. */
WRAP(FILE*, fopen, (const char* path, const char* mode), (path, mode),
     mode[0] == 'w' || mode[0] == 'a' ? WRITE : READ, NULL)
WRAP(FILE*, freopen, (const char* path, const char* mode, FILE* file), (path, mode, file),
     mode[0] == 'w' || mode[0] == 'a' ? WRITE : READ, NULL)

/* And remove(). */
WRAP(int, remove, (const char* path), (path), WRITE, -1)

/* Called by access(), below. */
static int direct_stat(const char* path, struct stat* sb) {
  static stat_t* real_stat = NULL;

  if (real_stat == NULL) {
    real_stat = (stat_t*) dlsym(RTLD_NEXT, "stat");
    assert(real_stat != NULL);
  }

  return real_stat(path, sb);
}

#elif defined(__linux__)

WRAP(int, __xstat, (int ver, const char* path, struct stat* sb), (ver, path, sb), READ, -1)
//WRAP(int, __lxstat, (int ver, const char* path, struct stat* sb), (ver, path, sb), READ, -1)
WRAP(int, __xstat64, (int ver, const char* path, struct stat64* sb), (ver, path, sb), READ, -1)
//WRAP(int, __lxstat64, (int ver, const char* path, struct stat64* sb), (ver, path, sb), READ, -1)

/* Within the source tree, we make all symbolic links look like hard links.  Otherwise, if a
 * symlink in the source tree pointed outside of it, and if a tool decided to read back that link
 * and follow it manually, we'd no longer know that it was accessing something that was meant to
 * be treated as source.
 *
 * To implement this, we redirect lstat -> stat when the file is in the current directory. */
typedef int __lxstat_t (int ver, const char* path, struct stat* sb);
int __lxstat (int ver, const char* path, struct stat* sb) {
  static __lxstat_t* real___lxstat = NULL;
  static __xstat_t* real___xstat = NULL;
  char buffer[PATH_MAX];

  if (real___lxstat == NULL) {
    real___lxstat = (__lxstat_t*) dlsym(RTLD_NEXT, "__lxstat");
    assert(real___lxstat != NULL);
  }
  if (real___xstat == NULL) {
    real___xstat = (__lxstat_t*) dlsym(RTLD_NEXT, "__xstat");
    assert(real___xstat != NULL);
  }

  path = remap_file("__lxstat", path, buffer, READ);
  if (path == NULL) return -1;
  if (path[0] == '/') {
    return real___lxstat(ver, path, sb);
  } else {
    return real___xstat(ver, path, sb);
  }
}
int ___lxstat (int ver, const char* path, struct stat* sb) {
  return __lxstat (ver, path, sb);
}

typedef int __lxstat64_t (int ver, const char* path, struct stat64* sb);
int __lxstat64 (int ver, const char* path, struct stat64* sb) {
  static __lxstat64_t* real___lxstat64 = NULL;
  static __xstat64_t* real___xstat64 = NULL;
  char buffer[PATH_MAX];

  if (real___lxstat64 == NULL) {
    real___lxstat64 = (__lxstat64_t*) dlsym(RTLD_NEXT, "__lxstat64");
    assert(real___lxstat64 != NULL);
  }
  if (real___xstat64 == NULL) {
    real___xstat64 = (__lxstat64_t*) dlsym(RTLD_NEXT, "__xstat64");
    assert(real___xstat64 != NULL);
  }

  path = remap_file("__lxstat64", path, buffer, READ);
  if (path == NULL) return -1;
  if (path[0] == '/') {
    return real___lxstat64(ver, path, sb);
  } else {
    return real___xstat64(ver, path, sb);
  }
}
int ___lxstat64 (int ver, const char* path, struct stat64* sb) {
  return __lxstat64 (ver, path, sb);
}

#ifndef _STAT_VER
/* glibc 2.33+ no longer defines `stat()` as an inline wrapper in the header, but instead exports
 * it as a regular symbol. So, we need to intercept it. And, we need to give `lstat()` special
 * treatment just like `__lxstat()` above. */

WRAP(int, stat, (const char* path, struct stat* sb), (path, sb), READ, -1)
WRAP(int, stat64, (const char* path, struct stat64* sb), (path, sb), READ, -1)
//WRAP(int, lstat, (const char* path, struct stat* sb), (path, sb), READ, -1)

typedef int lstat_t (const char* path, struct stat* sb);
int lstat(const char* path, struct stat* sb) {
  static lstat_t* real_lstat = NULL;
  static stat_t* real_stat = NULL;
  char buffer[PATH_MAX];

  if (real_lstat == NULL) {
    real_lstat = (lstat_t*) dlsym(RTLD_NEXT, "lstat");
    assert(real_lstat != NULL);
  }
  if (real_stat == NULL) {
    real_stat = (lstat_t*) dlsym(RTLD_NEXT, "stat");
    assert(real_stat != NULL);
  }

  path = remap_file("lstat", path, buffer, READ);
  if (path == NULL) return -1;
  if (path[0] == '/') {
    return real_lstat(path, sb);
  } else {
    return real_stat(path, sb);
  }
}
int _lstat(const char* path, struct stat* sb) {
  return lstat(path, sb);
}

#endif  /* defined(_STAT_VER) */

/* Cannot intercept intra-libc function calls on Linux, so we must intercept fopen() as well. */
WRAP(FILE*, fopen, (const char* path, const char* mode), (path, mode),
     mode[0] == 'w' || mode[0] == 'a' ? WRITE : READ, NULL)
WRAP(FILE*, fopen64, (const char* path, const char* mode), (path, mode),
     mode[0] == 'w' || mode[0] == 'a' ? WRITE : READ, NULL)
WRAP(FILE*, freopen, (const char* path, const char* mode, FILE* file), (path, mode, file),
     mode[0] == 'w' || mode[0] == 'a' ? WRITE : READ, NULL)
WRAP(FILE*, freopen64, (const char* path, const char* mode, FILE* file), (path, mode, file),
     mode[0] == 'w' || mode[0] == 'a' ? WRITE : READ, NULL)

/* And remove(). */
WRAP(int, remove, (const char* path), (path), WRITE, -1)

/* And dlopen(). */
WRAP(void*, dlopen, (const char* path, int flag), (path, flag), READ, NULL)

/* And exec*().  The PATH-checking versions need special handling. */
WRAP(int, execv, (const char* path, char* const argv[]), (path, argv), READ, -1)

typedef int execvpe_t(const char* path, char* const argv[], char* const envp[]);
int execvpe(const char* path, char* const argv[], char* const envp[]) {
  static execvpe_t* real_execvpe = NULL;
  char buffer[PATH_MAX];

  if (real_execvpe == NULL) {
    real_execvpe = (execvpe_t*) dlsym(RTLD_NEXT, "execvpe");
    assert(real_execvpe != NULL);
  }

  /* If the path does not contain a '/', PATH resolution will be done, so we don't want to
   * remap.
   */
  if (strchr(path, '/') != NULL) {
    path = remap_file("execvpe", path, buffer, READ);
    if (path == NULL) return -1;
  }
  return real_execvpe(path, argv, envp);
}
int _execvpe (const char* path, char* const argv[], char* const envp[]) {
  return execvpe(path, argv, envp);
}

typedef int execvp_t(const char* path, char* const argv[]);
int execvp(const char* path, char* const argv[]) {
  static execvp_t* real_execvp = NULL;
  char buffer[PATH_MAX];

  if (real_execvp == NULL) {
    real_execvp = (execvp_t*) dlsym(RTLD_NEXT, "execvp");
    assert(real_execvp != NULL);
  }

  /* If the path does not contain a '/', PATH resolution will be done, so we don't want to
   * remap.
   */
  if (strchr(path, '/') != NULL) {
    path = remap_file("execvp", path, buffer, READ);
    if (path == NULL) return -1;
  }
  return real_execvp(path, argv);
}
int _execvp (const char* path, char* const argv[]) {
  return execvp(path, argv);
}

/* Called by access(), below. */
static int direct_stat(const char* path, struct stat* sb) {
#ifdef _STAT_VER
  // glibc <2.33 defines `stat()` as an inline wrapper in the header file, which calls __xstat().
  static __xstat_t* real_xstat = NULL;

  if (real_xstat == NULL) {
    real_xstat = (__xstat_t*) dlsym(RTLD_NEXT, "__xstat");
    assert(real_xstat != NULL);
  }

  return real_xstat(_STAT_VER, path, sb);
#else  /* defined(_STAT_VER) */
  // glibc 2.33+ no longer defines stat() as an inline wrapper in the header, but instead exports
  // it as a regular symbol.
  static stat_t* real_stat = NULL;

  if (real_stat == NULL) {
    real_stat = (stat_t*) dlsym(RTLD_NEXT, "stat");
    assert(real_stat != NULL);
  }

  return real_stat(path, sb);
#endif  /* defined(_STAT_VER), #else */
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz) {
  char buffer[PATH_MAX];

  path = remap_file("readlink", path, buffer, READ);
  if (path == NULL) return -1;
  if (path[0] != '/') return EINVAL;  // no links in source directory
  return real_readlink(path, buf, bufsiz);
}

#else

WRAP(int, stat, (const char* path, struct stat* sb), (path, sb), READ, -1)
WRAP(int, lstat, (const char* path, struct stat* sb), (path, sb), READ, -1)

/* Called by access(), below. */
static int direct_stat(const char* path, struct stat* sb) {
  static stat_t* real_stat = NULL;

  if (real_stat == NULL) {
    real_stat = (stat_t*) dlsym(RTLD_NEXT, "stat");
    assert(real_stat != NULL);
  }

  return real_stat(path, sb);
}

#endif  /* platform */

static int intercepted_open(const char * pathname, int flags, va_list args) {
  static open_t* real_open;
  char buffer[PATH_MAX];
  const char* remapped;

#if __linux__
  // The new TCMalloc will attempt to open various files in /sys/ to read information about the CPU
  // (this is implemented within Abseil). If we actually do the dlsym below, we'll end up causing
  // a deadlock within TCMalloc because `dlsym` will cause TCMalloc's constructor to try to
  // initialize again, but the open call is being done within that. I thought perhaps this could be
  // resolved upstream (https://github.com/google/tcmalloc/issues/78) but after looking into it
  // more, there's a fundamental issue with calls to open these files causing trying to read these
  // files again. Since Abseil has "call_once" semantics to read the files in /sys/, even if you
  // tried to pre-cache it in TCMalloc, Abseil would end up deadlocking when you reentrantly tried
  // to initialize the CPU frequency.
  // Technically this only needs to be done if `real_open` isn't resolved, but my thought was that
  // there's no real use-case where Ekam would want to intercept /sys/ & thus this simplifies me
  // having to do more thorough testing.
  if (strncmp("/sys/", pathname, strlen("/sys/")) == 0) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
      mode = va_arg(args, int);
    }

    if (EKAM_DEBUG) {
      fprintf(stderr, "TCMalloc workaround. Bypassing Ekam for %s\n", pathname);
    }
    return syscall(SYS_openat, AT_FDCWD, pathname, flags, mode);
  }
#endif

  if (real_open == NULL) {
    real_open = (open_t*) dlsym(RTLD_NEXT, "open");
    assert(real_open != NULL);
  }

  remapped = remap_file("open", pathname, buffer, (flags & O_ACCMODE) == O_RDONLY ? READ : WRITE);
  if (remapped == NULL) return -1;

  if (flags & O_CREAT) {
    mode_t mode = va_arg(args, int);
    return real_open(remapped, flags, mode);
  } else {
    return real_open(remapped, flags, 0);
  }
}

int open(const char * pathname, int flags, ...) {
  va_list args;
  va_start(args, flags);
  return intercepted_open(pathname, flags, args);
}

int _open(const char * pathname, int flags, ...) {
  va_list args;
  va_start(args, flags);
  return intercepted_open(pathname, flags, args);
}

static int intercepted_open64(const char * pathname, int flags, va_list args) {
  static open_t* real_open64;
  char buffer[PATH_MAX];
  const char* remapped;

  if (real_open64 == NULL) {
    real_open64 = (open_t*) dlsym(RTLD_NEXT, "open64");
    assert(real_open64 != NULL);
  }

  remapped = remap_file("open64", pathname, buffer, (flags & O_ACCMODE) == O_RDONLY ? READ : WRITE);
  if (remapped == NULL) return -1;

  if(flags & O_CREAT) {
    mode_t mode = va_arg(args, int);
    return real_open64(remapped, flags, mode);
  } else {
    return real_open64(remapped, flags, 0);
  }
}

int open64(const char * pathname, int flags, ...) {
  va_list args;
  va_start(args, flags);
  return intercepted_open64(pathname, flags, args);
}

int _open64(const char * pathname, int flags, ...) {
  va_list args;
  va_start(args, flags);
  return intercepted_open64(pathname, flags, args);
}

static int intercepted_openat(int dirfd, const char * pathname, int flags, va_list args) {
#if __linux__
  /* We're going to emulate openat() on top of open() by finding out the full path of the directory
   * via /proc/self/fd. */
  char procfile[128];
  char buffer[PATH_MAX];
  ssize_t n;

  if (pathname[0] == '/') {
    return intercepted_open(pathname, flags, args);
  }

  sprintf(procfile, "/proc/self/fd/%d", dirfd);
  n = readlink(procfile, buffer, sizeof(buffer));
  if (n < 0) {
    return n;
  }
  if (n + strlen(pathname) + 2 >= sizeof(buffer)) {
    errno = ENAMETOOLONG;
    return -1;
  }

  buffer[n] = '/';
  strcpy(buffer + n + 1, pathname);
  return intercepted_open(buffer, flags, args);
#else
  fprintf(stderr, "openat(%s) not intercepted\n", pathname);
  errno = ENOSYS;
  return -1;
#endif
}

int openat(int dirfd, const char * pathname, int flags, ...) {
  va_list args;
  va_start(args, flags);
  return intercepted_openat(dirfd, pathname, flags, args);
}

int _openat(int dirfd, const char * pathname, int flags, ...) {
  va_list args;
  va_start(args, flags);
  return intercepted_openat(dirfd, pathname, flags, args);
}

static int intercepted_openat64(int dirfd, const char * pathname, int flags, va_list args) {
#if __linux__
  /* We're going to emulate openat() on top of open() by finding out the full path of the directory
   * via /proc/self/fd. */
  char procfile[128];
  char buffer[PATH_MAX];
  ssize_t n;

  if (pathname[0] == '/') {
    return intercepted_open64(pathname, flags, args);
  }

  sprintf(procfile, "/proc/self/fd/%d", dirfd);
  n = readlink(procfile, buffer, sizeof(buffer));
  if (n < 0) {
    return n;
  }
  if (n + strlen(pathname) + 2 >= sizeof(buffer)) {
    errno = ENAMETOOLONG;
    return -1;
  }

  buffer[n] = '/';
  strcpy(buffer + n + 1, pathname);
  return intercepted_open64(buffer, flags, args);
#else
  fprintf(stderr, "openat(%s) not intercepted\n", pathname);
  errno = ENOSYS;
  return -1;
#endif
}

int openat64(int dirfd, const char * pathname, int flags, ...) {
  va_list args;
  va_start(args, flags);
  return intercepted_openat64(dirfd, pathname, flags, args);
}

int _openat64(int dirfd, const char * pathname, int flags, ...) {
  va_list args;
  va_start(args, flags);
  return intercepted_openat64(dirfd, pathname, flags, args);
}

/* For rename(), we consider both locations to be outputs, since both are modified. */
typedef int rename_t(const char* from, const char* to);
int rename(const char* from, const char* to) {
  static rename_t* real_rename = NULL;
  char buffer[PATH_MAX];
  char buffer2[PATH_MAX];

  if (real_rename == NULL) {
    real_rename = (rename_t*) dlsym(RTLD_NEXT, "rename");
    assert(real_rename != NULL);
  }

  from = remap_file("rename", from, buffer, WRITE);
  if (from == NULL) return -1;
  to = remap_file("rename", to, buffer2, WRITE);
  if (to == NULL) return -1;
  return real_rename (from, to);
}
int _rename(const char* from, const char* to) {
  return rename(from, to);
}

/* We override mkdir to just make the directory under tmp/.  Ekam doesn't really care to track
 * directory creations. */
typedef int mkdir_t(const char* path, mode_t mode);
int mkdir(const char* path, mode_t mode) {
  static mkdir_t* real_mkdir = NULL;
  char buffer[PATH_MAX];
  char* slash_pos;

  if (real_mkdir == NULL) {
    real_mkdir = (mkdir_t*) dlsym(RTLD_NEXT, "mkdir");
    assert(real_mkdir != NULL);
  }

  if (*path == '/') {
    /* Absolute path.  Use the regular remap logic (which as of this writing will only allow
     * writes to /tmp). */
    path = remap_file("mkdir", path, buffer, WRITE);
    if (path == NULL) return -1;
    return real_mkdir(path, mode);
  } else {
    strcpy(buffer, "tmp/");
    if (strlen(path) + strlen(buffer) + 1 > PATH_MAX) {
      errno = ENAMETOOLONG;
      return -1;
    }

    strcat(buffer, path);

    /* Attempt to create parent directories. */
    slash_pos = buffer;
    while ((slash_pos = strchr(slash_pos, '/')) != NULL) {
      *slash_pos = '\0';
      /* We don't care if this fails -- we'll find out when we make the final mkdir call below. */
      real_mkdir(buffer, mode);
      *slash_pos = '/';
      ++slash_pos;
    }

    /* Finally create the requested directory. */
    return real_mkdir(buffer, mode);
  }
}
int _mkdir(const char* path, mode_t mode) {
  return mkdir(path, mode);
}

/* HACK:  Ekam doesn't really track directories right now, but some tools call access() on
 *   directories.  Here, we check if the path exists as a directory in src or tmp and, if so,
 *   go ahead and return success.  Otherwise, we remap as normal.
 * TODO:  Add some sort of directory handling to Ekam? */
typedef int access_t(const char* path, int mode);
int access(const char* path, int mode) {
  static access_t* real_access = NULL;
  char buffer[PATH_MAX];
  struct stat stats;

  if (real_access == NULL) {
    real_access = (access_t*) dlsym(RTLD_NEXT, "access");
    assert(real_access != NULL);
  }

  if (*path != '/') {
    strcpy(buffer, "src/");

    if (strlen(path) + strlen(buffer) + 1 > PATH_MAX) {
      errno = ENAMETOOLONG;
      return -1;
    }

    strcat(buffer, path);
    if (direct_stat(buffer, &stats) == 0 && S_ISDIR(stats.st_mode)) {
      /* Directory exists in src. */
      return 0;
    }

    memcpy(buffer, "tmp", 3);
    if (direct_stat(buffer, &stats) == 0 && S_ISDIR(stats.st_mode)) {
      /* Directory exists in tmp. */
      return 0;
    }

    path = remap_file("access", path, buffer, READ);
    if (path == NULL) return -1;
    /* TODO:  If checking W_OK, we should probably only succeed if this program created the file
     *   in the first place.  For the moment we simply disallow writing to source files, as a
     *   compromise. */
    if ((mode & W_OK) && strncmp(path, "src/", 4) == 0) {
      /* Cannot write to source files. */
      errno = EACCES;
      return -1;
    }
    return real_access(path, mode);
  } else {
    /* Absolute path.  Don't do anything fancy. */
    path = remap_file("access", path, buffer, (mode & W_OK) ? WRITE : READ);
    if (path == NULL) return -1;
    return real_access(path, mode);
  }
}
