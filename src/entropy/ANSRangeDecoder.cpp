/*
Copyright 2011-2025 Frederic Langlet
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

#include <algorithm>
#include <cstring>
#include <sstream>
#include "../BitStreamException.hpp"
#include "ANSRangeDecoder.hpp"
#include "EntropyUtils.hpp"

using namespace kanzi;
using namespace std;

const uint ANSRangeDecoder::ANS_TOP = 1 << 15; // max possible for ANS_TOP=1<<23
const int ANSRangeDecoder::DEFAULT_ANS0_CHUNK_SIZE = 16384;
const int ANSRangeDecoder::DEFAULT_LOG_RANGE = 12;
const int ANSRangeDecoder::MIN_CHUNK_SIZE = 1024;
const int ANSRangeDecoder::MAX_CHUNK_SIZE = 1 << 27; // 8*MAX_CHUNK_SIZE must not over


// The chunk size indicates how many bytes are encoded (per block) before
// resetting the frequency stats.
ANSRangeDecoder::ANSRangeDecoder(InputBitStream& bitstream, int order, int chunkSize) : _bitstream(bitstream)
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
    _freqs = new uint[dim * 256];
    _symbols = new ANSDecSymbol[dim * 256];
    _buffer = nullptr;
    _bufferSize = 0;
    _f2s = nullptr;
    _f2sSize = 0;
    _logRange = DEFAULT_LOG_RANGE;
}

ANSRangeDecoder::~ANSRangeDecoder()
{
    _dispose();

    if (_buffer != nullptr)
       delete[] _buffer;

    if (_f2s != nullptr)
       delete[] _f2s;

    delete[] _freqs;
    delete[] _symbols;
}

int ANSRangeDecoder::decodeHeader(uint frequencies[], uint alphabet[])
{
    _logRange = int(8 + _bitstream.readBits(3));

    if ((_logRange < 8) || (_logRange > 16)) {
        stringstream ss;
        ss << "Invalid bitstream: range = " << _logRange << " (must be in [8..16])";
        throw BitStreamException(ss.str(), BitStreamException::INVALID_STREAM);
    }

    int res = 0;
    const int dim = 255 * _order + 1;

    if (_f2sSize < (dim << _logRange)) {
        if (_f2s != nullptr)
           delete[] _f2s;

        _f2sSize = dim << _logRange;
        _f2s = new uint8[_f2sSize];
    }

    const uint scale = 1 << _logRange;
    int llr = 3;

    while (uint(1 << llr) <= _logRange)
        llr++;

    for (int k = 0; k < dim; k++) {
        const int alphabetSize = EntropyUtils::decodeAlphabet(_bitstream, alphabet);

        if (alphabetSize == 0)
            continue;

        uint* f = &frequencies[k << 8];

        if (alphabetSize != 256)
            memset(f, 0, sizeof(uint) * 256);

        const int chkSize = (alphabetSize >= 64) ? 8 : 6;
        uint sum = 0;

        // Decode all frequencies (but the first one) by chunks
        for (int i = 1; i < alphabetSize; i += chkSize) {
            // Read frequencies size for current chunk
            const uint logMax = uint(_bitstream.readBits(llr));

            if (logMax > _logRange) {
                stringstream ss;
                ss << "Invalid bitstream: incorrect frequency size ";
                ss << logMax << " in ANS range decoder";
                throw BitStreamException(ss.str(), BitStreamException::INVALID_STREAM);
            }

            const int endj = min(i + chkSize, alphabetSize);

            // Read frequencies
            for (int j = i; j < endj; j++) {
                const uint freq = (logMax == 0) ? 1 : uint(_bitstream.readBits(logMax) + 1);

                if (freq >= scale) {
                    stringstream ss;
                    ss << "Invalid bitstream: incorrect frequency " << freq;
                    ss << " for symbol '" << alphabet[j] << "' in ANS range decoder";
                    throw BitStreamException(ss.str(), BitStreamException::INVALID_STREAM);
                }

                f[alphabet[j]] = freq;
                sum += freq;
            }
        }

        // Infer first frequency
        if (scale <= sum) {
            stringstream ss;
            ss << "Invalid bitstream: incorrect frequency " << frequencies[alphabet[0]];
            ss << " for symbol '" << alphabet[0] << "' in ANS range decoder";
            throw BitStreamException(ss.str(), BitStreamException::INVALID_STREAM);
        }

        f[alphabet[0]] = uint(scale - sum);
        sum = 0;
        ANSDecSymbol* symb = &_symbols[k << 8];
        uint8* freq2sym = &_f2s[k << _logRange];

        // Create reverse mapping
        for (int i = 0; i < 256; i++) {
            if (f[i] == 0)
                continue;

            memset(&freq2sym[sum], i, size_t(f[i]));
            symb[i].reset(sum, f[i], _logRange);
            sum += f[i];
        }

        res += alphabetSize;
    }

    return res;
}

int ANSRangeDecoder::decode(byte block[], uint blkptr, uint count)
{
    if (count <= 32) {
        _bitstream.readBits(&block[blkptr], 8 * count);
        return count;
    }

    const uint minBufSize = 2 * uint(_chunkSize);

    if (_bufferSize < minBufSize) {
        if (_buffer != nullptr)
            delete[] _buffer;

        _bufferSize = minBufSize;
        _buffer = new byte[_bufferSize];
    }

    const uint end = blkptr + count;
    uint startChunk = blkptr;
    uint alphabet[256];

    while (startChunk < end) {
        const uint sizeChunk = min(uint(_chunkSize), end - startChunk);
        const int alphabetSize = decodeHeader(_freqs, alphabet);

        if (alphabetSize == 0)
            return startChunk - blkptr;

        if ((_order == 0) && (alphabetSize == 1)) {
            // Shortcut for chunks with only one symbol
            memset(&block[startChunk], alphabet[0], size_t(sizeChunk));
        } else {
            if (decodeChunk(&block[startChunk], sizeChunk) == false)
                break;
        }

        startChunk += sizeChunk;
    }

    return count;
}

bool ANSRangeDecoder::decodeChunk(byte block[], uint count)
{
    // Read chunk size
    const uint sz = uint(EntropyUtils::readVarInt(_bitstream));

    if (sz >= MAX_CHUNK_SIZE)
       return false;

    // Read initial ANS states
    uint st0 = uint(_bitstream.readBits(32));
    uint st1 = uint(_bitstream.readBits(32));
    uint st2 = uint(_bitstream.readBits(32));
    uint st3 = uint(_bitstream.readBits(32));

    if (count == 0)
        return true;

    // Read encoded data from bitstream
    memset(_buffer, 0, _bufferSize);
    _bitstream.readBits(&_buffer[0], 8 * sz);
    byte* p = &_buffer[0];

    const int mask = (1 << _logRange) - 1;
    const int count4 = count & -4;

    if (_order == 0) {
        for (int i = 0; i < count4; i += 4) {
            const uint8 cur3 = _f2s[st3 & mask];
            block[i] = byte(cur3);
            st3 = decodeSymbol(p, st3, _symbols[cur3], mask);
            const uint8 cur2 = _f2s[st2 & mask];
            block[i + 1] = byte(cur2);
            st2 = decodeSymbol(p, st2, _symbols[cur2], mask);
            const uint8 cur1 = _f2s[st1 & mask];
            block[i + 2] = byte(cur1);
            st1 = decodeSymbol(p, st1, _symbols[cur1], mask);
            const uint8 cur0 = _f2s[st0 & mask];
            block[i + 3] = byte(cur0);
            st0 = decodeSymbol(p, st0, _symbols[cur0], mask);
        }
    }
    else {
        const int quarter = count4 >> 2;
        int i0 = 0;
        int i1 = 1 * quarter;
        int i2 = 2 * quarter;
        int i3 = 3 * quarter;
        int prv0 = 0, prv1 = 0, prv2 = 0, prv3 = 0;

        for ( ; i0 < quarter; i0++, i1++, i2++, i3++) {
            const uint8 cur3 = _f2s[(prv3 << _logRange) + (st3 & mask)];
            const uint8 cur2 = _f2s[(prv2 << _logRange) + (st2 & mask)];
            const uint8 cur1 = _f2s[(prv1 << _logRange) + (st1 & mask)];
            const uint8 cur0 = _f2s[(prv0 << _logRange) + (st0 & mask)];
            st3 = decodeSymbol(p, st3, _symbols[(prv3 << 8) | cur3], mask);
            st2 = decodeSymbol(p, st2, _symbols[(prv2 << 8) | cur2], mask);
            st1 = decodeSymbol(p, st1, _symbols[(prv1 << 8) | cur1], mask);
            st0 = decodeSymbol(p, st0, _symbols[(prv0 << 8) | cur0], mask);
            block[i3] = byte(cur3);
            block[i2] = byte(cur2);
            block[i1] = byte(cur1);
            block[i0] = byte(cur0);
            prv3 = cur3;
            prv2 = cur2;
            prv1 = cur1;
            prv0 = cur0;
        }
    }

    for (uint i = count4; i < count; i++)
        block[i] = *p++;

    return true;
}
