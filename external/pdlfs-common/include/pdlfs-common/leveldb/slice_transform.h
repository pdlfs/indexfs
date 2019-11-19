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
 * Copyright (c) 2013 Facebook, Inc. All rights reserved.
 * This source code is dual-licensed under the GPLv2 and Apache 2.0
 * License that can both be found at https://github.com/facebook/rocksdb.
 * One may select either of the two licenses.
 */
#pragma once

#include "pdlfs-common/slice.h"

#include <string>

namespace pdlfs {

// Interface for specifying user-defined functions that perform a
// transformation on a slice.
class SliceTransform {
 public:
  SliceTransform() {}
  virtual ~SliceTransform();

  virtual Slice Transform(const Slice& input, std::string* scratch) const = 0;

  // The name of the transformation.
  virtual const char* Name() const = 0;
};

extern const SliceTransform* NewFixedPrefixTransform(size_t prefix_len);

extern const SliceTransform* NewEchoTransform();

}  // namespace pdlfs
