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
#include "posix_net.h"

#include "pdlfs-common/port.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

namespace pdlfs {

// Return errors as status objects.
extern Status PosixError(const Slice& err_context, int err_number);

std::string PosixSocketAddr::GetUri() const {
  char host[INET_ADDRSTRLEN];
  char tmp[50];
  snprintf(tmp, sizeof(tmp), "%s:%d",
           inet_ntop(AF_INET, &addr_.sin_addr, host, sizeof(host)),
           ntohs(addr_.sin_port));
  return tmp;
}

void PosixSocketAddr::Reset() {
  memset(&addr_, 0, sizeof(struct sockaddr_in));
  addr_.sin_family = AF_INET;
}

Status PosixSocketAddr::ResolvUri(const std::string& uri) {
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

  Status status;
  if (host.empty()) {
    addr_.sin_addr.s_addr = INADDR_ANY;
  } else {
    int h1, h2, h3, h4;
    char junk;
    const bool is_numeric =
        sscanf(host.c_str(), "%d.%d.%d.%d%c", &h1, &h2, &h3, &h4, &junk) == 4;
    status = Resolv(host.c_str(), is_numeric);
  }
  if (status.ok()) {
    SetPort(port.c_str());
  }
  return status;
}

Status PosixSocketAddr::Resolv(const char* host, bool is_numeric) {
  struct addrinfo *ai, hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  if (is_numeric) {
    hints.ai_flags = AI_NUMERICHOST;
  }
  int rv = getaddrinfo(host, NULL, &hints, &ai);
  if (rv != 0) {
    return Status::IOError("getaddrinfo", gai_strerror(rv));
  }
  const struct sockaddr_in* const in =
      reinterpret_cast<struct sockaddr_in*>(ai->ai_addr);
  addr_.sin_addr = in->sin_addr;
  freeaddrinfo(ai);
  return Status::OK();
}

Status PosixIf::IfConf(std::vector<Ifr>* results) {
  Ifr ifr;

  results->clear();
  for (size_t i = 0; i < ifconf_.ifc_len / sizeof(ifr_[0]); ++i) {
    struct sockaddr_in* s_in =
        reinterpret_cast<sockaddr_in*>(&ifr_[i].ifr_addr);

    const char* ip_addr =
        inet_ntop(AF_INET, &s_in->sin_addr, ifr.ip, INET_ADDRSTRLEN);
    // Skip if addr is unresolvable
    if (ip_addr != NULL) {
      strncpy(ifr.name, ifr_[i].ifr_name, sizeof(ifr.name));
      ifr.mtu = ifr_[i].ifr_mtu;

      results->push_back(ifr);
    }
  }

  return Status::OK();
}

Status PosixIf::Open() {
  Status s;
  fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);  // Ignore ipv6
  if (fd_ == -1) {
    s = PosixError("socket", errno);
    return s;
  }

  ifconf_.ifc_len = sizeof(ifr_);
  int r = ioctl(fd_, SIOCGIFCONF, &ifconf_);
  if (r == -1) {
    s = PosixError("ioctl", errno);
    ifconf_.ifc_len = 0;
  }

  return s;
}

// Return all network addresses associated with us.
Status FetchHostIPAddrs(std::vector<std::string>* ips) {
  PosixIf sock;
  std::vector<Ifr> results;
  Status s = sock.Open();
  if (s.ok()) {
    sock.IfConf(&results);
    for (size_t i = 0; i < results.size(); i++) {
      ips->push_back(results[i].ip);
    }
  }
  return s;
}

// Return name of the machine.
Status FetchHostname(std::string* hostname) {
  char buf[HOST_NAME_MAX];
  if (gethostname(buf, sizeof(buf)) == -1) {
    return PosixError("gethostname", errno);
  } else {
    *hostname = buf;
    return Status::OK();
  }
}

}  // namespace pdlfs
