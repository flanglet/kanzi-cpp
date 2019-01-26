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
#include "../IllegalArgumentException.hpp"

using namespace kanzi;

class FrequencyArrayComparator {
private:
    uint* _freqs;

public:
    FrequencyArrayComparator(uint freqs[]) { _freqs = freqs; }

    ~FrequencyArrayComparator() {}

    bool operator()(int i, int j);
};

inline bool FrequencyArrayComparator::operator()(int lidx, int ridx)
{
    // Check size (natural order) as first key
    // Check index (natural order) as second key
    return (_freqs[lidx] != _freqs[ridx]) ? _freqs[lidx] < _freqs[ridx] : lidx < ridx;
}


// The chunk size indicates how many bytes are encoded (per block) before
// resetting the frequency stats. 0 means that frequencies calculated at the
// beginning of the block apply to the whole block.
// The default chunk size is 65536 bytes.
HuffmanEncoder::HuffmanEncoder(OutputBitStream& bitstream, int chunkSize) THROW : _bitstream(bitstream)
{
    if (chunkSize < 1024)
        throw IllegalArgumentException("The chunk size must be at least 1024");

    if (chunkSize > HuffmanCommon::MAX_CHUNK_SIZE) {
        stringstream ss;
        ss << "The chunk size must be at most" << HuffmanCommon::MAX_CHUNK_SIZE;
        throw IllegalArgumentException(ss.str());
    }

    _chunkSize = chunkSize;

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
        throw BitStreamException("Could not generate codes: max code length (24 bits) exceeded",
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
// count > 1 by design
void HuffmanEncoder::computeCodeLengths(uint frequencies[], short sizes[], int count) THROW
{
    if (count == 1) {
        _sranks[0] = _alphabet[0];
        sizes[_alphabet[0]] = 1;
        return;
    }

    // Sort by increasing frequencies (first key) and increasing value (second key)
    vector<uint> v(_alphabet, _alphabet + count);
    FrequencyArrayComparator comparator(frequencies);
    sort(v.begin(), v.end(), comparator);
    memcpy(_sranks, &v[0], count*sizeof(uint));
    uint buffer[256]; 

    for (int i = 0; i < count; i++)
        buffer[i] = frequencies[_sranks[i]];

    computeInPlaceSizesPhase1(buffer, count);
    computeInPlaceSizesPhase2(buffer, count);

    for (int i = 0; i < count; i++) {
        short codeLen = short(buffer[i]);

        if ((codeLen <= 0) || (codeLen > HuffmanCommon::MAX_SYMBOL_SIZE)) {
            stringstream ss;
            ss << "Could not generate codes: max code length (";
            ss << HuffmanCommon::MAX_SYMBOL_SIZE;
            ss << " bits) exceeded";
            throw IllegalArgumentException(ss.str());
        }

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
int HuffmanEncoder::encode(byte block[], uint blkptr, uint len)
{
    if (len == 0)
        return 0;

    const int end = blkptr + len;
    const int sz = (_chunkSize == 0) ? len : _chunkSize;
    int startChunk = blkptr;
    uint8* data = (uint8*) &block[0];

    while (startChunk < end) {
        const int endChunk = (startChunk + sz < end) ? startChunk + sz : end;
        const int endChunk8 = ((endChunk - startChunk) & -8) + startChunk;
        Global::computeHistogram(&block[startChunk], endChunk - startChunk, _freqs, true);

        // Rebuild Huffman codes
        updateFrequencies(_freqs);

        for (int i = startChunk; i < endChunk8; i += 8) {
            uint val;
            val = _codes[data[i]];
            _bitstream.writeBits(val, val >> 24);
            val = _codes[data[i + 1]];
            _bitstream.writeBits(val, val >> 24);
            val = _codes[data[i + 2]];
            _bitstream.writeBits(val, val >> 24);
            val = _codes[data[i + 3]];
            _bitstream.writeBits(val, val >> 24);
            val = _codes[data[i + 4]];
            _bitstream.writeBits(val, val >> 24);
            val = _codes[data[i + 5]];
            _bitstream.writeBits(val, val >> 24);
            val = _codes[data[i + 6]];
            _bitstream.writeBits(val, val >> 24);
            val = _codes[data[i + 7]];
            _bitstream.writeBits(val, val >> 24);
        }

        for (int i = endChunk8; i < endChunk; i++) {
            const uint val = _codes[data[i]];
            _bitstream.writeBits(val, val >> 24);
        }

        startChunk = endChunk;
    }

    return len;
}
