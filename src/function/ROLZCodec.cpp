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

ROLZCodec::ROLZCodec(Context& ctx) THROW
{
    string transform = ctx.getString("transform", "NONE");

    _delegate = (transform.find("ROLZX") != string::npos) ? (Function<byte>*)new ROLZCodec2(LOG_POS_CHECKS2) : 
       (Function<byte>*)new ROLZCodec1(LOG_POS_CHECKS1);
}

bool ROLZCodec::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count) THROW
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("ROLZ codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("ROLZ codec: Invalid output block");

    if (input._array == output._array)
        return false;

    if (count > MAX_BLOCK_SIZE) {
        // Not a recoverable error: instead of silently fail the transform,
        // issue a fatal error.
        stringstream ss;
        ss << "The max ROLZ codec block size is " << MAX_BLOCK_SIZE << ", got " << count;
        throw invalid_argument(ss.str());
    }

    return _delegate->forward(input, output, count);
}

bool ROLZCodec::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count) THROW
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("ROLZ codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("ROLZ codec: Invalid output block");

    if (input._array == output._array)
        return false;

    if (count > MAX_BLOCK_SIZE) {
        // Not a recoverable error: instead of silently fail the transform,
        // issue a fatal error.
        stringstream ss;
        ss << "The max ROLZ codec block size is " << MAX_BLOCK_SIZE << ", got " << count;
        throw invalid_argument(ss.str());
    }

    return _delegate->inverse(input, output, count);
}

ROLZCodec1::ROLZCodec1(uint logPosChecks) THROW
{
    if ((logPosChecks < 2) || (logPosChecks > 8)) {
        stringstream ss;
        ss << "ROLZ codec: Invalid logPosChecks parameter: " << logPosChecks << " (must be in [2..8])";
        throw invalid_argument(ss.str());
    }

    _logPosChecks = logPosChecks;
    _posChecks = 1 << logPosChecks;
    _maskChecks = _posChecks - 1;
    _matches = new int32[ROLZCodec::HASH_SIZE << logPosChecks];
    memset(&_counters[0], 0, sizeof(int32) * 65536);
}

// return position index (_logPosChecks bits) + length (16 bits) or -1
int ROLZCodec1::findMatch(const byte buf[], const int pos, const int end)
{
    const uint16 key = ROLZCodec::getKey(&buf[pos - 2]);
    prefetchRead(&_counters[key]);
    const int32 counter = _counters[key];
    int32* matches = &_matches[key << _logPosChecks];
    prefetchRead(matches);
    const byte* curBuf = &buf[pos];
    const int32 hash32 = ROLZCodec::hash(curBuf);
    int bestLen = ROLZCodec1::MIN_MATCH - 1;
    int bestIdx = -1;

    if (matches[counter & _maskChecks] != 0) {
        const int maxMatch = min(ROLZCodec1::MAX_MATCH, end - pos);

        // Check all recorded positions
        for (int i = counter; i > counter - _posChecks; i--) {
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

            while ((n + 4 < maxMatch) && (*(reinterpret_cast<const int32*>(&buf[ref + n])) == *(reinterpret_cast<const int32*>(&curBuf[n]))))
                n += 4;

            while ((n < maxMatch) && (buf[ref + n] == curBuf[n]))
                n++;

            if (n > bestLen) {
                bestIdx = counter - i;
                bestLen = n;

                if (bestLen == maxMatch)
                    break;
            }
        }
    }

    // Register current position
    _counters[key]++;
    matches[(counter + 1) & _maskChecks] = hash32 | int32(pos);
    return (bestLen < ROLZCodec1::MIN_MATCH) ? -1 : (bestIdx << 16) | (bestLen - ROLZCodec1::MIN_MATCH);
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
    SliceArray<byte> lenBuf(new byte[sizeChunk / 4], sizeChunk / 4);
    SliceArray<byte> mIdxBuf(new byte[sizeChunk / 4], sizeChunk / 4);
    memset(&_counters[0], 0, sizeof(int32) * 65536);
    bool success = true;
    const int litOrder = (count < (1 << 17)) ? 0 : 1;
    dst[dstIdx++] = byte(litOrder);
    stringbuf buffer;
    iostream os(&buffer);

    // Main loop
    while (startChunk < srcEnd) {
        buffer.pubseekpos(0);
        litBuf._index = 0;
        lenBuf._index = 0;
        mIdxBuf._index = 0;

        memset(&_matches[0], 0, sizeof(int32) * (ROLZCodec::HASH_SIZE << _logPosChecks));
        const int endChunk = (startChunk + sizeChunk < srcEnd) ? startChunk + sizeChunk : srcEnd;
        sizeChunk = endChunk - startChunk;
        byte* buf = &src[startChunk];
        int srcIdx = 0;
        litBuf._array[litBuf._index++] = buf[srcIdx++];

        if (startChunk + 1 < srcEnd)
            litBuf._array[litBuf._index++] = buf[srcIdx++];

        int firstLitIdx = srcIdx;

        while (srcIdx < sizeChunk) {
            const int match = findMatch(buf, srcIdx, sizeChunk);

            if (match < 0) {
                srcIdx++;
                continue;
            }

            const int litLen = srcIdx - firstLitIdx;
            ROLZCodec1::emitToken(lenBuf._array, lenBuf._index, litLen, match & 0xFFFF);

            // Emit literals
            if (litLen > 0) {
                memcpy(&litBuf._array[litBuf._index], &buf[firstLitIdx], litLen);
                litBuf._index += litLen;
            }

            // Emit match index
            mIdxBuf._array[mIdxBuf._index++] = byte(match >> 16);
            srcIdx += ((match & 0xFFFF) + ROLZCodec1::MIN_MATCH);
            firstLitIdx = srcIdx;
        }

        // Emit last chunk literals
        const int litLen = srcIdx - firstLitIdx;
        ROLZCodec1::emitToken(lenBuf._array, lenBuf._index, litLen, 0);
        memcpy(&litBuf._array[litBuf._index], &buf[firstLitIdx], litLen);
        litBuf._index += litLen;

        // Scope to deallocate resources early
        {
            // Encode literal, match length and match index buffers
            DefaultOutputBitStream obs(os, 65536);
            obs.writeBits(litBuf._index, 32);
            obs.writeBits(lenBuf._index, 32);
            obs.writeBits(mIdxBuf._index, 32);
            ANSRangeEncoder litEnc(obs, litOrder);
            litEnc.encode(litBuf._array, 0, litBuf._index);
            litEnc.dispose();
            ANSRangeEncoder mEnc(obs, 0);
            mEnc.encode(lenBuf._array, 0, lenBuf._index);
            mEnc.encode(mIdxBuf._array, 0, mIdxBuf._index);
            mEnc.dispose();
            obs.close();
            os.flush();
        } 

        // Copy bitstream array to output	
        const int bufSize = int(os.tellp());	

        if (dstIdx + bufSize > output._length) {	
            input._index = startChunk + srcIdx;	
            success = false;	
            goto End;	
        }	

        os.seekg(0);	
        os.read(reinterpret_cast<char*>(&dst[dstIdx]), bufSize);	
        dstIdx += bufSize;
        startChunk = endChunk;
    }

End:
    if (success == true) {
        if (dstIdx + 4 > output._length) {
            input._index = srcEnd;
        } else {
            // Emit last literals
            dst[dstIdx++] = src[srcEnd];
            dst[dstIdx++] = src[srcEnd + 1];
            dst[dstIdx++] = src[srcEnd + 2];
            dst[dstIdx++] = src[srcEnd + 3];
            input._index = srcEnd + 4;
        }
    }

    output._index = dstIdx;
    delete[] litBuf._array;
    delete[] lenBuf._array;
    delete[] mIdxBuf._array;
    return (input._index == count) && (output._index < count);
}

void ROLZCodec1::emitToken(byte block[], int& idx, int litLen, int mLen)
{
    // mode LLLLLMMM -> L lit length, M match length
    const int mode = (litLen < 31) ? (litLen << 3) : 0xF8;

    if (mLen >= 7) {
        block[idx++] = byte(mode | 0x07);
        block[idx++] = byte(mLen - 7);
    }
    else {
        block[idx++] = byte(mode | mLen);
    }

    if (litLen >= 31) {
        litLen -= 31;

        if (litLen >= 1 << 7) {
            if (litLen >= 1 << 14) {
                if (litLen >= 1 << 21)
                    block[idx++] = byte(0x80 | (litLen >> 21));

                block[idx++] = byte(0x80 | (litLen >> 14));
            }

            block[idx++] = byte(0x80 | (litLen >> 7));
        }

        block[idx++] = byte(litLen & 0x7F);
    }
}

bool ROLZCodec1::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count) THROW
{
    byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    const int dstEnd = BigEndian::readInt32(&src[0]) - 4;
    int srcIdx = 4;
    int sizeChunk = min(dstEnd, ROLZCodec::CHUNK_SIZE);
    int startChunk = 0;
    const int litOrder = int(src[srcIdx++]);
    SliceArray<byte> litBuf(new byte[getMaxEncodedLength(sizeChunk)], getMaxEncodedLength(sizeChunk));
    SliceArray<byte> lenBuf(new byte[sizeChunk / 4], sizeChunk / 4);
    SliceArray<byte> mIdxBuf(new byte[sizeChunk / 4], sizeChunk / 4);
    memset(&_counters[0], 0, sizeof(int32) * 65536);
    bool success = true;

    // Main loop
    while (startChunk < dstEnd) {
        litBuf._index = 0;
        lenBuf._index = 0;
        mIdxBuf._index = 0;
        memset(&_matches[0], 0, sizeof(int32) * (ROLZCodec::HASH_SIZE << _logPosChecks));
        const int endChunk = min(startChunk + sizeChunk, dstEnd);
        sizeChunk = endChunk - startChunk;

        // Scope to deallocate resources early
        {
            // Decode literal, length and match index buffers
            istreambuf<char> buffer(reinterpret_cast<char*>(&src[srcIdx]), count - srcIdx);
            istream is(&buffer);
            DefaultInputBitStream ibs(is, 65536);
            const int litLen = int(ibs.readBits(32));
            const int mLenLen = int(ibs.readBits(32));
            const int mIdxLen = int(ibs.readBits(32));

            if ((litLen > sizeChunk) || (mLenLen > sizeChunk) || (mIdxLen > sizeChunk)) {
                input._index = srcIdx;
                output._index = startChunk;
                success = false;
                goto End;
            }

            ANSRangeDecoder litDec(ibs, litOrder);
            litDec.decode(litBuf._array, 0, litLen);
            litDec.dispose();
            ANSRangeDecoder mDec(ibs, 0);
            mDec.decode(lenBuf._array, 0, mLenLen);
            mDec.decode(mIdxBuf._array, 0, mIdxLen);
            mDec.dispose();

            srcIdx += int((ibs.read() + 7) >> 3);
            ibs.close();
        }

        byte* buf = &output._array[output._index];
        int dstIdx = 0;
        buf[dstIdx++] = litBuf._array[litBuf._index++];

        if (output._index + 1 < dstEnd)
            buf[dstIdx++] = litBuf._array[litBuf._index++];

        // Next chunk
        while (dstIdx < sizeChunk) {
            int litLen, matchLen;
            readLengths(lenBuf._array, lenBuf._index, litLen, matchLen);
            emitLiterals(litBuf, buf, dstIdx, litLen);
            litBuf._index += litLen;
            dstIdx += litLen;

            if (dstIdx >= sizeChunk) {
                // Last chunk literals not followed by match
                if (dstIdx == sizeChunk)
                    break;

                output._index += dstIdx;
                success = false;
                goto End;
            }

            // Sanity check
            if (output._index + dstIdx + matchLen + 3 > dstEnd) {
                output._index += dstIdx;
                success = false;
                goto End;
            }

            const uint32 key = ROLZCodec::getKey(&buf[dstIdx - 2]);
            prefetchRead(&_counters[key]);
            const int matchIdx = int(mIdxBuf._array[mIdxBuf._index++]);
            int32* matches = &_matches[key << _logPosChecks];
            const int32 ref = matches[(_counters[key] - matchIdx) & _maskChecks];
            const int32 savedIdx = dstIdx;
            dstIdx = ROLZCodec::emitCopy(buf, dstIdx, ref, matchLen);
            _counters[key]++;
            matches[_counters[key] & _maskChecks] = savedIdx;
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
    delete[] lenBuf._array;
    delete[] mIdxBuf._array;
    return srcIdx == count;
}

void ROLZCodec1::readLengths(byte block[], int& idx, int& litLen, int& mLen)
{
    // mode LLLLLMMM -> L lit length, M match length
    const int mode = int(block[idx++]);
    mLen = mode & 0x07;

    if (mLen == 7)
        mLen += int(block[idx++]);

    if (mode < 0xF8) {
        litLen = mode >> 3;
        return;
    }

    int next = int(block[idx++]);
    litLen = (next & 0x7F);

    if (next >= 128) {
        next = int(block[idx++]);
        litLen = (litLen << 7) | (next & 0x7F);

        if (next >= 128) {
            next = int(block[idx++]);
            litLen = (litLen << 7) | (next & 0x7F);

            if (next >= 128) {
                next = int(block[idx++]);
                litLen = (litLen << 7) | (next & 0x7F);
            }
        }
    }

    litLen += 31;
}

int ROLZCodec1::emitLiterals(SliceArray<byte>& litBuf, byte dst[], int dstIdx, int litLen)
{
    memcpy(&dst[dstIdx], &litBuf._array[litBuf._index], litLen);

    for (int n = 0; n < litLen; n++) {
        const uint32 key = ROLZCodec::getKey(&dst[dstIdx + n - 2]);
        int32* matches = &_matches[key << _logPosChecks];
        _counters[key]++;
        matches[_counters[key] & _maskChecks] = dstIdx + n;
    }

    return litLen;
}

ROLZEncoder::ROLZEncoder(uint litLogSize, uint mLogSize, byte buf[], int& idx)
    : _idx(idx)
    , _low(0)
    , _high(TOP)
    , _c1(1)
    , _ctx(0)
    , _pIdx(LITERAL_FLAG)
{
    _buf = buf;
    _logSizes[MATCH_FLAG] = mLogSize;
    _logSizes[LITERAL_FLAG] = litLogSize;
    _probs[MATCH_FLAG] = new uint16[256 << mLogSize];
    _probs[LITERAL_FLAG] = new uint16[256 << litLogSize];
    reset();
}

void ROLZEncoder::reset()
{
    const int mLogSize = _logSizes[MATCH_FLAG];

    for (int i = 0; i < (256 << mLogSize); i++)
        _probs[MATCH_FLAG][i] = PSCALE >> 1;

    const int litLogSize = _logSizes[LITERAL_FLAG];

    for (int i = 0; i < (256 << litLogSize); i++)
        _probs[LITERAL_FLAG][i] = PSCALE >> 1;
}

void ROLZEncoder::encodeBits(int val, int n)
{
    _c1 = 1;

    do {
        n--;
        encodeBit(val & (1 << n));
    } while (n != 0);
}

void ROLZEncoder::dispose()
{
    for (int i = 0; i < 8; i++) {
        _buf[_idx + i] = byte(_low >> 56);
        _low <<= 8;
    }

    _idx += 8;
}

ROLZDecoder::ROLZDecoder(uint litLogSize, uint mLogSize, byte buf[], int& idx)
    : _idx(idx)
    , _low(0)
    , _high(TOP)
    , _current(0)
    , _buf(buf)
    , _c1(1)
    , _ctx(0)
    , _pIdx(LITERAL_FLAG)
{
    for (int i = 0; i < 8; i++)
        _current = (_current << 8) | (uint64(_buf[_idx + i]) & 0xFF);

    _idx += 8;
    _logSizes[MATCH_FLAG] = mLogSize;
    _logSizes[LITERAL_FLAG] = litLogSize;
    _probs[MATCH_FLAG] = new uint16[256 << mLogSize];
    _probs[LITERAL_FLAG] = new uint16[256 << litLogSize];
    reset();
}

void ROLZDecoder::reset()
{
    const int mLogSize = _logSizes[MATCH_FLAG];

    for (int i = 0; i < (256 << mLogSize); i++)
        _probs[MATCH_FLAG][i] = PSCALE >> 1;

    const int litLogSize = _logSizes[LITERAL_FLAG];

    for (int i = 0; i < (256 << litLogSize); i++)
        _probs[LITERAL_FLAG][i] = PSCALE >> 1;
}

int ROLZDecoder::decodeBits(int n)
{
    _c1 = 1;
    const int mask = (1 << n) - 1;

    do {
        decodeBit();
        n--;
    } while (n != 0);

    return _c1 & mask;
}

ROLZCodec2::ROLZCodec2(uint logPosChecks) THROW
{
    if ((logPosChecks < 2) || (logPosChecks > 8)) {
        stringstream ss;
        ss << "ROLZX codec: Invalid logPosChecks parameter: " << logPosChecks << " (must be in [2..8])";
        throw invalid_argument(ss.str());
    }

    _logPosChecks = logPosChecks;
    _posChecks = 1 << logPosChecks;
    _maskChecks = _posChecks - 1;
    _matches = new int32[ROLZCodec::HASH_SIZE << logPosChecks];
}

// return position index (_logPosChecks bits) + length (16 bits) or -1
int ROLZCodec2::findMatch(const byte buf[], const int pos, const int end)
{
    const uint16 key = ROLZCodec::getKey(&buf[pos - 2]);
    prefetchRead(&_counters[key]);
    const int32 counter = _counters[key];
    int32* matches = &_matches[key << _logPosChecks];
    prefetchRead(matches);
    const byte* curBuf = &buf[pos];
    const int32 hash32 = ROLZCodec::hash(curBuf);
    int bestLen = ROLZCodec2::MIN_MATCH - 1;
    int bestIdx = -1;

    if (matches[counter & _maskChecks] != 0) {
        const int maxMatch = min(ROLZCodec2::MAX_MATCH, end - pos);

        // Check all recorded positions
        for (int i = counter; i > counter - _posChecks; i--) {
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
    }

    // Register current position
    _counters[key]++;
    matches[(counter + 1) & _maskChecks] = hash32 | int32(pos);
    return (bestLen < ROLZCodec2::MIN_MATCH) ? -1 : (bestIdx << 16) | (bestLen - ROLZCodec2::MIN_MATCH);
}

bool ROLZCodec2::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count) THROW
{
    if (output._length < getMaxEncodedLength(count))
        return false;

    const int srcEnd = count - 4;
    byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    BigEndian::writeInt32(&dst[0], count);
    int srcIdx = 0;
    int dstIdx = 4;
    int sizeChunk = min(count, ROLZCodec::CHUNK_SIZE);
    int startChunk = 0;
    ROLZEncoder re(9, _logPosChecks, &dst[0], dstIdx);
    memset(&_counters[0], 0, sizeof(int32) * 65536);

    while (startChunk < srcEnd) {
        memset(&_matches[0], 0, sizeof(int32) * (ROLZCodec::HASH_SIZE << _logPosChecks));
        const int endChunk = min(startChunk + sizeChunk, srcEnd);
        sizeChunk = endChunk - startChunk;
        re.reset();
        src = &input._array[startChunk];
        srcIdx = 0;

        // First literals
        re.setMode(LITERAL_FLAG);
        re.setContext(byte(0));
        re.encodeBits((LITERAL_FLAG << 8) | int(src[srcIdx]), 9);
        srcIdx++;

        if (startChunk + 1 < srcEnd) {
            re.encodeBits((LITERAL_FLAG << 8) | int(src[srcIdx]), 9);
            srcIdx++;
        }

        while (srcIdx < sizeChunk) {
            re.setContext(src[srcIdx - 1]);
            const int match = findMatch(src, srcIdx, sizeChunk);

            if (match < 0) {
                // Emit one literal
                re.encodeBits((LITERAL_FLAG << 8) | int(src[srcIdx]), 9);
                srcIdx++;
                continue;
            }

            // Emit one match length and index
            const int matchLen = match & 0xFFFF;
            re.encodeBits((MATCH_FLAG << 8) | matchLen, 9);
            const int matchIdx = match >> 16;
            re.setMode(MATCH_FLAG);
            re.setContext(src[srcIdx - 1]);
            re.encodeBits(matchIdx, _logPosChecks);
            re.setMode(LITERAL_FLAG);
            srcIdx += (matchLen + ROLZCodec2::MIN_MATCH);
        }

        startChunk = endChunk;
    }

    // Emit last literals
    re.setMode(LITERAL_FLAG);

    for (int i = 0; i < 4; i++, srcIdx++) {
        re.setContext(src[srcIdx - 1]);
        re.encodeBits((LITERAL_FLAG << 8) | int(src[srcIdx]), 9);
    }

    re.dispose();
    input._index = startChunk - sizeChunk + srcIdx;
    output._index = dstIdx;
    return (input._index == count) && (output._index < count);
}

bool ROLZCodec2::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count) THROW
{
    if (count == 0)
        return true;

    if (input._array == output._array)
        return false;

    byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    int srcIdx = 0;
    const int dstEnd = BigEndian::readInt32(&src[srcIdx]);
    srcIdx += 4;
    int sizeChunk = min(dstEnd, ROLZCodec::CHUNK_SIZE);
    int startChunk = 0;

    ROLZDecoder rd(9, _logPosChecks, &src[0], srcIdx);
    memset(&_counters[0], 0, sizeof(int32) * 65536);

    while (startChunk < dstEnd) {
        memset(&_matches[0], 0, sizeof(int32) * (ROLZCodec::HASH_SIZE << _logPosChecks));
        const int endChunk = min(startChunk + sizeChunk, dstEnd);
        sizeChunk = endChunk - startChunk;
        rd.reset();
        dst = &output._array[output._index];
        int dstIdx = 0;

        // First literals
        rd.setMode(LITERAL_FLAG);
        rd.setContext(byte(0));
        int val = rd.decodeBits(9);

        // Sanity check
        if ((val >> 8) == MATCH_FLAG) {
            output._index += dstIdx;
            break;
        }    

        dst[dstIdx++] = byte(val);

        if (output._index + 1 < dstEnd) {
           val = rd.decodeBits(9);

           // Sanity check
           if ((val >> 8) == MATCH_FLAG) {
               output._index += dstIdx;
               break;
           }   

           dst[dstIdx++] = byte(val);
        }    

        // Next chunk
        while (dstIdx < sizeChunk) {
            const int savedIdx = dstIdx;
            const uint32 key = ROLZCodec::getKey(&dst[dstIdx - 2]);
            int32* matches = &_matches[key << _logPosChecks];
            rd.setMode(LITERAL_FLAG);
            rd.setContext(dst[dstIdx - 1]);
            prefetchRead(&_counters[key]);
            const int val = rd.decodeBits(9);

            if ((val >> 8) == LITERAL_FLAG) {
                dst[dstIdx++] = byte(val);
            } else {
                // Read one match length and index
                const int matchLen = val & 0xFF;

                // Sanity check
                if (dstIdx + matchLen + 3 > dstEnd) {
                    output._index += dstIdx;
                    break;
                }

                rd.setMode(MATCH_FLAG);
                rd.setContext(dst[dstIdx - 1]);
                const int32 matchIdx = int32(rd.decodeBits(_logPosChecks));
                const int32 ref = matches[(_counters[key] - matchIdx) & _maskChecks];
                dstIdx = ROLZCodec::emitCopy(dst, dstIdx, ref, matchLen);
            }

            // Update map
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

