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

#include "pdlfs-common/leveldb/db/dbformat.h"
#include "pdlfs-common/leveldb/db/options.h"
#include "pdlfs-common/status.h"

/* clang-format off */
namespace pdlfs {

struct FileMetaData;

class TableCache;
class Iterator;

// Build a Table file from the contents of *iter.  The generated file
// will be named according to meta->number.  On success, the rest of
// *meta will be filled with metadata about the generated table.
// If no data is present in *iter, meta->file_size will be set to
// zero, and no Table file will be produced.
extern Status BuildTable(
    const std::string& dbname,
    Env* env,
    const DBOptions& options,
    TableCache* table_cache,
    Iterator* iter,
    SequenceNumber* min_seq,
    SequenceNumber* max_seq,
    FileMetaData* meta
);

}  // namespace pdlfs
/* clang-format on */
