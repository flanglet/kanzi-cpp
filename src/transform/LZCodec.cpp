/*
Copyright 2011-2026 Frederic Langlet
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
#include "LZCodec.hpp"
#include "../Memory.hpp"
#include "TransformFactory.hpp"

using namespace kanzi;
using namespace std;

LZCodec::LZCodec()
{
    _delegate = new LZXCodec<false>();
}

LZCodec::LZCodec(Context& ctx)
{
    const int lzType = ctx.getInt("lz", TransformFactory<kanzi::byte>::LZ_TYPE);

    if (lzType == TransformFactory<kanzi::byte>::LZF_TYPE) {
        _delegate = new LZFCodec(ctx);
    }
    else if (lzType == TransformFactory<kanzi::byte>::LZPX_TYPE) {
        _delegate = new LZPXCodec(ctx);
    }
    else if (lzType == TransformFactory<kanzi::byte>::LZP_TYPE) {
        _delegate = new LZPCodec(ctx);
    }
    else if (lzType == TransformFactory<kanzi::byte>::LZX_TYPE) {
        _delegate = new LZXCodec<true>(ctx);
    }
    else {
        _delegate = new LZXCodec<false>(ctx);
    }
}

bool LZCodec::forward(SliceArray<kanzi::byte>& input, SliceArray<kanzi::byte>& output, int count)
{
    if (count == 0)
        return true;

    if (input._array == output._array)
        return false;

    return _delegate->forward(input, output, count);
}

bool LZCodec::inverse(SliceArray<kanzi::byte>& input, SliceArray<kanzi::byte>& output, int count)
{
    if (count == 0)
        return true;

    if (input._array == output._array)
        return false;

    return _delegate->inverse(input, output, count);
}


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
const int LZXCodec<false>::READ_LENGTH_GUARD = 4;
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
template<>
const int LZXCodec<true>::READ_LENGTH_GUARD = 4;


template <bool T>
bool LZXCodec<T>::forward(SliceArray<kanzi::byte>& input, SliceArray<kanzi::byte>& output, int count)
{
    // Forward LZ processing:
    // 1. Allocate/reset the hash table and staging buffers.
    // 2. Reserve the last 18 input bytes as a safe literal tail.
    // 3. At each position:
    //    - hash the current position;
    //    - test the two repeat distances at srcIdx + 1;
    //    - if no repeat match is found, test the latest hash candidate at srcIdx;
    //    - optionally test matches at srcIdx + 1 and srcIdx + 2;
    //    - extend the selected match backwards to reduce the literal run.
    // 4. Emit the literal length, repeat or explicit distance, match length,
    //    and literals.
    // 5. Insert all positions covered by the match into the hash table.
    // 6. Append the final literals and concatenate the staging buffers.
    if (count == 0)
        return true;

    if (!SliceArray<kanzi::byte>::isValid(input))
        throw invalid_argument("LZ codec: Invalid input block");

    if (count > input._length - input._index)
        return false;

    if (!SliceArray<kanzi::byte>::isValid(output))
        throw invalid_argument("LZ codec: Invalid output block");

    if (output._length - output._index < getMaxEncodedLength(count))
        return false;

    // If too small, skip
    if (count < MIN_BLOCK_LENGTH)
        return false;

    if (_hashSize == 0) {
        const int newSize = 1 << HASH_LOG;
        int32* hashes = new int32[newSize];
        delete[] _hashes;
        _hashes = hashes;
        _hashSize = newSize;
    }

    if (_bufferSize < max(count / 5, 256)) {
        const int newSize = max(count / 5, 256);
        kanzi::byte* mLenBuf = new kanzi::byte[newSize];
        kanzi::byte* mBuf = nullptr;
        kanzi::byte* tkBuf = nullptr;

        try {
            mBuf = new kanzi::byte[newSize];
            tkBuf = new kanzi::byte[newSize];
        }
        catch (...) {
            delete[] mLenBuf;
            delete[] mBuf;
            delete[] tkBuf;
            throw;
        }

        delete[] _mLenBuf;
        delete[] _mBuf;
        delete[] _tkBuf;
        _mLenBuf = mLenBuf;
        _mBuf = mBuf;
        _tkBuf = tkBuf;
        _bufferSize = newSize;
    }

    memset(_hashes, 0, sizeof(int32) * _hashSize);
    const int srcEnd = count - 16 - 2;
    const kanzi::byte* src = &input._array[input._index];
    kanzi::byte* dst = &output._array[output._index];
    const int maxDist = (srcEnd < 4 * MAX_DISTANCE1) ? MAX_DISTANCE1 : MAX_DISTANCE2;
    dst[12] = (maxDist == MAX_DISTANCE1) ? kanzi::byte(0) : kanzi::byte(1);
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
    dst[12] |= kanzi::byte(((mm - 2) & 0x07) << 1); // minMatch in [2..9]
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
        const uint64 word0 = uint64(LittleEndian::readLong64(&src[srcIdx]));
        const uint64 word1 = word0 >> 8;
        const uint64 word2 = word1 >> 8;
        const int32 h0 = hash(word0);
        const int ref0 = _hashes[h0];
        _hashes[h0] = srcIdx;
        const int srcIdx1 = srcIdx + 1;
        int ref = srcIdx1 - repd[repIdx];
        const int minRef = max(srcIdx - maxDist, 0);

        if ((ref > minRef) && KANZI_MEM_EQ4(&src[srcIdx1], &src[ref])) {
            // Check repd0
            bestLen = findMatch(src, srcIdx1, ref, min(srcEnd - srcIdx1, MAX_MATCH));
        }
        else {
            ref = srcIdx1 - repd[repIdx ^ 1];

            if ((ref > minRef) && KANZI_MEM_EQ4(&src[srcIdx1], &src[ref])) {
                // Check repd1
                bestLen = findMatch(src, srcIdx1, ref, min(srcEnd - srcIdx1, MAX_MATCH));
            }
        }

        if (bestLen < minMatch) {
            // Check match at position in hash table
            ref = ref0;

            if ((ref > minRef) && KANZI_MEM_EQ4(&src[srcIdx], &src[ref])) {
                bestLen = findMatch(src, srcIdx, ref, min(srcEnd - srcIdx, MAX_MATCH));
            }

            // No good match ?
            if (bestLen < minMatch) {
                srcIdx = srcIdx1 + (srcInc >> 6);
                srcInc++;
                repIdx = 0;
                continue;
            }

            if ((srcIdx - ref != repd[0]) && (srcIdx - ref != repd[1])) {
                // Check if better match at next position
                const int32 h1 = hash(word1);
                const int ref1 = _hashes[h1];
                _hashes[h1] = srcIdx1;

                if ((ref1 > minRef + 1) && KANZI_MEM_EQ4(&src[srcIdx1 + bestLen - 3], &src[ref1 + bestLen - 3])) {
                    const int bestLen1 = findMatch(src, srcIdx1, ref1, min(srcEnd - srcIdx1, MAX_MATCH));

                    // Select best match
                    if (bestLen1 >= bestLen) {
                        ref = ref1;
                        bestLen = bestLen1;
                        srcIdx = srcIdx1;
                    }
                }

                if (T == true) {
                   const int srcIdx2 = srcIdx1 + 1;
                   const int32 h2 = hash(word2);
                   const int ref2 = _hashes[h2];
                   _hashes[h2] = srcIdx2;

                   if ((ref2 > minRef + 2) && KANZI_MEM_EQ4(&src[srcIdx2 + bestLen - 3], &src[ref2 + bestLen - 3])) {
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
            while ((srcIdx > anchor) && (ref > 0) && (src[srcIdx - 1] == src[ref - 1])) {
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
                const int32 h1 = hash(word1);
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
            //   1-byte distance: dist - 1
            //   2-byte distance: dist - 257
            //   3-byte distance: dist - 65793
            const int encodedDist = dist - 1;

            if (encodedDist < 256) {
                _mBuf[mIdx++] = kanzi::byte(encodedDist);
                token = 0x08;
            }
            else if (encodedDist < 65792) {
                const uint32 value = uint32(encodedDist - 256) << 16;
                BigEndian::writeInt32(&_mBuf[mIdx], int32(value));
                mIdx += 2;
                token = 0x10;
            }
            else {
                const uint32 value = uint32(encodedDist - 65792) << 8;
                BigEndian::writeInt32(&_mBuf[mIdx], int32(value));
                mIdx += 3;
                token = 0x18;
            }
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
            _tkBuf[tkIdx++] = kanzi::byte(token);
        }
        else {
            // Emit literal length
            if (litLen >= 7) {
                if (litLen >= (1 << 24))
                    return false;

                _tkBuf[tkIdx++] = kanzi::byte((7 << 5) | token);
                dstIdx += emitLength(&dst[dstIdx], litLen - 7);
            }
            else {
                _tkBuf[tkIdx++] = kanzi::byte((litLen << 5) | token);
            }

            // Emit literals
            emitLiterals(&src[anchor], &dst[dstIdx], litLen);
            dstIdx += litLen;
        }

        if (mIdx >= _bufferSize - 8) {
            // Expand match buffer
            kanzi::byte* mBuf = new kanzi::byte[(_bufferSize * 3) / 2];
            memcpy(&mBuf[0], &_mBuf[0], _bufferSize);

            if ( _mBuf != nullptr)
                delete[] _mBuf;

            _mBuf = mBuf;

            if (mLenIdx >= _bufferSize - 8) {
                kanzi::byte* mLenBuf = new kanzi::byte[(_bufferSize * 3) / 2];
                memcpy(&mLenBuf[0], &_mLenBuf[0], _bufferSize);

                if (_mLenBuf != nullptr)
                   delete[] _mLenBuf;

                _mLenBuf = mLenBuf;
            }

            _bufferSize = (_bufferSize * 3) / 2;
        }

        // Fill _hashes and update positions. The current position was already
        // inserted above; sample four positions out of every eight (offsets
        // 0, 3, 5, and 6; two even and two odd) to reduce hash-table work
        // while retaining coverage.
        anchor = srcIdx + bestLen;
        int hashIdx = srcIdx + 1;

        while (hashIdx + 6 < anchor) {
            const int32 hh0 = hash(&src[hashIdx]);
            const int32 hh1 = hash(&src[hashIdx + 3]);
            const int32 hh2 = hash(&src[hashIdx + 5]);
            const int32 hh3 = hash(&src[hashIdx + 6]);
            _hashes[hh0] = hashIdx;
            _hashes[hh1] = hashIdx + 3;
            _hashes[hh2] = hashIdx + 5;
            _hashes[hh3] = hashIdx + 6;
            hashIdx += 8;
        }

        const int32 hh0 = hash(&src[anchor - 4]);
        _hashes[hh0] = anchor - 4;
        const int32 hh1 = hash(&src[anchor - 2]);
        _hashes[hh1] = anchor - 2;
        const int32 hh2 = hash(&src[anchor - 1]);
        _hashes[hh2] = anchor - 1;
        srcIdx = anchor;
    }

    // Emit last literals
    const int litLen = count - anchor;

    if (dstIdx + litLen + tkIdx + mIdx + mLenIdx >= count)
        return false;

    if (litLen >= 7) {
        _tkBuf[tkIdx++] = kanzi::byte(7 << 5);
        dstIdx += emitLength(&dst[dstIdx], litLen - 7);
    }
    else {
        _tkBuf[tkIdx++] = kanzi::byte(litLen << 5);
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
bool LZXCodec<T>::inverse(SliceArray<kanzi::byte>& input, SliceArray<kanzi::byte>& output, int count)
{
    if (count == 0)
        return true;

    if (count < 13)
        return false;

    if (!SliceArray<kanzi::byte>::isValid(input))
        throw invalid_argument("LZ codec: Invalid input block");

    if (!SliceArray<kanzi::byte>::isValid(output))
        throw invalid_argument("LZ codec: Invalid output block");

    // CompressedInputStream supplies trailing input padding. The versioned
    // decoders rely on READ_LENGTH_GUARD bytes for readLength() lookahead.
    int bsVersion = _pCtx == nullptr ? 6 : _pCtx->getInt("bsVersion", 6);

    if (bsVersion < 6)
       return inverseV5(input, output, count);

    if (bsVersion < 7)
        return inverseV6(input, output, count);

    return inverseV7(input, output, count);
}


template <bool T>
bool LZXCodec<T>::inverseV6(SliceArray<kanzi::byte>& input, SliceArray<kanzi::byte>& output, int count)
{
    const int inputSize = input._length - input._index;

    // readLength() reads four bytes for a three-byte length encoding.
    if ((inputSize < READ_LENGTH_GUARD) || (count > inputSize - READ_LENGTH_GUARD))
       return false;

    const int dstCapacity = output._length - output._index;

    // A 16-byte match copy may write up to 15 bytes past the logical match end.
    if (dstCapacity < 15)
        return false;

    const int dstEnd = dstCapacity - 15;
    kanzi::byte* dst = &output._array[output._index];
    const kanzi::byte* src = &input._array[input._index];

    int tkIdx = LittleEndian::readInt32(&src[0]);
    int mIdx = LittleEndian::readInt32(&src[4]);
    int mLenIdx = LittleEndian::readInt32(&src[8]);

    // Sanity checks
    if ((tkIdx < 0) || (mIdx < 0) || (mLenIdx < 0))
        return false;

    if ((tkIdx <= 13) || (tkIdx > count) || (mIdx > count - tkIdx) || (mLenIdx > count - tkIdx - mIdx))
        return false;

    mIdx += tkIdx;
    mLenIdx += mIdx;

    const int tokenEnd = mIdx;
    const int matchEnd = mLenIdx;
    const int srcEnd = tkIdx - 13;
    const int litEnd = tkIdx;
    const int maxDist = ((int(src[12]) & 1) == 0) ? MAX_DISTANCE1 : MAX_DISTANCE2;
    const int minMatch = ((int(src[12]) >> 1) & 0x07) + 2;
    bool res = true;
    int srcIdx = 13;
    int dstIdx = 0;
    int repd0 = count;
    int repd1 = count;

    while (true) {
        const int token = int(src[tkIdx++]);

        // Get match length and distance
        int mLen, dist;

        if ((token & 0x18) == 0) {
            // Repetition distance, read mLen remainder (if any) outside of token
            mLen = token & 0x03;

            if (KANZI_UNLIKELY((mLen == 3) && (mLenIdx >= count))) {
                res = false;
                goto exit;
            }

            mLen += (mLen == 3 ? minMatch + int(readLength(src, mLenIdx)) : minMatch);
            dist = (token & 0x04) == 0 ? repd0 : repd1;
        }
        else {
            // Read mLen remainder (if any) outside of token
            mLen = token & 0x07;

            if (KANZI_UNLIKELY((mLen == 7) && (mLenIdx >= count))) {
                res = false;
                goto exit;
            }

            mLen += (mLen == 7 ? minMatch + int(readLength(src, mLenIdx)) : minMatch);
            if (KANZI_UNLIKELY(mIdx >= count)) {
                res = false;
                goto exit;
            }

            dist = int(src[mIdx++]);
            const int f1 = (token >> 4) & 1;
            const int f2 = (token >> 3) & f1;
            dist = (dist << (8 & -f1)) | (-f1 & int(src[mIdx]));
            mIdx += f1;
            dist = (dist << (8 & -f2)) | (-f2 & int(src[mIdx]));
            mIdx += f2;
        }

        if (token >= 32) {
            // Get literal length
            const uint litLen = (token >= 0xE0) ? uint(7) + readLength(src, srcIdx) : uint(token >> 5);

            if ((litLen > uint(dstEnd - dstIdx)) || (litLen > uint(litEnd - srcIdx))) {
                res = false;
                goto exit;
            }

            // Emit literals
            const kanzi::byte* s = &src[srcIdx];
            kanzi::byte* d = &dst[dstIdx];
            srcIdx += int(litLen);
            dstIdx += int(litLen);

            if (srcIdx >= srcEnd) {
                memcpy(d, s, litLen);
                break;
            }

            emitLiterals(s, d, litLen);
        }

        repd1 = repd0;
        repd0 = dist;
        const int mEnd = dstIdx + mLen;
        int ref = dstIdx - dist;

        // Sanity check
        if ((dist == 0) || (ref < 0) || (dist > maxDist) || (mEnd > dstEnd)) {
            res = false;
            goto exit;
        }

        if ((dist >= 64) && (mLen >= 64))
            prefetchRead(&dst[dstIdx + 64]);

        // Copy match
        if (dist >= 16) {
            do {
                // The stream decoder supplies trailing padding for this
                // 16-byte copy, which may write up to 15 bytes past mEnd.
                KANZI_MEM_CP16(&dst[dstIdx], &dst[ref]);
                ref += 16;
                dstIdx += 16;
            } while (dstIdx < mEnd);
        }
        else if (dist != 1) {
            const kanzi::byte* s = &dst[ref];
            kanzi::byte* p = &dst[dstIdx];
            const kanzi::byte* pend = &p[mLen];

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
    input._index += count;
    return res && (srcIdx == srcEnd + 13) && (tkIdx == tokenEnd) &&
        (mIdx == matchEnd) && (mLenIdx == count);
}


template <bool T>
bool LZXCodec<T>::inverseV7(SliceArray<kanzi::byte>& input, SliceArray<kanzi::byte>& output, int count)
{
    const int inputSize = input._length - input._index;

    // readLength() reads four bytes for a three-byte length encoding.
    if ((inputSize < READ_LENGTH_GUARD) || (count > inputSize - READ_LENGTH_GUARD))
       return false;

    const int dstCapacity = output._length - output._index;

    // A 16-byte match copy may write up to 15 bytes past the logical match end.
    if (dstCapacity < 15)
        return false;

    const int dstEnd = dstCapacity - 15;
    kanzi::byte* dst = &output._array[output._index];
    const kanzi::byte* src = &input._array[input._index];

    int tkIdx = LittleEndian::readInt32(&src[0]);
    int mIdx = LittleEndian::readInt32(&src[4]);
    int mLenIdx = LittleEndian::readInt32(&src[8]);

    // Sanity checks
    if ((tkIdx < 0) || (mIdx < 0) || (mLenIdx < 0))
        return false;

    if ((tkIdx <= 13) || (tkIdx > count) || (mIdx > count - tkIdx) || (mLenIdx > count - tkIdx - mIdx))
        return false;

    mIdx += tkIdx;
    mLenIdx += mIdx;

    const int tokenEnd = mIdx;
    const int matchEnd = mLenIdx;
    const int srcEnd = tkIdx - 13;
    const int litEnd = tkIdx;
    const int maxDist = ((int(src[12]) & 1) == 0) ? MAX_DISTANCE1 : MAX_DISTANCE2;
    const int minMatch = ((int(src[12]) >> 1) & 0x07) + 2;
    bool res = true;
    int srcIdx = 13;
    int dstIdx = 0;
    int repd0 = count;
    int repd1 = count;

    while (true) {
        const int token = int(src[tkIdx++]);

        // Get match length and distance
        int mLen, dist;

        if ((token & 0x18) == 0) {
            // Repetition distance, read mLen remainder (if any) outside of token
            mLen = token & 0x03;

            if (KANZI_UNLIKELY((mLen == 3) && (mLenIdx >= count))) {
                res = false;
                goto exit;
            }

            mLen += (mLen == 3 ? minMatch + int(readLength(src, mLenIdx)) : minMatch);
            dist = (token & 0x04) == 0 ? repd0 : repd1;
        }
        else {
            // Read mLen remainder (if any) outside of token
            mLen = token & 0x07;

            if (KANZI_UNLIKELY((mLen == 7) && (mLenIdx >= count))) {
                res = false;
                goto exit;
            }

            mLen += (mLen == 7 ? minMatch + int(readLength(src, mLenIdx)) : minMatch);

            if (KANZI_UNLIKELY(mIdx >= count)) {
                res = false;
                goto exit;
            }

            static const uint32 DIST_BIAS[4] = { 0, 1, 257, 65793 };
            const int width = (token >> 3) & 3;

            const uint32 value = uint32(BigEndian::readInt32(&src[mIdx]));
            dist = int((value >> (32 - (token & 0x18))) + DIST_BIAS[width]);
            mIdx += width;
        }

        if (token >= 32) {
            // Get literal length
            const uint litLen = (token >= 0xE0) ? uint(7) + readLength(src, srcIdx) : uint(token >> 5);

            if (KANZI_UNLIKELY((uint(srcIdx) + litLen > uint(litEnd)) || (litLen > uint(dstEnd - dstIdx)))) {
                res = false;
                goto exit;
            }

            // Emit literals
            const kanzi::byte* s = &src[srcIdx];
            kanzi::byte* d = &dst[dstIdx];
            srcIdx += int(litLen);
            dstIdx += int(litLen);

            if (KANZI_UNLIKELY(srcIdx >= srcEnd)) {
                memcpy(d, s, litLen);
                break;
            }

            emitLiterals(s, d, litLen);
        }

        repd1 = repd0;
        repd0 = dist;
        const int mEnd = dstIdx + mLen;
        int ref = dstIdx - dist;

        // Sanity check
        if (KANZI_UNLIKELY((ref < 0) || (dist > maxDist) || (mEnd > dstEnd))) {
            res = false;
            goto exit;
        }

        // Copy match
        if (dist >= 16) {
            if (mLen >= 64)
                prefetchRead(&dst[dstIdx + 64]);

            do {
                // The stream decoder supplies trailing padding for this
                // 16-byte copy, which may write up to 15 bytes past mEnd.
                KANZI_MEM_CP16(&dst[dstIdx], &dst[ref]);
                ref += 16;
                dstIdx += 16;
            } while (dstIdx < mEnd);
        }
        else if (dist != 1) {
            const kanzi::byte* s = &dst[ref];
            kanzi::byte* p = &dst[dstIdx];
            const kanzi::byte* pend = &p[mLen];

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
    input._index += count;
    return res && (srcIdx == srcEnd + 13) && (tkIdx == tokenEnd) &&
        (mIdx == matchEnd) && (mLenIdx == count);
}


template <bool T>
bool LZXCodec<T>::inverseV5(SliceArray<kanzi::byte>& input, SliceArray<kanzi::byte>& output, int count)
{
    const int inputSize = input._length - input._index;

    // readLength() reads four bytes for a three-byte length encoding.
    if ((inputSize < READ_LENGTH_GUARD) || (count > inputSize - READ_LENGTH_GUARD))
        return false;

    const int dstEnd = output._length - output._index;
    kanzi::byte* dst = &output._array[output._index];
    const kanzi::byte* src = &input._array[input._index];

    int tkIdx = LittleEndian::readInt32(&src[0]);
    int mIdx = LittleEndian::readInt32(&src[4]);
    int mLenIdx = LittleEndian::readInt32(&src[8]);

    // Sanity checks
    if ((tkIdx < 0) || (mIdx < 0) || (mLenIdx < 0))
        return false;

    if ((tkIdx <= 13) || (tkIdx > count) || (mIdx > count - tkIdx) || (mLenIdx > count - tkIdx - mIdx))
        return false;

    mIdx += tkIdx;
    mLenIdx += mIdx;

    const int tokenEnd = mIdx;
    const int matchEnd = mLenIdx;
    const int srcEnd = tkIdx - 13;
    const int litEnd = tkIdx;
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
            const uint litLen = (token >= 0xE0) ? uint(7) + readLength(src, srcIdx) : uint(token >> 5);

            if ((litLen > uint(dstEnd - dstIdx)) || (litLen > uint(litEnd - srcIdx))) {
                res = false;
                goto exit;
            }

            // Emit literals
            const kanzi::byte* s = &src[srcIdx];
            kanzi::byte* d = &dst[dstIdx];
            srcIdx += int(litLen);
            dstIdx += int(litLen);

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

            if (KANZI_UNLIKELY(mLenIdx >= count)) {
                res = false;
                goto exit;
            }

            mLen = minMatch + int(readLength(src, mLenIdx));
            dist = ((token & 0x10) == 0) ? repd0 : repd1;
        }
        else {
            // Read mLen remainder (if any) outside of token

            if (KANZI_UNLIKELY((mLen == 14) && (mLenIdx >= count))) {
                res = false;
                goto exit;
            }

            mLen = (mLen == 14) ? 14 + minMatch + int(readLength(src, mLenIdx)) : mLen + minMatch;

            if (KANZI_UNLIKELY(mIdx >= count)) {
                res = false;
                goto exit;
            }

            dist = int(src[mIdx++]);

            if (mFlag != 0)
                dist = (dist << 8) | int(src[mIdx++]);

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
        if ((dist == 0) || (ref < 0) || (dist > maxDist) || (mEnd > dstEnd)) {
            res = false;
            goto exit;
        }

        prefetchWrite(&dst[dstIdx]);

        // Copy match
        if (dist >= 16) {
            do {
                // The stream decoder supplies trailing padding for this
                // 16-byte copy, which may write up to 15 bytes past mEnd.
                KANZI_MEM_CP16(&dst[dstIdx], &dst[ref]);
                ref += 16;
                dstIdx += 16;
            } while (dstIdx < mEnd);
        }
        else if (dist != 1) {
            const kanzi::byte* s = &dst[ref];
            kanzi::byte* p = &dst[dstIdx];
            const kanzi::byte* pend = &p[mLen];

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
    input._index += count;
    return res && (srcIdx == srcEnd + 13) && (tkIdx == tokenEnd) &&
        (mIdx == matchEnd) && (mLenIdx == count);
}


const uint LZPCodec::HASH_LOG = 16;
const uint LZPCodec::HASH_SHIFT = 32 - HASH_LOG;
const int LZPCodec::MIN_MATCH = 64;
const int LZPCodec::MIN_BLOCK_LENGTH = 128;
const int LZPCodec::MATCH_FLAG = 0xFC;


bool LZPCodec::forward(SliceArray<kanzi::byte>& input, SliceArray<kanzi::byte>& output, int count)
{
    if (count == 0)
        return true;

    if (count < 4)
        return false;

    if (!SliceArray<kanzi::byte>::isValid(input))
        throw invalid_argument("LZP codec: Invalid input block");

    if (!SliceArray<kanzi::byte>::isValid(output))
        throw invalid_argument("LZP codec: Invalid output block");

    if (count > input._length - input._index)
        return false;

    if (output._length - output._index < getMaxEncodedLength(count))
        return false;

    // If too small, skip
    if (count < MIN_BLOCK_LENGTH)
        return false;

    const kanzi::byte* src = &input._array[input._index];
    kanzi::byte* dst = &output._array[output._index];
    const int srcEnd = count;
    const int dstEnd = count - (count >> 6);

    if (_hashSize == 0) {
        const int newSize = 1 << HASH_LOG;
        int32* hashes = new int32[newSize];
        delete[] _hashes;
        _hashes = hashes;
        _hashSize = newSize;
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
        const uint32 h = hash(ctx);
        const int32 ref = _hashes[h];
        _hashes[h] = srcIdx;
        int bestLen = 0;

        // Find a match. Check the last word first, then validate the other
        // seven words in the minimum match before extending beyond it.
        if ((ref != 0) &&
            KANZI_MEM_EQ8(&src[ref + MIN_MATCH - 8], &src[srcIdx + MIN_MATCH - 8]) &&
            KANZI_MEM_EQ16(&src[ref], &src[srcIdx]) &&
            KANZI_MEM_EQ16(&src[ref + 16], &src[srcIdx + 16]) &&
            KANZI_MEM_EQ16(&src[ref + 32], &src[srcIdx + 32]) &&
            KANZI_MEM_EQ8(&src[ref + 48], &src[srcIdx + 48])) {
            bestLen = MIN_MATCH + findMatch(src, srcIdx + MIN_MATCH,
                                            ref + MIN_MATCH,
                                            srcEnd - srcIdx - MIN_MATCH);
        }

        // No good match ?
        if (bestLen < MIN_MATCH) {
            const uint val = uint(src[srcIdx]);
            ctx = (ctx << 8) | val;
            dst[dstIdx++] = src[srcIdx++];

            if ((ref != 0) && (val == MATCH_FLAG)) {
                if (dstIdx >= dstEnd)
                    return false;

                dst[dstIdx++] = kanzi::byte(0xFF);
            }

            continue;
        }

        srcIdx += bestLen;
        ctx = LittleEndian::readInt32(&src[srcIdx - 4]);
        dst[dstIdx++] = kanzi::byte(MATCH_FLAG);
        const int extra = bestLen - MIN_MATCH;

        // Emit match length
        if (extra < 254) {
            if (dstIdx >= dstEnd)
                return false;

            dst[dstIdx++] = kanzi::byte(extra);
        }
        else {
            const int extensions = extra / 254;
            const int remainder = extra % 254;

            // The remainder byte is required after all extension bytes.
            if ((dstIdx >= dstEnd) || (extensions >= dstEnd - dstIdx))
                return false;

            memset(&dst[dstIdx], 0xFE, size_t(extensions));
            dstIdx += extensions;
            dst[dstIdx++] = kanzi::byte(remainder);
        }
    }

    while ((srcIdx < srcEnd) && (dstIdx < dstEnd)) {
        const uint32 h = hash(ctx);
        const int ref = _hashes[h];
        _hashes[h] = srcIdx;
        const uint val = uint(src[srcIdx]);
        ctx = (ctx << 8) | val;
        dst[dstIdx++] = src[srcIdx++];

        if ((ref != 0) && (val == MATCH_FLAG)) {
            if (dstIdx >= dstEnd)
                return false;

            dst[dstIdx++] = kanzi::byte(0xFF);
        }
    }

    input._index += srcIdx;
    output._index += dstIdx;
    return (srcIdx == count) && (dstIdx < dstEnd);
}

bool LZPCodec::inverse(SliceArray<kanzi::byte>& input, SliceArray<kanzi::byte>& output, int count)
{
    if (count == 0)
        return true;

    if (!SliceArray<kanzi::byte>::isValid(input))
        throw invalid_argument("LZP codec: Invalid input block");

    if (!SliceArray<kanzi::byte>::isValid(output))
        throw invalid_argument("LZP codec: Invalid output block");

    if (count > input._length - input._index)
        return false;

    if (count < 4)
        return false;

    const int srcEnd = count;
    const int dstEnd = output._length - output._index;
    const kanzi::byte* src = &input._array[input._index];
    kanzi::byte* dst = &output._array[output._index];

    if (dstEnd < count)
        return false;

    if (_hashSize == 0) {
        const int newSize = 1 << HASH_LOG;
        int32* hashes = new int32[newSize];
        delete[] _hashes;
        _hashes = hashes;
        _hashSize = newSize;
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
        // A run of ordinary literals does not need the previous table entry.
        // Keep the context recurrence and table stores in source order. The
        // left-shift recurrence is intentional: do not replace it with a
        // load of the four preceding output bytes.
        if ((src[srcIdx] != kanzi::byte(MATCH_FLAG)) && (dstIdx < dstEnd)) {
            const int run = min(8, min(srcEnd - srcIdx, dstEnd - dstIdx));
            int literalRun = 0;

            while ((literalRun < run) && (src[srcIdx + literalRun] != kanzi::byte(MATCH_FLAG)))
                literalRun++;

            if (literalRun > 0) {
                const int start = srcIdx;

                int i = 0;

                if (literalRun >= 8) {
                    uint32 h = hash(ctx);
                    _hashes[h] = dstIdx;
                    ctx = (ctx << 8) | uint32(src[start]);
                    h = hash(ctx);
                    _hashes[h] = dstIdx + 1;
                    ctx = (ctx << 8) | uint32(src[start + 1]);
                    h = hash(ctx);
                    _hashes[h] = dstIdx + 2;
                    ctx = (ctx << 8) | uint32(src[start + 2]);
                    h = hash(ctx);
                    _hashes[h] = dstIdx + 3;
                    ctx = (ctx << 8) | uint32(src[start + 3]);
                    h = hash(ctx);
                    _hashes[h] = dstIdx + 4;
                    ctx = (ctx << 8) | uint32(src[start + 4]);
                    h = hash(ctx);
                    _hashes[h] = dstIdx + 5;
                    ctx = (ctx << 8) | uint32(src[start + 5]);
                    h = hash(ctx);
                    _hashes[h] = dstIdx + 6;
                    ctx = (ctx << 8) | uint32(src[start + 6]);
                    h = hash(ctx);
                    _hashes[h] = dstIdx + 7;
                    ctx = (ctx << 8) | uint32(src[start + 7]);
                    i = 8;
                }
                else if (literalRun >= 4) {
                    uint32 h = hash(ctx);
                    _hashes[h] = dstIdx;
                    ctx = (ctx << 8) | uint32(src[start]);
                    h = hash(ctx);
                    _hashes[h] = dstIdx + 1;
                    ctx = (ctx << 8) | uint32(src[start + 1]);
                    h = hash(ctx);
                    _hashes[h] = dstIdx + 2;
                    ctx = (ctx << 8) | uint32(src[start + 2]);
                    h = hash(ctx);
                    _hashes[h] = dstIdx + 3;
                    ctx = (ctx << 8) | uint32(src[start + 3]);
                    i = 4;
                }

                for (; i < literalRun; i++) {
                    const uint32 h = hash(ctx);
                    _hashes[h] = dstIdx + i;
                    ctx = (ctx << 8) | uint32(src[start + i]);
                }

                memcpy(&dst[dstIdx], &src[start], size_t(literalRun));
                srcIdx += literalRun;
                dstIdx += literalRun;
                continue;
            }
        }

        const int32 h = hash(ctx);
        int ref = _hashes[h];
        _hashes[h] = dstIdx;

        if ((src[srcIdx] != kanzi::byte(MATCH_FLAG)) || (ref == 0)) {
            if (dstIdx >= dstEnd)
                return false;

            ctx = (ctx << 8) | uint32(src[srcIdx]);
            dst[dstIdx++] = src[srcIdx++];
            continue;
        }

        srcIdx++;

        if (srcIdx >= srcEnd)
            return false;

        if (src[srcIdx] == kanzi::byte(0xFF)) {
            if (dstIdx >= dstEnd)
                return false;

            ctx = (ctx << 8) | uint32(MATCH_FLAG);
            dst[dstIdx++] = kanzi::byte(MATCH_FLAG);
            srcIdx++;
            continue;
        }

        uint64 mLen = MIN_MATCH;

        if (src[srcIdx] == kanzi::byte(0xFE)) {
            const uint64 feWord = uint64(0xFEFEFEFEFEFEFEFE);

            // Consume complete runs of 0xFE eight bytes at a time. The
            // little-endian load makes the first non-0xFE byte the first
            // nonzero byte in diff, so trailingZeros(diff) identifies the
            // number of preceding extension bytes.
            while (srcEnd - srcIdx >= 8) {
                const uint64 diff = uint64(LittleEndian::readLong64(&src[srcIdx])) ^ feWord;

                if (diff != 0) {
                    const int run = Global::trailingZeros(diff) >> 3;
                    srcIdx += run;
                    mLen += uint64(run * 254);
                    break;
                }

                srcIdx += 8;
                mLen += uint64(8 * 254);
            }

            while ((srcIdx < srcEnd) && (src[srcIdx] == kanzi::byte(0xFE))) {
                srcIdx++;
                mLen += 254;
            }

            if (KANZI_UNLIKELY(srcIdx >= srcEnd))
                return false;
        }

        mLen += uint64(src[srcIdx++]);

        if (KANZI_UNLIKELY(mLen > uint64(dstEnd - dstIdx)))
            return false;

        const int matchLen = int(mLen);
        const int mEnd = dstIdx + matchLen;
        const int dist = dstIdx - ref;

        if (dist >= 16) {
            do {
                // The stream decoder supplies trailing padding for this
                // 16-byte copy, which may write up to 15 bytes past mEnd.
                KANZI_MEM_CP16(&dst[dstIdx], &dst[ref]);
                ref += 16;
                dstIdx += 16;
            } while (dstIdx < mEnd);
        }
        else {
            switch (dist) {
            case 1:
                memset(&dst[dstIdx], int(dst[ref]), size_t(matchLen));
                break;
            case 2:
            case 4:
            case 8: {
                // These distances divide an 8-byte word. Build one complete
                // period from the valid source bytes, then copy whole words.
                kanzi::byte pattern[8];

                for (int i = 0; i < 8; i++)
                    pattern[i] = dst[ref + (i % dist)];

                int copied = 0;

                while (matchLen - copied >= 8) {
                    KANZI_MEM_CP8(&dst[dstIdx + copied], &pattern[0]);
                    copied += 8;
                }

                if (copied < matchLen)
                    memcpy(&dst[dstIdx + copied], &pattern[0], size_t(matchLen - copied));

                break;
            }
            default: {
                // For the other short distances, use a local repeated pattern.
                // The phase changes at every wide copy, so copying from the
                // already expanded output would be an overlapping copy.
                kanzi::byte pattern[32];

                for (int i = 0; i < 32; i++)
                    pattern[i] = dst[ref + (i % dist)];

                int copied = 0;
                int phase = 0;

                while (matchLen - copied >= 16) {
                    KANZI_MEM_CP16(&dst[dstIdx + copied], &pattern[phase]);
                    copied += 16;
                    phase = (phase + 16) % dist;
                }

                if (matchLen - copied >= 8) {
                    KANZI_MEM_CP8(&dst[dstIdx + copied], &pattern[phase]);
                    copied += 8;
                    phase = (phase + 8) % dist;
                }

                if (copied < matchLen)
                    memcpy(&dst[dstIdx + copied], &pattern[phase], size_t(matchLen - copied));

                break;
            }
            }
        }

        dstIdx = mEnd;
        ctx = LittleEndian::readInt32(&dst[dstIdx - 4]);
    }

    input._index += srcIdx;
    output._index += dstIdx;
    return srcIdx == srcEnd;
}


const uint LZFCodec::HASH_LOG = 18;
const int LZFCodec::MIN_MATCH = 6;
const int LZFCodec::MIN_DISTANCE = 8;
const int LZFCodec::MAX_DISTANCE = (1 << 26) - 2;
const int LZFCodec::MAX_MATCH = 65793;
const int LZFCodec::TAIL = 18;
const int LZFCodec::MIN_BLOCK_LENGTH = LZFCodec::MIN_MATCH + LZFCodec::TAIL;


bool LZFCodec::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("LZF codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("LZF codec: Invalid output block");

    // If too small, skip
    if (count < MIN_BLOCK_LENGTH)
        return false;

    if (count > input._length - input._index)
        return false;

    if (output._length - output._index < getMaxEncodedLength(count))
        return false;

    const byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    const int srcEnd = count - TAIL;
    const int maxDistance = (srcEnd < 4 * 262144) ? 262151 : MAX_DISTANCE;

    if (_hashes == nullptr)
        _hashes = new int32[1 << HASH_LOG];

    memset(_hashes, 0xFF, (1 << HASH_LOG) * sizeof(int32));
    int pos = 0;
    int anchor = 0;
    int encoded = 5;
    int repeat = 0;
    int misses = 0;

    while (pos < srcEnd - MIN_MATCH) {
        int ref = -1;
        const int startPos = pos;
        const uint64 currentWord = uint64(LittleEndian::readLong64(&src[pos]));
        const int h = hash(currentWord);
        const int candidate = _hashes[h];

        const int pos1 = startPos + 1;
        const uint64 nextWord = currentWord >> 8;
        const int refAtPos1 = pos1 - repeat;
        const bool matchAtPos1 = (repeat >= MIN_DISTANCE) && (repeat <= pos1) &&
            equals6(nextWord, &src[refAtPos1]);

        // Try only the repeat distance at the next position before the current hash.
        if (matchAtPos1 == true) {
            pos = pos1;
            ref = refAtPos1;
        }
        else {
            const int candidateDistance = pos - candidate;
            const bool prefixMatch = (candidate >= 0) && (candidateDistance <= maxDistance) &&
                equals6(currentWord, &src[candidate]);

            if (prefixMatch == false) {
                // Keep a matching candidate while it is too close to encode; later
                // positions may use it once its distance reaches MIN_DISTANCE.
                _hashes[h] = pos;
            }
            else if (candidateDistance >= MIN_DISTANCE) {
                _hashes[h] = pos;
                ref = candidate;
            }

            if (ref < 0) {
                pos = pos + 1 + (misses >> 5);
                misses++;
                continue;
            }
        }

        // Use the pos+1 bound for both searches to avoid recomputing it.
        const int remaining = srcEnd - startPos;
        const int maxMatch = (std::min)(remaining - 1, int(MAX_MATCH));

        int length = findMatch(src, pos, ref, maxMatch);

        // Probe the next position and prefer an equally long match there,
        // since it reaches one byte farther. Keep the mandatory literal tail.
        if (length < remaining) {
            const int h1 = hash(nextWord);
            const int candidate1 = _hashes[h1];
            const int candidateDistance1 = pos1 - candidate1;

            // Hash entries always refer to previously scanned positions.
            const bool prefixMatch1 = (candidate1 >= 0) && (candidateDistance1 <= maxDistance) &&
                equals6(nextWord, &src[candidate1]);

            if (candidateDistance1 >= MIN_DISTANCE) {
                _hashes[h1] = pos1;

                // Test the last four bytes before extending a competing match.
                if ((prefixMatch1 == true) &&
                    KANZI_MEM_EQ4(&src[pos1 + length - 3], &src[candidate1 + length - 3])) {
                    const int length1 = findMatch(src, pos1, candidate1, maxMatch);

                    if (length1 >= length) {
                        pos = pos1;
                        ref = candidate1;
                        length = length1;
                    }
                }
            }
            else if (prefixMatch1 == false) {
                // Keep a matching candidate while it is too close to encode;
                // later positions may use it once its distance reaches MIN_DISTANCE.
                _hashes[h1] = pos1;
            }
        }

        while ((pos > anchor) && (ref > 0) && (src[pos - 1] == src[ref - 1]) && (length < MAX_MATCH)) {
            pos--;
            ref--;
            length++;
        }

        const int literals = pos - anchor;

        // Token bits    Meaning
        // 0000MMMM      Repeat-distance match.
        // 00FFLLLL      Literal run (FF=01,10,11).
        // FFDDMMMM      Explicit-distance match (FF=01,10,11); FF selects
        //               1-3 distance bytes and DD holds the low two bits of (distance - 8).
        // Literal token 0x3F extends from length 48.
        if (literals > 0) {
            if (literals < 48) {
                dst[encoded++] = byte(literals + 15);
            }
            else {
                if (KANZI_UNLIKELY(literals >= (1 << 24)))
                    return false;

                dst[encoded++] = byte(0x3F);
                encoded += writeLength(&dst[encoded], literals - 48);
            }

            for (int i = 0; i < literals; i += 16)
                KANZI_MEM_CP16(&dst[encoded + i], &src[anchor + i]);

            encoded += literals;
        }

        // For either match type, MMMM=0..14 means length 6..20; MMMM=15
        // adds an extension for (length - 21).
        const int lenExtra = length - MIN_MATCH;
        const int lenNibble = (lenExtra < 15) ? lenExtra : 15;
        const int distance = pos - ref;

        if (distance == repeat) {
            // Repeat match token 0000MMMM: reuse the last match distance.
            // The fixed prefix is a type marker, not encoded distance bits.
            dst[encoded++] = byte(lenNibble);
        }
        else {
            // Pack (distance - 8): low two bits in DD, remaining bits in
            // 1/2/3 little-endian bytes. One byte covers distances 8..1031;
            // two cover 1032..262151; larger distances use three bytes.
            const uint32 value = uint32(distance - MIN_DISTANCE);
            const int width = (value < 1024) ? 1 : ((value < 262144) ? 2 : 3);
            dst[encoded++] = byte((width << 6) | ((value & 3) << 4) | lenNibble);
            LittleEndian::writeInt32(&dst[encoded], int32(value >> 2));
            encoded += width;
        }

        if (lenNibble == 15)
            encoded += writeLength(&dst[encoded], lenExtra - 15);

        anchor = pos + length;
        _hashes[hash(&src[pos + 2])] = pos + 2;
        _hashes[hash(&src[pos + 4])] = pos + 4;
        _hashes[hash(&src[anchor - 4])] = anchor - 4;
        _hashes[hash(&src[anchor - 3])] = anchor - 3;
        _hashes[hash(&src[anchor - 1])] = anchor - 1;
        pos = anchor;
        repeat = distance;
        misses = 0;
    }

    const int literals = count - anchor;

    if (literals < 48)
        dst[encoded++] = byte(literals + 15);
    else {
        if (KANZI_UNLIKELY(literals >= (1 << 24)))
            return false;

        dst[encoded++] = byte(0x3F);
        encoded += writeLength(&dst[encoded], literals - 48);
    }

    memcpy(&dst[encoded], &src[anchor], size_t(literals));
    encoded += literals;

    if (encoded > count - count / 100)
        return false;

    LittleEndian::writeInt32(dst, count);
    dst[4] = byte(((MIN_MATCH - 2) << 1) | (maxDistance == MAX_DISTANCE));
    input._index += count;
    output._index += encoded;
    return true;
}

bool LZFCodec::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input) || !SliceArray<byte>::isValid(output) ||
        (count < 6) || (input._array == output._array) ||
        (count > input._length - input._index - 4) || (output._length - output._index < 15))
        return false;

    const byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    const int size = LittleEndian::readInt32(src);

    if ((size < 24) || (size > output._length - output._index - 15))
        return false;

    const int flags = int(src[4]);

    if ((flags & 0xE0) != 0)
        return false;

    const int minMatch = ((flags >> 1) & 15) + 2;
    const int maxDistance = (flags & 1) ? MAX_DISTANCE : 262151;
    const byte* ip = &src[5];
    const byte* const iend = &src[count];
    byte* op = dst;
    byte* const oend = &dst[size];
    int repeat = 0;

    while (ip < iend) {
        // 0000MMMM: repeat; 00FFLLLL: literal; FFDDMMMM: explicit match.
        const int token = int(*ip++);

         if ((token ^ 0x30) >= 0x30) {
            // Match token
            int distance = repeat;
            const int width = token >> 6;

            if (width != 0) {
                if (KANZI_UNLIKELY(width > iend - ip))
                    return false;

                static const uint32 MASK[4] = { 0, 255, 65535, 16777215 };
                const uint32 value = uint32(LittleEndian::readInt32(ip)) & MASK[width];
                const uint32 lowDistance = uint32((token >> 4) & 3);
                distance = int((value << 2) | lowDistance) + MIN_DISTANCE;
                ip = &ip[width];
            }

            int length = token & 15;

            if (KANZI_UNLIKELY((distance < MIN_DISTANCE) || (distance > maxDistance) || (distance > op - dst)))
                return false;

            if (length == 15) {
                // MMMM=15 extends from length 15 + minMatch.
                int extra;
                int idx = 0;

                if (!readLength(ip, idx, int(iend - ip), extra))
                    return false;

                ip = &ip[idx];
                length += extra;
            }

            length += minMatch;

            if (KANZI_UNLIKELY(length > oend - op - TAIL))
                return false;

            // For matches up to 20 bytes, a fixed 20-byte copy is safe
            // when the mandatory literal tail still fits.
            if (length <= 20) {
                const byte* ref = &op[-distance];

                if (distance >= 16) {
                    KANZI_MEM_CP16(op, ref);
                    memcpy(&op[16], &ref[16], 4);
                }
                else {
                    KANZI_MEM_CP8(op, ref);
                    KANZI_MEM_CP8(&op[8], &ref[8]);
                    memcpy(&op[16], &ref[16], 4);
                }

                op = &op[length];
                repeat = distance;
                continue;
            }

            byte* const end = &op[length];
            const byte* ref = &op[-distance];

            if (distance >= 16) {
                do {
                    KANZI_MEM_CP16(op, ref);
                    op = &op[16];
                    ref = &ref[16];
                } while (op < end);
            }
            else {
                do {
                    KANZI_MEM_CP8(op, ref);
                    op = &op[8];
                    ref = &ref[8];
                } while (op < end);
            }

            op = end;
            repeat = distance;
        }
        else {
            // Literal tokens 16..62 encode lengths 1..47.
            // Values 0..15 are repeat matches.
            // Token 0x3F extends from literal length 48.
            int literals = token - 15;

            if (token == 0x3F) {
                int extra;
                int idx = 0;

                if (!readLength(ip, idx, int(iend - ip), extra))
                    return false;

                ip = &ip[idx];
                literals = 48 + extra;
            }

            if (literals == iend - ip) {
                if (KANZI_UNLIKELY((literals < TAIL) || (literals != oend - op)))
                    return false;

                memcpy(op, ip, size_t(literals));
                input._index += count;
                output._index += size;
                return true;
            }

            if (KANZI_UNLIKELY((oend - op - literals < TAIL) || (iend - ip - literals < TAIL)))
                return false;

            // Emit literals
            for (int i = 0; i < literals; i += 16)
                KANZI_MEM_CP16(&op[i], &ip[i]);

            op = &op[literals];
            ip = &ip[literals];
        }
    }

    return false;
}


const uint LZPXCodec::HASH_LOG = 17;
const uint LZPXCodec::HASH_SHIFT = 15;
const int LZPXCodec::CHUNK_SIZE = 65536;
const int LZPXCodec::MIN_CHUNK_SIZE = 34;
const int LZPXCodec::MIN_ABSENT_CODES = 16;

// Each chunk starts with the minimum match length - 2 and a 256-bit literal
// bitmap (least significant bit first). Absent bytes, in ascending order,
// encode end-of-block, then consecutive lengths starting at the minimum.
// The predictor is shared across subblocks and updated for every source byte.
bool LZPXCodec::forward(SliceArray<kanzi::byte>& input, SliceArray<kanzi::byte>& output, int count)
{
    if (count == 0)
        return true;

    if (!SliceArray<kanzi::byte>::isValid(input))
        throw invalid_argument("LZPX codec: Invalid input block");

    if (!SliceArray<kanzi::byte>::isValid(output))
        throw invalid_argument("LZPX codec: Invalid output block");

    if ((count < 4) || (count > input._length - input._index) ||
        (count > output._length - output._index) || (input._array == output._array))
        return false;

    if (_hashes == nullptr)
        _hashes = new int32[1 << HASH_LOG];

    memset(_hashes, 0, sizeof(int32) * (1 << HASH_LOG));
    const kanzi::byte* src = &input._array[input._index];
    kanzi::byte* dst = &output._array[output._index];
    const int minMatch = 2;
    int srcIdx = 0;
    int dstIdx = 0;

    // Chunks need their own token alphabet: absent byte values encode the
    // end marker and match lengths. Limit chunks to 64 KiB and reserve at
    // least MIN_ABSENT_CODES values; the predictor table spans all chunks.
    while (srcIdx < count) {
        int size = min(count - srcIdx, CHUNK_SIZE);
        uint freqs[256] = { 0 };
        Global::computeHistogram(&src[srcIdx], size, freqs);
        int alphabet = 0;

        for (int i = 0; i < 256; i++)
            alphabet += (freqs[i] != 0);

        // Keep at least 32 match codes, as in flzp. Dense alphabets need
        // smaller subblocks; locate the first prefix with 223 distinct bytes.
        if (alphabet > 256 - MIN_ABSENT_CODES) {
            memset(freqs, 0, sizeof(freqs));
            alphabet = 0;
            size = 0;

            while (alphabet < 256 - MIN_ABSENT_CODES) {
                const uint val = uint(src[srcIdx + size++]);
                alphabet += (freqs[val]++ == 0);
            }
        }

        if (count - dstIdx < MIN_CHUNK_SIZE)
            return false;

        dst[dstIdx++] = kanzi::byte(minMatch - 2);
        memset(&dst[dstIdx], 0, 32);
        kanzi::byte codes[256];
        int nbCodes = 0;

        // Mark present bytes as literals; absent bytes become end and match codes.
        for (int i = 0; i < 256; i++) {
            if (freqs[i] == 0)
                codes[nbCodes++] = kanzi::byte(i);
            else
                dst[dstIdx + (i >> 3)] |= kanzi::byte(1 << (i & 7));
        }

        dstIdx += 32;
        const int end = srcIdx + size;
        const int maxMatch = minMatch + nbCodes - 2;

        if (srcIdx == 0) {
            // The first four bytes have no preceding context, so emit them as literals.
            while (srcIdx < 4) {
                if (dstIdx >= count)
                    return false;

                dst[dstIdx++] = src[srcIdx++];
            }
        }

        // Main loop
        while (srcIdx < end) {
            int length = 0;
            const uint32 h = hash(uint32(LittleEndian::readInt32(&src[srcIdx - 4])), HASH_SHIFT);
            const int ref = _hashes[h];

            if (ref != 0) {
                const int limit = min(maxMatch, end - srcIdx);
                length = findMatch(src, srcIdx, ref, limit);
            }

            if (dstIdx >= count)
                return false;

            if (length < minMatch) {
                dst[dstIdx++] = src[srcIdx];
                _hashes[hash(uint32(LittleEndian::readInt32(&src[srcIdx - 4])), HASH_SHIFT)] = srcIdx;
                srcIdx++;
            }
            else {
                dst[dstIdx++] = codes[length - minMatch + 1];
                const int next = srcIdx + length;

                // Update hash positions
                while (srcIdx < next) {
                    _hashes[hash(uint32(LittleEndian::readInt32(&src[srcIdx - 4])), HASH_SHIFT)] = srcIdx;
                    srcIdx++;
                }
            }
        }

        if (dstIdx >= count)
            return false;

        dst[dstIdx++] = codes[0];
    }

    if (dstIdx > count - count / 100)
        return false;

    input._index += count;
    output._index += dstIdx;
    return true;
}


bool LZPXCodec::inverse(SliceArray<kanzi::byte>& input, SliceArray<kanzi::byte>& output, int count)
{
    if (count == 0)
        return true;

    if (!SliceArray<kanzi::byte>::isValid(input))
        throw invalid_argument("LZPX codec: Invalid input block");

    if (!SliceArray<kanzi::byte>::isValid(output))
        throw invalid_argument("LZPX codec: Invalid output block");

    if ((count < MIN_CHUNK_SIZE) || (count > input._length - input._index) || (input._array == output._array))
        return false;

    if (_hashes == nullptr)
        _hashes = new int32[1 << HASH_LOG];

    memset(_hashes, 0, sizeof(int32) * (1 << HASH_LOG));
    const kanzi::byte* src = &input._array[input._index];
    kanzi::byte* dst = &output._array[output._index];
    const int dstEnd = output._length - output._index;
    int srcIdx = 0;
    int dstIdx = 0;

    // Each chunk header defines its token alphabet. Decode through its end
    // marker before reading the next header; the predictor table spans chunks.
    while (srcIdx < count) {
        if (count - srcIdx < MIN_CHUNK_SIZE)
            return false;

        const int minMatch = int(uint(src[srcIdx++])) + 2;

        int lengths[256];
        int nbCodes = 0;

        for (int i = 0; i < 256; i++) {
            if ((uint(src[srcIdx + (i >> 3)]) & (1 << (i & 7))) != 0)
                lengths[i] = -1;
            else
                lengths[i] = (nbCodes++ == 0) ? 0 : minMatch + nbCodes - 2;
        }

        srcIdx += 32;

        if (nbCodes < 2)
            return false;

        const int blockStart = dstIdx;
        bool endBlock = false;

        // Main loop
        while (srcIdx < count) {
            const kanzi::byte val = src[srcIdx++];
            int length = lengths[uint(val)];

            if (length == 0) {
                endBlock = true;
                break;
            }

            int ref = 0;

            if (length > 0) {
                if (dstIdx < 4)
                    return false;

                ref = _hashes[hash(uint32(LittleEndian::readInt32(&dst[dstIdx - 4])), HASH_SHIFT)];

                if ((ref == 0) || (ref >= dstIdx))
                    return false;
            }
            else {
                length = 1;
            }

            if ((length > dstEnd - dstIdx) || (length > CHUNK_SIZE - (dstIdx - blockStart)))
                return false;

            const int next = dstIdx + length;

            while (dstIdx < next) {
                if (dstIdx >= 4)
                    _hashes[hash(uint32(LittleEndian::readInt32(&dst[dstIdx - 4])), HASH_SHIFT)] = dstIdx;

                dst[dstIdx++] = (ref == 0) ? val : dst[ref++];
            }
        }

        if ((endBlock == false) || (dstIdx == blockStart))
            return false;
    }

    input._index += count;
    output._index += dstIdx;
    return true;
}
