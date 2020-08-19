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

#include "readonly_impl.h"

#include "db_impl.h"
#include "db_iter.h"
#include "table_cache.h"
#include "version_set.h"

#include "../merger.h"

#include "pdlfs-common/leveldb/filenames.h"
#include "pdlfs-common/leveldb/snapshot.h"

#include "pdlfs-common/env.h"
#include "pdlfs-common/mutexlock.h"
#include "pdlfs-common/port.h"
#include "pdlfs-common/status.h"

namespace pdlfs {

ReadonlyDBImpl::ReadonlyDBImpl(const Options& raw_options,
                               const std::string& dbname)
    : env_(raw_options.env),
      internal_comparator_(raw_options.comparator),
      internal_filter_policy_(raw_options.filter_policy),
      options_(SanitizeOptions(dbname, &internal_comparator_,
                               &internal_filter_policy_, raw_options, false)),
      owns_cache_(options_.block_cache != raw_options.block_cache),
      owns_table_cache_(options_.table_cache != raw_options.table_cache),
      dbname_(dbname),
      logfile_(NULL),
      log_(NULL) {
  table_cache_ = new TableCache(dbname_, &options_, options_.table_cache);

  versions_ =
      new VersionSet(dbname_, &options_, table_cache_, &internal_comparator_);
}

ReadonlyDBImpl::~ReadonlyDBImpl() {
  delete versions_;
  delete log_;
  delete logfile_;
  delete table_cache_;

  if (owns_cache_) delete options_.block_cache;
  if (owns_table_cache_) delete options_.table_cache;

  if (options_.detach_dir_on_close) {
    env_->DetachDir(dbname_.c_str());
  }
}

Status ReadonlyDBImpl::Load() {
  mutex_.AssertHeld();
  if (log_ != NULL) {
    return Reload();
  }

  env_->AttachDir(dbname_.c_str());
  std::string fname;
  const std::string man1 = DescriptorFileName(dbname_, 1);
  const std::string man2 = DescriptorFileName(dbname_, 2);
  if (env_->FileExists(man1.c_str())) {
    fname = man1;
  } else if (env_->FileExists(man2.c_str())) {
    fname = man2;
  } else {
    std::string current;
    const std::string curr = CurrentFileName(dbname_);
    // Use "CURRENT" file to obtain the current manifest file
    Status s = ReadFileToString(env_, curr.c_str(), &current);
    if (s.ok() && !current.empty()) {
      if (current[current.size() - 1] == '\n') {
        current.resize(current.size() - 1);
        fname = dbname_ + "/" + current;
      }
    }
  }

  if (fname.empty()) {
    return Status::Corruption(dbname_, "No valid manifest available");
  } else {
#if VERBOSE >= 1
    Log(options_.info_log, 1, "Fast forwarding db state to %s", fname.c_str());
#endif
    assert(logfile_ == NULL);
    Status s = env_->NewSequentialFile(fname.c_str(), &logfile_);
    if (s.ok()) {
      log_ = new log::Reader(logfile_, NULL, true /*checksum*/,
                             0 /*initial_offset*/);
      Slice record;
      std::string scratch;
      while (log_->ReadRecord(&record, &scratch) && s.ok()) {
        VersionEdit edit;
        s = edit.DecodeFrom(record);
        if (s.ok()) {
          s = versions_->ForeighApply(&edit);
        }
      }
    }
    return s;
  }
}

Status ReadonlyDBImpl::Reload() {
  mutex_.AssertHeld();
  if (log_ == NULL) {
    return Load();
  }

  env_->DetachDir(dbname_.c_str());
  env_->AttachDir(dbname_.c_str());
  Status s;
  Slice record;
  std::string scratch;
  bool ignore_EOF = true;
  while (log_->ReadRecord(&record, &scratch, ignore_EOF) && s.ok()) {
    VersionEdit edit;
    s = edit.DecodeFrom(record);
    if (s.ok()) {
      s = versions_->ForeighApply(&edit);
    }
    ignore_EOF = false;
  }

  return s;
}

Status ReadonlyDBImpl::InternalGet(const ReadOptions& options, const Slice& key,
                                   Buffer* value) {
  Status s;
  MutexLock ml(&mutex_);
  SequenceNumber snapshot;
  if (options.snapshot != NULL) {
    snapshot = reinterpret_cast<const SnapshotImpl*>(options.snapshot)->number_;
  } else {
    snapshot = versions_->LastSequence();
  }

  Version* current = versions_->current();
  current->Ref();

  // Unlock while reading from files
  {
    mutex_.Unlock();
    LookupKey lkey(key, snapshot);
    Version::GetStats ignored;
    current->Get(options, lkey, value, &s, &ignored);
    mutex_.Lock();
  }

  current->Unref();
  return s;
}

Status ReadonlyDBImpl::Get(const ReadOptions& options, const Slice& key,
                           std::string* value) {
  db::StringBuf buf(value);
  Status s = InternalGet(options, key, &buf);
  return s;
}

Status ReadonlyDBImpl::Get(const ReadOptions& options, const Slice& key,
                           Slice* value, char* scratch, size_t scratch_size) {
  db::DirectBuf buf(scratch, scratch_size);
  Status s = InternalGet(options, key, &buf);
  if (s.ok()) {
    *value = buf.Read();
    if (value->data() == NULL) {
      s = Status::BufferFull(Slice());
    }
  }
  return s;
}

namespace {
struct IterState {
  port::Mutex* mu;
  Version* version;
};
}  // namespace

static void CleanupIteratorState(void* arg1, void* arg2) {
  IterState* state = reinterpret_cast<IterState*>(arg1);
  state->mu->Lock();
  state->version->Unref();
  state->mu->Unlock();
  delete state;
}

Iterator* ReadonlyDBImpl::NewInternalIterator(
    const ReadOptions& options, SequenceNumber* lastest_snapshot) {
  IterState* cleanup = new IterState;
  mutex_.Lock();
  *lastest_snapshot = versions_->LastSequence();

  // Collect together all needed child iterators
  std::vector<Iterator*> list;
  versions_->current()->AddIterators(options, &list);
  Iterator* internal_iter =
      NewMergingIterator(&internal_comparator_, &list[0], list.size());
  versions_->current()->Ref();

  cleanup->mu = &mutex_;
  cleanup->version = versions_->current();
  internal_iter->RegisterCleanup(CleanupIteratorState, cleanup, NULL);

  mutex_.Unlock();
  return internal_iter;
}

Iterator* ReadonlyDBImpl::NewIterator(const ReadOptions& options) {
  SequenceNumber latest_snapshot;
  Iterator* iter = NewInternalIterator(options, &latest_snapshot);
  return NewDBIterator(
      NULL, user_comparator(), iter,
      (options.snapshot != NULL
           ? reinterpret_cast<const SnapshotImpl*>(options.snapshot)->number_
           : latest_snapshot),
      0);
}

const Snapshot* ReadonlyDBImpl::GetSnapshot() {
  return NULL;  // Implicitly the latest snapshot
}

void ReadonlyDBImpl::ReleaseSnapshot(const Snapshot* snapshot) {
  assert(snapshot == NULL);
}

bool ReadonlyDBImpl::GetProperty(const Slice& property, std::string* value) {
  return false;
}

void ReadonlyDBImpl::GetApproximateSizes(const Range* range, int n,
                                         uint64_t* sizes) {
  // TODO(opt): better implementation
  Version* v;
  {
    MutexLock ml(&mutex_);
    versions_->current()->Ref();
    v = versions_->current();
  }

  for (int i = 0; i < n; i++) {
    // Convert user_key into a corresponding internal key.
    InternalKey k1(range[i].start, kMaxSequenceNumber, kValueTypeForSeek);
    InternalKey k2(range[i].limit, kMaxSequenceNumber, kValueTypeForSeek);
    uint64_t start = versions_->ApproximateOffsetOf(v, k1);
    uint64_t limit = versions_->ApproximateOffsetOf(v, k2);
    sizes[i] = (limit >= start ? limit - start : 0);
  }

  {
    MutexLock ml(&mutex_);
    v->Unref();
  }
}

Status ReadonlyDBImpl::Dump(const DumpOptions&, const Range& range,
                            const std::string& dir, SequenceNumber* min_seq,
                            SequenceNumber* max_seq) {
  return Status::NotSupported(Slice());
}

Status ReadonlyDB::Open(const Options& options, const std::string& dbname,
                        DB** dbptr) {
  *dbptr = NULL;

  ReadonlyDBImpl* impl = new ReadonlyDBImpl(options, dbname);
#if VERBOSE >= 1
  Log(options.info_log, 1, "Opening db at %s ...", dbname.c_str());
#endif
  impl->mutex_.Lock();
  Status s = impl->Load();
  impl->mutex_.Unlock();
  if (s.ok()) {
    *dbptr = impl;
  } else {
    delete impl;
  }

  return s;
}

}  // namespace pdlfs
