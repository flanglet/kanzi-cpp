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
    _maxCodeLen = 0;

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
    uint16 sizes[256];

    for (int i = 0; i < 256; i++) {
        sizes[i] = 0;
        _codes[i] = 0;

        if (frequencies[i] > 0)
            _alphabet[count++] = i;
    }

    EntropyUtils::encodeAlphabet(_bitstream, _alphabet, 256, count);
    int retries = 0;

    while (true) {
        computeCodeLengths(frequencies, sizes, count);

        if (_maxCodeLen <= HuffmanCommon::MAX_SYMBOL_SIZE) {
            // Usual case
            HuffmanCommon::generateCanonicalCodes(sizes, _codes, _sranks, count);
            break;
        }

        // Rare: some codes exceed the budget for the max code length => normalize
        // frequencies (it boosts the smallest frequencies) and try once more.
        if (retries > 2) {
            stringstream ss;
            ss << "Could not generate Huffman codes: max code length (";
            ss << HuffmanCommon::MAX_SYMBOL_SIZE;
            ss << " bits) exceeded";
            throw invalid_argument(ss.str());
        }

        uint totalFreq = 0;

        for (int i = 0; i < count; i++)
            totalFreq += frequencies[_alphabet[i]];

        // Copy alphabet (modified by normalizeFrequencies)
        uint alphabet[256];
        memcpy(alphabet, _alphabet, sizeof(alphabet));
        retries++;
        EntropyUtils::normalizeFrequencies(frequencies, alphabet, count, totalFreq, 
           HuffmanCommon::MAX_CHUNK_SIZE >> (2 * retries));
    }

    // Transmit code lengths only, frequencies and codes do not matter
    ExpGolombEncoder egenc(_bitstream, true);
    uint16 prevSize = 2;

    // Pack size and code (size <= MAX_SYMBOL_SIZE bits)
    // Unary encode the code length differences
    for (int i = 0; i < count; i++) {
        const int s = _alphabet[i];
        _codes[s] |= (sizes[s] << 24);
        egenc.encodeByte(byte(sizes[s] - prevSize));
        prevSize = sizes[s];
    }

    return count;
}

void HuffmanEncoder::computeCodeLengths(uint frequencies[], uint16 sizes[], int count) THROW
{
    if (count == 1) {
        _sranks[0] = _alphabet[0];
        sizes[_alphabet[0]] = 1;
        _maxCodeLen = 1;
        return;
    }

    // Sort _sranks by increasing frequencies (first key) and increasing value (second key)
    for (int i = 0; i < count; i++)
        _sranks[i] = (frequencies[_alphabet[i]] << 8) | _alphabet[i];

    vector<uint> v(_sranks, _sranks + count);
    sort(v.begin(), v.end());
    memcpy(_sranks, &v[0], count * sizeof(uint));
    uint buffer[256]= { 0 };

    for (int i = 0; i < count; i++) {
        buffer[i] = _sranks[i] >> 8;
        _sranks[i] &= 0xFF;
    }

    // See [In-Place Calculation of Minimum-Redundancy Codes]
    // by Alistair Moffat & Jyrki Katajainen
    computeInPlaceSizesPhase1(buffer, count);
    computeInPlaceSizesPhase2(buffer, count);
    _maxCodeLen = 0;

    for (int i = 0; i < count; i++) {
        const uint codeLen = buffer[i];

        if (codeLen == 0)
            throw invalid_argument("Could not generate Huffman codes: invalid code length 0");

        if (_maxCodeLen < codeLen)
            _maxCodeLen = codeLen;

        sizes[_sranks[i]] = uint16(codeLen);
    }
}

void HuffmanEncoder::computeInPlaceSizesPhase1(uint data[], int n)
{
    for (int s = 0, r = 0, t = 0; t < n - 1; t++) {
        uint sum = 0;

        for (int i = 0; i < 2; i++) {
            if ((s >= n) || ((r < t) && (data[r] < data[s]))) {
                sum += data[r];
                data[r] = t;
                r++;
                continue;
            }

            sum += data[s];

            if (s > t)
                data[s] = 0;

            s++;
        }

        data[t] = sum;
    }
}

void HuffmanEncoder::computeInPlaceSizesPhase2(uint data[], int n)
{
    uint topLevel = n - 2; //root
    uint depth = 1;
    uint i = n;
    uint totalNodesAtLevel = 2;

    while (i > 0) {
        uint k = topLevel;

        while ((k != 0) && (data[k - 1] >= topLevel))
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
int HuffmanEncoder::encode(const byte block[], uint blkptr, uint count)
{
    if (count == 0)
        return 0;

    const int end = blkptr + count;
    int startChunk = blkptr;

    while (startChunk < end) {
        // Update frequencies and rebuild Huffman codes
        const int endChunk = min(startChunk + _chunkSize, end);
        Global::computeHistogram(&block[startChunk], endChunk - startChunk, _freqs, true);
        updateFrequencies(_freqs);
        const int endChunk4 = ((endChunk - startChunk) & -4) + startChunk;

        for (int i = startChunk; i < endChunk4; i += 4) {
            // Pack 4 codes into 1 uint64
            const uint code1 = _codes[int(block[i])];
            const uint codeLen1 = code1 >> 24;
            const uint code2 = _codes[int(block[i + 1])];
            const uint codeLen2 = code2 >> 24;
            const uint code3 = _codes[int(block[i + 2])];
            const uint codeLen3 = code3 >> 24;
            const uint code4 = _codes[int(block[i + 3])];
            const uint codeLen4 = code4 >> 24;
            const uint64 st = (uint64(code1) << (codeLen2 + codeLen3 + codeLen4)) | ((uint64(code2) & ((1 << codeLen2) - 1)) << (codeLen3 + codeLen4)) | ((uint64(code3) & ((1 << codeLen3) - 1)) << codeLen4) | (uint64(code4) & ((1 << codeLen4) - 1));
            _bitstream.writeBits(st, codeLen1 + codeLen2 + codeLen3 + codeLen4);
        }

        for (int i = endChunk4; i < endChunk; i++) {
            const uint code = _codes[int(block[i])];
            _bitstream.writeBits(code, code >> 24);
        }

        startChunk = endChunk;
    }

    return count;
}
