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

#include "pdlfs-common/leveldb/db.h"

namespace pdlfs {

class ReadonlyDB : public DB {
 public:
  // Open a db instance on a named image with only read access.
  // Stores a pointer to the db in *dbptr and return OK on success.
  // Otherwise, stores NULL in *dbptr and returns a non-ON status.
  // Multiple readonly db instances can be opened on a single db image
  // and can follow one read-write db instance to get new updates.
  // A single process should open a db image either with full access
  // or with readonly access but not both simultaneously.
  // Caller should delete *dbptr when it is no longer needed.
  static Status Open(const Options& options, const std::string& name,
                     DB** dbptr);

  ReadonlyDB() {}
  virtual ~ReadonlyDB();

  // Write operations are not supported
  virtual Status SyncWAL();
  virtual Status FlushMemTable(const FlushOptions&);
  virtual Status Put(const WriteOptions&, const Slice& key, const Slice& value);
  virtual Status Delete(const WriteOptions&, const Slice& key);
  virtual Status Write(const WriteOptions&, WriteBatch* updates);
  virtual Status AddL0Tables(const InsertOptions&, const std::string& dir);
  virtual void CompactRange(const Slice* begin, const Slice* end);
  virtual Status DrainCompactions();

  // Load an existing db image produced by another db.
  virtual Status Load() = 0;

  // Incrementally reload new updates.
  virtual Status Reload() = 0;
};

}  // namespace pdlfs
