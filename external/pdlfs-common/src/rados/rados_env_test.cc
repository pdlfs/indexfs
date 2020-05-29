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
#include "rados_env.h"

#include "pdlfs-common/testharness.h"
#include "pdlfs-common/testutil.h"

#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <vector>

// Parameters for opening ceph.
namespace {
const char* FLAGS_user_name = "client.admin";
const char* FLAGS_rados_cluster_name = "ceph";
const char* FLAGS_pool_name = "test";
const char* FLAGS_conf = NULL;  // Use ceph defaults
}  // namespace

namespace pdlfs {
namespace rados {

class RadosEnvTest {
 public:
  RadosEnvTest() {
    working_dir_ = "/tmp/testdir1/testdir2";
    RadosConnMgrOptions options;
    mgr_ = new RadosConnMgr(options);
    env_ = NULL;
  }

  void Open() {
    RadosConn* conn;
    ASSERT_OK(mgr_->OpenConn(FLAGS_rados_cluster_name, FLAGS_user_name,
                             FLAGS_conf, RadosConnOptions(), &conn));
    Osd* osd;
    ASSERT_OK(mgr_->OpenOsd(conn, FLAGS_pool_name, RadosOptions(), &osd));
    env_ = RadosConnMgr::OpenEnv(Env::Default(), osd, true, RadosEnvOptions());
    mgr_->Release(conn);
    env_->CreateDir(working_dir_.c_str());
  }

  ~RadosEnvTest() {
    env_->DeleteDir(working_dir_.c_str());
    delete env_;
    delete mgr_;
  }

  std::string working_dir_;
  RadosConnMgr* mgr_;
  Env* env_;
};

namespace {
void UseFile(Env* env, const char* dirname, const char* fname) {
  std::string rnddatastor;
  Random rnd(test::RandomSeed());
  for (int i = 0; i < 3; i++) {
    Slice rnddata = test::RandomString(&rnd, 16, &rnddatastor);
    env->DeleteFile(fname);
    ASSERT_OK(WriteStringToFile(env, rnddata, fname));
    ASSERT_TRUE(env->FileExists(fname));
    std::string tmp;
    ASSERT_OK(ReadFileToString(env, fname, &tmp));
    ASSERT_EQ(Slice(tmp), rnddata);
    std::vector<std::string> names;
    ASSERT_OK(env->GetChildren(dirname, &names));
    std::string name(fname + strlen(dirname) + 1);
    bool in = (std::find(names.begin(), names.end(), name) != names.end());
    ASSERT_TRUE(in);
  }

  env->DeleteFile(fname);
}
}  // namespace

TEST(RadosEnvTest, FileLock) {
  Open();
  FileLock* lock;
  std::string fname = LockFileName(working_dir_);
  ASSERT_OK(env_->LockFile(fname.c_str(), &lock));
  ASSERT_OK(env_->UnlockFile(lock));
  ASSERT_OK(env_->DeleteFile(fname.c_str()));
}

TEST(RadosEnvTest, CurrentFile) {
  Open();
  ASSERT_OK(SetCurrentFile(env_, working_dir_, 1));
  std::string fname = CurrentFileName(working_dir_);
  ASSERT_TRUE(env_->FileExists(fname.c_str()));
  ASSERT_OK(env_->DeleteFile(fname.c_str()));
}

TEST(RadosEnvTest, ReadWriteFiles) {
  Open();
  std::vector<std::string> fnames;
  fnames.push_back(DescriptorFileName(working_dir_, 1));
  fnames.push_back(LogFileName(working_dir_, 2));
  fnames.push_back(TableFileName(working_dir_, 3));
  fnames.push_back(SSTTableFileName(working_dir_, 4));
  fnames.push_back(TempFileName(working_dir_, 5));
  fnames.push_back(InfoLogFileName(working_dir_));
  fnames.push_back(OldInfoLogFileName(working_dir_));
  for (size_t i = 0; i < fnames.size(); i++) {
    UseFile(env_, working_dir_.c_str(), fnames[i].c_str());
  }
}

namespace {
// Reload the working dir. Check the existence of a specified file under the
// next context.
void Reload(Env* env, const std::string& dir, const char* fname) {
  ASSERT_OK(env->DetachDir(dir.c_str()));
  ASSERT_OK(env->CreateDir(dir.c_str()));
  ASSERT_TRUE(env->FileExists(fname));
}

// Reload the working dir readonly (cannot edit the directory by adding or
// deleting files). Check the existence of a specified file under the next
// context.
void ReloadReadonly(Env* env, const std::string& dir, const char* fname) {
  ASSERT_OK(env->DetachDir(dir.c_str()));
  ASSERT_OK(env->AttachDir(dir.c_str()));
  ASSERT_TRUE(env->FileExists(fname));
}
}  // namespace

TEST(RadosEnvTest, Reloading) {
  std::string fname = TableFileName(working_dir_, 7);
  for (int i = 0; i < 3; i++) {
    WriteStringToFile(env_, "xxxxxxxxx", fname.c_str());
    ReloadReadonly(env_, working_dir_, fname.c_str());
    Reload(env_, working_dir_, fname.c_str());
  }

  ASSERT_OK(env_->DeleteFile(fname.c_str()));
}

}  // namespace rados
}  // namespace pdlfs

namespace {
inline void PrintUsage() {
  fprintf(stderr, "Use --cluster, --user, --conf, and --pool to conf test.\n");
  exit(1);
}

void ParseArgs(int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    ::pdlfs::Slice a = argv[i];
    if (a.starts_with("--cluster=")) {
      FLAGS_rados_cluster_name = argv[i] + strlen("--cluster=");
    } else if (a.starts_with("--user=")) {
      FLAGS_user_name = argv[i] + strlen("--user=");
    } else if (a.starts_with("--conf=")) {
      FLAGS_conf = argv[i] + strlen("--conf=");
    } else if (a.starts_with("--pool=")) {
      FLAGS_pool_name = argv[i] + strlen("--pool=");
    } else {
      PrintUsage();
    }
  }

  printf("Cluster name: %s\n", FLAGS_rados_cluster_name);
  printf("User name: %s\n", FLAGS_user_name);
  printf("Storage pool: %s\n", FLAGS_pool_name);
  printf("Conf: %s\n", FLAGS_conf);
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc > 1) {
    ParseArgs(argc, argv);
    return ::pdlfs::test::RunAllTests(&argc, &argv);
  } else {
    return 0;
  }
}
