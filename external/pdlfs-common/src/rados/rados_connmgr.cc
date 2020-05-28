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

#include "rados_env.h"
#include "rados_osd.h"

#include "pdlfs-common/mutexlock.h"
#include "pdlfs-common/port.h"

namespace pdlfs {
namespace rados {

RadosConnOptions::RadosConnOptions()
    : client_mount_timeout(5), mon_op_timeout(5), osd_op_timeout(5) {}

RadosConnMgrOptions::RadosConnMgrOptions() : info_log(NULL) {}

RadosEnvOptions::RadosEnvOptions() : rados_root("/") {}

RadosOptions::RadosOptions() : force_syncio(false) {}

class RadosConnMgr::Rep {
 public:
  RadosConnMgrOptions options;
  port::Mutex mutex_;
  // State below protected by mutex_
  RadosConn list;  // Dummy list header

  Rep(const RadosConnMgrOptions& options) : options(options) {
    list.prev = &list;
    list.next = &list;

    if (!this->options.info_log) {
      this->options.info_log = Logger::Default();
    }
  }
};

RadosConnMgr::RadosConnMgr(const RadosConnMgrOptions& options)
    : rep_(new Rep(options)) {}

RadosConnMgr::~RadosConnMgr() {
  {
    MutexLock ml(&rep_->mutex_);
    assert(&rep_->list == rep_->list.prev);
    assert(&rep_->list == rep_->list.next);
  }

  delete rep_;
}

namespace {
inline void RadosConfSet(rados_t cluster, const char* opt, int val) {
  char tmp[20];
  snprintf(tmp, sizeof(tmp), "%d", val);
  rados_conf_set(cluster, opt, tmp);
}

Status RadosConfAndConnect(  ///
    rados_t cluster, const char* const conf_file,
    const RadosConnOptions& options) {
  int rv = rados_conf_read_file(cluster, conf_file);
  if (rv == 0) {
    RadosConfSet(cluster, "rados_mon_op_timeout", options.mon_op_timeout);
    RadosConfSet(cluster, "rados_osd_op_timeout", options.osd_op_timeout);
    RadosConfSet(cluster, "client_mount_timeout", options.client_mount_timeout);
    rv = rados_connect(cluster);
  }
  if (rv < 0) {
    return RadosError("Cannot conf/connect to rados", rv);
  } else {
    return Status::OK();
  }
}
}  // namespace

Status RadosConnMgr::OpenConn(  ///
    const char* cluster_name, const char* user_name, const char* conf_file,
    const RadosConnOptions& options, RadosConn** conn) {
  rados_t cluster;
  int rv = rados_create2(&cluster, cluster_name, user_name, 0);
  if (rv < 0) {
    return RadosError("Error creating hdl", rv);
  }
  Status status = RadosConfAndConnect(cluster, conf_file, options);
  if (!status.ok()) {
    rados_shutdown(cluster);
    return status;
  }
  MutexLock ml(&rep_->mutex_);
  RadosConn* const new_conn =
      static_cast<RadosConn*>(malloc(sizeof(RadosConn)));
  RadosConn* list = &rep_->list;
  new_conn->next = list;
  new_conn->prev = list->prev;
  new_conn->prev->next = new_conn;
  new_conn->next->prev = new_conn;
  rados_cluster_fsid(  ///
      cluster, new_conn->cluster_fsid, sizeof(new_conn->cluster_fsid));
  new_conn->cluster = cluster;
  new_conn->nrefs = 1;
  *conn = new_conn;
  return status;
}

void RadosConnMgr::Unref(RadosConn* const conn) {
  rep_->mutex_.AssertHeld();
  assert(conn->nrefs > 0);
  --conn->nrefs;
  if (!conn->nrefs) {
    conn->next->prev = conn->prev;
    conn->prev->next = conn->next;
#if VERBOSE >= 1
    Log(rep_->options.info_log, 1, "Shutting down rados cluster %s ...",
        conn->cluster_fsid);
#endif
    rados_shutdown(conn->cluster);
    free(conn);
  }
}

void RadosConnMgr::Release(RadosConn* conn) {
  MutexLock ml(&rep_->mutex_);
  if (conn) {
    Unref(conn);
  }
}

Status RadosConnMgr::OpenOsd(  ///
    RadosConn* conn, const char* pool_name, const RadosOptions& options,
    Osd** result) {
  Status status;
  rados_ioctx_t ioctx;
  int rv = rados_ioctx_create(conn->cluster, pool_name, &ioctx);
  if (rv < 0) {
    status = RadosError("Cannot create ioctx", rv);
  } else {
    MutexLock ml(&rep_->mutex_);
    RadosOsd* const osd = new RadosOsd;
    osd->connmgr_ = this;
    osd->conn_ = conn;
    ++conn->nrefs;
    osd->pool_name_ = pool_name;
    osd->force_syncio_ = options.force_syncio;
    osd->ioctx_ = ioctx;
    *result = osd;
  }
  return status;
}

Status RadosConnMgr::OpenEnv(  ///
    Env* base_env, Osd* osd, bool owns_osd, const RadosEnvOptions& options,
    Env** result) {
  RadosEnv* const env = new RadosEnv(base_env);
  env->rados_root_ = options.rados_root;
  env->wal_buf_size_ = 1 << 17;  // 128 kB
  env->owns_osd_ = owns_osd;
  env->ofs_ = new Ofs(osd);
  env->osd_ = osd;
  *result = env;
  return Status::OK();
}

}  // namespace rados
}  // namespace pdlfs
