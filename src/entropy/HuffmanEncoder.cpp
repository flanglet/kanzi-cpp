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
#include "HuffmanEncoder.hpp"
#include "EntropyUtils.hpp"
#include "ExpGolombEncoder.hpp"
#include "../BitStreamException.hpp"
#include "../Global.hpp"

using namespace kanzi;

// The chunk size indicates how many bytes are encoded (per block) before
// resetting the frequency stats. 0 means that frequencies calculated at the
// beginning of the block apply to the whole block.
// The default chunk size is 65536 bytes.
HuffmanEncoder::HuffmanEncoder(OutputBitStream& bitstream, int chunkSize) THROW : _bitstream(bitstream)
{
    if (chunkSize < 1024)
        throw invalid_argument("Huffman codec: The chunk size must be at least 1024");

    if (chunkSize > HuffmanCommon::MAX_CHUNK_SIZE) {
        stringstream ss;
        ss << "Huffman codec: The chunk size must be at most " << HuffmanCommon::MAX_CHUNK_SIZE;
        throw invalid_argument(ss.str());
    }

    _chunkSize = chunkSize;
    _maxCodeLength = 0;

    // Default frequencies, sizes and codes
    for (int i = 0; i < 256; i++) {
        _freqs[i] = 1;
        _codes[i] = i;
    }

    memset(_alphabet, 0, sizeof(_alphabet));
    memset(_sranks, 0, sizeof(_sranks));
}

// Rebuild Huffman codes
int HuffmanEncoder::updateFrequencies(uint frequencies[]) THROW
{
    int count = 0;
    short sizes[256];

    for (int i = 0; i < 256; i++) {
        sizes[i] = 0;
        _codes[i] = 0;

        if (frequencies[i] > 0)
            _alphabet[count++] = i;
    }

    EntropyUtils::encodeAlphabet(_bitstream, _alphabet, 256, count);
    computeCodeLengths(frequencies, sizes, count);

    // Transmit code lengths only, frequencies and codes do not matter
    // Unary encode the length difference
    ExpGolombEncoder egenc(_bitstream, true);
    short prevSize = 2;

    for (int i = 0; i < count; i++) {
        const short currSize = sizes[_alphabet[i]];
        egenc.encodeByte(byte(currSize - prevSize));
        prevSize = currSize;
    }

    // Create canonical codes
    if (HuffmanCommon::generateCanonicalCodes(sizes, _codes, _sranks, count) < 0) {
        throw BitStreamException("Could not generate Huffman codes: max code length (24 bits) exceeded",
            BitStreamException::INVALID_STREAM);
    }

    // Pack size and code (size <= MAX_SYMBOL_SIZE bits)
    for (int i = 0; i < count; i++) {
        const int s = _alphabet[i];
        _codes[s] |= (sizes[s] << 24);
    }

    return count;
}

// See [In-Place Calculation of Minimum-Redundancy Codes]
// by Alistair Moffat & Jyrki Katajainen
void HuffmanEncoder::computeCodeLengths(uint frequencies[], short sizes[], int count) THROW
{
    if (count == 1) {
        _sranks[0] = _alphabet[0];
        sizes[_alphabet[0]] = 1;
        return;
    }

    // Sort _sranks by increasing frequencies (first key) and increasing value (second key)
    for (int i = 0; i < count; i++)
        _sranks[i] = (frequencies[_alphabet[i]] << 8) | _alphabet[i];

    vector<uint> v(_sranks, _sranks + count);
    sort(v.begin(), v.end());
    memcpy(_sranks, &v[0], count * sizeof(uint));
    uint buffer[256];

    for (int i = 0; i < count; i++) {
        buffer[i] = _sranks[i] >> 8;
        _sranks[i] &= 0xFF;
    }

    computeInPlaceSizesPhase1(buffer, count);
    computeInPlaceSizesPhase2(buffer, count);
    _maxCodeLength = 0;

    for (int i = 0; i < count; i++) {
        short codeLen = short(buffer[i]);

        if ((codeLen <= 0) || (codeLen > HuffmanCommon::MAX_SYMBOL_SIZE)) {
            stringstream ss;
            ss << "Could not generate Huffman codes: max code length (";
            ss << HuffmanCommon::MAX_SYMBOL_SIZE;
            ss << " bits) exceeded";
            throw invalid_argument(ss.str());
        }

        if (_maxCodeLength > codeLen)
            _maxCodeLength = codeLen;

        sizes[_sranks[i]] = codeLen;
    }
}

void HuffmanEncoder::computeInPlaceSizesPhase1(uint data[], int n)
{
    for (int s = 0, r = 0, t = 0; t < n - 1; t++) {
        int sum = 0;

        for (int i = 0; i < 2; i++) {
            if ((s >= n) || ((r < t) && (data[r] < data[s]))) {
                sum += data[r];
                data[r] = t;
                r++;
            }
            else {
                sum += data[s];

                if (s > t)
                    data[s] = 0;

                s++;
            }
        }

        data[t] = sum;
    }
}

void HuffmanEncoder::computeInPlaceSizesPhase2(uint data[], int n)
{
    uint topLevel = n - 2; //root
    int depth = 1;
    int i = n;
    int totalNodesAtLevel = 2;

    while (i > 0) {
        int k = topLevel;

        while ((k > 0) && (data[k - 1] >= topLevel))
            k--;

        const int internalNodesAtLevel = topLevel - k;
        const int leavesAtLevel = totalNodesAtLevel - internalNodesAtLevel;

        for (int j = 0; j < leavesAtLevel; j++)
            data[--i] = depth;

        totalNodesAtLevel = internalNodesAtLevel << 1;
        topLevel = k;
        depth++;
    }
}

// Dynamically compute the frequencies for every chunk of data in the block
int HuffmanEncoder::encode(byte block[], uint blkptr, uint count)
{
    if (count == 0)
        return 0;

    const int end = blkptr + count;
    int startChunk = blkptr;
    uint8* data = reinterpret_cast<uint8*>(&block[0]);

    while (startChunk < end) {
        const int endChunk = min(startChunk + _chunkSize, end);
        Global::computeHistogram(&block[startChunk], endChunk - startChunk, _freqs, true);

        // Rebuild Huffman codes
        updateFrequencies(_freqs);

        if (_maxCodeLength <= 16) {
            const int endChunk4 = 4 * ((endChunk - startChunk) / 4) + startChunk;

            for (int i = startChunk; i < endChunk4; i += 4) {
                // Pack 4 codes into 1 uint64
                const uint code1 = _codes[data[i]];
                const uint codeLen1 = code1 >> 24;
                const uint code2 = _codes[data[i + 1]];
                const uint codeLen2 = code2 >> 24;
                const uint code3 = _codes[data[i + 2]];
                const uint codeLen3 = code3 >> 24;
                const uint code4 = _codes[data[i + 3]];
                const uint codeLen4 = code4 >> 24;
                const uint64 st = (uint64(code1) << (codeLen2 + codeLen3 + codeLen4)) | 
                   ((uint64(code2) & ((1 << codeLen2) - 1)) << (codeLen3 + codeLen4)) | 
                   ((uint64(code3) & ((1 << codeLen3) - 1)) << codeLen4) | 
                    (uint64(code4) & ((1 << codeLen4) - 1));
                _bitstream.writeBits(st, codeLen1 + codeLen2 + codeLen3 + codeLen4);
            }

            for (int i = endChunk4; i < endChunk; i++) {
                const uint code = _codes[data[i]];
                _bitstream.writeBits(code, code >> 24);
            }
        }
        else {
            const int endChunk3 = 3 * ((endChunk - startChunk) / 3) + startChunk;

            for (int i = startChunk; i < endChunk3; i += 3) {
                // Pack 3 codes into 1 uint64
                const uint code1 = _codes[data[i]];
                const uint codeLen1 = code1 >> 24;
                const uint code2 = _codes[data[i + 1]];
                const uint codeLen2 = code2 >> 24;
                const uint code3 = _codes[data[i + 2]];
                const uint codeLen3 = code3 >> 24;
                const uint64 st = (uint64(code1) << (codeLen2 + codeLen3)) | 
                   ((uint64(code2) & ((1 << codeLen2) - 1)) << codeLen3) |
                    (uint64(code3) & ((1 << codeLen3) - 1));
                _bitstream.writeBits(st, codeLen1 + codeLen2 + codeLen3);
            }

            for (int i = endChunk3; i < endChunk; i++) {
                const uint code = _codes[data[i]];
                _bitstream.writeBits(code, code >> 24);
            }
        }

        startChunk = endChunk;
    }

    return count;
}
