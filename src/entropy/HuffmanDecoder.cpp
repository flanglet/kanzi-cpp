
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

using namespace kanzi;

// The chunk size indicates how many bytes are encoded (per block) before
// resetting the frequency stats.
HuffmanDecoder::HuffmanDecoder(InputBitStream& bitstream, int chunkSize) THROW : _bitstream(bitstream)
{
    if (chunkSize < 1024)
        throw invalid_argument("Huffman codec: The chunk size must be at least 1024");

    if (chunkSize > HuffmanCommon::MAX_CHUNK_SIZE) {
        stringstream ss;
        ss << "Huffman codec: The chunk size must be at most " << HuffmanCommon::MAX_CHUNK_SIZE;
        throw invalid_argument(ss.str());
    }

    _chunkSize = chunkSize;
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
    const int count = EntropyUtils::decodeAlphabet(_bitstream, _alphabet);

    if (count == 0)
        return 0;

    ExpGolombDecoder egdec(_bitstream, true);
    int8 currSize = 2;

    // Read lengths from bitstream
    for (int i = 0; i < count; i++) {
        const uint s = _alphabet[i];

        if (s > 255) {
            string msg = "Invalid bitstream: incorrect Huffman symbol ";
            msg += to_string(s);
            throw BitStreamException(msg, BitStreamException::INVALID_STREAM);
        }

        _codes[s] = 0;
        currSize += int8(egdec.decodeByte());

        if ((currSize <= 0) || (currSize > HuffmanCommon::MAX_SYMBOL_SIZE)) {
            stringstream ss;
            ss << "Invalid bitstream: incorrect size " << int(currSize);
            ss << " for Huffman symbol " << s;
            throw BitStreamException(ss.str(), BitStreamException::INVALID_STREAM);
        }

        _sizes[s] = uint16(currSize);
    }

    // Create canonical codes
    if (HuffmanCommon::generateCanonicalCodes(_sizes, _codes, _alphabet, count) < 0) {
        stringstream ss;
        ss << "Could not generate Huffman codes: max code length (";
        ss << HuffmanCommon::MAX_SYMBOL_SIZE;
        ss << " bits) exceeded";
        throw BitStreamException(ss.str(), BitStreamException::INVALID_STREAM);
    }

    // Build decoding tables
    buildDecodingTable(count);
    return count;
}

// max(CodeLen) must be <= MAX_SYMBOL_SIZE
void HuffmanDecoder::buildDecodingTable(int count)
{
    memset(_table, 0, sizeof(_table));
    int length = 0;

    for (int i = 0; i < count; i++) {
        const uint16 s = uint16(_alphabet[i]);

        if (_sizes[s] > length)
            length = _sizes[s];

        // code -> size, symbol
        const uint16 val = (s << 8) | _sizes[s];
        const int code = int(_codes[s]);

        // All DECODING_BATCH_SIZE bit values read from the bit stream and
        // starting with the same prefix point to symbol s
        int idx = code << (DECODING_BATCH_SIZE - length);
        const int end = (code + 1) << (DECODING_BATCH_SIZE - length);

        while (idx < end)
            _table[idx++] = val;
    }
}

int HuffmanDecoder::decode(byte block[], uint blkptr, uint count)
{
    if (count == 0)
        return 0;

    int startChunk = blkptr;
    const int end = blkptr + count;

    while (startChunk < end) {
        // For each chunk, read code lengths, rebuild codes, rebuild decoding table
        const int alphabetSize = readLengths();

        if (alphabetSize <= 0)
            return startChunk - blkptr;

        const int endChunk = min(startChunk + _chunkSize, end);

        // Compute minimum number of bits required in bitstream for fast decoding
        const int minCodeLen = int(_sizes[_alphabet[0]]); // not 0
        int padding = 64 / minCodeLen;

        if (minCodeLen * padding != 64)
            padding++;

        const int endChunk4 = startChunk + max(((endChunk - startChunk - padding) & -4), 0);

        for (int i = startChunk; i < endChunk4; i += 4) {
            fetchBits();
            block[i] = decodeByte();
            block[i + 1] = decodeByte();
            block[i + 2] = decodeByte();
            block[i + 3] = decodeByte();
        }

        // Fallback to regular decoding
        for (int i = endChunk4; i < endChunk; i++)
            block[i] = slowDecodeByte();

        startChunk = endChunk;
    }

    return count;
}

byte HuffmanDecoder::slowDecodeByte() THROW
{
    int code = 0;

    for (uint8 codeLen = 1; codeLen < HuffmanCommon::MAX_SYMBOL_SIZE; codeLen++) {
        if (_bits == 0) {
            code = (code << 1) | _bitstream.readBit();
        }
        else {
            _bits--;
            code = (code << 1) | ((_state >> _bits) & 1);
        }

        const int idx = code << (DECODING_BATCH_SIZE - codeLen);

        if (uint8(_table[idx]) == codeLen)
            return byte(_table[idx] >> 8);
    }

    throw BitStreamException("Invalid bitstream: incorrect Huffman code",
        BitStreamException::INVALID_STREAM);
}
