/*
Copyright 2011-2017 Frederic Langlet
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

#ifndef _DefaultOutputBitStream_
#define _DefaultOutputBitStream_

#include "../OutputStream.hpp"
#include "../OutputBitStream.hpp"
#include "../Memory.hpp"


namespace kanzi 
{

   class DefaultOutputBitStream : public OutputBitStream
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

       void pushCurrent() THROW;

       void flush() THROW;

   public:
       DefaultOutputBitStream(OutputStream& os, uint bufferSize=65536) THROW;

       ~DefaultOutputBitStream();

       void writeBit(int bit) THROW;

       uint writeBits(uint64 bits, uint length) THROW;
       
       uint writeBits(const byte bits[], uint length) THROW;

       void close() THROW;

       // Return number of bits written so far
       uint64 written() const
       {
           // Number of bits flushed + bytes written in memory + bits written in memory
           return uint64(_written + (int64(_position) << 3) + int64(64 - _availBits));
       }

       bool isClosed() const { return _closed; }
   };

   // Write least significant bit of the input integer. Trigger exception if stream is closed
   inline void DefaultOutputBitStream::writeBit(int bit) THROW
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
   inline uint DefaultOutputBitStream::writeBits(uint64 value, uint count) THROW
   {
       if (count > 64)
           throw BitStreamException("Invalid bit count: " + to_string(count) + " (must be in [1..64])");

       _current |= ((value << (64 - count)) >> (64 - _availBits));
       uint remaining = count;

       if (count >= _availBits) {
           // Not enough spots available in 'current'
           remaining -= _availBits;
           pushCurrent();

           if (remaining != 0)
               _current = value << (64 - remaining);
       }

       _availBits -= remaining;
       return count;
   }

   // Push 64 bits of current value into buffer.
   inline void DefaultOutputBitStream::pushCurrent() THROW
   {
       BigEndian::writeLong64(&_buffer[_position], _current);
       _availBits = 64;
       _current = 0;
       _position += 8;

       if (_position >= _bufferSize)
           flush();
   }
}
#endif
