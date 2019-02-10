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

#include <fstream>
#include <iostream>
#include <sstream>
#include <streambuf>
#include "ROLZCodec.hpp"
#include "../IllegalArgumentException.hpp"
#include "../Memory.hpp"
#include "../bitstream/DefaultInputBitStream.hpp"
#include "../bitstream/DefaultOutputBitStream.hpp"
#include "../entropy/ANSRangeDecoder.hpp"
#include "../entropy/ANSRangeEncoder.hpp"

using namespace kanzi;

ROLZCodec::ROLZCodec(uint logPosChecks) THROW
{
    _delegate = new ROLZCodec1(logPosChecks);
}

ROLZCodec::ROLZCodec(map<string, string>& ctx) THROW
{
    string transform = "NONE";

    if (ctx.find("transform") != ctx.end()) {
        transform = ctx["transform"];
    }

    _delegate = (transform.find("ROLZX") != string::npos) ? (Function<byte>*) new ROLZCodec2(LOG_POS_CHECKS) : 
       (Function<byte>*) new ROLZCodec1(LOG_POS_CHECKS - 1);
}

bool ROLZCodec::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count) THROW
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw IllegalArgumentException("Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw IllegalArgumentException("Invalid output block");

    if (input._array == output._array)
        return false;

    if (count > MAX_BLOCK_SIZE) {
        // Not a recoverable error: instead of silently fail the transform,
        // issue a fatal error.
        stringstream ss;
        ss << "The max ROLZCodec block size is " << MAX_BLOCK_SIZE << ", got " << count;
        throw IllegalArgumentException(ss.str());
	 }

    return _delegate->forward(input, output, count);
}

bool ROLZCodec::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count) THROW
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw IllegalArgumentException("Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw IllegalArgumentException("Invalid output block");

    if (input._array == output._array)
        return false;

    if (count > MAX_BLOCK_SIZE) {
        // Not a recoverable error: instead of silently fail the transform,
        // issue a fatal error.
        stringstream ss;
        ss << "The max ROLZCodec block size is " << MAX_BLOCK_SIZE << ", got " << count;
        throw IllegalArgumentException(ss.str());
	}

	return _delegate->inverse(input, output, count);
}

ROLZCodec1::ROLZCodec1(uint logPosChecks) THROW
{
    if ((logPosChecks < 2) || (logPosChecks > 8)) {
        stringstream ss;
        ss << "Invalid logPosChecks parameter: " << logPosChecks << " (must be in [2..8])";
        throw IllegalArgumentException(ss.str());
    }

    _logPosChecks = logPosChecks;
    _posChecks = 1 << logPosChecks;
    _maskChecks = _posChecks - 1;
    _matches = new int32[ROLZCodec::HASH_SIZE << logPosChecks];
}

// return position index (_logPosChecks bits) + length (8 bits) or -1
int ROLZCodec1::findMatch(const byte buf[], const int pos, const int end)
{
    const uint32 key = ROLZCodec::getKey(&buf[pos - 2]);
    prefetchRead(&_matches[key << _logPosChecks]);
    prefetchRead(&_counters[key]);
    int32* matches = &_matches[key << _logPosChecks];
    const int32 hash32 = ROLZCodec::hash(&buf[pos]);
    const int32 counter = _counters[key];
    int bestLen = ROLZCodec::MIN_MATCH - 1;
    int bestIdx = -1;
    const byte* curBuf = &buf[pos];
    const int maxMatch = (end - pos >= ROLZCodec::MAX_MATCH) ? ROLZCodec::MAX_MATCH : end - pos;

    // Check all recorded positions
    for (int i = counter ; i > counter - _posChecks; i--) {
        int32 ref = matches[i & _maskChecks];

        if (ref == 0)
            break;

        // Hash check may save a memory access ...
        if ((ref & ROLZCodec::HASH_MASK) != hash32)
            continue;

        ref &= ~ROLZCodec::HASH_MASK;

        if (buf[ref] != curBuf[0])
            continue;

        int n = 1;

        while ((n < maxMatch) && (buf[ref + n] == curBuf[n]))
            n++;

        if (n > bestLen) {
            bestIdx = counter - i;
            bestLen = n;

            if (bestLen == maxMatch)
                break;
        }
    }

    // Register current position
    _counters[key]++;
    matches[(counter + 1) & _maskChecks] = hash32 | int32(pos);
    return (bestLen < ROLZCodec::MIN_MATCH) ? -1 : (bestIdx << 8) | (bestLen - ROLZCodec::MIN_MATCH);
}

bool ROLZCodec1::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count) THROW
{
    if (output._length < getMaxEncodedLength(count))
        return false;

    const int srcEnd = count - 4;
    byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    BigEndian::writeInt32(&dst[0], count);
    int dstIdx = 4;
    int sizeChunk = (count <= ROLZCodec::CHUNK_SIZE) ? count : ROLZCodec::CHUNK_SIZE;
    int startChunk = 0;
    SliceArray<byte> litBuf(new byte[getMaxEncodedLength(sizeChunk)], getMaxEncodedLength(sizeChunk));
    SliceArray<byte> mLenBuf(new byte[sizeChunk/2], sizeChunk/2);
    SliceArray<byte> mIdxBuf(new byte[sizeChunk/2], sizeChunk/2);
    memset(&_counters[0], 0, sizeof(int32) * 65536);
    bool success = true;

    while (startChunk < srcEnd) {
        litBuf._index = 0;
        mLenBuf._index = 0;
        mIdxBuf._index = 0;
        memset(&_matches[0], 0, sizeof(int32) * (ROLZCodec::HASH_SIZE << _logPosChecks));
        const int endChunk = (startChunk + sizeChunk < srcEnd) ? startChunk + sizeChunk : srcEnd;
        sizeChunk = endChunk - startChunk;
        byte* buf = &src[startChunk];
        int srcIdx = 0;
        litBuf._array[litBuf._index++] = buf[srcIdx++];

        if (startChunk+1 < srcEnd)
            litBuf._array[litBuf._index++] = buf[srcIdx++];

        int firstLitIdx = srcIdx;

        while (srcIdx < sizeChunk) {
            const int match = findMatch(buf, srcIdx, sizeChunk);

            if (match == -1) {
                srcIdx++;
                continue;
            }

            const int length = srcIdx - firstLitIdx;
            ROLZCodec1::emitLiteralLength(litBuf, length);

            // Emit literals
            if (length > 0) {
                memcpy(&litBuf._array[litBuf._index], &buf[firstLitIdx], length);
                litBuf._index += length;
            }

            // Emit match
            mLenBuf._array[mLenBuf._index++] = byte(match);
            mIdxBuf._array[mIdxBuf._index++] = byte(match>>8);
            srcIdx += ((match & 0xFF) + ROLZCodec::MIN_MATCH);
            firstLitIdx = srcIdx;
        }

        // Emit last chunk literals
        const int length = srcIdx - firstLitIdx;
        ROLZCodec1::emitLiteralLength(litBuf, length);
        memcpy(&litBuf._array[litBuf._index], &buf[firstLitIdx], length);
        litBuf._index += length;
        stringbuf buffer;
        iostream ios(&buffer);

        // Scope to deallocate resources early
        {
           // Encode literal, match length and match index buffers
           DefaultOutputBitStream obs(ios, 65536);
           obs.writeBits(litBuf._index, 32);
           ANSRangeEncoder litEnc(obs, 1);
           litEnc.encode(litBuf._array, 0, litBuf._index);
           litEnc.dispose();
           obs.writeBits(mLenBuf._index, 32);
           ANSRangeEncoder mLenEnc(obs, 0);
           mLenEnc.encode(mLenBuf._array, 0, mLenBuf._index);
           mLenEnc.dispose();
           //obs.writeBits(mIdxBuf._index, 32);
           ANSRangeEncoder mIdxEnc(obs, 0);
           mIdxEnc.encode(mIdxBuf._array, 0, mIdxBuf._index);
           mIdxEnc.dispose();
           obs.close();
           ios.flush();
        }

        // Copy bitstream array to output
        const int bufSize = int(ios.tellp());

        if (dstIdx + bufSize > output._length) {
            input._index = startChunk + srcIdx;
            success = false;
            goto End;
        }

        ios.seekg(0);
        ios.read(reinterpret_cast<char*>(&dst[dstIdx]), bufSize);
        dstIdx += bufSize;
        startChunk = endChunk;
    }

End: 
    if (success) {
        // Emit last literals
        dst[dstIdx++] = src[srcEnd];
        dst[dstIdx++] = src[srcEnd+1];
        dst[dstIdx++] = src[srcEnd+2];
        dst[dstIdx++] = src[srcEnd+3];
        input._index = srcEnd + 4;
    }

    output._index = dstIdx;
    delete[] litBuf._array;
    delete[] mLenBuf._array;
    delete[] mIdxBuf._array;
    return input._index == count;
}

void ROLZCodec1::emitLiteralLength(SliceArray<byte>& litBuf, const int length)
{
    if (length >= 1<<7) {
        if (length >= 1<<21)
            litBuf._array[litBuf._index++] = byte(0x80|((length>>21)&0x7F));

        if (length >= 1<<14)
            litBuf._array[litBuf._index++] = byte(0x80|((length>>14)&0x7F));

        litBuf._array[litBuf._index++] = byte(0x80|((length>>7)&0x7F));
    }

    litBuf._array[litBuf._index++] = byte(length&0x7F);
}


bool ROLZCodec1::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count) THROW
{
    byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    const int dstEnd = BigEndian::readInt32(&src[0]) - 4;
    int srcIdx = 4;
    int sizeChunk = (dstEnd < ROLZCodec::CHUNK_SIZE) ? dstEnd : ROLZCodec::CHUNK_SIZE;
    int startChunk = 0;
    stringbuf buffer;
    iostream ios(&buffer);
    ios.rdbuf()->sputn(reinterpret_cast<char*>(&src[4]), count - 4);
    ios.rdbuf()->pubseekpos(0);
    SliceArray<byte> litBuf(new byte[getMaxEncodedLength(sizeChunk)], getMaxEncodedLength(sizeChunk));
    SliceArray<byte> mLenBuf(new byte[sizeChunk/2], sizeChunk/2);
    SliceArray<byte> mIdxBuf(new byte[sizeChunk/2], sizeChunk/2);
    memset(&_counters[0], 0, sizeof(int32) * 65536);
    bool success = true;

    while (startChunk < dstEnd) {
        litBuf._index = 0;
        mLenBuf._index = 0;
        mIdxBuf._index = 0;
        memset(&_matches[0], 0, sizeof(int32) * (ROLZCodec::HASH_SIZE << _logPosChecks));
        const int endChunk = (startChunk + sizeChunk < dstEnd) ? startChunk + sizeChunk : dstEnd;
        sizeChunk = endChunk - startChunk;
        
        // Scope to deallocate resources early
        {
            // Decode literal, match length and match index buffers
            ios.rdbuf()->pubseekpos(srcIdx - 4);
            DefaultInputBitStream ibs(ios, 65536);
            int length = int(ibs.readBits(32));

            if (length <= sizeChunk) {
               ANSRangeDecoder litDec(ibs, 1);
               litDec.decode(litBuf._array, 0, length);
               litDec.dispose();
               length = int(ibs.readBits(32));
            }

            if (length <= sizeChunk) {
               ANSRangeDecoder mLenDec(ibs, 0);
               mLenDec.decode(mLenBuf._array, 0, length);
               mLenDec.dispose();
               ANSRangeDecoder mIdxDec(ibs, 0);
               mIdxDec.decode(mIdxBuf._array, 0, length);
               mIdxDec.dispose();
            }

            srcIdx += int((ibs.read() + 7) >> 3);
            ibs.close();

            if (length > sizeChunk) {
               input._index = srcIdx;
               output._index = startChunk;
               break;
            }
        }
         
        byte* buf = &output._array[output._index];
        int dstIdx = 0;
        buf[dstIdx++] = litBuf._array[litBuf._index++];

        if (output._index+1 < dstEnd)
            buf[dstIdx++] = litBuf._array[litBuf._index++];

        // Next chunk
        while (dstIdx < sizeChunk) {
            const int length = emitLiterals(litBuf, buf, dstIdx, output._index);
            litBuf._index += length;
            dstIdx += length;

            if (dstIdx >= endChunk) {
                  // Last chunk literals not followed by match
                  if (dstIdx == endChunk)
                        break;

                  output._index = dstIdx;
                  success = false;
                  goto End;
            }

            const int matchLen = mLenBuf._array[mLenBuf._index++] & 0xFF;

            // Sanity check
            if (dstIdx + matchLen + 3 > dstEnd) {
                  output._index = dstIdx;
                  success = false;
                  goto End;
            }

            const uint32 key = ROLZCodec::getKey(&buf[dstIdx - 2]);
            prefetchRead(&_counters[key]);
            const int matchIdx = mIdxBuf._array[mIdxBuf._index++] & 0xFF;
            int32* matches = &_matches[key << _logPosChecks];
            const int32 ref = output._index + matches[(_counters[key] - matchIdx) & _maskChecks];
            const int32 savedIdx = dstIdx;
            dstIdx = ROLZCodec::emitCopy(buf, dstIdx, ref, matchLen);
            _counters[key]++;
            matches[_counters[key] & _maskChecks] = savedIdx - output._index;
        }

        startChunk = endChunk;
        output._index += dstIdx;
    }

End:
    if (success) {
        // Emit last chunk literals
        dst[output._index++] = src[srcIdx++];
        dst[output._index++] = src[srcIdx++];
        dst[output._index++] = src[srcIdx++];
        dst[output._index++] = src[srcIdx++];
    }

    input._index = srcIdx;
    delete[] litBuf._array;
    delete[] mLenBuf._array;
    delete[] mIdxBuf._array;
    return srcIdx == count;
}

int ROLZCodec1::emitLiterals(SliceArray<byte>& litBuf, byte dst[], int dstIdx, int startIdx)
{
	// Read length
   int litLen = litBuf._array[litBuf._index++];
   int length = litLen & 0x7F;

   if ((litLen & 0x80) != 0) {
      litLen = litBuf._array[litBuf._index++];
      length = (length << 7) | (litLen & 0x7F);

      if ((litLen & 0x80) != 0) {
         litLen = litBuf._array[litBuf._index++];
         length = (length << 7) | (litLen & 0x7F);

         if ((litLen & 0x80) != 0) {
            litLen = litBuf._array[litBuf._index++];
            length = (length << 7) | (litLen & 0x7F);
         }
      }
   }

   // Emit literal bytes
   memcpy(&dst[dstIdx], &litBuf._array[litBuf._index], length);

   for (int n = 0; n < length; n++) {
      const uint32 key = ROLZCodec::getKey(&dst[dstIdx + n - 2]);
      int32* matches = &_matches[key << _logPosChecks];
      _counters[key]++;
      matches[_counters[key] & _maskChecks] = dstIdx + n - startIdx;
   }

   return length;
}


int ROLZCodec1::getMaxEncodedLength(int srcLen) const 
{
   return (srcLen <= 512) ? srcLen+32 : srcLen;
}

ROLZPredictor::ROLZPredictor(uint logPosChecks)
{
    _logSize = logPosChecks;
    _size = 1 << logPosChecks;
    _p = new uint32[256 * _size];
    reset();
}

void ROLZPredictor::reset()
{
    _c1 = 1;
    _ctx = 0;

    for (int i = 0; i < 256 * _size; i++) 
        _p[i] = (32768 << 16) | 32768;
}


ROLZEncoder::ROLZEncoder(Predictor* predictors[2], byte buf[], int& idx)
    : _idx(idx)
    , _low(0)
    , _high(TOP)
{
    _buf = buf;
    _predictors[0] = predictors[0];
    _predictors[1] = predictors[1];
    _predictor = _predictors[0];
}

void ROLZEncoder::encodeByte(byte val)
{
    encodeBit((val >> 7) & 1);
    encodeBit((val >> 6) & 1);
    encodeBit((val >> 5) & 1);
    encodeBit((val >> 4) & 1);
    encodeBit((val >> 3) & 1);
    encodeBit((val >> 2) & 1);
    encodeBit((val >> 1) & 1);
    encodeBit(val & 1);
}

void ROLZEncoder::encodeBit(int bit)
{
    // Calculate interval split
    const uint64 split = (((_high - _low) >> 4) * uint64(_predictor->get())) >> 8;

    // Update fields with new interval bounds
    _high -= (-bit & (_high - _low - split));
    _low += (~ - bit & (split + 1));

    // Update predictor
    _predictor->update(bit);

    // Emit unchanged first 32 bits
    while (((_low ^ _high) >> 24) == 0) {
        BigEndian::writeInt32(&_buf[_idx], int32(_high >> 32));
        _idx += 4;
        _low <<= 32;
        _high = (_high << 32) | MASK_0_32;
    }
}

void ROLZEncoder::dispose()
{
    for (int i = 0; i < 8; i++) {
        _buf[_idx + i] = byte(_low >> 56);
        _low <<= 8;
    }

    _idx += 8;
}

ROLZDecoder::ROLZDecoder(Predictor* predictors[2], byte buf[], int& idx)
    : _idx(idx)
    , _low(0)
    , _high(TOP)
    , _current(0)
{
    _buf = buf;

    for (int i = 0; i < 8; i++)
        _current = (_current << 8) | uint64(_buf[_idx + i] & 0xFF);

    _idx += 8;
    _predictors[0] = predictors[0];
    _predictors[1] = predictors[1];
    _predictor = _predictors[0];
}

byte ROLZDecoder::decodeByte()
{
    return byte((decodeBit() << 7) | (decodeBit() << 6) | (decodeBit() << 5) | (decodeBit() << 4) | (decodeBit() << 3) | (decodeBit() << 2) | (decodeBit() << 1) | decodeBit());
}

int ROLZDecoder::decodeBit()
{
    // Calculate interval split
    const uint64 mid = _low + ((((_high - _low) >> 4) * uint64(_predictor->get())) >> 8);
    int bit;

    // Update predictor
    if (mid >= _current) {
        bit = 1;
        _high = mid;
        _predictor->update(1);
    }
    else {
        bit = 0;
        _low = mid + 1;
        _predictor->update(0);
    }

    // Read 32 bits
    while (((_low ^ _high) >> 24) == 0) {
        _low = (_low << 32) & MASK_0_56;
        _high = ((_high << 32) | MASK_0_32) & MASK_0_56;
        const uint64 val = uint64(BigEndian::readInt32(&_buf[_idx])) & MASK_0_32;
        _current = ((_current << 32) | val) & MASK_0_56;
        _idx += 4;
    }

    return bit;
}

ROLZCodec2::ROLZCodec2(uint logPosChecks) THROW
    : _litPredictor(9)
    , _matchPredictor(logPosChecks)
{
    if ((logPosChecks < 2) || (logPosChecks > 8)) {
        stringstream ss;
        ss << "Invalid logPosChecks parameter: " << logPosChecks << " (must be in [2..8])";
        throw IllegalArgumentException(ss.str());
    }

    _logPosChecks = logPosChecks;
    _posChecks = 1 << logPosChecks;
    _maskChecks = _posChecks - 1;
    _matches = new int32[ROLZCodec::HASH_SIZE << logPosChecks];
}

// return position index (_logPosChecks bits) + length (8 bits) or -1
int ROLZCodec2::findMatch(const byte buf[], const int pos, const int end)
{
    const uint32 key = ROLZCodec::getKey(&buf[pos - 2]);
    prefetchRead(&_matches[key << _logPosChecks]);
    prefetchRead(&_counters[key]);
    int32* matches = &_matches[key << _logPosChecks];
    const int32 hash32 = ROLZCodec::hash(&buf[pos]);
    const int32 counter = _counters[key];
    int bestLen = ROLZCodec::MIN_MATCH - 1;
    int bestIdx = -1;
    const byte* curBuf = &buf[pos];
    const int maxMatch = (end - pos >= ROLZCodec::MAX_MATCH) ? ROLZCodec::MAX_MATCH : end - pos;

    // Check all recorded positions
    for (int i = counter ; i > counter - _posChecks; i--) {
        int32 ref = matches[i & _maskChecks];

        if (ref == 0)
            break;

        // Hash check may save a memory access ...
        if ((ref & ROLZCodec::HASH_MASK) != hash32)
            continue;

        ref &= ~ROLZCodec::HASH_MASK;

        if (buf[ref] != curBuf[0])
            continue;

        int n = 1;

        while ((n < maxMatch) && (buf[ref + n] == curBuf[n]))
            n++;

        if (n > bestLen) {
            bestIdx = counter - i;
            bestLen = n;

            if (bestLen == maxMatch)
                break;
        }
    }

    // Register current position
    _counters[key]++;
    matches[(counter + 1) & _maskChecks] = hash32 | int32(pos);
    return (bestLen < ROLZCodec::MIN_MATCH) ? -1 : (bestIdx << 8) | (bestLen - ROLZCodec::MIN_MATCH);
}

bool ROLZCodec2::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count) THROW
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw IllegalArgumentException("Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw IllegalArgumentException("Invalid output block");

    if (input._array == output._array)
        return false;

    if (output._length < getMaxEncodedLength(count))
        return false;

    const int srcEnd = count - 4;
    byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    BigEndian::writeInt32(&dst[0], count);
    int srcIdx = 0;
    int dstIdx = 4;
    int sizeChunk = (count <= ROLZCodec::CHUNK_SIZE) ? count : ROLZCodec::CHUNK_SIZE;
    int startChunk = 0;
    _litPredictor.reset();
    _matchPredictor.reset();
    Predictor* predictors[2] = { &_litPredictor, &_matchPredictor };
    ROLZEncoder re(predictors, &dst[0], dstIdx);
    memset(&_counters[0], 0, sizeof(int32) * 65536);

    while (startChunk < srcEnd) {
        memset(&_matches[0], 0, sizeof(int32) * (ROLZCodec::HASH_SIZE << _logPosChecks));
        const int endChunk = (startChunk + sizeChunk < srcEnd) ? startChunk + sizeChunk : srcEnd;
        sizeChunk = endChunk - startChunk;
        src = &input._array[startChunk];
        srcIdx = 0;
        _litPredictor.setContext(0);
        re.setContext(LITERAL_FLAG);
        re.encodeBit(LITERAL_FLAG);
        re.encodeByte(src[srcIdx++]);

        if (startChunk + 1 < srcEnd) {
            re.encodeBit(LITERAL_FLAG);
            re.encodeByte(src[srcIdx++]);
        }

        while (srcIdx < sizeChunk) {
            _litPredictor.setContext(src[srcIdx - 1]);
            re.setContext(LITERAL_FLAG);
            const int match = findMatch(src, srcIdx, sizeChunk);

            if (match == -1) {
                re.encodeBit(LITERAL_FLAG);
                re.encodeByte(src[srcIdx]);
                srcIdx++;
            }
            else {
                const int matchLen = match & 0xFF;
                re.encodeBit(MATCH_FLAG);
                re.encodeByte(byte(matchLen));
                const int matchIdx = match >> 8;
                _matchPredictor.setContext(src[srcIdx - 1]);
                re.setContext(MATCH_FLAG);

                for (int shift = _logPosChecks - 1; shift >= 0; shift--)
                    re.encodeBit((matchIdx >> shift) & 1);

                srcIdx += (matchLen + ROLZCodec::MIN_MATCH);
            }
        }

        startChunk = endChunk;
    }

    // Emit last literals
    re.setContext(LITERAL_FLAG);

    for (int i = 0; i < 4; i++, srcIdx++) {
        _litPredictor.setContext(src[srcIdx - 1]);
        re.encodeBit(LITERAL_FLAG);
        re.encodeByte(src[srcIdx]);
    }

    re.dispose();
    input._index = startChunk - sizeChunk + srcIdx;
    output._index = dstIdx;
    return input._index == count;
}

bool ROLZCodec2::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count) THROW
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw IllegalArgumentException("Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw IllegalArgumentException("Invalid output block");

    if (input._array == output._array)
        return false;

    byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    int srcIdx = 0;
    const int dstEnd = BigEndian::readInt32(&src[srcIdx]);
    srcIdx += 4;
    int sizeChunk = (dstEnd < ROLZCodec::CHUNK_SIZE) ? dstEnd : ROLZCodec::CHUNK_SIZE;
    int startChunk = 0;
    _litPredictor.reset();
    _matchPredictor.reset();
    Predictor* predictors[2] = { &_litPredictor, &_matchPredictor };
    ROLZDecoder rd(predictors, &src[0], srcIdx);
    memset(&_counters[0], 0, sizeof(int32) * 65536);

    while (startChunk < dstEnd) {
        memset(&_matches[0], 0, sizeof(int32) * (ROLZCodec::HASH_SIZE << _logPosChecks));
        const int endChunk = (startChunk + sizeChunk < dstEnd) ? startChunk + sizeChunk : dstEnd;
        sizeChunk = endChunk - startChunk;
        dst = &output._array[output._index];
        int dstIdx = 0;
        _litPredictor.setContext(0);
        rd.setContext(LITERAL_FLAG);
        int bit = rd.decodeBit();

        if (bit == LITERAL_FLAG) {
            dst[dstIdx++] = rd.decodeByte();

            if (output._index + 1 < dstEnd) {
                bit = rd.decodeBit();

                if (bit == LITERAL_FLAG)
                    dst[dstIdx++] = rd.decodeByte();
            }
        }

        // Sanity check
        if (bit == MATCH_FLAG) {
            output._index += dstIdx;
            break;
        }

        while (dstIdx < sizeChunk) {
            const int savedIdx = dstIdx;
            const uint32 key = ROLZCodec::getKey(&dst[dstIdx - 2]);
            int32* matches = &_matches[key << _logPosChecks];
            _litPredictor.setContext(dst[dstIdx - 1]);
            rd.setContext(LITERAL_FLAG);
            prefetchRead(&_counters[key]);

            if (rd.decodeBit() == MATCH_FLAG) {
                // Match flag
                const int matchLen = rd.decodeByte() & 0xFF;

                // Sanity check
                if (dstIdx + matchLen + 3 > dstEnd) {
                    output._index += dstIdx;
                    break;
                }

                _matchPredictor.setContext(dst[dstIdx - 1]);
                rd.setContext(MATCH_FLAG);
                int32 matchIdx = 0;

                for (int shift = _logPosChecks - 1; shift >= 0; shift--)
                    matchIdx |= (rd.decodeBit() << shift);

                const int32 ref = matches[(_counters[key] - matchIdx) & _maskChecks];
                dstIdx = ROLZCodec::emitCopy(dst, dstIdx, ref, matchLen);
            }
            else {
                // Literal flag
                dst[dstIdx++] = rd.decodeByte();
            }

            // Update
            _counters[key]++;
            matches[_counters[key] & _maskChecks] = savedIdx;
        }

        startChunk = endChunk;
        output._index += dstIdx;
    }

    rd.dispose();
    input._index = srcIdx;
    return srcIdx == count;
}

int ROLZCodec2::getMaxEncodedLength(int srcLen) const 
{
    // Since we do not check the dst index for each byte (for speed purpose)
    // allocate some extra buffer for incompressible data.
    if (srcLen >= ROLZCodec::CHUNK_SIZE)
        return srcLen;
         
    return (srcLen <= 512) ? srcLen + 32 : srcLen + srcLen / 8;
}