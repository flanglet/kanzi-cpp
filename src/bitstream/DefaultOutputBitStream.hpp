/*
Copyright 2011-2025 Frederic Langlet
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
#ifndef _DefaultOutputBitStream_
#define _DefaultOutputBitStream_

#include "../BitStreamException.hpp"
#include "../OutputStream.hpp"
#include "../OutputBitStream.hpp"
#include "../Memory.hpp"
#include "../Seekable.hpp"
#include "../util/strings.hpp"


namespace kanzi
{

#if defined(_MSC_VER) && _MSC_VER <= 1500
   class DefaultOutputBitStream FINAL : public OutputBitStream
#else
   class DefaultOutputBitStream FINAL : public OutputBitStream, public Seekable
#endif
   {
   private:
       OutputStream& _os;
       byte* _buffer;
       bool _closed;
       uint _bufferSize;
       uint _position; // index of current byte in buffer
       uint _availBits; // bits not consumed in _current
       int64 _written;
       uint64 _current; // cached bits

       void pushCurrent();

       void flush();

       void _close();

   public:
       DefaultOutputBitStream(OutputStream& os, uint bufferSize=65536);

       ~DefaultOutputBitStream();

       void writeBit(int bit);

       uint writeBits(uint64 bits, uint length);

       uint writeBits(const byte bits[], uint length);

       void close() { _close(); }

#if !defined(_MSC_VER) || _MSC_VER > 1500
       int64 tell();

       bool seek(int64 pos);
#endif

       // Return number of bits written so far
       uint64 written() const
       {
           // Number of bits flushed + bytes written in memory + bits written in memory
           return uint64(_written + (int64(_position) << 3) + int64(64 - _availBits));
       }

       bool isClosed() const { return _closed; }
   };

   // Write least significant bit of the input integer. Trigger exception if stream is closed
   inline void DefaultOutputBitStream::writeBit(int bit)
   {
       if (_availBits <= 1) { // _availBits = 0 if stream is closed => force pushCurrent()
           _current |= (uint64(bit) & 1);
           pushCurrent();
       }
       else {
           _availBits--;
           _current |= (uint64(bit & 1) << _availBits);
       }
   }

   // Write 'count' (in [1..64]) bits. Trigger exception if stream is closed
   inline uint DefaultOutputBitStream::writeBits(uint64 value, uint count)
   {
       if (count > 64)
           throw BitStreamException("Invalid bit count: " + TOSTR(count) + " (must be in [1..64])");
 
       _current |= ((value << (64 - count)) >> (64 - _availBits));

       if (count >= _availBits) {
           // Not enough spots available in 'current'
           const uint remaining = count - _availBits;
           pushCurrent();

           if (remaining != 0) {
               _availBits -= remaining;
               _current = value << _availBits;
           }
       }
       else {
           _availBits -= count;
       }

       return count;
   }

   // Push 64 bits of current value into buffer.
   inline void DefaultOutputBitStream::pushCurrent()
   {
       BigEndian::writeLong64(&_buffer[_position], _current);
       _availBits = 64;
       _current = 0;
       _position += 8;

       if (_position >= _bufferSize - 8)
           flush();
   }

#if !defined(_MSC_VER) || _MSC_VER > 1500
   inline int64 DefaultOutputBitStream::tell()
   {
       if (isClosed() == true)
           return -1;

       _os.clear();
       const int64 res = int64(_os.tellp());
       return (res < 0) ? -1 : 8 * res + (int64(_position) << 3) + int64(64 - _availBits);
   }

   // Only support a new position at the byte boundary (pos & 7 == 0)
   inline bool DefaultOutputBitStream::seek(int64 pos)
   {
       if (isClosed() == true)
           return false;

       if ((pos < 0) || ((pos & 7) != 0))
           return false;

       // Flush buffer
       // Round down to byte alignment
       const uint a = _availBits & -8;

       for (uint i = 56; i >= a; i -= 8) {
          _buffer[_position++] = byte(_current >> i);

          if (_position >= _bufferSize)
             flush();
       }

       _availBits = 64;
       flush();
       _os.clear();
       _os.seekp(std::streampos(pos >> 3));
       return true;
   }
#endif

}
#endif

