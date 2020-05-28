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
#include "pdlfs-common/osd.h"
#include "pdlfs-common/status.h"

#include <string>

namespace pdlfs {
namespace rados {

struct RadosConn;  // Opaque handle to a rados cluster
struct RadosConnOptions {
  RadosConnOptions();
  // Timeouts (in seconds) excised when bootstrapping ceph rados.
  // Default: 5 sec
  int client_mount_timeout;

  // Timeouts (in seconds) excised when communicating with ceph mon.
  // Default: 5 sec
  int mon_op_timeout;

  // Timeouts (in seconds) excised when communicating with ceph osd.
  // Default: 5 sec
  int osd_op_timeout;
};

// Options for constructing a RadosConnMgr.
struct RadosConnMgrOptions {
  RadosConnMgrOptions();
  // Logger object for information.
  // Default: NULL
  Logger* info_log;
};

// Options for constructing a rados env.
struct RadosEnvOptions {
  RadosEnvOptions();
  // Rados mount point. All files and directories beneath it will sink into
  // rados and be stored at rados.
  // Default: "/";
  std::string rados_root;
};

// Options for constructing a rados osd instance.
struct RadosOptions {
  RadosOptions();
  // Disable async i/o. All write operations are done synchronously.
  // Default: false
  bool force_syncio;
};

// This primary interface an external user uses to obtain rados env objects.
// Creating a rados env is a 3-step process. A user first opens a rados
// connection. Next, the user uses the connection to create a rados osd object.
// The user then uses the rados osd object to obtain a rados env.
class RadosConnMgr {
 public:
  explicit RadosConnMgr(const RadosConnMgrOptions& options);
  ~RadosConnMgr();

  // Open a rados connection. Return OK on success, or a non-OK status on
  // errors. The returned rados connection instance shall be released through
  // the connection manager when it is no longer needed.
  Status OpenConn(const std::string& conf_file, const RadosConnOptions& options,
                  RadosConn** conn);

  // Create a rados osd instance backed by an open rados connection. Return OK
  // on success, or a non-OK status on errors. The returned osd instance shall
  // be released when it is no longer needed.
  Status OpenOsd(RadosConn* conn, const std::string& pool_name,
                 const RadosOptions& options, Osd** osd);

  // Create a rados env instance backed by an open osd instance. Return OK on
  // success, or a non-OK status on errors. The returned env instance shall be
  // deleted when it is no longer needed. For testing/debugging purposes, a
  // non-rados osd instance may be used to create a ceph rados env.
  //
  // The resulting env provides a virtual filesystem namespace tree mounted on
  // the local filesystem at options.rados_root, such that each directory is
  // regarded as a fileset mapped to a remote rados object storing the members
  // of the fileset, and each file under that set is mapped to an object that
  // stores the contents of that file.
  //
  // For example, if "rados_root" is set to "/", directory "/a/b/c" will be
  // mapped to a remote object named "_a_b_c", and file "/a/b/c/d" will be
  // mapped to "_a_b_c_d". If "rados_root" is "/a", directory "/a/b/c" will then
  // be mapped to "_b_c". And if "rados_root" is "/a/b/c", directory "/a/b/c"
  // will then be mapped to "_".
  //
  // REQUIRES: neither base_env nor osd may be NULL.
  static Status OpenEnv(Env* base_env, Osd* osd, bool owns_osd,
                        const RadosEnvOptions& options, Env** env);

  void Release(RadosConn* conn);

 private:
  // No copying allowed
  void operator=(const RadosConnMgr& other);
  RadosConnMgr(const RadosConnMgr&);

  void Unref(RadosConn* conn);

  class Rep;
  Rep* rep_;
};

}  // namespace rados
}  // namespace pdlfs
