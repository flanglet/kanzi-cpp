
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

#include <cstring>
#include <sstream>
#include <algorithm>
#include <vector>
#include "HuffmanDecoder.hpp"
#include "EntropyUtils.hpp"
#include "ExpGolombDecoder.hpp"
#include "../BitStreamException.hpp"
#include "../IllegalArgumentException.hpp"

using namespace kanzi;

// The chunk size indicates how many bytes are encoded (per block) before
// resetting the frequency stats. 
HuffmanDecoder::HuffmanDecoder(InputBitStream& bitstream, int chunkSize) THROW : _bitstream(bitstream)
{
    if (chunkSize < 1024)
        throw IllegalArgumentException("The chunk size must be at least 1024");

    if (chunkSize > HuffmanCommon::MAX_CHUNK_SIZE) {
        stringstream ss;
        ss << "The chunk size must be at most " << HuffmanCommon::MAX_CHUNK_SIZE;
        throw IllegalArgumentException(ss.str());
    }

    _chunkSize = chunkSize;
    _minCodeLen = 8;
    _state = 0;
    _bits = 0;

    // Default lengths & canonical codes
    for (int i = 0; i < 256; i++) {
        _codes[i] = i;
        _sizes[i] = 8;
    }

    memset(_alphabet, 0, sizeof(_alphabet));
}

int HuffmanDecoder::readLengths() THROW
{
    int count = EntropyUtils::decodeAlphabet(_bitstream, _alphabet);

    if (count == 0)
        return 0;

    ExpGolombDecoder egdec(_bitstream, true);
    _minCodeLen = HuffmanCommon::MAX_SYMBOL_SIZE; // max code length
    int prevSize = 2;

    // Read lengths
    for (int i = 0; i < count; i++) {
        const uint s = _alphabet[i];

        if (s >= 256) {
            string msg = "Invalid bitstream: incorrect Huffman symbol ";
            msg += to_string(s);
            throw BitStreamException(msg, BitStreamException::INVALID_STREAM);
        }

        _codes[s] = 0;
        int currSize = prevSize + egdec.decodeByte();

        if ((currSize <= 0) || (currSize > HuffmanCommon::MAX_SYMBOL_SIZE)) {
            stringstream ss;
            ss << "Invalid bitstream: incorrect size " << currSize;
            ss << " for Huffman symbol " << s;
            throw BitStreamException(ss.str(), BitStreamException::INVALID_STREAM);
        }

        if (_minCodeLen > currSize)
            _minCodeLen = currSize;

        _sizes[s] = short(currSize);
        prevSize = currSize;
    }

    // Create canonical codes
    if (HuffmanCommon::generateCanonicalCodes(_sizes, _codes, _alphabet, count) < 0) {
        stringstream ss;
        ss << "Could not generate codes: max code length (";
        ss << HuffmanCommon::MAX_SYMBOL_SIZE;
        ss << " bits) exceeded";
        throw BitStreamException(ss.str(), BitStreamException::INVALID_STREAM);
    }

    // Build decoding tables
    buildDecodingTables(count);
    return count;
}

// Build decoding tables
// The slow decoding table contains the codes in natural order.
// The fast decoding table contains all the prefixes with DECODING_BATCH_SIZE bits.
void HuffmanDecoder::buildDecodingTables(int count)
{
    memset(_fdTable, 0, sizeof(_fdTable));
    memset(_sdTable, 0, sizeof(_sdTable));

    for (int i = 0; i <= HuffmanCommon::MAX_SYMBOL_SIZE; i++)
        _sdtIndexes[i] = SYMBOL_ABSENT;

    int length = 0;

    for (int i = 0; i < count; i++) {
        const uint s = _alphabet[i];
        const uint code = _codes[s];

        if (_sizes[s] > length) {
            length = _sizes[s];
            _sdtIndexes[length] = i - code;
        }

        // Fill slow decoding table
        const uint16 val = uint16((_sizes[s] << 8) | s);
        _sdTable[i] = val;

        // Fill fast decoding table
        // Find location index in table
        if (length < DECODING_BATCH_SIZE) {
            uint idx = code << (DECODING_BATCH_SIZE - length);
            const uint end = idx + (1 << (DECODING_BATCH_SIZE - length));

            // All DECODING_BATCH_SIZE bit values read from the bit stream and
            // starting with the same prefix point to symbol r
            while (idx < end)
               _fdTable[idx++] = val;
        }
        else {
            const uint idx = code >> (length - DECODING_BATCH_SIZE);
            _fdTable[idx] = val;
        }
    }
}

// Use fastDecodeByte until the near end of chunk or block.
int HuffmanDecoder::decode(byte block[], uint blkptr, uint count)
{
    if (count == 0)
        return 0;

    if (_minCodeLen == 0)
        return -1;

    int startChunk = blkptr;
    const int end = blkptr + count;

    while (startChunk < end) {
        // Reinitialize the Huffman tables
        if (readLengths() <= 0)
            return startChunk - blkptr;

        // Compute minimum number of bits required in bitstream for fast decoding
        int endPaddingSize = 64 / _minCodeLen;

        if (_minCodeLen * endPaddingSize != 64)
            endPaddingSize++;

        const int endChunk = (startChunk + _chunkSize < end) ? startChunk + _chunkSize : end;
        const int endChunk8 = max((endChunk - endPaddingSize) & -8, 0);
        int i = startChunk;

        // Fast decoding (read DECODING_BATCH_SIZE bits at a time)
        for (; i < endChunk8; i+=8)
        {
            block[i]   = fastDecodeByte();
            block[i+1] = fastDecodeByte();
            block[i+2] = fastDecodeByte();
            block[i+3] = fastDecodeByte();
            block[i+4] = fastDecodeByte();
            block[i+5] = fastDecodeByte();
            block[i+6] = fastDecodeByte();
            block[i+7] = fastDecodeByte();
        }

        // Fallback to regular decoding (read one bit at a time)
        for (; i < endChunk; i++)
            block[i] = slowDecodeByte(0, 0);

        startChunk = endChunk;
    }

    return count;
}

byte HuffmanDecoder::slowDecodeByte(int code, int codeLen) THROW
{
    while (codeLen < HuffmanCommon::MAX_SYMBOL_SIZE) {
        codeLen++;
        code <<= 1;

        if (_bits == 0)
            code |= _bitstream.readBit();
        else {
            // Consume remaining bits in 'state'
            _bits--;
            code |= ((_state >> _bits) & 1);
        }

        const int idx = _sdtIndexes[codeLen];

        if (idx == SYMBOL_ABSENT) // No code with this length ?
            continue;

        if ((_sdTable[idx + code] >> 8) == uint16(codeLen))
            return byte(_sdTable[idx + code]);
    }

    throw BitStreamException("Invalid bitstream: incorrect Huffman code",
        BitStreamException::INVALID_STREAM);
}

// 64 bits must be available in the bitstream
inline byte HuffmanDecoder::fastDecodeByte()
{
    if (_bits < DECODING_BATCH_SIZE) {
        // Fetch more bits from bitstream       
        const uint64 mask = uint64(1 << _bits) - 1; // for _bits = 0
        _state = ((_state & mask) << (64 - _bits)) | _bitstream.readBits(64 - _bits);
        _bits = 64;
    }

    // Retrieve symbol from fast decoding table
    const int val = _fdTable[(_state >> (_bits - DECODING_BATCH_SIZE)) & DECODING_MASK];

    if (val > MAX_DECODING_INDEX) {
        _bits -= DECODING_BATCH_SIZE;
        return slowDecodeByte(int(_state >> _bits) & DECODING_MASK, DECODING_BATCH_SIZE);
    }

    _bits -= (val >> 8);
    return byte(val);
}
