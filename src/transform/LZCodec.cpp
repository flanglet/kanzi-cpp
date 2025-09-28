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

#include "LZCodec.hpp"
#include "../Memory.hpp"
#include "../util.hpp" // Visual Studio min/max
#include "TransformFactory.hpp"

using namespace kanzi;
using namespace std;

LZCodec::LZCodec()
{
    _delegate = new LZXCodec<false>();
}

LZCodec::LZCodec(Context& ctx)
{
    const int lzType = ctx.getInt("lz", TransformFactory<byte>::LZ_TYPE);

    if (lzType == TransformFactory<byte>::LZP_TYPE) {
        _delegate = new LZPCodec(ctx);
    }
    else if (lzType == TransformFactory<byte>::LZX_TYPE) {
        _delegate = new LZXCodec<true>(ctx);
    }
    else {
        _delegate = new LZXCodec<false>(ctx);
    }
}

bool LZCodec::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (input._array == output._array)
        return false;

    return _delegate->forward(input, output, count);
}

bool LZCodec::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (input._array == output._array)
        return false;

    return _delegate->inverse(input, output, count);
}


template<>
const uint LZXCodec<false>::HASH_SEED = 0x1E35A7BD;
template<>
const uint LZXCodec<false>::HASH_LOG = 16;
template<>
const uint LZXCodec<false>::HASH_RSHIFT = 64 - HASH_LOG;
template<>
const uint LZXCodec<false>::HASH_LSHIFT = 24;
template<>
const int LZXCodec<false>::MAX_DISTANCE1 = (1 << 16) - 2;
template<>
const int LZXCodec<false>::MAX_DISTANCE2 = (1 << 24) - 2;
template<>
const int LZXCodec<false>::MIN_MATCH4 = 4;
template<>
const int LZXCodec<false>::MIN_MATCH6 = 6;
template<>
const int LZXCodec<false>::MIN_MATCH9 = 9;
template<>
const int LZXCodec<false>::MAX_MATCH = 65535 + 254 + MIN_MATCH4;
template<>
const int LZXCodec<false>::MIN_BLOCK_LENGTH = 24;
template<>
const uint LZXCodec<true>::HASH_SEED = 0x1E35A7BD;
template<>
const uint LZXCodec<true>::HASH_LOG = 19;
template<>
const uint LZXCodec<true>::HASH_RSHIFT = 64 - HASH_LOG;
template<>
const uint LZXCodec<true>::HASH_LSHIFT = 24;
template<>
const int LZXCodec<true>::MAX_DISTANCE1 = (1 << 16) - 2;
template<>
const int LZXCodec<true>::MAX_DISTANCE2 = (1 << 24) - 2;
template<>
const int LZXCodec<true>::MIN_MATCH4 = 4;
template<>
const int LZXCodec<true>::MIN_MATCH6 = 6;
template<>
const int LZXCodec<true>::MIN_MATCH9 = 9;
template<>
const int LZXCodec<true>::MAX_MATCH = 65535 + 254 + MIN_MATCH4;
template<>
const int LZXCodec<true>::MIN_BLOCK_LENGTH = 24;



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
        _hashSize = 1 << HASH_LOG;

        if (_hashes != nullptr)
            delete[] _hashes;

        _hashes = new int32[_hashSize];
    }

    if (_bufferSize < max(count / 5, 256)) {
        _bufferSize = max(count / 5, 256);

        if (_mLenBuf != nullptr)
            delete[] _mLenBuf;

        _mLenBuf = new byte[_bufferSize];

        if (_mBuf != nullptr)
            delete[] _mBuf;

        _mBuf = new byte[_bufferSize];

        if (_tkBuf != nullptr)
            delete[] _tkBuf;

        _tkBuf = new byte[_bufferSize];
    }

    memset(_hashes, 0, sizeof(int32) * _hashSize);
    const int srcEnd = count - 16 - 1;
    const byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    const int maxDist = (srcEnd < 4 * MAX_DISTANCE1) ? MAX_DISTANCE1 : MAX_DISTANCE2;
    dst[12] = (maxDist == MAX_DISTANCE1) ? byte(0) : byte(1);
    int mm = MIN_MATCH4;

    if (_pCtx != nullptr) {
        Global::DataType dt = (Global::DataType)_pCtx->getInt("dataType", Global::UNDEFINED);

        if (dt == Global::DNA) {
            // Longer min match for DNA input
            mm = MIN_MATCH6;
        }
        else if (dt == Global::SMALL_ALPHABET) {
            return false;
        }
    }

    // dst[12] = 0000MMMD (4 bits + 3 bits minMatch + 1 bit max distance)
    dst[12] |= byte(((mm - 2) & 0x07) << 1); // minMatch in [2..9]
    const int minMatch = mm;
    int srcIdx = 0;
    int dstIdx = 13;
    int anchor = 0;
    int mIdx = 0;
    int mLenIdx = 0;
    int tkIdx = 0;
    int repd[] = { count, count };
    int repIdx = 0;
    int srcInc = 0;

    while (srcIdx < srcEnd) {
        int bestLen = 0;
        const int32 h0 = hash(&src[srcIdx]);
        const int ref0 = _hashes[h0];
        _hashes[h0] = srcIdx;
        const int srcIdx1 = srcIdx + 1;
        int ref = srcIdx1 - repd[repIdx];
        const int minRef = max(srcIdx - maxDist, 0);

        if ((ref > minRef) && (memcmp(&src[srcIdx1], &src[ref], 4) == 0)) {
            // Check repd first
            bestLen = findMatch(src, srcIdx1, ref, min(srcEnd - srcIdx1, MAX_MATCH));
        }

        if (bestLen < minMatch) {
            // Check match at position in hash table
            ref = ref0;

            if ((ref > minRef) && (memcmp(&src[srcIdx], &src[ref], 4) == 0)) {
                bestLen = findMatch(src, srcIdx, ref, min(srcEnd - srcIdx, MAX_MATCH));
            }

            // No good match ?
            if (bestLen < minMatch) {
                srcIdx = srcIdx + (srcInc >> 6) + 1;
                srcInc++;
                repIdx = 0;
                continue;
            }

            if ((srcIdx - ref != repd[0]) && (srcIdx - ref != repd[1])) {
                // Check if better match at next position
                const int32 h1 = hash(&src[srcIdx1]);
                const int ref1 = _hashes[h1];
                _hashes[h1] = srcIdx1;

                if ((ref1 > minRef + 1) && (memcmp(&src[srcIdx1 + bestLen - 3], &src[ref1 + bestLen - 3], 4) == 0)) {
                    const int bestLen1 = findMatch(src, srcIdx1, ref1, min(srcEnd - srcIdx1, MAX_MATCH));

                    // Select best match
                    if (bestLen1 >= bestLen) {
                        ref = ref1;
                        bestLen = bestLen1;
                        srcIdx = srcIdx1;
                    }
                }

                if (T == true) {
                   const int srcIdx2 = srcIdx + 2;
                   const int32 h2 = hash(&src[srcIdx2]);
                   const int ref2 = _hashes[h2];
                   _hashes[h2] = srcIdx2;

                   if ((ref2 > minRef + 2) && (memcmp(&src[srcIdx2 + bestLen - 3], &src[ref2 + bestLen - 3], 4) == 0)) {
                       const int bestLen2 = findMatch(src, srcIdx2, ref2, min(srcEnd - srcIdx2, MAX_MATCH));

                       // Select best match
                       if (bestLen2 >= bestLen) {
                           ref = ref2;
                           bestLen = bestLen2;
                           srcIdx = srcIdx2;
                       }
                    }
                }
            }

            // Extend backwards
            while ((srcIdx > anchor) && (ref > minRef) && (src[srcIdx - 1] == src[ref - 1])) {
                bestLen++;
                ref--;
                srcIdx--;
            }

            if (bestLen > MAX_MATCH) {
                ref += (bestLen - MAX_MATCH);
                srcIdx += (bestLen - MAX_MATCH);
                bestLen = MAX_MATCH;
            }
        }
        else {
            if ((bestLen >= MAX_MATCH) || (src[srcIdx] != src[ref - 1])) {
                srcIdx++;
                const int32 h1 = hash(&src[srcIdx]);
                _hashes[h1] = srcIdx;
            }
            else {
                bestLen++;
                ref--;
            }
        }

        // Emit match
        srcInc = 0;

        // Token: 3 bits litLen + 2 bits flag + 3 bits mLen (LLLFFMMM)
        //    or  3 bits litLen + 3 bits flag + 2 bits mLen (LLLFFFMM)
        // LLL : <= 7 --> LLL == literal length (if 7, remainder encoded outside of token)
        // MMM : <= 7 --> MMM == match length (if 7, remainder encoded outside of token)
        // MM  : <= 3 --> MM  == match length (if 3, remainder encoded outside of token)
        // FF = 01    --> 1 byte dist
        // FF = 10    --> 2 byte dist
        // FF = 11    --> 3 byte dist
        // FFF = 000  --> dist == repd0
        // FFF = 001  --> dist == repd1
        const int dist = srcIdx - ref;
        int token, mLenTh;

        if (dist == repd[0]) {
            token = 0x00;
            mLenTh = 3;
        }
        else if (dist == repd[1]) {
            token = 0x04;
            mLenTh = 3;
        }
        else {
            // Emit distance (since not repeat)
            if (dist >= 65536) {
                _mBuf[mIdx] = byte(dist >> 16);
                _mBuf[mIdx + 1] = byte(dist >> 8);
                mIdx += 2;
                token = 0x18;
            }
            else {
                _mBuf[mIdx] = byte(dist >> 8);
                const int inc = dist >= 256 ? 1 : 0;
                mIdx += inc;
                token = (inc + 1) << 3;
            }

            _mBuf[mIdx++] = byte(dist);
            mLenTh = 7;
        }

        const int mLen = bestLen - minMatch;

        // Emit match length
        if (mLen >= mLenTh) {
            token += mLenTh;
            mLenIdx += emitLength(&_mLenBuf[mLenIdx], mLen - mLenTh);
        }
        else {
            token += mLen;
        }

        repd[1] = repd[0];
        repd[0] = dist;
        repIdx = 1;
        const int litLen = srcIdx - anchor;

        // Emit token
        // Literals to process ?
        if (litLen == 0) {
            _tkBuf[tkIdx++] = byte(token);
        }
        else {
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

        if (mIdx >= _bufferSize - 8) {
            // Expand match buffer
            byte* mBuf = new byte[(_bufferSize * 3) / 2];
            memcpy(&mBuf[0], &_mBuf[0], _bufferSize);

            if ( _mBuf != nullptr)
                delete[] _mBuf;

            _mBuf = mBuf;

            if (mLenIdx >= _bufferSize - 8) {
                byte* mLenBuf = new byte[(_bufferSize * 3) / 2];
                memcpy(&mLenBuf[0], &_mLenBuf[0], _bufferSize);

                if (_mLenBuf != nullptr)
                   delete[] _mLenBuf;

                _mLenBuf = mLenBuf;
            }

            _bufferSize = (_bufferSize * 3) / 2;
        }

        // Fill _hashes and update positions
        anchor = srcIdx + bestLen;
        prefetchRead(&src[anchor + 64]);
        prefetchRead(&src[anchor + 128]);

        while (srcIdx + 4 < anchor) {
            srcIdx += 4;
            const int32 h0 = hash(&src[srcIdx - 3]);
            const int32 h1 = hash(&src[srcIdx - 2]);
            const int32 h2 = hash(&src[srcIdx - 1]);
            const int32 h3 = hash(&src[srcIdx - 0]);
            _hashes[h0] = srcIdx - 3;
            _hashes[h1] = srcIdx - 2;
            _hashes[h2] = srcIdx - 1;
            _hashes[h3] = srcIdx - 0;
        }

        while (++srcIdx < anchor) {
            const int32 h = hash(&src[srcIdx]);
            _hashes[h] = srcIdx;
        }
    }

    // Emit last literals
    const int litLen = count - anchor;

    if (dstIdx + litLen + tkIdx + mIdx >= output._index + count)
        return false;

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
    LittleEndian::writeInt32(&dst[8], mIdx);
    memcpy(&dst[dstIdx], &_tkBuf[0], tkIdx);
    dstIdx += tkIdx;
    memcpy(&dst[dstIdx], &_mBuf[0], mIdx);
    dstIdx += mIdx;
    memcpy(&dst[dstIdx], &_mLenBuf[0], mLenIdx);
    dstIdx += mLenIdx;
    input._index += count;
    output._index += dstIdx;
    return dstIdx <= count - (count / 100);
}

template <bool T>
bool LZXCodec<T>::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    int bsVersion = _pCtx == nullptr ? 6 : _pCtx->getInt("bsVersion", 6);

    if (bsVersion < 6)
       return inverseV5(input, output, count);

    return inverseV6(input, output, count);
}


template <bool T>
bool LZXCodec<T>::inverseV6(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (count < 13)
        return false;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("LZ codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("LZ codec: Invalid output block");

    const int dstEnd = output._length;
    byte* dst = &output._array[output._index];
    const byte* src = &input._array[input._index];

    int tkIdx = LittleEndian::readInt32(&src[0]);
    int mIdx = LittleEndian::readInt32(&src[4]);
    int mLenIdx = LittleEndian::readInt32(&src[8]);

    // Sanity checks
    if ((tkIdx < 0) || (mIdx < 0) || (mLenIdx < 0))
        return false;

    if ((tkIdx > count) || (mIdx > count - tkIdx) || (mLenIdx > count - tkIdx - mIdx))
        return false;

    mIdx += tkIdx;
    mLenIdx += mIdx;

    const int srcEnd = tkIdx - 13;
    const int maxDist = ((int(src[12]) & 1) == 0) ? MAX_DISTANCE1 : MAX_DISTANCE2;
    const int minMatch = ((int(src[12]) >> 1) & 0x07) + 2;
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
            const byte* s = &src[srcIdx];
            byte* d = &dst[dstIdx];
            srcIdx += litLen;
            dstIdx += litLen;

            if (srcIdx >= srcEnd) {
                memcpy(d, s, litLen);
                break;
            }

            emitLiterals(s, d, litLen);
        }

        prefetchRead(&src[mLenIdx]);

        // Get match length and distance
        int mLen, dist;

        if ((token & 0x18) == 0) {
            // Repetition distance, read mLen remainder (if any) outside of token
            mLen = token & 0x03;
            mLen += (mLen == 3 ? minMatch + readLength(src, mLenIdx) : minMatch);
            dist = (token & 0x04) == 0 ? repd0 : repd1;
        }
        else {
            // Read mLen remainder (if any) outside of token
            mLen = token & 0x07;
            mLen += (mLen == 7 ? minMatch + readLength(src, mLenIdx) : minMatch);
            dist = int(src[mIdx++]);
            const int f1 = (token >> 4) & 1;
            const int f2 = (token >> 3) & f1;
            dist = (dist << (8 * f1)) | (-f1 & int(src[mIdx]));
            mIdx += f1;
            dist = (dist << (8 * f2)) | (-f2 & int(src[mIdx]));
            mIdx += f2;
        }

        repd1 = repd0;
        repd0 = dist;
        const int mEnd = dstIdx + mLen;
        int ref = dstIdx - dist;

        // Sanity check
        if ((ref < 0) || (dist > maxDist) || (mEnd > dstEnd)) {
            res = false;
            goto exit;
        }

        prefetchWrite(&dst[dstIdx]);

        // Copy match
        if (dist >= 16) {
            do {
                // No overlap
                memcpy(&dst[dstIdx], &dst[ref], 16);
                ref += 16;
                dstIdx += 16;
            } while (dstIdx < mEnd);
        }
        else if (dist != 1) {
            const byte* s = &dst[ref];
            byte* p = &dst[dstIdx];
            const byte* pend = &p[mLen];

            while (p < pend)
               *p++ = *s++;
        }
        else {
            // dist = 1
            memset(&dst[dstIdx], int(dst[ref]), mLen);
        }

        dstIdx = mEnd;
    }

exit:
    output._index += dstIdx;
    input._index += mIdx;
    return res && (srcIdx == srcEnd + 13);
}


template <bool T>
bool LZXCodec<T>::inverseV5(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (count < 13)
        return false;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("LZ codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("LZ codec: Invalid output block");

    const int dstEnd = output._length;
    byte* dst = &output._array[output._index];
    const byte* src = &input._array[input._index];

    int tkIdx = LittleEndian::readInt32(&src[0]);
    int mIdx = LittleEndian::readInt32(&src[4]);
    int mLenIdx = LittleEndian::readInt32(&src[8]);

    // Sanity checks
    if ((tkIdx < 0) || (mIdx < 0) || (mLenIdx < 0))
        return false;

    if ((tkIdx > count) || (mIdx > count - tkIdx) || (mLenIdx > count - tkIdx - mIdx))
        return false;

    mIdx += tkIdx;
    mLenIdx += mIdx;

    const int srcEnd = tkIdx - 13;
    const int mFlag = int(src[12]) & 1;
    const int maxDist = (mFlag == 0) ? MAX_DISTANCE1 : MAX_DISTANCE2;
    const int mmIdx = (int(src[12]) >> 1) & 0x03;
    const int MIN_MATCHES[4] = { MIN_MATCH4, MIN_MATCH9, MIN_MATCH6, MIN_MATCH6 };
    const int minMatch = MIN_MATCHES[mmIdx];
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
            const byte* s = &src[srcIdx];
            byte* d = &dst[dstIdx];
            srcIdx += litLen;
            dstIdx += litLen;

            if (srcIdx >= srcEnd) {
                memcpy(d, s, litLen);
                break;
            }

            emitLiterals(s, d, litLen);
        }

        // Get match length and distance
        int mLen = token & 0x0F;
        int dist;

        if (mLen == 15) {
            // Repetition distance, read mLen fully outside of token
            mLen = minMatch + readLength(src, mLenIdx);
            dist = ((token & 0x10) == 0) ? repd0 : repd1;
        }
        else {
            // Read mLen remainder (if any) outside of token
            mLen = (mLen == 14) ? 14 + minMatch + readLength(src, mLenIdx) : mLen + minMatch;
            dist = int(src[mIdx++]);

            if (mFlag != 0)
                dist = (dist << 8) | int(src[mIdx++]);

            //if ((token & 0x10) != 0) {
            //    dist = (dist << 8) | int(src[mIdx++]);
            //}
            const int t = (token >> 4) & 1;
            dist = (dist << (8 * t)) | (-t & int(src[mIdx]));
            mIdx += t;
        }

        prefetchRead(&src[mLenIdx]);
        repd1 = repd0;
        repd0 = dist;
        const int mEnd = dstIdx + mLen;
        int ref = dstIdx - dist;

        // Sanity check
        if ((ref < 0) || (dist > maxDist) || (mEnd > dstEnd)) {
            res = false;
            goto exit;
        }

        prefetchWrite(&dst[dstIdx]);

        // Copy match
        if (dist >= 16) {
            do {
                // No overlap
                memcpy(&dst[dstIdx], &dst[ref], 16);
                ref += 16;
                dstIdx += 16;
            } while (dstIdx < mEnd);
        }
        else if (dist != 1) {
            const byte* s = &dst[ref];
            byte* p = &dst[dstIdx];
            const byte* pend = &p[mLen];

            while (p < pend)
               *p++ = *s++;
        }
        else {
            // dist = 1
            memset(&dst[dstIdx], int(dst[ref]), mLen);
        }

        dstIdx = mEnd;
    }

exit:
    output._index += dstIdx;
    input._index += mIdx;
    return res && (srcIdx == srcEnd + 13);
}


const uint LZPCodec::HASH_SEED = 0x7FEB352D;
const uint LZPCodec::HASH_LOG = 16;
const uint LZPCodec::HASH_SHIFT = 32 - HASH_LOG;
const int LZPCodec::MIN_MATCH = 64;
const int LZPCodec::MIN_BLOCK_LENGTH = 128;
const int LZPCodec::MATCH_FLAG = 0xFC;


bool LZPCodec::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (count < 4)
        return false;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("LZP codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("LZP codec: Invalid output block");

    if (output._length < getMaxEncodedLength(count))
        return false;

    // If too small, skip
    if (count < MIN_BLOCK_LENGTH)
        return false;

    const byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    const int srcEnd = count;
    const int dstEnd = count - (count >> 6);

    if (_hashSize == 0) {
        _hashSize = 1 << HASH_LOG;

        if (_hashes != nullptr)
            delete[] _hashes;

        _hashes = new int32[_hashSize];
    }

    memset(_hashes, 0, sizeof(int32) * _hashSize);
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    uint ctx = LittleEndian::readInt32(&src[0]);
    int srcIdx = 4;
    int dstIdx = 4;

    while ((srcIdx < srcEnd - MIN_MATCH) && (dstIdx < dstEnd)) {
        prefetchRead(&src[srcIdx + MIN_MATCH]);
        const uint32 h = (HASH_SEED * ctx) >> HASH_SHIFT;
        const int32 ref = _hashes[h];
        _hashes[h] = srcIdx;
        int bestLen = 0;

        // Find a match
        if ((ref != 0) && (memcmp(&src[ref + MIN_MATCH - 8], &src[srcIdx + MIN_MATCH - 8], 8) == 0))
            bestLen = findMatch(src, srcIdx, ref, srcEnd - srcIdx);

        // No good match ?
        if (bestLen < MIN_MATCH) {
            const uint val = uint(src[srcIdx]);
            ctx = (ctx << 8) | val;
            dst[dstIdx++] = src[srcIdx++];

            if ((ref != 0) && (val == MATCH_FLAG))
                dst[dstIdx++] = byte(0xFF);

            continue;
        }

        srcIdx += bestLen;
        prefetchRead(&src[srcIdx - 4]);
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
        const uint val = uint(src[srcIdx]);
        ctx = (ctx << 8) | val;
        dst[dstIdx++] = src[srcIdx++];

        if ((ref != 0) && (val == MATCH_FLAG))
            dst[dstIdx++] = byte(0xFF);
    }

    input._index += srcIdx;
    output._index += dstIdx;
    return (srcIdx == count) && (dstIdx < dstEnd);
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
    const byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];

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
    uint32 ctx = LittleEndian::readInt32(&dst[0]);
    int srcIdx = 4;
    int dstIdx = 4;

    while (srcIdx < srcEnd) {
        const int32 h = (HASH_SEED * ctx) >> HASH_SHIFT;
        int ref = _hashes[h];
        _hashes[h] = dstIdx;

        if ((ref == 0) || (src[srcIdx] != byte(MATCH_FLAG))) {
            ctx = (ctx << 8) | uint32(src[srcIdx]);
            dst[dstIdx++] = src[srcIdx++];
            continue;
        }

        srcIdx++;

        if (src[srcIdx] == byte(0xFF)) {
            ctx = (ctx << 8) | uint32(MATCH_FLAG);
            dst[dstIdx++] = byte(MATCH_FLAG);
            srcIdx++;
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
        const int mEnd = dstIdx + mLen;

        if (dstIdx >= ref + 8) {
            do {
                // No overlap
                memcpy(&dst[dstIdx], &dst[ref], 8);
                ref += 8;
                dstIdx += 8;
            } while (dstIdx < mEnd);
        }
        else {
            for (int i = 0; i < mLen; i++)
                dst[dstIdx + i] = dst[ref + i];
        }

        dstIdx = mEnd;
        ctx = LittleEndian::readInt32(&dst[dstIdx - 4]);
    }

    input._index += srcIdx;
    output._index += dstIdx;
    return srcIdx == srcEnd;
}
