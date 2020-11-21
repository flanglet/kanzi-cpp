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

LZCodec::LZCodec() THROW
{
    _delegate = new LZXCodec();
}

LZCodec::LZCodec(Context& ctx) THROW
{
    int lzpType = ctx.getInt("lz", FunctionFactory<byte>::LZ_TYPE);
    _delegate = (lzpType == FunctionFactory<byte>::LZP_TYPE) ? (Function<byte>*)new LZPCodec() : 
       (Function<byte>*)new LZXCodec();
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

int LZXCodec::emitLastLiterals(const byte src[], byte dst[], int litLen)
{
    int dstIdx = 1;

    if (litLen >= 7) {
        dst[0] = byte(7 << 5);
        dstIdx += emitLength(&dst[1], litLen - 7);
    }
    else {
        dst[0] = byte(litLen << 5);
    }

    memcpy(&dst[dstIdx], src, litLen);
    return dstIdx + litLen;
}

bool LZXCodec::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count)
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

    const int srcEnd = count - 16;
    byte* dst = &output._array[output._index];
    byte* src = &input._array[input._index];

    if (_hashSize == 0) {
        _hashSize = 1 << HASH_LOG;
        delete[] _hashes;
        _hashes = new int32[_hashSize];
    }

    if (_bufferSize < max(count / 5, 256)) {
        _bufferSize = max(count / 5, 256);
        delete[] _buffer;
        _buffer = new byte[_bufferSize];
    }

    memset(_hashes, 0, sizeof(int32) * _hashSize);
    const int maxDist = (srcEnd < 4 * MAX_DISTANCE1) ? MAX_DISTANCE1 : MAX_DISTANCE2;
    dst[4] = (maxDist == MAX_DISTANCE1) ? byte(0) : byte(1);
    int srcIdx = 0;
    int dstIdx = 5;
    int anchor = 0;
    int mIdx = 0;

    while (srcIdx < srcEnd) {
        const int minRef = max(srcIdx - maxDist, 0);
        const int32 h = hash(&src[srcIdx]);
        const int ref = _hashes[h];
        int bestLen = 0;

        // Find a match
        if ((ref > minRef) && (LZCodec::sameInts(src, ref, srcIdx) == true)) {
            const int maxMatch = min(srcEnd - srcIdx, MAX_MATCH);
            bestLen = 4;

            while ((bestLen + 4 < maxMatch) && (LZCodec::sameInts(src, ref + bestLen, srcIdx + bestLen) == true))
                bestLen += 4;

            while ((bestLen < maxMatch) && (src[ref + bestLen] == src[srcIdx + bestLen]))
                bestLen++;
        }

        // No good match ?
        if ((bestLen < MIN_MATCH) || ((bestLen == MIN_MATCH) && (srcIdx - ref >= MIN_MATCH_MIN_DIST))) {
            _hashes[h] = srcIdx;
            srcIdx++;
            continue;
        }

        // Emit token
        // Token: 3 bits litLen + 1 bit flag + 4 bits mLen (LLLFMMMM)
        // flag = if maxDist = (1<<17)-1, then highest bit of distance
        //        else 1 if dist needs 3 bytes (> 0xFFFF) and 0 otherwise
        const int mLen = bestLen - MIN_MATCH;
        const int dist = srcIdx - ref;
        const int token = ((dist > 0xFFFF) ? 0x10 : 0) | min(mLen, 0x0F);

        // Literals to process ?
        if (anchor == srcIdx) {
            dst[dstIdx++] = byte(token);
        }
        else {
            // Process match
            const int litLen = srcIdx - anchor;

            // Emit literal length
            if (litLen >= 7) {
                dst[dstIdx++] = byte((7 << 5) | token);
                dstIdx += emitLength(&dst[dstIdx], litLen - 7);
            }
            else {
                dst[dstIdx++] = byte((litLen << 5) | token);
            }

            // Emit literals
            emitLiterals(&src[anchor], &dst[dstIdx], litLen);
            dstIdx += litLen;
        }

        // Emit match length
        if (mLen >= 15)
            mIdx += emitLength(&_buffer[mIdx], mLen - 15);

        // Emit distance
        if ((maxDist == MAX_DISTANCE2) && (dist > 0xFFFF))
            _buffer[mIdx++] = byte(dist >> 16);

        _buffer[mIdx++] = byte(dist >> 8);
        _buffer[mIdx++] = byte(dist);

        if (mIdx >= _bufferSize - 16) {
            // Expand match buffer
            byte* buf = new byte[_bufferSize << 1];
            memcpy(&buf[0], &_buffer[0], _bufferSize);
            delete[] _buffer;
            _bufferSize <<= 1;
            _buffer = buf;
        }

        // Fill _hashes and update positions
        anchor = srcIdx + bestLen;
        _hashes[h] = srcIdx;
        srcIdx++;

        while (srcIdx < anchor) {
            _hashes[hash(&src[srcIdx])] = srcIdx;
            srcIdx++;
        }
    }

    // Emit last literals
    dstIdx += emitLastLiterals(&src[anchor], &dst[dstIdx], srcEnd + 16 - anchor);
    LittleEndian::writeInt32(&dst[0], dstIdx);
    memcpy(&dst[dstIdx], &_buffer[0], mIdx);
    dstIdx += mIdx;
    input._index = srcEnd + 16;
    output._index = dstIdx;
    return true;
}

bool LZXCodec::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count)
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

    int mIdx = int(LittleEndian::readInt32(&src[0]));

    if (mIdx > count)
        return false;

    const int srcEnd = mIdx - 5;
    const int matchEnd = mIdx + count - 16;
    const int maxDist = (src[4] == byte(1)) ? MAX_DISTANCE2 : MAX_DISTANCE1;
    int dstIdx = 0;
    int srcIdx = 5;
    bool res = true;

    while (true) {
        const int token = int(src[srcIdx++]);

        if (token >= 32) {
            // Get literal length
            int litLen = token >> 5;

            if (litLen == 7) {
                while ((srcIdx < srcEnd) && (src[srcIdx] == byte(0xFF))) {
                    srcIdx++;
                    litLen += 0xFF;
                }

                litLen += int(src[srcIdx++]);
            }

            // Emit literals
            emitLiterals(&src[srcIdx], &dst[dstIdx], litLen);
            srcIdx += litLen;
            dstIdx += litLen;

            if ((dstIdx > dstEnd) || (srcIdx > srcEnd))
                break;
        }

        // Get match length
        int mLen = token & 0x0F;

        if (mLen == 15) {
            while ((mIdx < matchEnd) && (src[mIdx] == byte(0xFF))) {
                mIdx++;
                mLen += 0xFF;
            }

            if (mIdx < matchEnd)
                mLen += int(src[mIdx++]);
        }

        mLen += MIN_MATCH;
        const int mEnd = dstIdx + mLen;

        // Sanity check
        if (mEnd > dstEnd + 16) {
            res = false;
            goto exit;
        }

        // Get distance
        int dist = (int(src[mIdx]) << 8) | int(src[mIdx + 1]);
        mIdx += 2;

        if ((token & 0x10) != 0) {
            dist = (maxDist == MAX_DISTANCE1) ? dist + 65536 : (dist << 8) | int(src[mIdx++]);
        }

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
    return res && (srcIdx == srcEnd + 5);
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
