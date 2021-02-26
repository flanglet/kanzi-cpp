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

#ifndef _DefaultInputBitStream_
#define _DefaultInputBitStream_

#include <istream>
#include "../InputBitStream.hpp"
#include "../InputStream.hpp"
#include "../Memory.hpp"


namespace kanzi {

   class DefaultInputBitStream : public InputBitStream
   {
   private:
       InputStream& _is;
       byte* _buffer;
       int _position; // index of current byte (consumed if bitIndex == -1)
       int _availBits; // bits not consumed in _current
       int64 _read;
       uint64 _current;
       bool _closed;
       int _maxPosition;
       uint _bufferSize;

       int readFromInputStream(uint count) THROW;

       void pullCurrent();

       void _close() THROW;


   public:
       // Returns 1 or 0
       int readBit() THROW;

       uint64 readBits(uint length) THROW;

       uint readBits(byte bits[], uint count) THROW;

       void close() THROW { _close(); }

       // Number of bits read
       uint64 read() const
       {
           return uint64(_read + (int64(_position) << 3) - int64(_availBits));
       }

       // Return false when the bitstream is closed or the End-Of-Stream has been reached
       bool hasMoreToRead();

       bool isClosed() const { return _closed; }

       DefaultInputBitStream(InputStream& is, uint bufferSize = 65536) THROW;

       ~DefaultInputBitStream();
   };

   // Returns 1 or 0
   inline int DefaultInputBitStream::readBit() THROW
   {
       if (_availBits == 0)
           pullCurrent(); // Triggers an exception if stream is closed

       _availBits--;
       return int(_current >> _availBits) & 1;
   }

   inline uint64 DefaultInputBitStream::readBits(uint count) THROW
   {
       if ((count == 0) || (count > 64))
           throw BitStreamException("Invalid bit count: " + to_string(count) + " (must be in [1..64])");

       if (int(count) <= _availBits) {
           // Enough spots available in 'current'
           _availBits -= count;
           return (_current >> _availBits) & (uint64(-1) >> (64 - count));
       }

       // Not enough spots available in 'current'
       count -= _availBits;
       const uint64 res = _current & ((uint64(1) << _availBits) - 1);
       pullCurrent();
       _availBits -= count;
       return (res << count) | (_current >> _availBits);
   }

   // Pull 64 bits of current value from buffer.
   inline void DefaultInputBitStream::pullCurrent()
   {
       if (_position + 7 > _maxPosition) {
           if (_position > _maxPosition)
               readFromInputStream(_bufferSize);

           if (_position + 7 > _maxPosition) {
               // End of stream: overshoot max position => adjust bit index
               uint shift = uint(_maxPosition - _position) * 8;
               _availBits = int(shift + 8);
               uint64 val = 0;

               while (_position <= _maxPosition) {
                   val |= ((uint64(_buffer[_position++]) & 0xFF) << shift);
                   shift -= 8;
               }

               _current = val;
               return;
           }
       }

       // Regular processing, buffer length is multiple of 8
       _current = LittleEndian::readLong64(&_buffer[_position]);
       _availBits = 64;
       _position += 8;
   }
}
#endif
