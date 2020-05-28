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

static const char* FLAGS_conf_file = "/tmp/ceph.conf";

namespace pdlfs {
namespace rados {

class RadosConnMgrTest {
  // Empty
};

TEST(RadosConnMgrTest, OpenAndClose) {
  RadosConnMgr* mgr = new RadosConnMgr(RadosConnMgrOptions());
  RadosConn* conn;
  ASSERT_OK(mgr->OpenConn(FLAGS_conf_file, RadosConnOptions(), &conn));
  mgr->Release(conn);
  delete mgr;
}

}  // namespace rados
}  // namespace pdlfs

int main(int argc, char* argv[]) {
  ::pdlfs::Slice token;
  if (argc > 1) {
    token = ::pdlfs::Slice(argv[argc - 1]);
  }
  if (token.starts_with("--withrados=")) {
    token.remove_prefix(strlen("--withrados="));
    FLAGS_conf_file = &token[0];
    return ::pdlfs::test::RunAllTests(&argc, &argv);
  } else {
    return 0;
  }
}
