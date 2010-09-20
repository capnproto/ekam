/* ekam -- http://code.google.com/p/ekam
 * Copyright (c) 2010 Kenton Varda and contributors.  All rights reserved.
 * Portions copyright Google, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of the ekam project nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define _GNU_SOURCE
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

/****************************************************************************************/
/* Bootstrap pthreads without linking agaist libpthread. */

typedef int pthread_once_func(pthread_once_t*, void (*)(void));
typedef int pthread_mutex_lock_func(pthread_mutex_t*);
typedef int pthread_mutex_unlock_func(pthread_mutex_t*);

static pthread_once_func* dynamic_pthread_once = NULL;
static pthread_mutex_lock_func* dynamic_pthread_mutex_lock = NULL;
static pthread_mutex_unlock_func* dynamic_pthread_mutex_unlock = NULL;

int fake_pthread_once(pthread_once_t* once_control, void (*init_func)(void)) {
  init_func();
  return 0;
}
int fake_pthread_mutex_lock(pthread_mutex_t* mutex) { return 0; }
int fake_pthread_mutex_unlock(pthread_mutex_t* mutex) { return 0; }

void init_pthreads() {
  if (dynamic_pthread_once == NULL) {
    dynamic_pthread_once =
        (pthread_once_func*) dlsym(RTLD_DEFAULT, "pthread_once");
  }
  // TODO:  For some reason the pthread_once returned by dlsym() doesn't do anything?
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

static pthread_once_t init_once_control = PTHREAD_ONCE_INIT;

static void init_streams_once() {
  if (ekam_call_stream == NULL) {
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
  } else {
    assert(ekam_return_stream != NULL);
  }
}

static void init_streams() {
  init_pthreads();
  dynamic_pthread_once(&init_once_control, &init_streams_once);
}

/****************************************************************************************/

typedef enum usage {
  READ,
  WRITE
} usage_t;

static const char TAG_PROVIDER_PREFIX[] = "/ekam-provider/";
static const char TMP_PREFIX[] = "/tmp/";

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

static const char* remap_file(const char* pathname, char* buffer, usage_t usage) {
  char* pos;

  if (0) {
    fprintf(stderr, "remap for %s: %s\n", (usage == READ ? "read" : "write"), pathname);
  }

  init_streams();

  if (strlen(pathname) >= PATH_MAX) {
    /* Too long. */
    errno = ENAMETOOLONG;
    return NULL;
  }

  if (get_cached_result(pathname, buffer, usage)) {
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
        return buffer;
      }
      *pos = ':';
    }

    /* Ask ekam to remap the file name. */
    fputs(usage == READ ? "findProvider " : "newProvider ", ekam_call_stream);
    fputs(buffer, ekam_call_stream);
    fputs("\n", ekam_call_stream);
  } else if (strncmp(pathname, TMP_PREFIX, strlen(TMP_PREFIX)) == 0) {
    /* Temp file.  Ignore. */
    funlockfile(ekam_call_stream);
    return pathname;
  } else if (pathname[0] == '/') {
    /* Absolute path.  Note the access but don't remap. */
    if (usage == WRITE) {
      /* Cannot write to absolute paths. */
      funlockfile(ekam_call_stream);
      errno = EACCES;
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
    return pathname;
  } else if (strcmp(pathname, ".") == 0) {
    /* HACK:  Don't try to remap current directory. */
    funlockfile(ekam_call_stream);
    return pathname;
  } else {
    /* Ask ekam to remap the file name. */
    fputs(usage == READ ? "findInput " : "newOutput ", ekam_call_stream);
    fputs(pathname, ekam_call_stream);
    fputs("\n", ekam_call_stream);
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
    return NULL;
  }

  cache_result(pathname, buffer, usage);

  return buffer;
}

/****************************************************************************************/

typedef int OpenFunc(const char * pathname, int flags, ...);

#define WRAP(RETURNTYPE, NAME, PARAMTYPES, PARAMS, USAGE)                   \
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
    path = remap_file(path, buffer, USAGE);                                 \
    if (path == NULL) return -1;                                            \
    return real_##NAME PARAMS;                                              \
  }                                                                         \
  RETURNTYPE _##NAME PARAMTYPES {                                           \
    return NAME PARAMS;                                                     \
  }

WRAP(int, access, (const char* path, int mode), (path, mode), (mode & W_OK) ? WRITE : READ)
WRAP(int, stat, (const char* path, struct stat* sb), (path, sb), READ)
WRAP(int, lstat, (const char* path, struct stat* sb), (path, sb), READ)
WRAP(int, chdir, (const char* path), (path), READ)
WRAP(int, chmod, (const char* path, mode_t mode), (path, mode), WRITE)
WRAP(int, lchmod, (const char* path, mode_t mode), (path, mode), WRITE)
WRAP(int, unlink, (const char* path), (path), WRITE)
WRAP(int, link, (const char* target, const char* path), (target, path), WRITE)
WRAP(int, symlink, (const char* target, const char* path), (target, path), WRITE)

static int intercepted_open(const char * pathname, int flags, va_list args) {
  static OpenFunc* real_open;
  char buffer[PATH_MAX];
  const char* remapped;

  if (real_open == NULL) {
    real_open = (OpenFunc*) dlsym(RTLD_NEXT, "open");
    assert(real_open != NULL);
  }

  remapped = remap_file(pathname, buffer, (flags & O_ACCMODE) == O_RDONLY ? READ : WRITE);
  if (remapped == NULL) return -1;

  if(flags & O_CREAT) {
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
  static OpenFunc* real_open64;
  char buffer[PATH_MAX];
  const char* remapped;

  if (real_open64 == NULL) {
    real_open64 = (OpenFunc*) dlsym(RTLD_NEXT, "open64");
    assert(real_open64 != NULL);
  }

  remapped = remap_file(pathname, buffer, (flags & O_ACCMODE) == O_RDONLY ? READ : WRITE);
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

/* For rename(), we consider both locations to be outputs, since both are modified. */
typedef int rename_t(const char* from, const char* to);
int rename(const char* from, const char* to) {
  static rename_t* real_rename = NULL;
  char buffer[1024];
  char buffer2[1024];

  if (real_rename == NULL) {
    real_rename = (rename_t*) dlsym(RTLD_NEXT, "rename");
    assert(real_rename != NULL);
  }

  from = remap_file(from, buffer, WRITE);
  if (from == NULL) return -1;
  to = remap_file(to, buffer2, WRITE);
  if (to == NULL) return -1;
  return real_rename (from, to);
}
int _rename(const char* from, const char* to) {
  return rename(from, to);
}

/*
int execve(const char* path, char* const* argv, char* const* envp) {
  fprintf(stderr, "exec(%s", path);
  while (*argv != NULL) {
    fprintf(stderr, ", %s", *argv);
    ++argv;
  }
  fprintf(stderr, ")\n");
  exit(1);
}
int _execve(const char* path, char* const* argv, char* const* envp) {
  fprintf(stderr, "exec(%s", path);
  while (*argv != NULL) {
    fprintf(stderr, ", %s", *argv);
    ++argv;
  }
  fprintf(stderr, ")\n");
  exit(1);
}
*/
