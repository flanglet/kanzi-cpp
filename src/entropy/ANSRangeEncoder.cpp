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

#include <sstream>
#include "ANSRangeEncoder.hpp"
#include "EntropyUtils.hpp"
#include "../Global.hpp"

using namespace kanzi;
using namespace std;

// The chunk size indicates how many bytes are encoded (per block) before
// resetting the frequency stats.
ANSRangeEncoder::ANSRangeEncoder(OutputBitStream& bitstream, int order, int chunkSize, int logRange) THROW : _bitstream(bitstream)
{
    if ((order != 0) && (order != 1))
        throw invalid_argument("ANS Codec: The order must be 0 or 1");

    if (chunkSize < MIN_CHUNK_SIZE) {
        stringstream ss;
        ss << "ANS Codec: The chunk size must be at least " << MIN_CHUNK_SIZE;
        throw invalid_argument(ss.str());
    }

    if (chunkSize > MAX_CHUNK_SIZE) {
        stringstream ss;
        ss << "ANS Codec: The chunk size must be at most " << MAX_CHUNK_SIZE;
        throw invalid_argument(ss.str());
    }

    if ((logRange < 8) || (logRange > 16)) {
        stringstream ss;
        ss << "ANS Codec: Invalid range: " << logRange << " (must be in [8..16])";
        throw invalid_argument(ss.str());
    }

    _chunkSize = min(chunkSize << (8 * order), MAX_CHUNK_SIZE);
    _order = order;
    const int32 dim = 255 * order + 1;
    _alphabet = new uint[dim * 256];
    _freqs = new uint[dim * 257]; // freqs[x][256] = total(freqs[x][0..255])
    _symbols = new ANSEncSymbol[dim * 256];
    _buffer = new byte[0];
    _bufferSize = 0;
    _logRange = logRange;
}

ANSRangeEncoder::~ANSRangeEncoder()
{
    _dispose();
    delete[] _buffer;
    delete[] _symbols;
    delete[] _freqs;
    delete[] _alphabet;
}


// Compute cumulated frequencies and encode header
int ANSRangeEncoder::updateFrequencies(uint frequencies[], int lr)
{
    int res = 0;
    const int endk = 255 * _order + 1;
    _bitstream.writeBits(lr - 8, 3); // logRange

    for (int k = 0; k < endk; k++) {
        uint* f = &frequencies[k * 257];
        ANSEncSymbol* symb = &_symbols[k << 8];
        uint* curAlphabet = &_alphabet[k << 8];
        const int alphabetSize = EntropyUtils::normalizeFrequencies(f, curAlphabet, 256, f[256], 1 << lr);

        if (alphabetSize > 0) {
            int sum = 0;

            for (int i = 0; i < 256; i++) {
                if (f[i] == 0)
                    continue;

                symb[i].reset(sum, f[i], lr);
                sum += f[i];
            }
        }

        encodeHeader(alphabetSize, curAlphabet, f, lr);
        res += alphabetSize;
    }

    return res;
}

// Encode alphabet and frequencies
bool ANSRangeEncoder::encodeHeader(int alphabetSize, uint alphabet[], uint frequencies[], int lr)
{
    const int encoded = EntropyUtils::encodeAlphabet(_bitstream, alphabet, 256, alphabetSize);

    if (encoded < 0)
        return false;

    if (encoded == 0)
        return true;

    const int chkSize = (alphabetSize >= 64) ? 8 : 6;
    int llr = 3;

    while ((1 << llr) <= lr)
        llr++;

    // Encode all frequencies (but the first one) by chunks
    for (int i = 1; i < alphabetSize; i += chkSize) {
        uint max = frequencies[alphabet[i]] - 1;
        uint logMax = 0;
        const int endj = min(i + chkSize, alphabetSize);

        // Search for max frequency log size in next chunk
        for (int j = i + 1; j < endj; j++) {
            if (frequencies[alphabet[j]] - 1 > max)
                max = frequencies[alphabet[j]] - 1;
        }

        while (uint(1 << logMax) <= max)
            logMax++;

        _bitstream.writeBits(logMax, llr);

        if (logMax == 0) // all frequencies equal one in this chunk
            continue;

        // Write frequencies
        for (int j = i; j < endj; j++)
            _bitstream.writeBits(frequencies[alphabet[j]] - 1, logMax);
    }

    return true;
}

// Dynamically compute the frequencies for every chunk of data in the block
int ANSRangeEncoder::encode(const byte block[], uint blkptr, uint count)
{
    if (count == 0)
        return 0;

    const uint end = blkptr + count;
    uint startChunk = blkptr;
    uint sz = uint(_chunkSize);
    const uint size = max(min(sz + (sz >> 3), 2 * count), uint(65536));

    if (_bufferSize < size) {
        delete[] _buffer;
        _bufferSize = size;
        _buffer = new byte[_bufferSize];
    }

    while (startChunk < end) {
        const uint sizeChunk = min(sz, end - startChunk);
        uint lr = _logRange;

        // Lower log range if the size of the data chunk is small
        while ((lr > 8) && (uint(1 << lr) > sizeChunk))
            lr--;

        const int alphabetSize = rebuildStatistics(&block[startChunk], sizeChunk, lr);

        // Skip chunk if only one symbol
        if ((alphabetSize <= 1) && (_order == 0)) {
            startChunk += sizeChunk;
            continue;
        }

        encodeChunk(&block[startChunk], sizeChunk);
        startChunk += sizeChunk;
    }

    return count;
}

void ANSRangeEncoder::encodeChunk(const byte block[], int end)
{
    int st0 = ANS_TOP;
    int st1 = ANS_TOP;
    byte* p0 = &_buffer[_bufferSize - 1];
    byte* p = p0;

    if (_order == 0) {
        int start = end - 1;

        if ((end & 1) != 0) {
            p[0] = block[start];
            start--;
            p--;
        }

        for (int i = start; i > 0; i -= 2) {
            st0 = encodeSymbol(p, st0, _symbols[int(block[i])]);
            st1 = encodeSymbol(p, st1, _symbols[int(block[i - 1])]);
        }
    }
    else { // order 1
        int prv = int(block[end - 1]);

        for (int i = end - 2; i >= 0; i--) {
            const int cur = int(block[i]);
            st0 = encodeSymbol(p, st0, _symbols[(cur << 8) | prv]);
            prv = cur;
        }

        // Last symbol
        st0 = encodeSymbol(p, st0, _symbols[prv]);
    }

    // Write chunk size
    EntropyUtils::writeVarInt(_bitstream, uint32(p0 - p));

    // Write final ANS state
    _bitstream.writeBits(st0, 32);

    if (_order == 0)
        _bitstream.writeBits(st1, 32);

    if (p != p0) {
        // Write encoded data to bitstream
        _bitstream.writeBits(&p[1], 8 * uint(p0 - p));
    }
}

// Compute chunk frequencies, cumulated frequencies and encode chunk header
int ANSRangeEncoder::rebuildStatistics(const byte block[], int end, int lr)
{
    Global::computeHistogram(block, end, _freqs, _order == 0, true);
    return updateFrequencies(_freqs, lr);
}