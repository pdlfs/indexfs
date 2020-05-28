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
#include "rados_connmgr.h"

#include "pdlfs-common/testharness.h"

#include <stdio.h>
#include <string.h>

// Parameters for ceph.
namespace {
const char* FLAHS_user_name = "client.admin";
const char* FLAGS_rados_cluster_name = "ceph";
const char* FLAGS_conf = NULL;  // Use ceph defaults
}  // namespace

namespace pdlfs {
namespace rados {

class RadosConnMgrTest {
  // Empty
};

TEST(RadosConnMgrTest, OpenAndClose) {
  RadosConnMgrOptions options;
  RadosConnMgr* const mgr = new RadosConnMgr(options);
  RadosConn* conn;
  ASSERT_OK(mgr->OpenConn(FLAGS_rados_cluster_name, FLAHS_user_name, FLAGS_conf,
                          RadosConnOptions(), &conn));
  mgr->Release(conn);
  delete mgr;
}

}  // namespace rados
}  // namespace pdlfs

int main(int argc, char* argv[]) {
  if (argc > 1) {
    for (int i = 1; i < argc; ++i) {
      ::pdlfs::Slice a = argv[i];
      if (a.starts_with("--user=")) {
        FLAHS_user_name = argv[i] + strlen("--user=");
      } else if (a.starts_with("--cluster")) {
        FLAGS_rados_cluster_name = argv[i] + strlen("--cluster=");
      } else if (a.starts_with("--conf")) {
        FLAGS_conf = argv[i] + strlen("--conf");
      }
    }
    return ::pdlfs::test::RunAllTests(&argc, &argv);
  } else {
    return 0;
  }
}
