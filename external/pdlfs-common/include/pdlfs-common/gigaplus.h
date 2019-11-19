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

#include "pdlfs-common/slice.h"

#include <stdint.h>
#include <utility>

// We currently do not support dynamic changes of the total number of metadata
// servers. However, we support restarting the file system metadata with
// distinct sets of servers. For example, one can start a metadata cluster using
// an initial set of servers, stop the cluster, and then restart it using a
// different set (and number) of servers. The file system will rebalance itself
// so that each new metadata server could get an equal load compared to other
// metadata servers.
namespace pdlfs {

// Common options shared among all directory indices.
struct DirIndexOptions {
  // The number of physical servers.
  // This option can change between indexfs restarts.
  // There is no default value.
  // Valid values are [1, 65536]
  int num_servers;

  // The number of virtual servers.
  // This option cannot change between indexfs restarts.
  // There is no default value.
  // Valid values are [num_servers, 65536]
  int num_virtual_servers;

  // If true, the implementation will do aggressive checking of the
  // data it is processing and will stop early if it detects any
  // errors.
  // Default: false
  bool paranoid_checks;

  DirIndexOptions();
};

class DirIndex {
 public:
  // Create a new index using the specified settings.
  DirIndex(int zserver, const DirIndexOptions* options);
  // Create an empty index whose state is about to be reset.
  DirIndex(const DirIndexOptions* options);

  // Swap the states of two DirIndex instances.
  void Swap(DirIndex& other);

  // Discard the current index and override it with another index image.
  bool TEST_Reset(const Slice& other);

  // Update the index by merging another index of the same directory.
  bool Update(const Slice& other);

  // Update the index by merging another index of the same directory.
  void Update(const DirIndex& other);

  // Return the server responsible for the given partition.
  int GetServerForIndex(int index) const;

  // Return the partition responsible for the given file name
  int GetIndex(const Slice& name) const;

  // Return the partition responsible for the given file name hash.
  int HashToIndex(const Slice& hash) const;

  // Return the server responsible for the given file name.
  int SelectServer(const Slice& name) const;

  // Return the server responsible for the given file name hash.
  int HashToServer(const Slice& hash) const;

  // Return true iff the bit of a partition is set.
  bool IsSet(int index) const;

  // Set the bit for the partition at the given index.
  void Set(int index);

  // Set the bits for all partitions.
  void SetAll();

  // Clear the bit for the partition at the given index.
  void TEST_Unset(int index);

  // Revert all bits and roll back to the initial state.
  void TEST_RevertAll();

  // Return true if the given partition can be further divided.
  bool IsSplittable(int index) const;

  // Return the next child partition for the given parent partition.
  int NewIndexForSplitting(int index) const;

  // Return the zeroth server of the directory being indexed.
  int ZerothServer() const;

  // Return the internal bitmap radix of the index.
  int Radix() const;

  // Return the in-memory representation of this index.
  Slice Encode() const;

  // Return true if the given hash will belong to the given child partition.
  static bool ToBeMigrated(int index, const char* hash);

  // Put the corresponding hash value into *dst.
  static void PutHash(std::string* dst, const Slice& name);

  // Return the hash value of the specified name string.
  static Slice Hash(const Slice& name, char* scratch);

  // Return the server responsible for a given index.
  static int MapIndexToServer(int index, int zeroth_server, int num_servers);

  // Return a random server for a specified directory.
  static int RandomServer(const Slice& dir, int seed);

  // Return a pair of random servers for a specified directory.
  static std::pair<int, int> RandomServers(const Slice& dir, int seed);

  ~DirIndex();

 private:
  struct View;
  static bool ParseDirIndex(const Slice& input, bool checks, View*);
  const DirIndexOptions* options_;
  struct Rep;
  Rep* rep_;

  // No copying allowed
  void operator=(const DirIndex&);
  DirIndex(const DirIndex&);
};

}  // namespace pdlfs
