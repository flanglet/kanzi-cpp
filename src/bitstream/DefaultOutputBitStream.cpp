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

#include "DefaultOutputBitStream.hpp"
#include "../IllegalArgumentException.hpp"
#include "../Memory.hpp"
#include "../io/IOException.hpp"
#include <fstream>

using namespace kanzi;

uint64 DefaultOutputBitStream::MASKS[65] = {
   0x0,
   0x1,
   0x3,
   0x7,
   0xF,
   0x1F,
   0x3F,
   0x7F,
   0xFF,
   0x1FF,
   0x3FF,
   0x7FF,
   0xFFF,
   0x1FFF,
   0x3FFF,
   0x7FFF,
   0xFFFF,
   0x1FFFF,
   0x3FFFF,
   0x7FFFF,
   0xFFFFF,
   0x1FFFFF,
   0x3FFFFF,
   0x7FFFFF,
   0xFFFFFF,
   0x1FFFFFF,
   0x3FFFFFF,
   0x7FFFFFF,
   0xFFFFFFF,
   0x1FFFFFFF,
   0x3FFFFFFF,
   0x7FFFFFFF,
   0xFFFFFFFF,
   0x1FFFFFFFF,
   0x3FFFFFFFF,
   0x7FFFFFFFF,
   0xFFFFFFFFF,
   0x1FFFFFFFFF,
   0x3FFFFFFFFF,
   0x7FFFFFFFFF,
   0xFFFFFFFFFF,
   0x1FFFFFFFFFF,
   0x3FFFFFFFFFF,
   0x7FFFFFFFFFF,
   0xFFFFFFFFFFF,
   0x1FFFFFFFFFFF,
   0x3FFFFFFFFFFF,
   0x7FFFFFFFFFFF,
   0xFFFFFFFFFFFF,
   0x1FFFFFFFFFFFF,
   0x3FFFFFFFFFFFF,
   0x7FFFFFFFFFFFF,
   0xFFFFFFFFFFFFF,
   0x1FFFFFFFFFFFFF,
   0x3FFFFFFFFFFFFF,
   0x7FFFFFFFFFFFFF,
   0xFFFFFFFFFFFFFF,
   0x1FFFFFFFFFFFFFF,
   0x3FFFFFFFFFFFFFF,
   0x7FFFFFFFFFFFFFF,
   0xFFFFFFFFFFFFFFF,
   0x1FFFFFFFFFFFFFFF,
   0x3FFFFFFFFFFFFFFF,
   0x7FFFFFFFFFFFFFFF,
   0xFFFFFFFFFFFFFFFF,
};

DefaultOutputBitStream::DefaultOutputBitStream(OutputStream& os, uint bufferSize) THROW : _os(os)
{
    if (bufferSize < 1024)
        throw IllegalArgumentException("Invalid buffer size (must be at least 1024)");

    if (bufferSize > 1 << 29)
        throw IllegalArgumentException("Invalid buffer size (must be at most 536870912)");

    if ((bufferSize & 7) != 0)
        throw IllegalArgumentException("Invalid buffer size (must be a multiple of 8)");

    _availBits = 64;
    _bufferSize = bufferSize;
    _buffer = new byte[_bufferSize];
    _position = 0;
    _current = 0;
    _written = 0;
    _closed = false;
}

// Write least significant bit of the input integer. Trigger exception if stream is closed
inline void DefaultOutputBitStream::writeBit(int bit) THROW
{
    if (_availBits <= 1) // _availBits = 0 if stream is closed => force pushCurrent()
    {
        _current |= (bit & 1);
        pushCurrent();
    }
    else {
        _availBits--;
        _current |= (uint64(bit & 1) << _availBits);
    }
}

// Write 'count' (in [1..64]) bits. Trigger exception if stream is closed
int DefaultOutputBitStream::writeBits(uint64 value, uint count) THROW
{
    if (count > 64)
        throw BitStreamException("Invalid count: " + to_string(count) + " (must be in [1..64])");

    if (count < uint(_availBits)) {
        // Enough spots available in 'current'
        _current |= ((value & MASKS[count]) << (_availBits - count));
        _availBits -= count;
    }
    else {
        const uint remaining = count - _availBits;
        value &= MASKS[count];
        _current |= (value >> remaining);
        pushCurrent();

        if (remaining != 0) {
            _current = value << (64 - remaining);
            _availBits -= remaining;
        }
    }

    return count;
}

uint DefaultOutputBitStream::writeBits(byte bits[], uint count) THROW
{
    if (isClosed() == true)
        throw BitStreamException("Stream closed", BitStreamException::STREAM_CLOSED);

    int remaining = count;
    int start = 0;

    // Byte aligned cursor ?
    if ((_availBits & 7) == 0) {
        // Fill up _current
        while ((_availBits != 64) && (remaining >= 8)) {
            writeBits(uint64(bits[start]), 8);
            start++;
            remaining -= 8;
        }

        // Copy bits array to internal buffer
        while (uint(remaining >> 3) >= _bufferSize - _position) {
            memcpy(&_buffer[_position], &bits[start], _bufferSize - _position);
            start += (_bufferSize - _position);
            remaining -= ((_bufferSize - _position) << 3);
            _position = _bufferSize;
            flush();
        }

        const int r = (remaining >> 6) << 3;

        if (r > 0) {
            memcpy(&_buffer[_position], &bits[start], r);
            start += r;
            _position += r;
            remaining -= (r << 3);
        }
    }
    else {
        // Not byte aligned
        const int r = _availBits - 64;

        while (remaining >= 64) {
            const uint64 value = uint64(BigEndian::readLong64(&bits[start]));
            _current |= (value >> -r);
            pushCurrent();
            _current = (value << r);
            _availBits += r;
            start += 8;
            remaining -= 64;
        }
    }

    // Last bytes
    while (remaining >= 8) {
        writeBits(uint64(bits[start]), 8);
        start++;
        remaining -= 8;
    }

    if (remaining > 0)
        writeBits(uint64(bits[start]) >> (8 - remaining), remaining);

    return count;
}

void DefaultOutputBitStream::close() THROW
{
    if (isClosed() == true)
        return;

    int savedBitIndex = _availBits;
    uint savedPosition = _position;
    uint64 savedCurrent = _current;

    try {
        // Push last bytes (the very last byte may be incomplete)
        int size = ((64 - _availBits) + 7) >> 3;
        pushCurrent();
        _position -= (8 - size);
        flush();
    }
    catch (BitStreamException& e) {
        // Revert fields to allow subsequent attempts in case of transient failure
        _position = savedPosition;
        _availBits = savedBitIndex;
        _current = savedCurrent;
        throw e;
    }

    try {
        _os.flush();

        if (!_os.good())
            throw BitStreamException("Write to bitstream failed.", BitStreamException::INPUT_OUTPUT);
    }
    catch (ios_base::failure& e) {
        throw BitStreamException(e.what(), BitStreamException::INPUT_OUTPUT);
    }

    _closed = true;
    _position = 0;

    // Reset fields to force a flush() and trigger an exception
    // on writeBit() or writeBits()
    _availBits = 0;
    delete[] _buffer;
    _bufferSize = 8;
    _buffer = new byte[_bufferSize];
    _written -= 64; // adjust for method written()
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

// Write buffer to underlying stream
void DefaultOutputBitStream::flush() THROW
{
    if (isClosed() == true)
        throw BitStreamException("Stream closed", BitStreamException::STREAM_CLOSED);

    try {
        if (_position > 0) {
            _os.write(reinterpret_cast<char*>(_buffer), _position);

            if (!_os.good())
                throw BitStreamException("Write to bitstream failed", BitStreamException::INPUT_OUTPUT);

            _written += (uint64(_position) << 3);
            _position = 0;
        }
    }
    catch (ios_base::failure& e) {
        throw BitStreamException(e.what(), BitStreamException::INPUT_OUTPUT);
    }
}

DefaultOutputBitStream::~DefaultOutputBitStream()
{
    close();
    delete[] _buffer;
}
