/*
 * Copyright (c) 2019 Carnegie Mellon University,
 * Copyright (c) 2019 Triad National Security, LLC, as operator of
 *     Los Alamos National Laboratory.
 *
 * All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file. See the AUTHORS file for names of contributors.
 */

/*
 * Copyright (c) 2011 The LevelDB Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found at https://github.com/google/leveldb.
 */
#include "pdlfs-common/env.h"

#include "pdlfs-common/env_files.h"
#include "pdlfs-common/env_lazy.h"
#include "pdlfs-common/pdlfs_config.h"
#include "pdlfs-common/port.h"

#include <stdio.h>

#if defined(PDLFS_RADOS)
#include "pdlfs-common/rados/rados_ld.h"
#endif

#if defined(PDLFS_PLATFORM_POSIX)
#include "posix/posix_env.h"
#include "posix/posix_logger.h"
#endif

#if defined(PDLFS_GLOG)
#include <glog/logging.h>
#endif

namespace pdlfs {

static const size_t kBufferSize = 8192;

Env::~Env() {}

SequentialFile::~SequentialFile() {}

RandomAccessFile::~RandomAccessFile() {}

WritableFile::~WritableFile() {}

WritableFileWrapper::~WritableFileWrapper() {}

Logger::~Logger() {}

FileLock::~FileLock() {}

ThreadPool::~ThreadPool() {}

EnvWrapper::~EnvWrapper() {}

Env* Env::Open(const char* name, const char* conf, bool* is_system) {
  *is_system = false;
  if (name == NULL) name = "";
  if (conf == NULL) conf = "";
  Slice env_name(name), env_conf(conf);
#if VERBOSE >= 1
  const char* env_name_str = env_name.c_str();
  if (env_name.empty()) {
    env_name_str = "~";
  }
  const char* env_conf_str = env_conf.c_str();
  if (env_conf.empty()) {
    env_conf_str = "~";
  }
  // Verbose(__LOG_ARGS__, 1, "env.name -> %s", env_name_str);
  // Verbose(__LOG_ARGS__, 1, "env.conf -> %s", env_conf_str);
#endif
// RADOS
#if defined(PDLFS_RADOS)
  if (env_name == "rados") {
    return (Env*)PDLFS_Load_rados_env(env_conf.c_str());
  }
#endif
  if (env_name == "unbufferedio") {
    *is_system = true;
    return Env::GetUnBufferedIoEnv();
  }
  if (env_name.empty()) {
    // Warn(__LOG_ARGS__, "Open env without specifying a name...");
  }
  if (env_name.empty() || env_name == "default") {
    *is_system = true;
    Env* env = Env::Default();
    return env;
  } else {
    return NULL;
  }
}

static Status DoWriteStringToFile(Env* env, const Slice& data,
                                  const char* fname, bool should_sync) {
  WritableFile* file;
  Status s = env->NewWritableFile(fname, &file);
  if (!s.ok()) {
    return s;
  }
  s = file->Append(data);
  if (s.ok() && should_sync) {
    s = file->Sync();
  }
  if (s.ok()) {
    s = file->Close();
  }
  delete file;  // Will auto-close if we did not close above
  if (!s.ok()) {
    env->DeleteFile(fname);
  }
  return s;
}

Status WriteStringToFile(Env* env, const Slice& data, const char* fname) {
  return DoWriteStringToFile(env, data, fname, false);
}

Status WriteStringToFileSync(Env* env, const Slice& data, const char* fname) {
  return DoWriteStringToFile(env, data, fname, true);
}

Status ReadFileToString(Env* env, const char* fname, std::string* data) {
  data->clear();
  SequentialFile* file;
  Status s = env->NewSequentialFile(fname, &file);
  if (!s.ok()) {
    return s;
  }
  char* space = new char[kBufferSize];
  while (true) {
    Slice fragment;
    s = file->Read(kBufferSize, &fragment, space);
    if (!s.ok()) {
      break;
    }
    data->append(fragment.data(), fragment.size());
    if (fragment.empty()) {
      break;
    }
  }
  delete[] space;
  delete file;
  return s;
}

namespace {
/* clang-format off */
#if defined(PDLFS_PLATFORM_POSIX) && defined(PDLFS_GLOG)
class PosixGoogleLogger : public Logger {
 public:
  PosixGoogleLogger() {}
  // We try twice: the first time with a fixed-size stack allocated buffer
  // and the second time with a much larger heap allocated buffer.
  static char* VsnprintfWrapper(char (&buffer)[500], const char* fmt,
                                va_list ap) {
    char* base;
    int bufsize;
    for (int iter = 0; iter < 2; iter++) {
      if (iter == 0) {
        bufsize = sizeof(buffer);
        base = buffer;
      } else {
        bufsize = 30000;
        base = new char[bufsize];
      }
      char* p = base;
      char* limit = base + bufsize - 1;

      // Print the message
      if (p < limit) {
        va_list backup_ap;
        va_copy(backup_ap, ap);
        p += vsnprintf(p, limit - p, fmt, backup_ap);
        va_end(backup_ap);
      }

      // Truncate to available space
      if (p >= limit) {
        if (iter == 0) {
          continue;  // Try again with larger buffer
        } else {
          p = limit - 1;
        }
      }

      // Add newline if necessary
      if (p == base || p[-1] != '\n') {
        *p++ = '\n';
      }

      p[0] = 0;
      break;
    }

    return base;
  }

  virtual void Logv(const char* file, int line, int severity, int verbose,
                    const char* format, va_list ap) {
    if (severity > 0 || VLOG_IS_ON(verbose)) {
      char buffer[500];
      char* msg = VsnprintfWrapper(buffer, format, ap);
      ::google::LogMessage(file, line, severity).stream() << msg;
      if (msg != buffer) {
        delete[] msg;
      }
    }
  }
};
#endif
/* clang-format on */
class NoOpLogger : public Logger {
 public:
  NoOpLogger() {}
  virtual void Logv(const char* file, int line, int severity, int verbose,
                    const char* format, va_list ap) {
    // empty
  }
};
}  // namespace

void Log0v(Logger* logger, const char* srcfile, int srcln, int loglvl,
           const char* fmt, ...) {
  if (logger) {
    va_list ap;
    va_start(ap, fmt);
    logger->Logv(srcfile, srcln, 0, loglvl, fmt, ap);
    va_end(ap);
  }
}

Logger* Logger::Default() {
#if defined(PDLFS_PLATFORM_POSIX) && defined(PDLFS_GLOG)
  static PosixGoogleLogger logger;
  return &logger;
#elif defined(PDLFS_PLATFORM_POSIX)
  static PosixLogger logger(stderr, port::PthreadId);
  return &logger;
#else
  static NoOpLogger logger;
  return &logger;
#endif
}

}  // namespace pdlfs
