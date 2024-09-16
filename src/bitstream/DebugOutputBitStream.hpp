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
#ifndef _DebugOutputBitStream_
#define _DebugOutputBitStream_

#include "../OutputBitStream.hpp"
#include "../OutputStream.hpp"
#include "../Seekable.hpp"

namespace kanzi
{

#if defined(WIN32) || defined(_WIN32)
   class DebugOutputBitStream FINAL : public OutputBitStream
#else
   class DebugOutputBitStream FINAL : public OutputBitStream, public Seekable
#endif
   {
   private:
       OutputBitStream& _delegate;
       OutputStream& _out;
       int _width;
       int _idx;
       bool _mark;
       bool _show;
       bool _hexa;
       byte _current;

       void printByte(byte val);

       void _close() { _delegate.close(); }

   public:
       DebugOutputBitStream(OutputBitStream& obs);

       DebugOutputBitStream(OutputBitStream& obs, OutputStream& os);

       DebugOutputBitStream(OutputBitStream& obs, OutputStream& os, int width);

       ~DebugOutputBitStream();

       void writeBit(int bit);

       uint writeBits(uint64 bits, uint length);

       uint writeBits(const byte bits[], uint length);

       // Return number of bits written so far
       uint64 written() const { return _delegate.written(); }

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

#if !defined(WIN32) && !defined(_WIN32)                                                                                                                // Return a position at the byte boundary
   inline int64 DebugOutputBitStream::tell()
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
   inline bool DebugOutputBitStream::seek(int64)
   {
       throw std::ios_base::failure("Not supported");
#else
   inline bool DebugOutputBitStream::seek(int64 pos)
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

