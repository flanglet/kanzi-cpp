/*
Copyright 2011-2022 Frederic Langlet
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

#include "LZCodec.hpp"
#include "../Memory.hpp"
#include "../util.hpp" // Visual Studio min/max
#include "TransformFactory.hpp"

using namespace kanzi;
using namespace std;

LZCodec::LZCodec() THROW
{
    _delegate = new LZXCodec<false>();
}

LZCodec::LZCodec(Context& ctx) THROW
{
    const int lzType = ctx.getInt("lz", TransformFactory<byte>::LZ_TYPE);

    if (lzType == TransformFactory<byte>::LZP_TYPE) {
        _delegate = (Transform<byte>*)new LZPCodec(ctx);
    } else if (lzType == TransformFactory<byte>::LZX_TYPE) {
        _delegate = (Transform<byte>*)new LZXCodec<true>(ctx);
    } else {
        _delegate = (Transform<byte>*)new LZXCodec<false>(ctx);
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
        delete[] _mLenBuf;
        _mLenBuf = new byte[_bufferSize];
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
    dst[12] = (maxDist == MAX_DISTANCE1) ? byte(0) : byte(1);
    int mm = MIN_MATCH1;

    if (_pCtx != nullptr) {
        Global::DataType dt = (Global::DataType)_pCtx->getInt("dataType", Global::UNDEFINED);

        if (dt == Global::DNA) {
            // Longer min match for DNA input
            mm = MIN_MATCH2;
            dst[12] |= byte(2);
        }
    }

    const int minMatch = mm;
    const int dThreshold = (maxDist == MAX_DISTANCE1) ? maxDist + 1 : 1 << 16;
    int srcIdx = 0;
    int dstIdx = 13;
    int anchor = 0;
    int mIdx = 0;
    int mLenIdx = 0;
    int tkIdx = 0;
    int repd0 = count;
    int repd1 = 0;

    while (srcIdx < srcEnd) {
        const int minRef = max(srcIdx - maxDist, 0);
        const int32 h0 = hash(&src[srcIdx]);
        const int32 h1 = hash(&src[srcIdx + 1]);
        int ref = srcIdx + 1 - repd0;
        int bestLen = 0;

        if (ref > minRef) {
            // Check repd0 first
            if (memcmp(&src[srcIdx + 1], &src[ref], 4) == 0)
                bestLen = findMatch(src, srcIdx + 1, ref, min(srcEnd - srcIdx - 1, MAX_MATCH));
        }

        if (bestLen < minMatch) {
            ref = _hashes[h0];
            _hashes[h0] = srcIdx;

            if (ref <= minRef) {
                srcIdx++;
                continue;
            }

            if (memcmp(&src[srcIdx], &src[ref], 4) == 0)
                bestLen = findMatch(src, srcIdx, ref, min(srcEnd - srcIdx, MAX_MATCH));
        } else {
            srcIdx++;
            _hashes[h0] = srcIdx;
        }

        // No good match ?
        if ((bestLen < minMatch) || ((bestLen == minMatch) && (srcIdx - ref >= MIN_MATCH_MIN_DIST) && (srcIdx - ref != repd0))) {
            srcIdx++;
            continue;
        }

        if (ref != srcIdx - repd0) {
            // Check if better match at next position
            const int ref1 = _hashes[h1];
            _hashes[h1] = srcIdx + 1;

            if (ref1 > minRef + 1) {
                int bestLen1 = findMatch(src, srcIdx + 1, ref1, min(srcEnd - srcIdx - 1, MAX_MATCH));

                // Select best match
                if ((bestLen1 > bestLen) || ((bestLen1 == bestLen) && (ref1 > ref + 1))) {
                    ref = ref1;
                    bestLen = bestLen1;
                    srcIdx++;
                }
            }
        }

        const int d = srcIdx - ref;
        int dist;

        if (d == repd0) {
            dist = 0;
        } else {
            dist = (d == repd1) ? 1 : d + 1;
            repd1 = repd0;
            repd0 = d;
        }

        // Emit token
        // Token: 3 bits litLen + 1 bit flag + 4 bits mLen (LLLFMMMM)
        // flag = if maxDist = MAX_DISTANCE1, then highest bit of distance
        //        else 1 if dist needs 3 bytes (> 0xFFFF) and 0 otherwise
        const int mLen = bestLen - minMatch;
        const int token = ((dist > 0xFFFF) ? 0x10 : 0x00) | min(mLen, 15);

        // Literals to process ?
        if (anchor == srcIdx) {
            _tkBuf[tkIdx++] = byte(token);
        } else {
            // Process literals
            const int litLen = srcIdx - anchor;

            // Emit literal length
            if (litLen >= 7) {
                if (litLen >= (1 << 24))
                    return false;

                _tkBuf[tkIdx++] = byte((7 << 5) | token);
                dstIdx += emitLength(&dst[dstIdx], litLen - 7);
            } else {
                _tkBuf[tkIdx++] = byte((litLen << 5) | token);
            }

            // Emit literals
            emitLiterals(&src[anchor], &dst[dstIdx], litLen);
            dstIdx += litLen;
        }

        // Emit match length
        if (mLen >= 15)
            mLenIdx += emitLength(&_mLenBuf[mLenIdx], mLen - 15);

        // Emit distance
        if (dist >= dThreshold)
            _mBuf[mIdx++] = byte(dist >> 16);

        _mBuf[mIdx++] = byte(dist >> 8);
        _mBuf[mIdx++] = byte(dist);

        if (mIdx >= _bufferSize - 8) {
            // Expand match buffer
            byte* mBuf = new byte[_bufferSize << 1];
            memcpy(&mBuf[0], &_mBuf[0], _bufferSize);
            delete[] _mBuf;
            _mBuf = mBuf;

            if (mLenIdx >= _bufferSize - 4) {
                byte* mLenBuf = new byte[_bufferSize << 1];
                memcpy(&mLenBuf[0], &_mLenBuf[0], _bufferSize);
                delete[] _mLenBuf;
                _mLenBuf = mLenBuf;
            }

            _bufferSize <<= 1;
        }

        // Fill _hashes and update positions
        anchor = srcIdx + bestLen;
        srcIdx++;

        while (srcIdx < anchor) {
            _hashes[hash(&src[srcIdx])] = srcIdx;
            srcIdx++;
        }
    }

    // Emit last literals
    const int litLen = count - anchor;

    if (dstIdx + litLen + tkIdx + mIdx >= output._index + count)
        return false;

    if (litLen >= 7) {
        _tkBuf[tkIdx++] = byte(7 << 5);
        dstIdx += emitLength(&dst[dstIdx], litLen - 7);
    } else {
        _tkBuf[tkIdx++] = byte(litLen << 5);
    }

    memcpy(&dst[dstIdx], &src[anchor], litLen);
    dstIdx += litLen;

    // Emit buffers: literals + tokens + matches
    LittleEndian::writeInt32(&dst[0], dstIdx);
    LittleEndian::writeInt32(&dst[4], tkIdx);
    LittleEndian::writeInt32(&dst[8], mIdx);
    memcpy(&dst[dstIdx], &_tkBuf[0], tkIdx);
    dstIdx += tkIdx;
    memcpy(&dst[dstIdx], &_mBuf[0], mIdx);
    dstIdx += mIdx;
    memcpy(&dst[dstIdx], &_mLenBuf[0], mLenIdx);
    dstIdx += mLenIdx;
    input._index = count;
    output._index = dstIdx;
    return true;
}

template <bool T>
bool LZXCodec<T>::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (count < 13)
        return false;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("LZ codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("LZ codec: Invalid output block");

    const int dstEnd = output._length - 16;
    byte* dst = &output._array[output._index];
    byte* src = &input._array[input._index];

    int tkIdx = int(LittleEndian::readInt32(&src[0]));
    int mIdx = tkIdx + int(LittleEndian::readInt32(&src[4]));
    int mLenIdx = mIdx + int(LittleEndian::readInt32(&src[8]));

    if ((tkIdx < 0) || (mIdx < 0) || (mLenIdx < 0) || (mLenIdx > count))
        return false;

    const int srcEnd = tkIdx - 13;
    const int maxDist = ((int(src[12]) & 1) == 0) ? MAX_DISTANCE1 : MAX_DISTANCE2;
    const int minMatch = ((int(src[12]) & 2) == 0) ? MIN_MATCH1 : MIN_MATCH2;
    bool res = true;
    int srcIdx = 13;
    int dstIdx = 0;
    int repd0 = 0;
    int repd1 = 0;

    while (true) {
        const int token = int(src[tkIdx++]);

        if (token >= 32) {
            // Get literal length
            const int litLen = (token >= 0xE0) ? 7 + readLength(src, srcIdx) : token >> 5;

            // Emit literals
            srcIdx += litLen;
            dstIdx += litLen;

            if ((srcIdx >= srcEnd) || (dstIdx >= dstEnd)) {
                memcpy(&dst[dstIdx - litLen], &src[srcIdx - litLen], litLen);
                break;
            }

            emitLiterals(&src[srcIdx - litLen], &dst[dstIdx - litLen], litLen);
        }

        // Get match length
        int mLen = token & 0x0F;

        if (mLen == 15)
            mLen += readLength(src, mLenIdx);

        mLen += minMatch;
        const int mEnd = dstIdx + mLen;

        // Get distance
        int d = (int(src[mIdx]) << 8) | int(src[mIdx + 1]);
        mIdx += 2;

        if ((token & 0x10) != 0) {
            d = (maxDist == MAX_DISTANCE1) ? d + 65536 : (d << 8) | int(src[mIdx++]);
        }

        int dist;

        if (d == 0) {
            dist = repd0;
        } else {
            dist = (d == 1) ? repd1 : d - 1;
            repd1 = repd0;
            repd0 = dist;
        }

        // Sanity check
        if ((dstIdx < dist) || (dist > maxDist) || (mEnd > dstEnd + 16)) {
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
        } else {
            const int ref = dstIdx - dist;

            for (int i = 0; i < mLen; i++)
                dst[dstIdx + i] = dst[ref + i];
        }

        dstIdx = mEnd;
    }

exit:
    output._index = dstIdx;
    input._index = mIdx;
    return res && (srcIdx == srcEnd + 13);
}

bool LZPCodec::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("LZP codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("LZP codec: Invalid output block");

    if (output._length < getMaxEncodedLength(count))
        return false;

    // If too small, skip
    if (count < MIN_BLOCK_LENGTH)
        return false;

    byte* dst = &output._array[output._index];
    byte* src = &input._array[input._index];
    const int srcEnd = count;
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

    while ((srcIdx < srcEnd - MIN_MATCH) && (dstIdx < dstEnd)) {
        const uint32 h = (HASH_SEED * ctx) >> HASH_SHIFT;
        const int32 ref = _hashes[h];
        _hashes[h] = srcIdx;
        int bestLen = 0;

        // Find a match
        if ((ref > minRef) && (memcmp(&src[ref + MIN_MATCH - 4], &src[srcIdx + MIN_MATCH - 4], 4) == 0))
            bestLen = findMatch(src, srcIdx, ref, srcEnd - srcIdx);

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

    while ((srcIdx < srcEnd) && (dstIdx < dstEnd)) {
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
            return false;

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
