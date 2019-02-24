// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "plasma/malloc.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <string>
#include <unordered_map>
#include <vector>

#include "plasma/common.h"
#include "plasma/plasma.h"

extern "C" {
void* fake_mmap(size_t);
int fake_munmap(void*, int64_t);

#define MMAP(s) fake_mmap(s)
#define MUNMAP(a, s) fake_munmap(a, s)
#define DIRECT_MMAP(s) fake_mmap(s)
#define DIRECT_MUNMAP(a, s) fake_munmap(a, s)
#define USE_DL_PREFIX
#define HAVE_MORECORE 0
#define DEFAULT_MMAP_THRESHOLD MAX_SIZE_T
#define DEFAULT_GRANULARITY ((size_t)128U * 1024U)

#include "thirdparty/dlmalloc.c"  // NOLINT

#undef MMAP
#undef MUNMAP
#undef DIRECT_MMAP
#undef DIRECT_MUNMAP
#undef USE_DL_PREFIX
#undef HAVE_MORECORE
#undef DEFAULT_GRANULARITY
}

// dlmalloc.c defined DEBUG which will conflict with ARROW_LOG(DEBUG).
#ifdef DEBUG
#undef DEBUG
#endif

struct mmap_record {
  int fd;
  int64_t size;
};

namespace {

/// Hashtable that contains one entry per segment that we got from the OS
/// via mmap. Associates the address of that segment with its file descriptor
/// and size.
std::unordered_map<void*, mmap_record> mmap_records;

}  // namespace

constexpr int GRANULARITY_MULTIPLIER = 2;

static void* pointer_advance(void* p, ptrdiff_t n) { return (unsigned char*)p + n; }

static void* pointer_retreat(void* p, ptrdiff_t n) { return (unsigned char*)p - n; }

static ptrdiff_t pointer_distance(void const* pfrom, void const* pto) {
  return (unsigned char const*)pto - (unsigned char const*)pfrom;
}

// Create a buffer. This is creating a temporary file and then
// immediately unlinking it so we do not leave traces in the system.
int create_buffer(int64_t size) {
  int fd;
  std::string file_template = plasma::plasma_config->directory;
#ifdef _WIN32
  if (!CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                         (DWORD)((uint64_t)size >> (CHAR_BIT * sizeof(DWORD))),
                         (DWORD)(uint64_t)size, NULL)) {
    fd = -1;
  }
#else
  file_template += "/plasmaXXXXXX";
  std::vector<char> file_name(file_template.begin(), file_template.end());
  file_name.push_back('\0');
  fd = mkstemp(&file_name[0]);
  if (fd < 0) {
    ARROW_LOG(FATAL) << "create_buffer failed to open file " << &file_name[0];
    return -1;
  }

  FILE* file = fdopen(fd, "a+");
  if (!file) {
    close(fd);
    ARROW_LOG(FATAL) << "create_buffer: fdopen failed for " << &file_name[0];
    return -1;
  }
  // Immediately unlink the file so we do not leave traces in the system.
  if (unlink(&file_name[0]) != 0) {
    ARROW_LOG(FATAL) << "failed to unlink file " << &file_name[0];
    return -1;
  }
  if (!plasma::plasma_config->hugepages_enabled) {
    // Increase the size of the file to the desired size. This seems not to be
    // needed for files that are backed by the huge page fs, see also
    // http://www.mail-archive.com/kvm-devel@lists.sourceforge.net/msg14737.html
    if (ftruncate(fd, (off_t)size) != 0) {
      ARROW_LOG(FATAL) << "failed to ftruncate file " << &file_name[0];
      return -1;
    }
  }
  int ret = dup(fd);
  if (ret < 0) {
    ARROW_LOG(FATAL) << "failed to dup the descriptor";
  } else {
    fclose(file);
    fd = ret;
  }
#endif
  return fd;
}

void* fake_mmap(size_t size) {
  // Add kMmapRegionsGap so that the returned pointer is deliberately not
  // page-aligned. This ensures that the segments of memory returned by
  // fake_mmap are never contiguous.
  size += kMmapRegionsGap;

  int fd = create_buffer(size);
  ARROW_CHECK(fd >= 0) << "Failed to create buffer during mmap";
  // MAP_POPULATE can be used to pre-populate the page tables for this memory region
  // which avoids work when accessing the pages later. However it causes long pauses
  // when mmapping the files. Only supported on Linux.
  void* pointer = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (pointer == MAP_FAILED) {
    ARROW_LOG(ERROR) << "mmap failed with error: " << std::strerror(errno);
    if (errno == ENOMEM && plasma::plasma_config->hugepages_enabled) {
      ARROW_LOG(ERROR)
          << "  (this probably means you have to increase /proc/sys/vm/nr_hugepages)";
    }
    return pointer;
  }

  // Increase dlmalloc's allocation granularity directly.
  mparams.granularity *= GRANULARITY_MULTIPLIER;

  mmap_record& record = mmap_records[pointer];
  record.fd = fd;
  record.size = size;

  // We lie to dlmalloc about where mapped memory actually lives.
  pointer = pointer_advance(pointer, kMmapRegionsGap);
  ARROW_LOG(DEBUG) << pointer << " = fake_mmap(" << size << ")";
  return pointer;
}

int fake_munmap(void* addr, int64_t size) {
  ARROW_LOG(DEBUG) << "fake_munmap(" << addr << ", " << size << ")";
  addr = pointer_retreat(addr, kMmapRegionsGap);
  size += kMmapRegionsGap;

  auto entry = mmap_records.find(addr);

  if (entry == mmap_records.end() || entry->second.size != size) {
    // Reject requests to munmap that don't directly match previous
    // calls to mmap, to prevent dlmalloc from trimming.
    return -1;
  }

  int r = munmap(addr, size);
  if (r == 0) {
    close(entry->second.fd);
  }

  mmap_records.erase(entry);
  return r;
}

void GetMallocMapinfo(void* addr, int* fd, int64_t* map_size, ptrdiff_t* offset) {
  // TODO(rshin): Implement a more efficient search through mmap_records.
  for (const auto& entry : mmap_records) {
    if (addr >= entry.first && addr < pointer_advance(entry.first, entry.second.size)) {
      *fd = entry.second.fd;
      *map_size = entry.second.size;
      *offset = pointer_distance(entry.first, addr);
      return;
    }
  }
  *fd = -1;
  *map_size = 0;
  *offset = 0;
}

int64_t GetMmapSize(int fd) {
  for (const auto& entry : mmap_records) {
    if (entry.second.fd == fd) {
      return entry.second.size;
    }
  }
  ARROW_LOG(FATAL) << "failed to find entry in mmap_records for fd " << fd;
  return -1;  // This code is never reached.
}

void SetMallocGranularity(int value) { change_mparam(M_GRANULARITY, value); }
