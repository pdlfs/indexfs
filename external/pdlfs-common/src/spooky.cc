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
#include "pdlfs-common/spooky.h"
#include "spooky/SpookyV2.h"

#include <string.h>

namespace pdlfs {

void Spooky128(  ///
    const void* k, size_t n, const uint64_t seed1, const uint64_t seed2,
    void* result) {
  char* const buf = static_cast<char*>(result);
  uint64_t v1 = seed1;
  uint64_t v2 = seed2;
  ::SpookyHash::Hash128(k, n, &v1, &v2);
  memcpy(buf, &v1, 8);
  memcpy(buf + 8, &v2, 8);
}

}  // namespace pdlfs
