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
#include "../util.hpp" // Visual Studio min/max
#include "../Memory.hpp"
#include "FunctionFactory.hpp"
#include "LZCodec.hpp"

using namespace kanzi;
using namespace std;

LZCodec::LZCodec() THROW
{
    _delegate = new LZXCodec<false>();
}

LZCodec::LZCodec(Context& ctx) THROW
{
    const int lzType = ctx.getInt("lz", FunctionFactory<byte>::LZ_TYPE);

    if (lzType == FunctionFactory<byte>::LZP_TYPE) {
        _delegate = (Function<byte>*)new LZPCodec();
    } else if (lzType == FunctionFactory<byte>::LZX_TYPE) {
        _delegate = (Function<byte>*)new LZXCodec<true>();
    } else {
        _delegate = (Function<byte>*)new LZXCodec<false>();
    }
}

bool LZCodec::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count) THROW
{
    if (count == 0)
        return true;

    if (input._array == output._array)
        return false;

    return _delegate->forward(input, output, count);
}

bool LZCodec::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count) THROW
{
    if (count == 0)
        return true;

    if (input._array == output._array)
        return false;

    return _delegate->inverse(input, output, count);
}

template <bool T>
bool LZXCodec<T>::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("LZ codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("LZ codec: Invalid output block");

    if (input._array == output._array)
        return false;

    if (output._length < getMaxEncodedLength(count))
        return false;

    // If too small, skip
    if (count < MIN_BLOCK_LENGTH)
        return false;

    if (_hashSize == 0) {
        _hashSize = (T == true) ? 1 << HASH_LOG2 : 1 << HASH_LOG1;
        delete[] _hashes;
        _hashes = new int32[_hashSize];
    }

    if (_bufferSize < max(count / 5, 256)) {
        _bufferSize = max(count / 5, 256);
        delete[] _mBuf;
        _mBuf = new byte[_bufferSize];
        delete[] _tkBuf;
        _tkBuf = new byte[_bufferSize];
    }

    memset(_hashes, 0, sizeof(int32) * _hashSize);
    const int srcEnd = count - 16 - 1;
    byte* dst = &output._array[output._index];
    byte* src = &input._array[input._index];
    const int maxDist = (srcEnd < 4 * MAX_DISTANCE1) ? MAX_DISTANCE1 : MAX_DISTANCE2;
    dst[8] = (maxDist == MAX_DISTANCE1) ? byte(0) : byte(1);
    int srcIdx = 0;
    int dstIdx = 9;
    int anchor = 0;
    int mIdx = 0;
    int tkIdx = 0;
    int repd = 0;

    while (srcIdx < srcEnd) {
        const int minRef = max(srcIdx - maxDist, 0);
        int32 h = hash(&src[srcIdx]);
        int ref = _hashes[h];
        _hashes[h] = srcIdx;
        int bestLen = 0;

        if (ref > minRef) {
            const int maxMatch = min(srcEnd - srcIdx, MAX_MATCH);
            bestLen = findMatch(src, srcIdx, ref, maxMatch);
        }

        // No good match ?
        if ((bestLen < MIN_MATCH) || ((bestLen == MIN_MATCH) && (srcIdx - ref >= MIN_MATCH_MIN_DIST))) {
            srcIdx++;
            continue;
        }

        // Check if better match at next position
        const int32 h2 = hash(&src[srcIdx + 1]);
        const int ref2 = _hashes[h2];
        _hashes[h2] = srcIdx + 1;
        int bestLen2 = 0;

        if (ref2 > minRef + 1) {
            const int maxMatch = min(srcEnd - srcIdx - 1, MAX_MATCH);
            bestLen2 = findMatch(src, srcIdx + 1, ref2, maxMatch);
        }

        // Select best match
        if (bestLen2 > bestLen + 1) {
            h = h2;
            ref = ref2;
            bestLen = bestLen2;
            srcIdx++;
        }

        // Emit token
        // Token: 3 bits litLen + 1 bit flag + 4 bits mLen (LLLFMMMM)
        // flag = if maxDist = (1<<17)-1, then highest bit of distance
        //        else 1 if dist needs 3 bytes (> 0xFFFF) and 0 otherwise
        const int mLen = bestLen - MIN_MATCH;
        const int d = srcIdx - ref;
        const int dist = (d == repd) ? 0 : d + 1;
        repd = d;
        const int token = ((dist > 0xFFFF) ? 0x10 : 0) | min(mLen, 0x0F);

        // Literals to process ?
        if (anchor == srcIdx) {
            _tkBuf[tkIdx++] = byte(token);
        }
        else {
            // Process literals
            const int litLen = srcIdx - anchor;

            // Emit literal length
            if (litLen >= 7) {
                if (litLen >= (1 << 24))
                    return false;

                _tkBuf[tkIdx++] = byte((7 << 5) | token);
                dstIdx += emitLength(&dst[dstIdx], litLen - 7);
            }
            else {
                _tkBuf[tkIdx++] = byte((litLen << 5) | token);
            }

            // Emit literals
            emitLiterals(&src[anchor], &dst[dstIdx], litLen);
            dstIdx += litLen;
        }

        // Emit match length
        if (mLen >= 15)
            mIdx += emitLength(&_mBuf[mIdx], mLen - 15);

        // Emit distance
        if ((maxDist == MAX_DISTANCE2) && (dist > 0xFFFF))
            _mBuf[mIdx++] = byte(dist >> 16);

        _mBuf[mIdx++] = byte(dist >> 8);
        _mBuf[mIdx++] = byte(dist);

        if (mIdx >= _bufferSize - 4) {
            // Expand match buffer
            byte* buf = new byte[_bufferSize << 1];
            memcpy(&buf[0], &_mBuf[0], _bufferSize);
            delete[] _mBuf;
            _bufferSize <<= 1;
            _mBuf = buf;
        }

        // Fill _hashes and update positions
        anchor = srcIdx + bestLen;
        srcIdx++;

        while (srcIdx < anchor) {
            _hashes[hash(&src[srcIdx])] = srcIdx;
            srcIdx++;
        }
    }

    if ((dstIdx + tkIdx + mIdx) > (output._length - output._index))
        return false;

    // Emit last literals
    const int litLen = count - anchor;

    if (litLen >= 7) {
        _tkBuf[tkIdx++] = byte(7 << 5);
        dstIdx += emitLength(&dst[dstIdx], litLen - 7);
    }
    else {
        _tkBuf[tkIdx++] = byte(litLen << 5);
    }

    memcpy(&dst[dstIdx], &src[anchor], litLen);
    dstIdx += litLen;

    // Emit buffers: literals + tokens + matches
    LittleEndian::writeInt32(&dst[0], dstIdx);
    LittleEndian::writeInt32(&dst[4], tkIdx);
    memcpy(&dst[dstIdx], &_tkBuf[0], tkIdx);
    dstIdx += tkIdx;
    memcpy(&dst[dstIdx], &_mBuf[0], mIdx);
    dstIdx += mIdx;
    input._index = count;
    output._index = dstIdx;
    return dstIdx < count;
}


template <bool T>
bool LZXCodec<T>::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("LZ codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("LZ codec: Invalid output block");

    if (input._array == output._array)
        return false;

    const int dstEnd = output._length - 16;
    byte* dst = &output._array[output._index];
    byte* src = &input._array[input._index];

    int tkIdx = int(LittleEndian::readInt32(&src[0]));
    int mIdx = tkIdx + int(LittleEndian::readInt32(&src[4]));

    if ((tkIdx < 0) || (mIdx < 0) || (tkIdx > count) || (mIdx > count))
        return false;

    const int srcEnd = tkIdx - 9;
    const int maxDist = (src[8] == byte(1)) ? MAX_DISTANCE2 : MAX_DISTANCE1;
    bool res = true;
    int srcIdx = 9;
    int dstIdx = 0;
    int repd = 0;

    while (true) {
        const int token = int(src[tkIdx++]);

        if (token >= 32) {
            // Get literal length
            int litLen = token >> 5;

            if (litLen == 7)
                litLen += readLength(src, srcIdx);

            // Emit literals
            emitLiterals(&src[srcIdx], &dst[dstIdx], litLen);
            srcIdx += litLen;
            dstIdx += litLen;

            if ((dstIdx > dstEnd) || (srcIdx >= srcEnd))
                break;
        }

        // Get match length
        int mLen = token & 0x0F;

        if (mLen == 15)
            mLen += readLength(src, mIdx);

        mLen += MIN_MATCH;
        const int mEnd = dstIdx + mLen;

        // Sanity check
        if (mEnd > dstEnd + 16) {
            res = false;
            goto exit;
        }

        // Get distance
        int d = (int(src[mIdx]) << 8) | int(src[mIdx + 1]);
        mIdx += 2;

        if ((token & 0x10) != 0) {
            d = (maxDist == MAX_DISTANCE1) ? d + 65536 : (d << 8) | int(src[mIdx++]);
        }

        const int dist = (d == 0) ? repd : d - 1;
        repd = dist;

        // Sanity check
        if ((dstIdx < dist) || (dist > maxDist)) {
            res = false;
            goto exit;
        }

        // Copy match
        if (dist >= 16) {
            int ref = dstIdx - dist;

            do {
                // No overlap
                memcpy(&dst[dstIdx], &dst[ref], 16);
                ref += 16;
                dstIdx += 16;
            } while (dstIdx < mEnd);
        }
        else {
            const int ref = dstIdx - dist;

            for (int i = 0; i < mLen; i++)
                dst[dstIdx + i] = dst[ref + i];
        }

        dstIdx = mEnd;
    }

exit:
    output._index = dstIdx;
    input._index = mIdx;
    return res && (srcIdx == srcEnd + 9);
}

bool LZPCodec::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("LZP codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("LZP codec: Invalid output block");

    if (input._array == output._array)
        return false;

    if (output._length < getMaxEncodedLength(count))
        return false;

    // If too small, skip
    if (count < MIN_BLOCK_LENGTH)
        return false;

    byte* dst = &output._array[output._index];
    byte* src = &input._array[input._index];
    const int srcEnd = count - 8;
    const int dstEnd = output._length - 4;

    if (_hashSize == 0) {
        _hashSize = 1 << HASH_LOG;
        delete[] _hashes;
        _hashes = new int32[_hashSize];
    }

    memset(_hashes, 0, sizeof(int32) * _hashSize);
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    int32 ctx = LittleEndian::readInt32(&src[0]);
    int srcIdx = 4;
    int dstIdx = 4;
    int minRef = 4;

    while ((srcIdx < srcEnd) && (dstIdx < dstEnd)) {
        const uint32 h = (HASH_SEED * ctx) >> HASH_SHIFT;
        const int32 ref = _hashes[h];
        _hashes[h] = srcIdx;
        int bestLen = 0;

        // Find a match
        if ((ref > minRef) && (LZCodec::sameInts(src, ref, srcIdx) == true)) {
            const int maxMatch = srcEnd - srcIdx;
            bestLen = 4;

            while ((bestLen < maxMatch) && (LZCodec::sameInts(src, ref + bestLen, srcIdx + bestLen) == true))
                bestLen += 4;

            while ((bestLen < maxMatch) && (src[ref + bestLen] == src[srcIdx + bestLen]))
                bestLen++;
        }

        // No good match ?
        if (bestLen < MIN_MATCH) {
            const int val = int32(src[srcIdx]);
            ctx = (ctx << 8) | val;
            dst[dstIdx++] = src[srcIdx++];

            if (ref != 0) {
                if (val == MATCH_FLAG)
                    dst[dstIdx++] = byte(0xFF);

                if (minRef < bestLen)
                    minRef = srcIdx + bestLen;
            }

            continue;
        }

        srcIdx += bestLen;
        ctx = LittleEndian::readInt32(&src[srcIdx - 4]);
        dst[dstIdx++] = byte(MATCH_FLAG);
        bestLen -= MIN_MATCH;

        // Emit match length
        while (bestLen >= 254) {
            bestLen -= 254;
            dst[dstIdx++] = byte(0xFE);

            if (dstIdx >= dstEnd)
                break;
        }

        dst[dstIdx++] = byte(bestLen);
    }

    while ((srcIdx < srcEnd + 8) && (dstIdx < dstEnd)) {
        const uint32 h = (HASH_SEED * ctx) >> HASH_SHIFT;
        const int ref = _hashes[h];
        _hashes[h] = srcIdx;
        const int val = int32(src[srcIdx]);
        ctx = (ctx << 8) | val;
        dst[dstIdx++] = src[srcIdx++];

        if ((ref != 0) && (val == MATCH_FLAG) && (dstIdx < dstEnd))
            dst[dstIdx++] = byte(0xFF);
    }

    input._index = srcIdx;
    output._index = dstIdx;
    return (srcIdx == count) && (dstIdx < (count - (count >> 6)));
}

bool LZPCodec::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("LZP codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("LZP codec: Invalid output block");

    if (input._array == output._array)
        return false;

    if (count < 4)
        return false;

    const int srcEnd = count;
    byte* dst = &output._array[output._index];
    byte* src = &input._array[input._index];

    if (_hashSize == 0) {
        _hashSize = 1 << HASH_LOG;
        delete[] _hashes;
        _hashes = new int32[_hashSize];
    }

    memset(_hashes, 0, sizeof(int32) * _hashSize);
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    int32 ctx = LittleEndian::readInt32(&dst[0]);
    int srcIdx = 4;
    int dstIdx = 4;

    while (srcIdx < srcEnd) {
        const int32 h = (HASH_SEED * ctx) >> HASH_SHIFT;
        const int32 ref = _hashes[h];
        _hashes[h] = dstIdx;

        if ((ref == 0) || (src[srcIdx] != byte(MATCH_FLAG))) {
            dst[dstIdx] = src[srcIdx];
            ctx = (ctx << 8) | int32(dst[dstIdx]);
            srcIdx++;
            dstIdx++;
            continue;
        }

        srcIdx++;

        if (src[srcIdx] == byte(0xFF)) {
            dst[dstIdx] = byte(MATCH_FLAG);
            ctx = (ctx << 8) | int32(MATCH_FLAG);
            srcIdx++;
            dstIdx++;
            continue;
        }

        int mLen = MIN_MATCH;

        while ((srcIdx < srcEnd) && (src[srcIdx] == byte(0xFE))) {
            srcIdx++;
            mLen += 254;
        }

        if (srcIdx >= srcEnd)
            break;

        mLen += int(src[srcIdx++]);

        for (int i = 0; i < mLen; i++)
            dst[dstIdx + i] = dst[ref + i];

        dstIdx += mLen;
        ctx = LittleEndian::readInt32(&dst[dstIdx - 4]);
    }

    input._index = srcIdx;
    output._index = dstIdx;
    return srcIdx == srcEnd;
}
