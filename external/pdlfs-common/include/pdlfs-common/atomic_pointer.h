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

/*
 * Copyright (c) 2011 The LevelDB Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found at https://github.com/google/leveldb.
 */
#pragma once

#if __cplusplus >= 201103L
#include <atomic>
#endif
#ifdef PDLFS_OS_WIN
#include <windows.h>
#endif
#ifdef PDLFS_OS_MACOSX
#include <libkern/OSAtomic.h>
#endif

#if defined(_M_X64) || defined(__x86_64__)
#define ARCH_CPU_X86_FAMILY 1
#elif defined(_M_IX86) || defined(__i386__) || defined(__i386)
#define ARCH_CPU_X86_FAMILY 1
#elif defined(__ARMEL__)
#define ARCH_CPU_ARM_FAMILY 1
#elif defined(__aarch64__)
#define ARCH_CPU_ARM64_FAMILY 1
#elif defined(__ppc__) || defined(__powerpc__) || defined(__powerpc64__)
#define ARCH_CPU_PPC_FAMILY 1
#endif

// AtomicPointer provides storage for a lock-free pointer.
// Platform-dependent implementation of AtomicPointer:
// - If the platform provides a cheap barrier, we use it with raw pointers
// - If <atomic> is present (on newer versions of gcc, it is), we use
//   a <atomic>-based AtomicPointer.  However we prefer the memory
//   barrier based version, because at least on a gcc 4.4 32-bit build
//   on linux, we have encountered a buggy <atomic> implementation.
//   Also, some <atomic> implementations are much slower than a memory-barrier
//   based implementation (~16ns for <atomic> based acquire-load vs. ~1ns for
//   a barrier based acquire-load).
// This code is based on atomicops-internals-* in Google's perftools:
// http://code.google.com/p/google-perftools/source/browse/#svn%2Ftrunk%2Fsrc%2Fbase
namespace pdlfs {
namespace port {

// Define MemoryBarrier() if available
// Windows on x86
#if defined(PDLFS_OS_WIN) && defined(COMPILER_MSVC) && \
    defined(ARCH_CPU_X86_FAMILY)
// windows.h already provides a MemoryBarrier(void) macro
// http://msdn.microsoft.com/en-us/library/ms684208(v=vs.85).aspx
#define PLATFORM_HAVE_MEMORY_BARRIER

// Mac OS
#elif defined(PDLFS_OS_MACOSX)
inline void MemoryBarrier() {
#if __cplusplus >= 201103L
  std::atomic_thread_fence(std::memory_order_seq_cst);
#else
  OSMemoryBarrier();
#endif
}
#define PLATFORM_HAVE_MEMORY_BARRIER

// Gcc on x86
#elif defined(ARCH_CPU_X86_FAMILY) && defined(__GNUC__)
inline void MemoryBarrier() {
  // See http://gcc.gnu.org/ml/gcc/2003-04/msg01180.html for a discussion on
  // this idiom. Also see http://en.wikipedia.org/wiki/Memory_ordering.
  __asm__ __volatile__("" : : : "memory");
}
#define PLATFORM_HAVE_MEMORY_BARRIER

// Sun Studio
#elif defined(ARCH_CPU_X86_FAMILY) && defined(__SUNPRO_CC)
inline void MemoryBarrier() {
  // See http://gcc.gnu.org/ml/gcc/2003-04/msg01180.html for a discussion on
  // this idiom. Also see http://en.wikipedia.org/wiki/Memory_ordering.
  asm volatile("" : : : "memory");
}
#define PLATFORM_HAVE_MEMORY_BARRIER

// ARM Linux
#elif defined(ARCH_CPU_ARM_FAMILY) && defined(__linux__)
using LinuxKernelMemoryBarrierFunc = void (*)(void);
// The Linux ARM kernel provides a highly optimized device-specific memory
// barrier function at a fixed memory address that is mapped in every
// user-level process.
//
// This beats using CPU-specific instructions which are, on single-core
// devices, un-necessary and very costly (e.g. ARMv7-A "dmb" takes more
// than 180ns on a Cortex-A8 like the one on a Nexus One). Benchmarking
// shows that the extra function call cost is completely negligible on
// multi-core devices.
//

inline void MemoryBarrier() { (*(LinuxKernelMemoryBarrierFunc)0xffff0fa0)(); }
#define PLATFORM_HAVE_MEMORY_BARRIER

// ARM64
#elif defined(ARCH_CPU_ARM64_FAMILY)
inline void MemoryBarrier() { asm volatile("dmb sy" : : : "memory"); }
#define PLATFORM_HAVE_MEMORY_BARRIER

// PPC
#elif defined(ARCH_CPU_PPC_FAMILY) && defined(__GNUC__)
inline void MemoryBarrier() {
  // TODO for some powerpc expert: is there a cheaper suitable variant?
  // Perhaps by having separate barriers for acquire and release ops.
  asm volatile("sync" : : : "memory");
}
#define PLATFORM_HAVE_MEMORY_BARRIER
#endif

// AtomicPointer built using platform-specific MemoryBarrier()
#if defined(PLATFORM_HAVE_MEMORY_BARRIER)
class AtomicPointer {
 private:
  void* rep_;

 public:
  AtomicPointer() {}
  explicit AtomicPointer(void* p) : rep_(p) {}

  inline void* NoBarrier_Load() const { return rep_; }
  inline void NoBarrier_Store(void* v) { rep_ = v; }

  inline void* Acquire_Load() const {
    void* result = rep_;
    MemoryBarrier();
    return result;
  }

  inline void Release_Store(void* v) {
    MemoryBarrier();
    rep_ = v;
  }
};

// AtomicPointer based on <cstdatomic>
#elif __cplusplus >= 201103L
class AtomicPointer {
 private:
  std::atomic<void*> rep_;

 public:
  AtomicPointer() {}
  explicit AtomicPointer(void* v) : rep_(v) {}

  inline void* Acquire_Load() const {
    return rep_.load(std::memory_order_acquire);
  }

  inline void Release_Store(void* v) {
    rep_.store(v, std::memory_order_release);
  }

  inline void* NoBarrier_Load() const {
    return rep_.load(std::memory_order_relaxed);
  }

  inline void NoBarrier_Store(void* v) {
    rep_.store(v, std::memory_order_relaxed);
  }
};

// Atomic pointer based on sparc memory barriers
#elif defined(__sparcv9) && defined(__GNUC__)
class AtomicPointer {
 private:
  void* rep_;

 public:
  AtomicPointer() {}

  explicit AtomicPointer(void* v) : rep_(v) {}

  inline void* Acquire_Load() const {
    void* val;
    __asm__ __volatile__(
        "ldx [%[rep_]], %[val] \n\t"
        "membar #LoadLoad|#LoadStore \n\t"
        : [ val ] "=r"(val)
        : [ rep_ ] "r"(&rep_)
        : "memory");
    return val;
  }

  inline void Release_Store(void* v) {
    __asm__ __volatile__(
        "membar #LoadStore|#StoreStore \n\t"
        "stx %[v], [%[rep_]] \n\t"
        :
        : [ rep_ ] "r"(&rep_), [ v ] "r"(v)
        : "memory");
  }

  inline void* NoBarrier_Load() const { return rep_; }

  inline void NoBarrier_Store(void* v) { rep_ = v; }
};

// Atomic pointer based on ia64 acq/rel
#elif defined(__ia64) && defined(__GNUC__)
class AtomicPointer {
 private:
  void* rep_;

 public:
  AtomicPointer() {}

  explicit AtomicPointer(void* v) : rep_(v) {}

  inline void* Acquire_Load() const {
    void* val;
    __asm__ __volatile__("ld8.acq %[val] = [%[rep_]] \n\t"
                         : [ val ] "=r"(val)
                         : [ rep_ ] "r"(&rep_)
                         : "memory");
    return val;
  }

  inline void Release_Store(void* v) {
    __asm__ __volatile__("st8.rel [%[rep_]] = %[v]  \n\t"
                         :
                         : [ rep_ ] "r"(&rep_), [ v ] "r"(v)
                         : "memory");
  }

  inline void* NoBarrier_Load() const { return rep_; }

  inline void NoBarrier_Store(void* v) { rep_ = v; }
};

// We have neither MemoryBarrier(), nor <atomic>
#else
#error Please implement AtomicPointer for this platform.
#endif

#undef PLATFORM_HAVE_MEMORY_BARRIER
#undef ARCH_CPU_X86_FAMILY
#undef ARCH_CPU_ARM_FAMILY
#undef ARCH_CPU_ARM64_FAMILY
#undef ARCH_CPU_PPC_FAMILY

}  // namespace port
}  // namespace pdlfs
