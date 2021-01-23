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

#ifndef _BlockCompressor_
#define _BlockCompressor_

#include <map>
#include <vector>

#include "../concurrent.hpp"
#include "../Context.hpp"
#include "../InputStream.hpp"
#include "../Listener.hpp"
#include "../io/CompressedOutputStream.hpp"

namespace kanzi {

   class FileCompressResult {
   public:
       int _code;
       uint64 _read;
       uint64 _written;
       std::string _errMsg;

       FileCompressResult():
           _errMsg("")
       {
           _code = 0;
           _read = 0;
           _written = 0;
       }

       FileCompressResult(int code, uint64 read, uint64 written, const std::string& errMsg) :
           _errMsg(errMsg)
       {
           _code = code;
           _read = read;
           _written = written;
       }

       ~FileCompressResult() {}
   };

#ifdef CONCURRENCY_ENABLED
   template <class T, class R>
   class FileCompressWorker : public Task<R> {
   public:
       FileCompressWorker(BoundedConcurrentQueue<T>* queue) { _queue = queue; }

       ~FileCompressWorker() {}

       R run();

   private:
       BoundedConcurrentQueue<T>* _queue;
   };
#endif

   template <class T>
   class FileCompressTask : public Task<T> {
   public:
       static const int DEFAULT_BUFFER_SIZE = 65536;

       FileCompressTask(Context& ctx, std::vector<Listener*>& listeners);

       ~FileCompressTask();

       T run();

       void dispose();

   private:
       Context _ctx;
       InputStream* _is;
       CompressedOutputStream* _cos;
       std::vector<Listener*> _listeners;
   };

   class BlockCompressor {
       friend class FileCompressTask<FileCompressResult>;

   public:
       BlockCompressor(std::map<std::string, std::string>& m) THROW;

       ~BlockCompressor();

       int compress(uint64& written);

       bool addListener(Listener& bl);

       bool removeListener(Listener& bl);

       void dispose();

   private:
       static const int DEFAULT_BLOCK_SIZE = 4 * 1024 * 1024;
       static const int MIN_BLOCK_SIZE = 1024;
       static const int MAX_BLOCK_SIZE = 1024 * 1024 * 1024;
       static const int DEFAULT_CONCURRENCY = 1;
       static const int MAX_CONCURRENCY = 64;

       int _verbosity;
       bool _overwrite;
       bool _checksum;
       bool _skipBlocks;
       std::string _inputName;
       std::string _outputName;
       std::string _codec;
       std::string _transform;
       int _blockSize;
       int _level; // command line compression level
       int _jobs;
       std::vector<Listener*> _listeners;

       static void notifyListeners(std::vector<Listener*>& listeners, const Event& evt);

       static void getTransformAndCodec(int level, std::string tranformAndCodec[2]);
   };
}
#endif
