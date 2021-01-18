/*
Copyright 2011-2021 Frederic Langlet
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

#ifndef _CompressedInputStream_
#define _CompressedInputStream_

#include <string>
#include <vector>
#include "../concurrent.hpp"
#include "../Context.hpp"
#include "../Listener.hpp"
#include "../InputStream.hpp"
#include "../OutputStream.hpp"
#include "../InputBitStream.hpp"
#include "../SliceArray.hpp"
#include "../util/XXHash32.hpp"

#if __cplusplus >= 201103L
   #include <functional>
#endif

using namespace std;

namespace kanzi
{

   class DecodingTaskResult {
   public:
       int _blockId;
       int _decoded;
       byte* _data;
       int _error; // 0 = OK
       std::string _msg;
       int _checksum;
       bool _skipped;
       clock_t _completionTime;

       DecodingTaskResult()
           : _blockId(-1)
           , _msg()
           , _completionTime(clock())
       {
          _data = nullptr;
          _decoded = 0;
          _error = 0;
          _checksum = 0;
          _skipped = false;
       }

       DecodingTaskResult(SliceArray<byte>& data, int blockId, int decoded, int checksum,
          int error, const std::string& msg, bool skipped = false)
           : _msg(msg)
           , _completionTime(clock())
       {
           _data = data._array;
           _blockId = blockId;
           _error = error;
           _decoded = decoded;
           _checksum = checksum;
           _skipped = skipped;
       }

       DecodingTaskResult(const DecodingTaskResult& result)
           : _msg(result._msg)
           , _completionTime(result._completionTime)
       {
           _data = result._data;
           _blockId = result._blockId;
           _error = result._error;
           _decoded = result._decoded;
           _checksum = result._checksum;
           _skipped = result._skipped;
       }

       DecodingTaskResult& operator = (const DecodingTaskResult& result)
       {
           _msg = result._msg;
           _data = result._data;
           _blockId = result._blockId;
           _error = result._error;
           _decoded = result._decoded;
           _checksum = result._checksum;
           _completionTime = result._completionTime;
           _skipped = result._skipped;
           return *this;
       }

       ~DecodingTaskResult() {}
   };

   // A task used to decode a block
   // Several tasks may run in parallel. The transforms can be computed concurrently
   // but the entropy decoding is sequential since all tasks share the same bitstream.
   template <class T>
   class DecodingTask : public Task<T> {
   private:
       SliceArray<byte>* _data;
       SliceArray<byte>* _buffer;
       int _blockLength;
       uint64 _transformType;
       uint32 _entropyType;
       int _blockId;
       InputBitStream* _ibs;
       XXHash32* _hasher;
       atomic_int* _processedBlockId;
       std::vector<Listener*> _listeners;
       Context _ctx;

   public:
       DecodingTask(SliceArray<byte>* iBuffer, SliceArray<byte>* oBuffer, int blockSize,
           uint64 transformType, uint32 entropyType, int blockId,
           InputBitStream* ibs, XXHash32* hasher,
           atomic_int* processedBlockId, std::vector<Listener*>& listeners,
           Context& ctx);

       ~DecodingTask(){};

       T run() THROW;
   };

   class CompressedInputStream : public InputStream {
       friend class DecodingTask<DecodingTaskResult>;

   private:
       static const int BITSTREAM_TYPE = 0x4B414E5A; // "KANZ"
       static const int BITSTREAM_FORMAT_VERSION = 10;
       static const int DEFAULT_BUFFER_SIZE = 256 * 1024;
       static const int EXTRA_BUFFER_SIZE = 256;
       static const byte COPY_BLOCK_MASK = byte(0x80);
       static const byte TRANSFORMS_MASK = byte(0x10);
       static const int MIN_BITSTREAM_BLOCK_SIZE = 1024;
       static const int MAX_BITSTREAM_BLOCK_SIZE = 1024 * 1024 * 1024;
       static const int CANCEL_TASKS_ID = -1;
       static const int MAX_CONCURRENCY = 64;
       static const int MAX_BLOCK_ID = int((uint(1) << 31) - 1);

       int _blockSize;
       uint8 _nbInputBlocks;
       XXHash32* _hasher;
       SliceArray<byte>* _sa; // for all blocks
       SliceArray<byte>** _buffers; // per block
       uint32 _entropyType;
       uint64 _transformType;
       InputBitStream* _ibs;
       InputStream& _is;
       atomic_bool _initialized;
       atomic_bool _closed;
       atomic_int _blockId;
       int _maxIdx;
       int _jobs;
       std::vector<Listener*> _listeners;
       std::streamsize _gcount;
       Context _ctx;

       void readHeader() THROW;

       int processBlock() THROW;

       int _get();

       static void notifyListeners(std::vector<Listener*>& listeners, const Event& evt);

   public:
       CompressedInputStream(InputStream& is, int jobs);

#if __cplusplus >= 201103L
       CompressedInputStream(InputStream& is, Context& ctx,
          std::function<InputBitStream*(InputStream&)>* createBitStream=nullptr);
#else
       CompressedInputStream(InputStream& is, Context& ctx);
#endif

       ~CompressedInputStream();

       bool addListener(Listener& bl);

       bool removeListener(Listener& bl);

       std::streampos tellg();

       std::istream& seekp(streampos pos) THROW;

       std::istream& read(char* s, std::streamsize n) THROW;

       int get() THROW;

       int peek() THROW;

       std::streamsize gcount() const { return _gcount; }

       void close() THROW;

       uint64 getRead();
   };
}
#endif