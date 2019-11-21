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

#include "pdlfs-common/rpc.h"
#include "pdlfs-common/logging.h"
#include "pdlfs-common/mutexlock.h"
#include "pdlfs-common/pdlfs_config.h"
#include "pdlfs-common/port.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#if defined(PDLFS_MARGO_RPC)
#include "margo/margo_rpc.h"
#endif

#if defined(PDLFS_MERCURY_RPC)
#include "mercury/mercury_rpc.h"
#endif

namespace pdlfs {

RPCOptions::RPCOptions()
    : impl(rpc::kSocketRPC),
      mode(rpc::kServerClient),
      rpc_timeout(5000000),
      num_rpc_threads(1),
      extra_workers(NULL),
      addr_cache_size(128),
      env(NULL),
      fs(NULL) {}

Status RPC::status() const {  ///
  return Status::OK();
}

RPC::~RPC() {}

namespace {
class SocketRPC {
 public:
  // Server address is resolved immediately at the constructor,
  // but won't be opened until Start() is called.
  explicit SocketRPC(const RPCOptions& options);
  ~SocketRPC();
  class ThreadedLooper;
  class Client;
  class Addr;

  friend class RPCImpl;
  // Open a new socket in fd_, try binding it to addr_, and start background
  // progressing. Return OK on success, or a non-OK status on errors. The
  // returned status is also remembered in status_, preventing future
  // operations on errors.
  Status Start();
  // Stop background progressing.
  Status Stop();

 private:
  struct CallState {
    struct sockaddr_in addr;  // Location of the caller
    socklen_t addrlen;
    size_t msgsz;  // Payload size
    char msg[1];
  };
  void HandleIncomingCall(CallState* call);

  // No copying allowed
  void operator=(const SocketRPC& rpc);
  SocketRPC(const SocketRPC&);

  rpc::If* const if_;
  Env* const env_;
  Addr* addr_;
  port::Mutex mutex_;
  ThreadedLooper* looper_;
  Status status_;
  int fd_;
};

void SocketRPC::HandleIncomingCall(CallState* const call) {
  rpc::If::Message in, out;
  in.contents = Slice(call->msg, call->msgsz);
  if_->Call(in, out);
  int nbytes =
      sendto(fd_, out.contents.data(), out.contents.size(), 0,
             reinterpret_cast<struct sockaddr*>(&call->addr), call->addrlen);
  if (nbytes != out.contents.size()) {
    //
  }
}

class SocketRPC::Addr {
 public:
  explicit Addr(const RPCOptions& options) {
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
  }

  void SetPort(const char* p) {
    int port = -1;
    if (p && p[0]) port = atoi(p);
    if (port < 0) {
      // Have the OS pick up a port for us
      port = 0;
    }
    addr.sin_port = htons(port);
  }
  // Translate a human-readable address string into a binary socket address to
  // which we can bind or connect. Return OK on success, or a non-OK status
  // on errors.
  Status Resolv(const char* host, bool is_numeric);
  Status ResolvUri(const std::string& uri);
  const struct sockaddr_in* rep() const { return &addr; }
  struct sockaddr_in* rep() {
    return &addr;
  }

 private:
  struct sockaddr_in addr;

  // Copyable
};

Status SocketRPC::Addr::ResolvUri(const std::string& uri) {
  std::string host, port;
  // E.g.: uri = "ignored://127.0.0.1:22222", "127.0.0.1", ":22222"
  //                     |  |        |         |            |
  //                     |  |        |         |            |
  //                     a  b        c         b           b,c
  size_t a = uri.find("://");  // Ignore protocol definition
  size_t b = (a == std::string::npos) ? 0 : a + 3;
  size_t c = uri.find(':', b);
  if (c != std::string::npos) {
    host = uri.substr(b, c - b);
    port = uri.substr(c + 1);
  } else {
    host = uri.substr(b);
  }
  Status status = Resolv(host.c_str(), false);
  if (status.ok()) {
    SetPort(port.c_str());
  }
  return status;
}

Status SocketRPC::Addr::Resolv(const char* host, bool is_numeric) {
  // First, quickly handle empty strings and strings that are known to be
  // numeric like "127.0.0.1"
  if (!host || !host[0]) {
    addr.sin_addr.s_addr = INADDR_ANY;
  } else if (is_numeric) {
    in_addr_t in_addr = inet_addr(host);
    if (in_addr == INADDR_NONE) {
      return Status::InvalidArgument("ip addr", host);
    } else {
      addr.sin_addr.s_addr = in_addr;
    }
  } else {  // Likely lengthy name resolution inevitable...
    struct addrinfo *ai, hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = PF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;
    int ret = getaddrinfo(host, NULL, &hints, &ai);
    if (ret != 0) {
      return Status::IOError("getaddrinfo", gai_strerror(ret));
    } else {
      const struct sockaddr_in* const in =
          reinterpret_cast<struct sockaddr_in*>(ai->ai_addr);
      addr.sin_addr = in->sin_addr;
    }
  }

  return Status::OK();
}

class SocketRPC::ThreadedLooper {
 public:
  ThreadedLooper(SocketRPC* rpc, const RPCOptions& options)
      : num_threads_(options.num_rpc_threads),
        max_msgsz_(1432),
        rpc_(rpc),
        shutting_down_(NULL),
        bg_cv_(&rpc_->mutex_),
        bg_threads_(0),
        bg_id_(0) {}

  void Start() {
    rpc_->mutex_.AssertHeld();
    while (bg_threads_ < num_threads_) {
      rpc_->env_->StartThread(BGLoopWrapper, this);
      ++bg_threads_;
    }
  }

  void Stop() {
    rpc_->mutex_.AssertHeld();
    shutting_down_.Release_Store(this);
    while (bg_threads_) {
      bg_cv_.Wait();
    }
  }

  ~ThreadedLooper() {
    Stop();  // Release background threads
  }

 private:
  static void BGLoopWrapper(void* arg);
  void BGLoop();

  // Constant after construction
  int const num_threads_;  // Total num of progressing threads to create
  size_t const max_msgsz_;
  SocketRPC* const rpc_;

  // State below is protected by rpc_->mutex_
  port::AtomicPointer shutting_down_;
  port::CondVar bg_cv_;
  int bg_threads_;
  int bg_id_;
};

void SocketRPC::ThreadedLooper::BGLoopWrapper(void* arg) {
  ThreadedLooper* const lo = reinterpret_cast<ThreadedLooper*>(arg);
  lo->BGLoop();
}

void SocketRPC::ThreadedLooper::BGLoop() {
  SocketRPC* const r = rpc_;
  struct pollfd po;
  po.events = POLLIN;
  r->mutex_.Lock();
  int const myid = bg_id_++;
  po.fd = r->fd_;
  r->mutex_.Unlock();
  CallState* const call = static_cast<CallState*>(
      malloc(sizeof(struct CallState) - 1 + max_msgsz_));
  int err = 0;

  while (true) {
    if (shutting_down_.Acquire_Load() || err) {
      MutexLock ml(&r->mutex_);
      if (err && r->status_.ok()) {
        r->status_ = Status::IOError(strerror(err));
      }
      assert(bg_threads_ > 0);
      --bg_threads_;
      if (!bg_threads_) {
        bg_cv_.SignalAll();
      }
      break;
    }

    call->addrlen = sizeof(struct sockaddr_in);
    int nret = recvfrom(po.fd, call->msg, max_msgsz_, MSG_DONTWAIT,
                        reinterpret_cast<struct sockaddr*>(&call->addr),
                        &call->addrlen);
    if (nret > 0) {
      call->msgsz = nret;
      r->HandleIncomingCall(call);
      continue;
    } else if (nret == 0) {  // Is this really gonna happen?
      continue;
    } else if (errno == EWOULDBLOCK) {
      nret = poll(&po, 1, 200);
    }

    if (nret == -1) {
      err = errno;
    }
  }

  free(call);
}

class SocketRPC::Client : public rpc::If {
 public:
  explicit Client(const RPCOptions& options)
      : rpc_timeout_(options.rpc_timeout),
        max_msgsz_(1432),
        env_(options.env),
        fd_(-1) {}

  virtual ~Client() {
    if (fd_ != -1) {
      close(fd_);
    }
  }

  // REQUIRES: OpenAndConnect() has been successfully called.
  virtual Status Call(Message& in, Message& out) RPCNOEXCEPT;

  // Connect to a specified remote destination. This destination must not be a
  // wildcard address. Return OK on success, or a non-OK status on errors. The
  // returned status is also cached in status_, preventing future Call()
  // operations on errors.
  Status OpenAndConnect(const Addr& addr);

 private:
  // No copying allowed
  void operator=(const Client&);
  Client(const Client&);
  const uint64_t rpc_timeout_;  // In microseconds
  const size_t max_msgsz_;
  Env* const env_;
  Status status_;
  int fd_;
};

// We do a synchronous send, followed by one or more non-blocking receives
// so that we can easily check timeouts without waiting for the data
// indefinitely. We use a timed poll to check data availability.
Status SocketRPC::Client::Call(Message& in, Message& out) RPCNOEXCEPT {
  if (!status_.ok()) {
    return status_;
  }
  int nret = send(fd_, in.contents.data(), in.contents.size(), 0);
  if (nret != in.contents.size()) {
    status_ = Status::IOError("send", strerror(errno));
    return status_;
  }
  const uint64_t start = CurrentMicros();
  std::string& buf = out.extra_buf;
  buf.reserve(max_msgsz_);
  buf.resize(1);
  struct pollfd po;
  memset(&po, 0, sizeof(struct pollfd));
  po.events = POLLIN;
  po.fd = fd_;
  while (true) {
    nret = recv(fd_, &buf[0], max_msgsz_, MSG_DONTWAIT);
    if (nret > 0) {
      out.contents = Slice(&buf[0], nret);
      break;
    } else if (nret == 0) {  // Is this really possible though?
      out.contents = Slice();
      break;
    } else if (errno == EWOULDBLOCK) {
      // We wait for 0.2 second and therefore timeouts are only checked
      // roughly every that amount of time.
      nret = poll(&po, 1, 200);
    }

    // Either recv or poll may have returned errors
    if (nret == -1) {
      status_ = Status::IOError("recv or poll", strerror(errno));
      break;
    } else if (CurrentMicros() - start >= rpc_timeout_) {
      status_ = Status::Disconnected("timeout");
      break;
    }
  }

  return status_;
}

Status SocketRPC::Client::OpenAndConnect(const Addr& addr) {
  if ((fd_ = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
    status_ = Status::IOError(strerror(errno));
  } else {
    int ret = connect(fd_, reinterpret_cast<const struct sockaddr*>(addr.rep()),
                      sizeof(struct sockaddr_in));
    if (ret == -1) {
      status_ = Status::IOError(strerror(errno));
    }
  }
  return status_;
}

SocketRPC::SocketRPC(const RPCOptions& options)
    : if_(options.fs), env_(options.env), addr_(new Addr(options)), fd_(-1) {
  looper_ = new ThreadedLooper(this, options);
  status_ = addr_->ResolvUri(options.uri);
}

SocketRPC::~SocketRPC() {
  mutex_.Lock();  // Lock required for stopping bg progressing
  delete looper_;
  mutex_.Unlock();
  delete addr_;
  if (fd_ != -1) {
    close(fd_);
  }
}

Status SocketRPC::Start() {
  MutexLock ml(&mutex_);
  if (status_.ok() && fd_ == -1) {
    if ((fd_ = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
      status_ = Status::IOError(strerror(errno));
    } else {
      int ret =
          bind(fd_, reinterpret_cast<const struct sockaddr*>(addr_->rep()),
               sizeof(struct sockaddr_in));
      if (ret == -1) {
        status_ = Status::IOError(strerror(errno));
      }
    }
    if (status_.ok()) {
      looper_->Start();
    }
  }
  return status_;
}

Status SocketRPC::Stop() {
  MutexLock ml(&mutex_);
  looper_->Stop();
  return status_;
}

// A dummy structure for error propagation.
class Err : public rpc::If {
 public:
  explicit Err(const Status& err) : status_(err) {}

  virtual Status Call(Message& in, Message& out) RPCNOEXCEPT { return status_; }

 private:
  Status status_;

  // Copyable
};

class RPCImpl : public RPC {
 public:
  explicit RPCImpl(const RPCOptions& options) : options_(options), rpc_(NULL) {
    if (options_.mode == rpc::kServerClient) {
      rpc_ = new SocketRPC(options);
    }
  }

  virtual rpc::If* OpenStubFor(const std::string& uri) {
    SocketRPC::Addr addr(options_);
    Status status = addr.ResolvUri(uri);
    if (status.ok()) {
      SocketRPC::Client* cli = new SocketRPC::Client(options_);
      cli->OpenAndConnect(addr);
      return cli;
    } else {
      return new Err(status);
    }
  }

  virtual Status status() const {
    if (rpc_) {
      MutexLock ml(&rpc_->mutex_);
      return rpc_->status_;
    } else {
      return Status::OK();
    }
  }

  virtual Status Start() {
    if (rpc_) return rpc_->Start();
    return Status::OK();
  }

  virtual Status Stop() {
    if (rpc_) return rpc_->Stop();
    return Status::OK();
  }

  virtual ~RPCImpl() {  ///
    delete rpc_;
  }

 private:
  // No copying allowed
  void operator=(const RPCImpl& impl);
  RPCImpl(const RPCImpl&);

  const RPCOptions options_;
  SocketRPC* rpc_;
};

}  // namespace

RPCServer::~RPCServer() {
  std::vector<RPCInfo>::iterator it;
  for (it = rpcs_.begin(); it != rpcs_.end(); ++it) {
    delete it->rpc;
    delete it->pool;
  }
}

void RPCServer::AddChannel(const std::string& listening_uri, int workers) {
  RPCInfo info;
  RPCOptions options;
  options.env = env_;
  info.pool = ThreadPool::NewFixed(workers);
  options.extra_workers = info.pool;
  options.fs = fs_;
  options.uri = listening_uri;
  info.rpc = RPC::Open(options);
  rpcs_.push_back(info);
}

Status RPCServer::status() const {
  Status s;
  std::vector<RPCInfo>::const_iterator it;
  for (it = rpcs_.begin(); it != rpcs_.end(); ++it) {
    assert(it->rpc != NULL);
    s = it->rpc->status();
    if (!s.ok()) {
      break;
    }
  }
  return s;
}

Status RPCServer::Start() {
  Status s;
  std::vector<RPCInfo>::iterator it;
  for (it = rpcs_.begin(); it != rpcs_.end(); ++it) {
    assert(it->rpc != NULL);
    s = it->rpc->Start();
    if (!s.ok()) {
      break;
    }
  }
  return s;
}

Status RPCServer::Stop() {
  Status s;
  std::vector<RPCInfo>::iterator it;
  for (it = rpcs_.begin(); it != rpcs_.end(); ++it) {
    assert(it->rpc != NULL);
    s = it->rpc->Stop();
    if (!s.ok()) {
      break;
    }
  }
  return s;
}

namespace rpc {

If::~If() {}

namespace {
#if defined(PDLFS_MARGO_RPC)
class MargoRPCImpl : public RPC {
  MargoRPC* rpc_;

 public:
  virtual Status Start() { return rpc_->Start(); }
  virtual Status Stop() { return rpc_->Stop(); }

  virtual If* OpenClientFor(const std::string& addr) {
    return new MargoRPC::Client(rpc_, addr);
  }

  MargoRPCImpl(const RPCOptions& options) {
    rpc_ = new MargoRPC(options.mode == kServerClient, options);
    rpc_->Ref();
  }

  virtual ~MargoRPCImpl() { rpc_->Unref(); }
};
#endif
}  // namespace

namespace {
#if defined(PDLFS_MERCURY_RPC)
class MercuryRPCImpl : public RPC {
  MercuryRPC::LocalLooper* looper_;
  MercuryRPC* rpc_;

 public:
  virtual Status status() const { return rpc_->status(); }
  virtual Status Start() { return looper_->Start(); }
  virtual Status Stop() { return looper_->Stop(); }

  virtual If* OpenStubFor(const std::string& addr) {
    return new MercuryRPC::Client(rpc_, addr);
  }

  MercuryRPCImpl(const RPCOptions& options) {
    rpc_ = new MercuryRPC(options.mode == kServerClient, options);
    looper_ = new MercuryRPC::LocalLooper(rpc_, options);
    rpc_->Ref();
  }

  virtual ~MercuryRPCImpl() {
    rpc_->Unref();
    delete looper_;
  }
};
#endif
}  // namespace

}  // namespace rpc

RPC* RPC::Open(const RPCOptions& raw_options) {
  assert(raw_options.uri.size() != 0);
  assert(raw_options.mode != rpc::kServerClient || raw_options.fs != NULL);
  RPCOptions options(raw_options);
  if (options.env == NULL) {
    options.env = Env::Default();
  }
#if VERBOSE >= 1
  Verbose(__LOG_ARGS__, 1, "rpc.uri -> %s", options.uri.c_str());
  Verbose(__LOG_ARGS__, 1, "rpc.timeout -> %llu (microseconds)",
          (unsigned long long)options.rpc_timeout);
  Verbose(__LOG_ARGS__, 1, "rpc.num_io_threads -> %d", options.num_rpc_threads);
  Verbose(__LOG_ARGS__, 1, "rpc.extra_workers -> [%s]",
          options.extra_workers != NULL
              ? options.extra_workers->ToDebugString().c_str()
              : "NULL");
#endif
  RPC* rpc = NULL;
#if defined(PDLFS_MARGO_RPC)
  if (options.impl == kMargoRPC) {
    rpc = new rpc::MargoRPCImpl(options);
  }
#endif
#if defined(PDLFS_MERCURY_RPC)
  if (options.impl == rpc::kMercuryRPC) {
    rpc = new rpc::MercuryRPCImpl(options);
  }
#endif
  if (options.impl == rpc::kSocketRPC) {
    rpc = new RPCImpl(options);
  }
  if (rpc == NULL) {
#ifndef NDEBUG
    char msg[] = "The requested rpc impl is unavailable\n";
    fwrite(msg, 1, sizeof(msg), stderr);
    abort();
#else
    Error(__LOG_ARGS__, "No rpc implementation is available");
    exit(EXIT_FAILURE);
#endif
  } else {
    return rpc;
  }
}

}  // namespace pdlfs
