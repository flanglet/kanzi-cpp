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

#include "DefaultInputBitStream.hpp"
#include "../IllegalArgumentException.hpp"
#include "../Memory.hpp"
#include "../io/IOException.hpp"

using namespace kanzi;

DefaultInputBitStream::DefaultInputBitStream(InputStream& is, uint bufferSize) THROW : _is(is)
{
    if (bufferSize < 1024)
        throw IllegalArgumentException("Invalid buffer size (must be at least 1024)");

    if (bufferSize > 1 << 29)
        throw IllegalArgumentException("Invalid buffer size (must be at most 536870912)");

    if ((bufferSize & 7) != 0)
        throw IllegalArgumentException("Invalid buffer size (must be a multiple of 8)");

    _bufferSize = bufferSize;
    _buffer = new byte[_bufferSize];
    _availBits = 0;
    _maxPosition = -1;
    _position = 0;
    _current = 0;
    _read = 0;
    _closed = false;
}

DefaultInputBitStream::~DefaultInputBitStream()
{
    close();
    delete[] _buffer;
}

// Returns 1 or 0
inline int DefaultInputBitStream::readBit() THROW
{
    if (_availBits  == 0)
        pullCurrent(); // Triggers an exception if stream is closed

    _availBits--;
    return int(_current >> _availBits) & 1;
}

uint64 DefaultInputBitStream::readBits(uint count) THROW
{
    if ((count == 0) || (count > 64))
        throw BitStreamException("Invalid bit count: " + to_string(count) + " (must be in [1..64])");

    uint64 res;

    if (count <= uint(_availBits)) {
        // Enough spots available in 'current'
        uint shift = _availBits - count;

        if (_availBits == 0) {
            pullCurrent();
            shift += (_availBits - 64); // adjust if _availBits != 64 (end of stream)
        }

        res = (_current >> shift) & (uint64(-1) >> (64 - count));
        _availBits -= count;
    }
    else {
        // Not enough spots available in 'current'
        const uint remaining = count - _availBits;
        res = _current & ((uint64(1) << _availBits) - 1);
        pullCurrent();
        _availBits -= remaining;
        res = (res << remaining) | (_current >> _availBits);
    }

    return res;
}

uint DefaultInputBitStream::readBits(byte bits[], uint count) THROW
{
    if (isClosed() == true)
        throw BitStreamException("Stream closed", BitStreamException::STREAM_CLOSED);

    int remaining = count;
    int start = 0;

    // Byte aligned cursor ?
    if ((_availBits & 7) == 0) {
        if (_availBits == 0)
            pullCurrent();

        // Empty _current
        while ((_availBits > 0) && (remaining >= 8)) {
            bits[start] = byte(readBits(8));
            start++;
            remaining -= 8;
        }

        prefetchRead(&_buffer[_position]);

        // Copy internal buffer to bits array
        while ((remaining >> 3) > _maxPosition + 1 - _position) {
            memcpy(&bits[start], &_buffer[_position], _maxPosition + 1 - _position);
            start += (_maxPosition + 1 - _position);
            remaining -= ((_maxPosition + 1 - _position) << 3);
            readFromInputStream(_bufferSize);
        }

        const int r = (remaining >> 6) << 3;

        if (r > 0) {
            memcpy(&bits[start], &_buffer[_position], r);
            _position += r;
            start += r;
            remaining -= (r << 3);
        }
    }
    else {
        // Not byte aligned
        const int r = 64 - _availBits;

        while (remaining >= 64) {
            const uint64 v = _current & (uint64(-1) >> (64 - _availBits));
            pullCurrent();
            _availBits -= r;
            BigEndian::writeLong64(&bits[start], (v << r) | (_current >> _availBits));
            start += 8;
            remaining -= 64;
        }
    }

    // Last bytes
    while (remaining >= 8) {
        bits[start] = byte(readBits(8));
        start++;
        remaining -= 8;
    }

    if (remaining > 0)
        bits[start] = byte(readBits(remaining) << (8 - remaining));

    return count;
}

void DefaultInputBitStream::close() THROW
{
    if (isClosed() == true)
        return;

    _closed = true;

    // Reset fields to force a readFromInputStream() and trigger an exception
    // on readBit() or readBits()
    _availBits = 0;
    _maxPosition = -1;
}

int DefaultInputBitStream::readFromInputStream(uint count) THROW
{
    if (isClosed() == true)
        throw BitStreamException("Stream closed", BitStreamException::STREAM_CLOSED);

    int size = -1;

    try {
        _read += (uint64(_maxPosition + 1) << 3);
        _is.read(reinterpret_cast<char*>(_buffer), count);

        if (_is.good()) {
            size = count;
        }
        else {
            size = int(_is.gcount());

            if (!_is.eof()) {
                _position = 0;
                _maxPosition = (size <= 0) ? -1 : size - 1;
                throw BitStreamException("No more data to read in the bitstream",
                    BitStreamException::END_OF_STREAM);
            }
        }
    }
    catch (IOException& e) {
        _position = 0;
        _maxPosition = (size <= 0) ? -1 : size - 1;
        throw BitStreamException(e.what(), BitStreamException::INPUT_OUTPUT);
    }

    _position = 0;
    _maxPosition = (size <= 0) ? -1 : size - 1;
    return size;
}

// Return false when the bitstream is closed or the End-Of-Stream has been reached
bool DefaultInputBitStream::hasMoreToRead()
{
    if (isClosed() == true)
        return false;

    if ((_position < _maxPosition) || (_availBits > 0))
        return true;

    try {
        readFromInputStream(_bufferSize);
    }
    catch (BitStreamException&) {
        return false;
    }

    return true;
}

// Pull 64 bits of current value from buffer.
inline void DefaultInputBitStream::pullCurrent()
{
    if (_position > _maxPosition)
        readFromInputStream(_bufferSize);

    if (_position + 7 > _maxPosition) {
        // End of stream: overshoot max position => adjust bit index
        uint shift = (_maxPosition - _position) << 3;
        _availBits = shift + 8;
        uint64 val = 0;

        while (_position <= _maxPosition) {
            val |= ((uint64(_buffer[_position++] & 0xFF)) << shift);
            shift -= 8;
        }

        _current = val;
    }
    else {
        // Regular processing, buffer length is multiple of 8
        _current = BigEndian::readLong64(&_buffer[_position]);
        _availBits = 64;
        _position += 8;
    }
}
