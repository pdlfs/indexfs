#pragma once

/*
 * Copyright (c) 2015 The SILT Authors.
 * Copyright (c) 2019 Carnegie Mellon University,
 * Copyright (c) 2019 Triad National Security, LLC, as operator of
 *     Los Alamos National Laboratory.
 *
 * All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file. See the AUTHORS file for names of contributors.
 */

namespace pdlfs {
namespace ectrie {

class sign_interleave {
 public:
  template <typename T>
  static T encode(const T& v) {
    // 0, 1, 2, 3, ... => 0, 2, 4, 8, ...
    // -1, -2, -3 -4, ... => 1, 3, 5, 7, ...
    if ((v & (static_cast<T>(1) << (sizeof(T) * 8 - 1))) == 0)
      return static_cast<T>(v << 1);
    else
      return static_cast<T>(((~v) << 1) | 1);
  }

  template <typename T>
  static T decode(const T& v) {
    // 0, 2, 4, 8, ... => 0, 1, 2, 3, ...
    // 1, 3, 5, 7, ... => -1, -2, -3, -4, ...
    if ((v & 1) == 0)
      return static_cast<T>((v >> 1) &
                            ~(static_cast<T>(1) << (sizeof(T) * 8 - 1)));
    else
      return static_cast<T>(~(v >> 1) |
                            (static_cast<T>(1) << (sizeof(T) * 8 - 1)));
  }
};

}  // namespace ectrie
}  // namespace pdlfs
