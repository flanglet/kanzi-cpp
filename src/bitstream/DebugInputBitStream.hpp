/*
Copyright 2011-2024 Frederic Langlet
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
#ifndef _DebugInputBitStream_
#define _DebugInputBitStream_

#include "../InputBitStream.hpp"
#include "../OutputStream.hpp"
#include "../Seekable.hpp"


namespace kanzi {

#if defined(WIN32) || defined(_WIN32)
   class DebugInputBitStream FINAL : public InputBitStream
#else
   class DebugInputBitStream FINAL : public InputBitStream, public Seekable
#endif
   {
   private:
       InputBitStream& _delegate;
       OutputStream& _out;
       int _width;
       int _idx;
       bool _mark;
       bool _hexa;
       bool _show;
       byte _current;

       void printByte(byte val);

       void _close() { _delegate.close(); }

   public:
       DebugInputBitStream(InputBitStream& ibs);

       DebugInputBitStream(InputBitStream& ibs, OutputStream& os);

       DebugInputBitStream(InputBitStream& ibs, OutputStream& os, int width);

       ~DebugInputBitStream();

       // Returns 1 or 0
       int readBit();

       uint64 readBits(uint length);

       uint readBits(byte bits[], uint length);

       // Number of bits read
       uint64 read() const { return _delegate.read(); }

       // Return false when the bitstream is closed or the End-Of-Stream has been reached
       bool hasMoreToRead() { return _delegate.hasMoreToRead(); }

       void close() { _close(); }

       void showByte(bool show) { _show = show; }

       void setHexa(bool hexa) { _hexa = hexa; }

       bool hexa() const { return _hexa; }

       bool showByte() const { return _show; }

       void setMark(bool mark) { _mark = mark; }

       bool mark() const { return _mark; }

#if !defined(WIN32) && !defined(_WIN32)
       int64 tell();

       bool seek(int64 pos);
#endif
   };

#if !defined(WIN32) && !defined(_WIN32)
   // Return a position at the byte boundary
   inline int64 DebugInputBitStream::tell()
   {
#ifdef NO_RTTI
       throw std::ios_base::failure("Not supported");
#else     
       Seekable* seekable = dynamic_cast<Seekable*>(&_delegate);

       if (seekable == nullptr)
           throw std::ios_base::failure("The stream is not seekable");

       return seekable->tell();
#endif
   }

   // Only support a new position at the byte boundary (pos & 7 == 0)
#ifdef NO_RTTI
   inline bool DebugInputBitStream::seek(int64)
   {
       throw std::ios_base::failure("Not supported");
#else     
   inline bool DebugInputBitStream::seek(int64 pos)
   {
       Seekable* seekable = dynamic_cast<Seekable*>(&_delegate);

       if (seekable == nullptr)
           throw std::ios_base::failure("The stream is not seekable");

       return seekable->seek(pos);
#endif
    }
#endif
}
#endif

