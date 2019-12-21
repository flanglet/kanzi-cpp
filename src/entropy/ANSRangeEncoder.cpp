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

#include <sstream>
#include "ANSRangeEncoder.hpp"
#include "EntropyUtils.hpp"
#include "../Global.hpp"

using namespace kanzi;

// The chunk size indicates how many bytes are encoded (per block) before
// resetting the frequency stats. 0 means that frequencies calculated at the
// beginning of the block apply to the whole block.
ANSRangeEncoder::ANSRangeEncoder(OutputBitStream& bitstream, int order, int chunkSize, int logRange) THROW : _bitstream(bitstream)
{
    if ((order != 0) && (order != 1))
        throw invalid_argument("ANS Codec: The order must be 0 or 1");

    if ((chunkSize != 0) && (chunkSize != -1) && (chunkSize < 1024))
        throw invalid_argument("ANS Codec: The chunk size must be at least 1024");

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

    if (chunkSize == -1)
        chunkSize = DEFAULT_ANS0_CHUNK_SIZE << (8 * order);

    _order = order;
    const int32 dim = 255 * order + 1;
    _alphabet = new uint[dim * 256];
    _freqs = new uint[dim * 257]; // freqs[x][256] = total(freqs[x][0..255])
    _symbols = new ANSEncSymbol[dim * 256];
    _buffer = new byte[0];
    _bufferSize = 0;
    _logRange = logRange;
    _chunkSize = chunkSize;
}

ANSRangeEncoder::~ANSRangeEncoder()
{
    dispose();
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
    EntropyUtils::encodeAlphabet(_bitstream, alphabet, 256, alphabetSize);

    if (alphabetSize == 0)
        return true;

    const int chkSize = (alphabetSize >= 64) ? 12 : 6;
    int llr = 3;

    while (1 << llr <= lr)
        llr++;

    // Encode all frequencies (but the first one) by chunks
    for (int i = 1; i < alphabetSize; i += chkSize) {
        uint max = 0;
        uint logMax = 1;
        const int endj = (i + chkSize < alphabetSize) ? i + chkSize : alphabetSize;

        // Search for max frequency log size in next chunk
        for (int j = i; j < endj; j++) {
            if (frequencies[alphabet[j]] > max)
                max = frequencies[alphabet[j]];
        }

        while (uint(1 << logMax) <= max)
            logMax++;

        _bitstream.writeBits(logMax - 1, llr);

        // Write frequencies
        for (int j = i; j < endj; j++)
            _bitstream.writeBits(frequencies[alphabet[j]], logMax);
    }

    return true;
}

// Dynamically compute the frequencies for every chunk of data in the block
int ANSRangeEncoder::encode(byte block[], uint blkptr, uint len)
{
    if (len == 0)
        return 0;

    const int end = blkptr + len;
    int sz = (_chunkSize == 0) ? len : _chunkSize;

    if (sz > MAX_CHUNK_SIZE)
        sz = MAX_CHUNK_SIZE;

    int startChunk = blkptr;

    if (_bufferSize < uint(sz + (sz >> 3))) {
        delete[] _buffer;
        _bufferSize = uint(sz + (sz >> 3));
        _buffer = new byte[_bufferSize];
    }

    while (startChunk < end) {
        const int sizeChunk = min(sz, end - startChunk);
        int lr = _logRange;

        // Lower log range if the size of the data chunk is small
        while ((lr > 8) && (1 << lr > sizeChunk))
            lr--;

        rebuildStatistics(&block[startChunk], sizeChunk, lr);
        encodeChunk(&block[startChunk], sizeChunk);
        startChunk += sizeChunk;
    }

    return len;
}

void ANSRangeEncoder::encodeChunk(byte block[], int end)
{
    int st = ANS_TOP;
    const uint8* data = reinterpret_cast<uint8*>(&block[0]);
    byte* p0 = &_buffer[_bufferSize - 1];
    byte* p = p0;

    if (_order == 0) {
        for (int i = end - 1; i >= 0; i--) {
            st = encodeSymbol(p, st, _symbols[data[i]]);
        }
    }
    else { // order 1
        int prv = int(block[end - 1]) & 0xFF;

        for (int i = end - 2; i >= 0; i--) {
            const int cur = int(data[i]);
            st = encodeSymbol(p, st, _symbols[(cur << 8) | prv]);
            prv = cur;
        }

        // Last symbol
        st = encodeSymbol(p, st, _symbols[prv]);
    }

    // Write chunk size
    EntropyUtils::writeVarInt(_bitstream, uint32(p0 - p));

    // Write final ANS state
    _bitstream.writeBits(st, 32);

    if (p != p0) {
        // Write encoded data to bitstream
        _bitstream.writeBits(&p[1], 8 * uint(p0 - p));
    }
}

// Compute chunk frequencies, cumulated frequencies and encode chunk header
int ANSRangeEncoder::rebuildStatistics(byte block[], int end, int lr)
{
    Global::computeHistogram(block, end, _freqs, _order == 0, true);
    return updateFrequencies(_freqs, lr);
}

