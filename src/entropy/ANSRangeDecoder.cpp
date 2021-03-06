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
#include "ANSRangeDecoder.hpp"
#include "EntropyUtils.hpp"

using namespace kanzi;
using namespace std;

// The chunk size indicates how many bytes are encoded (per block) before
// resetting the frequency stats.
ANSRangeDecoder::ANSRangeDecoder(InputBitStream& bitstream, int order, int chunkSize) THROW : _bitstream(bitstream)
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

    _chunkSize = min(chunkSize << (8 * order), MAX_CHUNK_SIZE);
    _order = order;
    const int dim = 255 * order + 1;
    _alphabet = new uint[dim * 256];
    _freqs = new uint[dim * 256];
    _symbols = new ANSDecSymbol[dim * 256];
    _buffer = new byte[0];
    _bufferSize = 0;
    _f2s = new uint8[0];
    _f2sSize = 0;
    _logRange = 9;
}

ANSRangeDecoder::~ANSRangeDecoder()
{
    _dispose();
    delete[] _buffer;
    delete[] _symbols;
    delete[] _f2s;
    delete[] _freqs;
    delete[] _alphabet;
}

int ANSRangeDecoder::decodeHeader(uint frequencies[])
{
    _logRange = int(8 + _bitstream.readBits(3));

    if ((_logRange < 8) || (_logRange > 16)) {
        stringstream ss;
        ss << "Invalid bitstream: range = " << _logRange << " (must be in [8..16])";
        throw BitStreamException(ss.str(), BitStreamException::INVALID_STREAM);
    }

    int res = 0;
    const int dim = 255 * _order + 1;
    const int scale = 1 << _logRange;

    if (_f2sSize < dim * scale) {
        delete[] _f2s;
        _f2sSize = dim * scale;
        _f2s = new uint8[_f2sSize];
    }

    for (int k = 0; k < dim; k++) {
        uint* f = &frequencies[k << 8];
        uint* curAlphabet = &_alphabet[k << 8];
        int alphabetSize = EntropyUtils::decodeAlphabet(_bitstream, curAlphabet);

        if (alphabetSize == 0)
            continue;

        if (alphabetSize != 256)
            memset(f, 0, sizeof(uint) * 256);

        const int chkSize = (alphabetSize >= 64) ? 8 : 6;
        int sum = 0;
        int llr = 3;

        while (uint(1 << llr) <= _logRange)
            llr++;

        // Decode all frequencies (but the first one) by chunks
        for (int i = 1; i < alphabetSize; i += chkSize) {
            // Read frequencies size for current chunk
            const int logMax = int(_bitstream.readBits(llr));

            if ((1 << logMax) > scale) {
                stringstream ss;
                ss << "Invalid bitstream: incorrect frequency size ";
                ss << logMax << " in ANS range decoder";
                throw BitStreamException(ss.str(), BitStreamException::INVALID_STREAM);
            }

            const int endj = min(i + chkSize, alphabetSize);

            // Read frequencies
            for (int j = i; j < endj; j++) {
                const int freq = (logMax == 0) ? 1 : int(_bitstream.readBits(logMax) + 1);

                if ((freq < 0) || (freq >= scale)) {
                    stringstream ss;
                    ss << "Invalid bitstream: incorrect frequency " << freq;
                    ss << " for symbol '" << curAlphabet[j] << "' in ANS range decoder";
                    throw BitStreamException(ss.str(), BitStreamException::INVALID_STREAM);
                }

                f[curAlphabet[j]] = uint(freq);
                sum += freq;
            }
        }

        // Infer first frequency
        if (scale <= sum) {
            stringstream ss;
            ss << "Invalid bitstream: incorrect frequency " << frequencies[_alphabet[0]];
            ss << " for symbol '" << _alphabet[0] << "' in ANS range decoder";
            throw BitStreamException(ss.str(), BitStreamException::INVALID_STREAM);
        }

        f[curAlphabet[0]] = uint(scale - sum);
        sum = 0;
        ANSDecSymbol* symb = &_symbols[k << 8];
        uint8* freq2sym = &_f2s[k << _logRange];

        // Create reverse mapping
        for (int i = 0; i < 256; i++) {
            if (f[i] == 0)
                continue;

            for (int j = f[i] - 1; j >= 0; j--)
                freq2sym[sum + j] = uint8(i);

            symb[i].reset(sum, f[i], _logRange);
            sum += f[i];
        }

        res += alphabetSize;
    }

    return res;
}

int ANSRangeDecoder::decode(byte block[], uint blkptr, uint count)
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
        const int alphabetSize = decodeHeader(_freqs);

        if (alphabetSize == 0)
            return startChunk - blkptr;

        if ((_order == 0) && (alphabetSize == 1)) {
            // Shortcut for chunks with only one symbol
            memset(&block[startChunk], _alphabet[0], sizeChunk);
        } else {
            decodeChunk(&block[startChunk], sizeChunk);
        }

        startChunk += sizeChunk;
    }

    return count;
}

void ANSRangeDecoder::decodeChunk(byte block[], int end)
{
    // Read chunk size
    const int sz = int(EntropyUtils::readVarInt(_bitstream) & (MAX_CHUNK_SIZE - 1));

    // Read initial ANS state
    int st0 = int(_bitstream.readBits(32));
    int st1 = (_order == 0) ? int(_bitstream.readBits(32)) : 0;

    // Read bit buffer
    if (sz != 0)
        _bitstream.readBits(&_buffer[0], 8 * sz);

    byte* p = &_buffer[0];
    const int mask = (1 << _logRange) - 1;

    if (_order == 0) {
        const int end2 = (end & -2) - 1;

        for (int i = 0; i < end2; i += 2) {
            const uint8 cur1 = _f2s[st1 & mask];
            block[i] = byte(cur1);
            st1 = decodeSymbol(p, st1, _symbols[cur1], mask);
            const uint8 cur0 = _f2s[st0 & mask];
            block[i + 1] = byte(cur0);
            st0 = decodeSymbol(p, st0, _symbols[cur0], mask);
        }

        if ((end & 1) != 0)
            block[end - 1] = _buffer[sz - 1];
    }
    else {
        uint8 prv = 0;
        ANSDecSymbol* symbols = &_symbols[0];

        for (int i = 0; i < end; i++) {
            prefetchRead(symbols);
            uint8* f2s = &_f2s[(prv << _logRange)];
            prefetchRead(f2s);
            const uint8 cur = f2s[st0 & mask];
            block[i] = byte(cur);
            st0 = decodeSymbol(p, st0, symbols[cur], mask);
            prv = cur;
            symbols = &_symbols[prv << 8];
        }
    }
}
