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

#pragma once

#include "pdlfs-common/env.h"

#include <assert.h>
#include <string>

namespace pdlfs {

// An enhanced WritableFile abstraction with richer semantics
// on durability control.
class SynchronizableFile : public WritableFile {
 public:
  SynchronizableFile() {}
  virtual ~SynchronizableFile();

  // Force file data [0, offset) to be flushed to the underlying storage
  // hardware. After this call, file data at [offset, ...) may still be buffered
  // in memory.
  virtual Status SyncBefore(uint64_t offset) = 0;

  // Flush file buffering and force data to be sent to the underlying storage
  // software, but not necessarily the hardware.
  virtual Status EmptyBuffer() = 0;
};

// Always buffer a certain amount of data before eventually flushing data to
// a given *base. Ignore all explicit Flush() calls, but EmptyBuffer(), Sync(),
// and SyncBefore() calls are respected. May lose data for clients that only
// use Flush() calls to ensure data durability. To avoid losing data, clients
// may choose to call Sync() at a certain time interval, or use EmptyBuffer()
// calls to force data flush.
// Implementation is not thread-safe and requires external synchronization for
// use by multiple threads.
// Write buffering will cause an extra copy of data in memory
class MinMaxBufferedWritableFile : public SynchronizableFile {
 public:
  std::string* buffer_store() { return &buf_; }

  // *base must remain alive during the lifetime of this class and will be
  // closed and deleted when the destructor of this class is called.
  MinMaxBufferedWritableFile(WritableFile* base, size_t min, size_t max)
      : base_(base), offset_(0), min_buf_size_(min), max_buf_size_(max) {
    assert(min_buf_size_ <= max_buf_size_);
    buf_.reserve(max_buf_size_);
  }

  virtual ~MinMaxBufferedWritableFile() {
    if (base_ != NULL) {
      base_->Close();
      delete base_;
    }
  }

  virtual Status Close() {
    Status status = EmptyBuffer();
    if (status.ok()) {
      status = base_->Close();
    }
    delete base_;
    base_ = NULL;
    return status;
  }

  virtual Status Append(const Slice& data) {
    Status status;
    Slice chunk = data;
    while (buf_.size() + chunk.size() >= max_buf_size_) {
      size_t left = max_buf_size_ - buf_.size();
      buf_.append(chunk.data(), left);
      status = EmptyBuffer();
      if (status.ok()) {
        chunk.remove_prefix(left);
      } else {
        break;
      }
    }
    if (status.ok()) {
      if (chunk.size() != 0) {
        buf_.append(chunk.data(), chunk.size());
      }
      if (buf_.size() >= min_buf_size_) {
        status = EmptyBuffer();
      }
    }
    return status;
  }

  virtual Status SyncBefore(uint64_t offset) {
    if (offset_ >= offset) {
      return Status::OK();  // Data already flushed out
    } else {
      return EmptyBuffer();
    }
  }

  virtual Status Sync() {
    Status status = EmptyBuffer();
    if (status.ok()) {
      status = base_->Sync();
    }
    return status;
  }

  virtual Status Flush() {
    return Status::OK();  // Ignore all Flush() calls
  }

  virtual Status EmptyBuffer() {
    Status status;
    const size_t buf_size = buf_.size();
    assert(buf_size <= max_buf_size_);
    if (buf_size != 0) {
      status = base_->Append(buf_);
      if (status.ok()) status = base_->Flush();
      if (status.ok()) {
        offset_ += buf_size;
        buf_.resize(0);
      }
    }
    return status;
  }

 private:
  WritableFile* base_;
  uint64_t offset_;  // Number of bytes flushed out
  const size_t min_buf_size_;
  const size_t max_buf_size_;
  std::string buf_;
};

// Performance stats collected by a MonitoredWritableFile.
class WritableFileStats {
 public:
  WritableFileStats() { Reset(); }

  // Return the total number of flush operations invoked.
  uint32_t TotalFlushOps() const { return num_flushes_; }

  // Return the total number of sync operation invoked.
  uint32_t TotalSyncs() const { return num_syncs_; }

  // Return the total number of bytes written out.
  uint64_t TotalBytes() const { return bytes_; }

  // Return the total number of write operations witnessed.
  uint64_t TotalOps() const { return ops_; }

 private:
  friend class MonitoredWritableFile;
  void Reset();

  uint32_t num_syncs_;
  uint32_t num_flushes_;
  uint64_t bytes_;
  uint64_t ops_;
};

// A WritableFile wrapper implementation that collects write performance stats
// in an external WritableFileStats object. Implementation is not thread safe.
// External synchronization is needed for use by multiple threads.
class MonitoredWritableFile : public WritableFile {
 public:
  // REQUIRES: *base must remain alive during the lifetime of this object. *base
  // is closed and deleted when the destructor of this class is called.
  MonitoredWritableFile(WritableFileStats* stats, WritableFile* base)
      : stats_(stats) {
    Reset(base);
  }
  virtual ~MonitoredWritableFile() {
    if (base_ != NULL) {
      base_->Close();
      delete base_;
    }
  }

  // REQUIRES: External synchronization.
  virtual Status Flush() {
    if (base_ == NULL) {
      return Status::AssertionFailed("base_ is empty");
    } else {
      Status status = base_->Flush();
      if (status.ok()) {
        stats_->num_flushes_++;
      }
      return status;
    }
  }

  // REQUIRES: External synchronization.
  virtual Status Sync() {
    if (base_ == NULL) {
      return Status::AssertionFailed("base_ is empty");
    } else {
      Status status = base_->Sync();
      if (status.ok()) {
        stats_->num_syncs_++;
      }
      return status;
    }
  }

  // REQUIRES: External synchronization.
  virtual Status Append(const Slice& data) {
    if (base_ == NULL) {
      return Status::Disconnected(Slice());
    } else {
      Status status = base_->Append(data);
      if (status.ok()) {
        stats_->bytes_ += data.size();
        stats_->ops_ += 1;
      }
      return status;
    }
  }

  // REQUIRES: External synchronization.
  virtual Status Close() {
    if (base_ != NULL) {
      Status status = base_->Close();
      delete base_;
      base_ = NULL;
      return status;
    } else {
      return Status::OK();
    }
  }

 private:
  // Reset the counters and the base target.
  void Reset(WritableFile* base) {
    stats_->Reset();
    base_ = base;
  }

  WritableFileStats* stats_;
  WritableFile* base_;
};

// Performance stats collected by a MonitoredSequentialFile.
class SequentialFileStats {
 public:
  SequentialFileStats() { Reset(); }

  // Total number of bytes read in.
  uint64_t TotalBytes() const { return bytes_; }

  // Total number of read operations witnessed.
  uint64_t TotalOps() const { return ops_; }

 private:
  friend class MonitoredSequentialFile;
  void Reset();

  uint64_t bytes_;
  uint64_t ops_;
};

// A SequentialFile wrapper implementation that collects read performance stats
// in an external SequentialFileStats object. Implementation is not thread safe.
// External synchronization is needed for use by multiple threads.
class MonitoredSequentialFile : public SequentialFile {
 public:
  MonitoredSequentialFile(SequentialFileStats* stats, SequentialFile* base)
      : stats_(stats) {
    Reset(base);
  }
  virtual ~MonitoredSequentialFile() { delete base_; }

  // REQUIRES: External synchronization.
  virtual Status Read(size_t n, Slice* result, char* scratch) {
    if (base_ == NULL) {
      return Status::AssertionFailed("base_ is empty");
    } else {
      Status status = base_->Read(n, result, scratch);
      if (status.ok()) {
        stats_->bytes_ += result->size();
        stats_->ops_ += 1;
      }
      return status;
    }
  }

  // REQUIRES: External synchronization.
  virtual Status Skip(uint64_t n) {
    if (base_ == NULL) {
      return Status::AssertionFailed("base_ is empty");
    } else {
      return base_->Skip(n);
    }
  }

 private:
  // Reset the counters and the base target.
  void Reset(SequentialFile* base) {
    stats_->Reset();
    base_ = base;
  }

  SequentialFileStats* stats_;
  SequentialFile* base_;
};

// Performance stats collected by a MonitoredRandomAccessFile.
class RandomAccessFileStats {
 public:
  RandomAccessFileStats();
  ~RandomAccessFileStats();

  // Total number of bytes read in.
  uint64_t TotalBytes() const;

  // Total number of read operations witnessed.
  uint64_t TotalOps() const;

 private:
  friend class MonitoredRandomAccessFile;
  void AcceptRead(uint64_t n);
  void Reset();

  struct Rep;
  Rep* rep_;
};

// A RandomAccessFile wrapper implementation that collects read performance
// stats in an external RandomAccessFileStats object. Implementation is thread
// safe. External synchronization is not needed for use by multiple threads.
class MonitoredRandomAccessFile : public RandomAccessFile {
 public:
  MonitoredRandomAccessFile(RandomAccessFileStats* stats,
                            RandomAccessFile* base)
      : stats_(stats) {
    Reset(base);
  }
  virtual ~MonitoredRandomAccessFile() { delete base_; }

  // Implementation is safe for concurrent use by multiple threads.
  virtual Status Read(uint64_t offset, size_t n, Slice* result,
                      char* scratch) const {
    Status status = base_->Read(offset, n, result, scratch);
    if (status.ok()) {
      stats_->AcceptRead(result->size());
    }
    return status;
  }

 private:
  // Reset the counters and the base target.
  void Reset(RandomAccessFile* base) {
    stats_->Reset();
    base_ = base;
  }

  RandomAccessFileStats* stats_;
  RandomAccessFile* base_;
};

// Convert a sequential file into a fully buffered random access file by
// pre-fetching all file contents into memory and use that to serve all future
// read requests to the underlying file. At most "max_buf_size_" worth of data
// will be fetched and buffered in memory. Callers must explicitly call Load()
// to pre-populate the file contents in memory.
class WholeFileBufferedRandomAccessFile : public RandomAccessFile {
 public:
  WholeFileBufferedRandomAccessFile(SequentialFile* base, size_t buf_size,
                                    size_t io_size = 4096)
      : base_(base), max_buf_size_(buf_size), io_size_(io_size) {
    buf_ = new char[max_buf_size_];
    buf_size_ = 0;
  }

  virtual ~WholeFileBufferedRandomAccessFile() {
    delete[] buf_;
    if (base_ != NULL) {
      delete base_;
    }
  }

  // The returned slice will remain valid as long as the file is not deleted.
  // Safe for concurrent use by multiple threads.
  virtual Status Read(uint64_t offset, size_t n, Slice* result,
                      char* scratch) const {
    if (offset < buf_size_) {
      if (n > buf_size_ - offset) n = buf_size_ - offset;
      *result = Slice(buf_ + offset, n);
    } else {
      *result = Slice();
    }

    return Status::OK();
  }

  // REQUIRES: Load() has not been called before.
  Status Load();

 private:
  SequentialFile* base_;
  const size_t max_buf_size_;
  const size_t io_size_;
  size_t buf_size_;
  char* buf_;
};

}  // namespace pdlfs
