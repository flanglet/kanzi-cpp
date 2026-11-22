/*
Copyright 2011-2026 Frederic Langlet
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
you may obtain a copy of the License at

                http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#pragma once
#ifndef knz_ScratchAllocator
#define knz_ScratchAllocator

#include <cstddef>
#include "types.hpp"

namespace kanzi
{
    // Interface for custom scratch-buffer memory management across streams,
    // transforms, and entropy codecs.
    // 
    // Thread safety:
    //   A single ScratchAllocator shared through a stream or Context may be called
    //   concurrently from multiple worker threads, and storage allocated on one   
    //   thread may be passed to release() on a different thread (e.g. during   
    //   stream buffer handoff, close(), or destructor teardown). Implementations
    //   intended for concurrent streams (-j > 1) MUST be thread-safe.           
    // 
    // Lifetime:
    //   The allocator instance MUST outlive every stream, Context, and worker task
    //   that uses it (including until stream destruction / task drain completes). 
    class ScratchAllocator
    {
    public:
        virtual ~ScratchAllocator() {}

        // Return uninitialized storage for at least `size` bytes, or throw
        // std::bad_alloc on failure. Must never return nullptr when `size > 0`.
        // 
        // Alignment:
        //   When `size > 0`, the returned pointer MUST be aligned to at least
        //   8 bytes (safe for int32/int64/uint64 access; 16- or 64-byte alignment
        //   is recommended for cache-line and SIMD efficiency).
        // 
        // Zero-size requests:
        //   If `size == 0`, implementations may return either nullptr or a valid
        //   zero-byte token that is safe to pass to release(ptr, 0).
        // 
        // Note:
        //   Storage contents are uninitialized. Callers must explicitly zero or
        //   placement-construct objects (note kanzi::byte is std::byte in C++17+
        //   and uint8_t in C++98).
        virtual byte* allocate(std::size_t size) = 0;
  
        // Return storage previously obtained from this exact allocator instance.
        // 
        // Parameters:
        //   - `data`: pointer previously returned by allocate(size), or nullptr
        //     (in which case release() MUST be a safe no-op).
        //   - `size`: MUST equal the exact `size` originally passed to allocate()
        //     (not an over-allocated / pooled capacity).
        // 
        // Exception safety:
        //   Must not throw (may be invoked from destructors and unwind paths).
        virtual void release(byte* data, std::size_t size) = 0;
    };
}
  
#endif

