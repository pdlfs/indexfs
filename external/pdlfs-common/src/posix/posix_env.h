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
#pragma once

#include "pdlfs-common/env.h"
#include "pdlfs-common/hashmap.h"
#include "pdlfs-common/mutexlock.h"
#include "pdlfs-common/port.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

namespace pdlfs {

// Return errors as status objects.
extern Status PosixError(const Slice& err_context, int err_number);

inline int LockOrUnlock(int fd, bool lock) {
  errno = 0;
  struct flock f;
  memset(&f, 0, sizeof(f));
  f.l_type = static_cast<short>(lock ? F_WRLCK : F_UNLCK);
  f.l_whence = SEEK_SET;
  f.l_start = 0;
  f.l_len = 0;  // Cover the entire file
  return fcntl(fd, F_SETLK, &f);
}

class PosixFileLock : public FileLock {
 public:
  PosixFileLock() {}
  std::string name_;
  int fd_;
};

// Set of locked files.  We keep a separate set instead of just
// relying on fcntrl(F_SETLK) since fcntl(F_SETLK) does not provide
// any protection against multiple uses from the same process.
class LockTable {
 private:
  port::Mutex mu_;
  HashSet locked_files_;

  // No copying allowed
  LockTable(const LockTable&);
  void operator=(const LockTable&);

 public:
  LockTable() {}

  void Remove(const Slice& fname) {
    MutexLock l(&mu_);
    locked_files_.Erase(fname);
  }

  bool Insert(const Slice& fname) {
    MutexLock l(&mu_);
    if (locked_files_.Contains(fname)) {
      return false;
    } else {
      locked_files_.Insert(fname);
      return true;
    }
  }
};

class PosixBufferedSequentialFile : public SequentialFile {
 private:
  const std::string filename_;
  FILE* const file_;

 public:
  PosixBufferedSequentialFile(const char* fname, FILE* f)
      : filename_(fname), file_(f) {}

  virtual ~PosixBufferedSequentialFile();

  virtual Status Read(size_t n, Slice* result, char* scratch) {
    Status s;
    size_t r = fread_unlocked(scratch, 1, n, file_);
    *result = Slice(scratch, r);
    if (r < n) {
      if (feof(file_)) {
        // Leave status as ok if we hit eof
      } else {
        // A partial read with an error: return a non-ok status
        s = PosixError(filename_, errno);
      }
    }
    return s;
  }

  virtual Status Skip(uint64_t n) {
    if (fseek(file_, n, SEEK_CUR)) {
      return PosixError(filename_, errno);
    }
    return Status::OK();
  }
};

class PosixSequentialFile : public SequentialFile {
 private:
  const std::string filename_;
  const int fd_;

 public:
  PosixSequentialFile(const char* fname, int fd) : filename_(fname), fd_(fd) {}

  virtual ~PosixSequentialFile();

  virtual Status Read(size_t n, Slice* result, char* scratch) {
    Status s;
    ssize_t nr = read(fd_, scratch, n);
    if (nr == -1) {
      s = PosixError(filename_, errno);
    } else if (nr != 0) {
      *result = Slice(scratch, static_cast<size_t>(nr));
    } else {  // EOF
      *result = Slice(scratch, 0);
    }

    return s;
  }

  virtual Status Skip(uint64_t n) {
    off_t r = lseek(fd_, n, SEEK_CUR);
    if (r == -1) {
      return PosixError(filename_, errno);
    } else {
      return Status::OK();
    }
  }
};

class PosixRandomAccessFile : public RandomAccessFile {
 private:
  const std::string filename_;
  const int fd_;

 public:
  PosixRandomAccessFile(const char* fname, int fd)
      : filename_(fname), fd_(fd) {}

  virtual ~PosixRandomAccessFile();

  virtual Status Read(uint64_t offset, size_t n, Slice* result,
                      char* scratch) const {
    Status s;
    ssize_t r = pread(fd_, scratch, n, static_cast<off_t>(offset));
    if (r < 0) {
      // An error: return a non-ok status
      s = PosixError(filename_, errno);
      *result = Slice();
    } else {
      *result = Slice(scratch, r);
    }
    return s;
  }
};

class PosixBufferedWritableFile : public WritableFile {
 private:
  std::string filename_;
  FILE* file_;

 public:
  PosixBufferedWritableFile(const char* fname, FILE* f)
      : filename_(fname), file_(f) {}

  virtual ~PosixBufferedWritableFile() {
    if (file_ != NULL) {
      // Ignoring any potential errors
      fclose(file_);
    }
  }

  virtual Status Append(const Slice& data) {
    size_t r = fwrite_unlocked(data.data(), 1, data.size(), file_);
    if (r != data.size()) {
      return PosixError(filename_, errno);
    }
    return Status::OK();
  }

  virtual Status Close() {
    Status result;
    if (fclose(file_) != 0) {
      result = PosixError(filename_, errno);
    }
    file_ = NULL;
    return result;
  }

  virtual Status Flush() {
    if (fflush_unlocked(file_) != 0) {
      return PosixError(filename_, errno);
    }
    return Status::OK();
  }

  Status SyncDirIfManifest() {
    const char* f = filename_.c_str();
    const char* sep = strrchr(f, '/');
    Slice basename;
    std::string dir;
    if (sep == NULL) {
      dir = ".";
      basename = f;
    } else {
      dir = std::string(f, sep - f);
      basename = sep + 1;
    }
    Status s;
    if (basename.starts_with("MANIFEST")) {
      int fd = open(dir.c_str(), O_RDONLY);
      if (fd < 0) {
        s = PosixError(dir, errno);
      } else {
        if (fsync(fd) < 0) {
          s = PosixError(dir, errno);
        }
        close(fd);
      }
    }
    return s;
  }

  virtual Status Sync() {
    // Ensure new files referred to by the manifest are in the file system.
    Status s = SyncDirIfManifest();
    if (!s.ok()) {
      return s;
    }
    if (fflush_unlocked(file_) != 0 || fdatasync(fileno(file_)) != 0) {
      s = Status::IOError(filename_, strerror(errno));
    }
    return s;
  }
};

class PosixWritableFile : public WritableFile {
 private:
  std::string filename_;
  int fd_;

 public:
  PosixWritableFile(const char* fname, int fd) : filename_(fname), fd_(fd) {}

  virtual ~PosixWritableFile() {
    if (fd_ != -1) {
      close(fd_);
    }
  }

  virtual Status Append(const Slice& buf) {
    if (buf.empty()) return Status::OK();
    ssize_t nw = write(fd_, buf.data(), buf.size());
    if (nw != buf.size()) {
      return PosixError(filename_, errno);
    } else {
      return Status::OK();
    }
  }

  virtual Status Close() {
    close(fd_);
    fd_ = -1;
    return Status::OK();
  }

  virtual Status Flush() {
    // Do nothing since we never buffer any data
    return Status::OK();
  }

  virtual Status SyncDirIfManifest() {
    const char* f = filename_.c_str();
    const char* sep = strrchr(f, '/');
    Slice basename;
    std::string dir;
    if (sep == NULL) {
      dir = ".";
      basename = f;
    } else {
      dir = std::string(f, sep - f);
      basename = sep + 1;
    }
    Status s;
    if (basename.starts_with("MANIFEST")) {
      int fd = open(dir.c_str(), O_RDONLY);
      if (fd < 0) {
        s = PosixError(dir, errno);
      } else {
        if (fsync(fd) < 0) {
          s = PosixError(dir, errno);
        }
        close(fd);
      }
    }
    return s;
  }

  virtual Status Sync() {
    // Ensure new files referred to by the manifest are in the file system.
    Status s = SyncDirIfManifest();
    if (!s.ok()) {
      return s;
    } else {
      int r = fdatasync(fd_);
      if (r != 0) {
        s = PosixError(filename_, errno);
      }
    }

    return s;
  }
};

class PosixEmptyFile : public RandomAccessFile {
 public:
  PosixEmptyFile() {}
  virtual ~PosixEmptyFile() {}

  virtual Status Read(uint64_t offset, size_t n, Slice* result,
                      char* scratch) const {
    *result = Slice();
    return Status::OK();
  }
};

}  // namespace pdlfs
