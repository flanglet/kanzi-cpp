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

#include <fstream>
#include <iostream>
#include <sstream>
#include <streambuf>
#include "ROLZCodec.hpp"
#include "../Global.hpp"
#include "../Memory.hpp"
#include "../bitstream/DefaultInputBitStream.hpp"
#include "../bitstream/DefaultOutputBitStream.hpp"
#include "../entropy/ANSRangeDecoder.hpp"
#include "../entropy/ANSRangeEncoder.hpp"

using namespace kanzi;
using namespace std;

const int ROLZCodec::HASH_SIZE = 65536;
const int ROLZCodec::CHUNK_SIZE = 16 * 1024 * 1024;
const int32 ROLZCodec::HASH = 200002979;
const int32 ROLZCodec::HASH_MASK = ~(ROLZCodec::CHUNK_SIZE - 1);
const int ROLZCodec::MAX_BLOCK_SIZE = 1024 * 1024 * 1024;
const int ROLZCodec::MIN_BLOCK_SIZE = 64;


ROLZCodec::ROLZCodec(uint logPosChecks)
{
    _delegate = new ROLZCodec1(logPosChecks);
}

ROLZCodec::ROLZCodec(Context& ctx)
{
    string transform = ctx.getString("transform", "NONE");
    _delegate = (transform.find("ROLZX") != string::npos) ? static_cast<Transform<byte>*>(new ROLZCodec2(ctx)) :
       static_cast<Transform<byte>*>(new ROLZCodec1(ctx));
}

bool ROLZCodec::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (count < MIN_BLOCK_SIZE)
        return false;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("ROLZ codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("ROLZ codec: Invalid output block");

    if (input._array == output._array)
        return false;

    if (count > MAX_BLOCK_SIZE)
        return false;

    return _delegate->forward(input, output, count);
}

bool ROLZCodec::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("ROLZ codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("ROLZ codec: Invalid output block");

    if (input._array == output._array)
        return false;

    if (count > MAX_BLOCK_SIZE)
        return false;

    return _delegate->inverse(input, output, count);
}


const int ROLZCodec1::MIN_MATCH3 = 3;
const int ROLZCodec1::MIN_MATCH4 = 4;
const int ROLZCodec1::MIN_MATCH7 = 7;
const int ROLZCodec1::MAX_MATCH = MIN_MATCH3 + 65535;
const int ROLZCodec1::LOG_POS_CHECKS = 4;


ROLZCodec1::ROLZCodec1(uint logPosChecks) :
    _logPosChecks(logPosChecks)
{
    if ((logPosChecks < 2) || (logPosChecks > 8)) {
        stringstream ss;
        ss << "ROLZ codec: Invalid logPosChecks parameter: " << logPosChecks << " (must be in [2..8])";
        throw invalid_argument(ss.str());
    }

    _pCtx = nullptr;
    _posChecks = 1 << _logPosChecks;
    _maskChecks = uint8(_posChecks - 1);
    _minMatch = MIN_MATCH3;
    _mSize = 0;
    _matches = nullptr;
    memset(&_counters[0], 0, sizeof(_counters));
}

ROLZCodec1::ROLZCodec1(Context& ctx) :
    _pCtx(&ctx)
{
    _logPosChecks = LOG_POS_CHECKS;
    _posChecks = 1 << _logPosChecks;
    _maskChecks = uint8(_posChecks - 1);
    _minMatch = MIN_MATCH3;
    _mSize = 0;
    _matches = nullptr;
    memset(&_counters[0], 0, sizeof(_counters));
}


// return position index (_logPosChecks bits) + length (16 bits) or -1
int ROLZCodec1::findMatch(const byte buf[], int pos, int end, uint32 hash32, const uint32* matches, const uint8* counter) const
{
    const int s = int(*counter);
    const int e = s - _posChecks;
    prefetchRead(matches);
    const byte* curBuf = &buf[pos];
    int bestLen = 0;
    int bestIdx = -1;
    const int maxMatch = min(ROLZCodec1::MAX_MATCH, end - pos) - 8;

    // Check all recorded positions
    for (int i = s; i > e; i--) {
        uint32 ref = matches[i & _maskChecks];

        // Hash check may save a memory access ...
        if ((ref & ROLZCodec::HASH_MASK) != hash32)
            continue;

        ref &= ~ROLZCodec::HASH_MASK;

        if (buf[ref + bestLen] != curBuf[bestLen])
            continue;

        int n = 0;

        while (n < maxMatch) {
            const int64 diff = LittleEndian::readLong64(&buf[ref + n]) ^ LittleEndian::readLong64(&curBuf[n]);

            if (diff != 0) {
                n += (Global::trailingZeros(uint64(diff)) >> 3);
                break;
            }

            n += 8;
        }

        if (n > bestLen) {
            bestIdx = i;
            bestLen = n;
        }
    }

    return (bestLen < _minMatch) ? -1 : ((s - bestIdx) << 16) | (bestLen - _minMatch);
}

bool ROLZCodec1::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (output._length < getMaxEncodedLength(count))
        return false;

    const int srcEnd = count - 4;
    const byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    BigEndian::writeInt32(&dst[0], count);
    int dstIdx = 5;
    int sizeChunk = min(count, ROLZCodec::CHUNK_SIZE);
    int startChunk = 0;
    SliceArray<byte> litBuf(new byte[getMaxEncodedLength(sizeChunk)], getMaxEncodedLength(sizeChunk));
    SliceArray<byte> lenBuf(new byte[sizeChunk / 5], sizeChunk / 5);
    SliceArray<byte> mIdxBuf(new byte[sizeChunk / 4], sizeChunk / 4);
    SliceArray<byte> tkBuf(new byte[sizeChunk / 4], sizeChunk / 4);
    memset(&_counters[0], 0, sizeof(_counters));
    bool success = true;
    const int litOrder = (count < (1 << 17)) ? 0 : 1;
    int flags = litOrder;
    stringbuf buffer;
    iostream ios(&buffer);
    _minMatch = MIN_MATCH3;
    int delta = 2;

    if (_pCtx != nullptr) {
        Global::DataType dt = (Global::DataType) _pCtx->getInt("dataType", Global::UNDEFINED);

        if (dt == Global::UNDEFINED) {
            uint freqs0[256] = { 0 };
            Global::computeHistogram(&src[0], count, freqs0);
            dt = Global::detectSimpleType(count, freqs0);

            if (dt != Global::UNDEFINED)
                _pCtx->putInt("dataType", dt);
        }

        if (dt == Global::EXE) {
            delta = 3;
            flags |= 8;
        } else if (dt == Global::DNA) {
            delta = 8;
            _minMatch = MIN_MATCH7;
            flags |= 4;
        } else if (dt == Global::MULTIMEDIA) {
            delta = 8;
            _minMatch = MIN_MATCH4;
            flags |= 2;
        }
    }

    if (_mSize == 0) {
       _mSize = size_t(ROLZCodec::HASH_SIZE << _logPosChecks);

       if (_matches != nullptr)
           delete[] _matches;

       _matches = new uint32[_mSize];
    }

    flags |= (_logPosChecks << 4);
    dst[4] = byte(flags);
    const bool cond = _minMatch == MIN_MATCH3;
	
    // Main loop
    while (startChunk < srcEnd) {
        litBuf._index = 0;
        lenBuf._index = 0;
        mIdxBuf._index = 0;
        tkBuf._index = 0;
        memset(&_matches[0], 0, sizeof(uint32) * size_t(ROLZCodec::HASH_SIZE << _logPosChecks));
        const int endChunk = min(startChunk + sizeChunk, srcEnd);
        sizeChunk = endChunk - startChunk;
        const byte* buf = &src[startChunk];
        const byte* ref = &src[startChunk - delta];
        int srcIdx = 0;
        const int n = min(srcEnd - startChunk, 8);

        for (int j = 0; j < n; j++)
            litBuf._array[litBuf._index++] = buf[srcIdx++];

        int firstLitIdx = srcIdx;
        int srcInc = 0;

        while (srcIdx < sizeChunk) {
            const uint32 key = (cond == true) ? ROLZCodec::getKey1(&ref[srcIdx]): ROLZCodec::getKey2(&ref[srcIdx]);
            uint8* counter = &_counters[key];
            uint32* matches = &_matches[key << _logPosChecks];
            uint32 hash32 = ROLZCodec::hash(&buf[srcIdx]);
            int match = findMatch(buf, srcIdx, sizeChunk, hash32, matches, counter);

            // Register current position
            *counter = (*counter + 1) & _maskChecks;
            matches[*counter] = hash32 | int32(srcIdx);

            if (match < 0) {
                srcIdx++;
                srcIdx += (srcInc >> 6);
                srcInc++;
                continue;
            }

            // Check if better match at next position
            const int srcIdx1 = srcIdx + 1;
            const uint32 key2 = (cond == true) ? ROLZCodec::getKey1(&ref[srcIdx1]) : ROLZCodec::getKey2(&ref[srcIdx1]);
            counter = &_counters[key2];
            matches = &_matches[key2 << _logPosChecks];
            hash32 = ROLZCodec::hash(&buf[srcIdx1]);
            const int match2 = findMatch(buf, srcIdx1, sizeChunk, hash32, matches, counter);

            if ((match2 >= 0) && ((match2 & 0xFFFF) > (match & 0xFFFF))) {
                // New match is better
                match = match2;
                srcIdx = srcIdx1;

                // Register current position
                *counter = (*counter + 1) & _maskChecks;
                matches[*counter] = hash32 | int32(srcIdx);
            }

            // token LLLLLMMM -> L lit length, M match length
            const int litLen = srcIdx - firstLitIdx;
            const int token = (litLen < 31) ? litLen << 3 : 0xF8;
            const int mLen = match & 0xFFFF;

            if (mLen >= 7) {
                tkBuf._array[tkBuf._index++] = byte(token | 0x07);
                lenBuf._index += emitLength(&lenBuf._array[lenBuf._index], mLen - 7);
            }
            else {
                tkBuf._array[tkBuf._index++] = byte(token | mLen);
            }

            // Emit literals
            if (litLen > 0) {
                lenBuf._index += (litLen >= 31 ? emitLength(&lenBuf._array[lenBuf._index], litLen - 31) : 0);
                memcpy(&litBuf._array[litBuf._index], &buf[firstLitIdx], size_t(litLen));
                litBuf._index += litLen;
            }

            // Emit match index
            mIdxBuf._array[mIdxBuf._index++] = byte(match >> 16);
            srcIdx += (mLen + _minMatch);
            firstLitIdx = srcIdx;
            srcInc = 0;
        }

        // Emit last chunk literals
        const int litLen = sizeChunk - firstLitIdx;

        if (tkBuf._index != 0) {
           // At least one match to emit
           const int token = (litLen < 31) ? litLen << 3 : 0xF8;
           tkBuf._array[tkBuf._index++] = byte(token);
        }

        lenBuf._index += (litLen >= 31 ? emitLength(&lenBuf._array[lenBuf._index], litLen - 31) : 0);
        memcpy(&litBuf._array[litBuf._index], &buf[firstLitIdx], size_t(litLen));
        litBuf._index += litLen;

        try {
            // Encode literal, match length and match index buffers
            DefaultOutputBitStream obs(ios, 65536);
            obs.writeBits(litBuf._index, 32);
            obs.writeBits(tkBuf._index, 32);
            obs.writeBits(lenBuf._index, 32);
            obs.writeBits(mIdxBuf._index, 32);
            ANSRangeEncoder litEnc(obs, litOrder);
            litEnc.encode(litBuf._array, 0, litBuf._index);
            litEnc.dispose();
            ANSRangeEncoder mEnc(obs, 0, 32768);
            mEnc.encode(tkBuf._array, 0, tkBuf._index);
            mEnc.encode(lenBuf._array, 0, lenBuf._index);
            mEnc.encode(mIdxBuf._array, 0, mIdxBuf._index);
            mEnc.dispose();
        }
        catch (BitStreamException&) {
            delete[] litBuf._array;
            delete[] lenBuf._array;
            delete[] mIdxBuf._array;
            delete[] tkBuf._array;
            throw;
        }

        // Copy bitstream array to output
        const int bufSize = int(ios.tellp());

        if (dstIdx + bufSize > output._length) {
            input._index = startChunk + srcIdx;
            success = false;
            goto End;
        }

        buffer.pubseekpos(0);
        ios.read(reinterpret_cast<char*>(&dst[dstIdx]), streamsize(bufSize));
        dstIdx += bufSize;
        startChunk = endChunk;
    }

End:
    if (success == true) {
        if (dstIdx + 4 > output._length) {
            input._index = srcEnd;
        }
        else {
            // Emit last literals
            memcpy(&dst[dstIdx], &src[srcEnd], 4);
            dstIdx += 4;
            input._index = srcEnd + 4;
        }
    }

    output._index += dstIdx;
    delete[] litBuf._array;
    delete[] lenBuf._array;
    delete[] mIdxBuf._array;
    delete[] tkBuf._array;
    return (input._index == count) && (output._index < count);
}


bool ROLZCodec1::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    const int end = BigEndian::readInt32(&src[0]);

    if ((end <= 4) || (end - 4 > output._length - output._index))
        return false;

    const int dstEnd = end - 4;
    int srcIdx = 5;
    int sizeChunk = min(dstEnd, ROLZCodec::CHUNK_SIZE);
    int startChunk = 0;
    const int flags = int(src[4]);
    const int litOrder = flags & 1;
    _minMatch = MIN_MATCH3;
    int delta = 2;
	
    switch (flags & 0x0E) {
        case 2: // MULTIMEDIA
           _minMatch = MIN_MATCH4;
           delta = 8;
           break;
        case 4: // DNA
           _minMatch = MIN_MATCH7;
           delta = 8;
           break;
        case 8: // EXE
           delta = 3;
           break;
        default:
           break;
    }

    _logPosChecks = flags >> 4;

    if ((_logPosChecks < 2) || (_logPosChecks > 8))
       return false;

    if (_mSize < size_t(ROLZCodec::HASH_SIZE << _logPosChecks)) {
       _mSize = size_t(ROLZCodec::HASH_SIZE << _logPosChecks);

       if (_matches != nullptr)
           delete[] _matches;

       _matches = new uint32[_mSize];
    }

    _posChecks = 1 << _logPosChecks;
    _maskChecks = uint8(_posChecks - 1);

    byte* arena = new byte[sizeChunk + sizeChunk / 5 + 2 * sizeChunk / 4];
    SliceArray<byte> litBuf(&arena[0], sizeChunk);
    SliceArray<byte> mIdxBuf(&arena[sizeChunk], sizeChunk / 4);
    SliceArray<byte> tkBuf(&arena[sizeChunk + sizeChunk / 4], sizeChunk / 4);
    SliceArray<byte> lenBuf(&arena[sizeChunk + sizeChunk / 2], sizeChunk / 5);
    memset(&_counters[0], 0, sizeof(_counters));
    bool success = true;
    const int litBufSize = litBuf._length;

    // Main loop
    while (startChunk < dstEnd) {
        litBuf._index = 0;
        lenBuf._index = 0;
        mIdxBuf._index = 0;
        tkBuf._index = 0;
        memset(&_matches[0], 0, sizeof(uint32) * size_t(ROLZCodec::HASH_SIZE << _logPosChecks));
        const int endChunk = min(startChunk + sizeChunk, dstEnd);
        sizeChunk = endChunk - startChunk;
        bool onlyLiterals = false;

        try
        {
            // Decode literal, length and match index buffers
            istreambuf<char> buffer(reinterpret_cast<char*>(&src[srcIdx]), max(min(count - srcIdx, sizeChunk + 16), 65536));
            istream is(&buffer);
            DefaultInputBitStream ibs(is, 65536);
            const int litLen = int(ibs.readBits(32));
            const int tkLen = int(ibs.readBits(32));
            const int mLenLen = int(ibs.readBits(32));
            const int mIdxLen = int(ibs.readBits(32));

            if ((litLen < 0) || (tkLen < 0) || (mLenLen < 0) || (mIdxLen < 0)) {
                input._index += srcIdx;
                output._index += startChunk;
                success = false;
                goto End;
            }

            if ((litLen > litBuf._length) || (tkLen > tkBuf._length) || (mLenLen > lenBuf._length) || (mIdxLen > mIdxBuf._length)) {
                input._index += srcIdx;
                output._index += startChunk;
                success = false;
                goto End;
            }

            ANSRangeDecoder litDec(ibs, litOrder);
            litDec.decode(litBuf._array, 0, litLen);
            litDec.dispose();
            ANSRangeDecoder mDec(ibs, 0, 32768);
            mDec.decode(tkBuf._array, 0, tkLen);
            mDec.decode(lenBuf._array, 0, mLenLen);
            mDec.decode(mIdxBuf._array, 0, mIdxLen);
            mDec.dispose();

            onlyLiterals = tkLen == 0;
            srcIdx += int((ibs.read() + 7) >> 3);
        }
        catch (BitStreamException&) {
            delete[] arena;
            throw;
        }

        if (onlyLiterals == true) {
            // Shortcut when no match
            memcpy(&output._array[output._index], &litBuf._array[0], size_t(sizeChunk));
            startChunk = endChunk;
            output._index += sizeChunk;
            continue;
        }

        const bool cond = _minMatch == MIN_MATCH3;
        byte* buf = &output._array[output._index];
        const byte* refBuf = &output._array[output._index - delta];
        int dstIdx = 0;
        const int n = min(dstEnd - output._index, 8);

        for (int j = 0; j < n; j++)
            buf[dstIdx++] = litBuf._array[litBuf._index++];

        // Next chunk
        while (dstIdx < sizeChunk) {
            // token LLLLLMMM -> L lit length, M match length
            const int token = int(tkBuf._array[tkBuf._index++]);
            int mLen = token & 0x07;
            mLen += (mLen == 7 ? _minMatch + readLength(lenBuf._array, lenBuf._index) : _minMatch);

            // Emit literals
            const int litLen = (token < 0xF8) ? token >> 3 : readLength(lenBuf._array, lenBuf._index) + 31;

            if (litLen > 0) {
                if (dstIdx + litLen > litBufSize) {
                    success = false;
                    goto End;
                }

                memcpy(&buf[dstIdx], &litBuf._array[litBuf._index], size_t(litLen));
                int srcInc = 0;

                if (cond == true) {
                     for (int k = 0; k < litLen; k++) {
                        const uint32 key = ROLZCodec::getKey1(&refBuf[dstIdx + k]);
                        uint8* counter = &_counters[key];
                        uint32* matches = &_matches[key << _logPosChecks];
                        *counter = (*counter + 1) & _maskChecks;
                        matches[*counter] = dstIdx + k;
                        k += (srcInc >> 6);
                        srcInc++;
                    }
                } else {
                     for (int k = 0; k < litLen; k++) {
                        const uint32 key = ROLZCodec::getKey2(&refBuf[dstIdx + k]);
                        uint8* counter = &_counters[key];
                        uint32* matches = &_matches[key << _logPosChecks];
                        *counter = (*counter + 1) & _maskChecks;
                        matches[*counter] = dstIdx + k;
                        k += (srcInc >> 6);
                        srcInc++;
                    }
                }

                litBuf._index += litLen;
                dstIdx += litLen;
                prefetchRead(&litBuf._array[litBuf._index]);

                if (dstIdx >= sizeChunk) {
                    // Last chunk literals not followed by match
                    if (dstIdx == sizeChunk)
                        break;

                    output._index += dstIdx;
                    success = false;
                    goto End;
                }
            }

            // Sanity check
            if (output._index + dstIdx + mLen > dstEnd) {
                success = false;
                goto End;
            }

            const uint8 mIdx = uint8(mIdxBuf._array[mIdxBuf._index++]);
            const uint32 key = (cond == true) ? ROLZCodec::getKey1(&refBuf[dstIdx]) : ROLZCodec::getKey2(&refBuf[dstIdx]);
            uint32* matches = &_matches[key << _logPosChecks];
            const int32 ref = matches[(_counters[key] - mIdx) & _maskChecks];
            _counters[key] = (_counters[key] + 1) & _maskChecks;
            matches[_counters[key]] = dstIdx;
            dstIdx = ROLZCodec::emitCopy(buf, dstIdx, ref, mLen);
        }

        startChunk = endChunk;
        output._index += dstIdx;
    }

End:
    if (success == true) {
        // Emit last chunk literals
        if ((output._index + 4 > output._length) || (srcIdx + 4 > input._length)) {
           success = false;
        }
        else {
           memcpy(&dst[output._index], &src[srcIdx], 4);
           output._index += 4;
           srcIdx += 4;
        }
    }

    input._index += srcIdx;
    delete[] arena;
    return (success == true) && (srcIdx == count);
}


const uint64 ROLZEncoder::TOP = 0x00FFFFFFFFFFFFFF;
const uint64 ROLZEncoder::MASK_0_32 = 0x00000000FFFFFFFF;
const int ROLZEncoder::MATCH_FLAG = 0;
const int ROLZEncoder::LITERAL_FLAG = 1;
const int ROLZEncoder::PSCALE = 0xFFFF;


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

void ROLZEncoder::encode9Bits(int val)
{
    _c1 = 1;
    encodeBit(val & 0x100);
    encodeBit(val & 0x80);
    encodeBit(val & 0x40);
    encodeBit(val & 0x20);
    encodeBit(val & 0x10);
    encodeBit(val & 0x08);
    encodeBit(val & 0x04);
    encodeBit(val & 0x02);
    encodeBit(val & 0x01);
}

void ROLZEncoder::dispose()
{
    for (int i = 0; i < 8; i++) {
        _buf[_idx + i] = byte(_low >> 56);
        _low <<= 8;
    }

    _idx += 8;
}


const uint64 ROLZDecoder::TOP = 0x00FFFFFFFFFFFFFF;
const uint64 ROLZDecoder::MASK_0_56 = 0x00FFFFFFFFFFFFFF;
const uint64 ROLZDecoder::MASK_0_32 = 0x00000000FFFFFFFF;
const int ROLZDecoder::MATCH_FLAG = 0;
const int ROLZDecoder::LITERAL_FLAG = 1;
const int ROLZDecoder::PSCALE = 0xFFFF;


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

int ROLZDecoder::decode9Bits()
{
    _c1 = 1;
    decodeBit();
    decodeBit();
    decodeBit();
    decodeBit();
    decodeBit();
    decodeBit();
    decodeBit();
    decodeBit();
    decodeBit();
    return _c1 & 0x1FF;
}


const int ROLZCodec2::MATCH_FLAG = 0;
const int ROLZCodec2::LITERAL_FLAG = 1;
const int ROLZCodec2::MATCH_CTX = 0;
const int ROLZCodec2::LITERAL_CTX = 1;
const int ROLZCodec2::MIN_MATCH3 = 3;
const int ROLZCodec2::MIN_MATCH7 = 7;
const int ROLZCodec2::MAX_MATCH = MIN_MATCH3 + 255;
const int ROLZCodec2::LOG_POS_CHECKS = 5;


ROLZCodec2::ROLZCodec2(uint logPosChecks) :
    _logPosChecks(logPosChecks)
{
    if ((logPosChecks < 2) || (logPosChecks > 8)) {
        stringstream ss;
        ss << "ROLZX codec: Invalid logPosChecks parameter: " << logPosChecks << " (must be in [2..8])";
        throw invalid_argument(ss.str());
    }

    _pCtx = nullptr;
    _posChecks = 1 << _logPosChecks;
    _maskChecks = uint8(_posChecks - 1);
    _minMatch = MIN_MATCH3;
    _matches = new uint32[ROLZCodec::HASH_SIZE << _logPosChecks];
    memset(&_counters[0], 0, sizeof(_counters));
}

ROLZCodec2::ROLZCodec2(Context& ctx) :
    _pCtx(&ctx)
{
    _logPosChecks = LOG_POS_CHECKS;
    _posChecks = 1 << _logPosChecks;
    _maskChecks = uint8(_posChecks - 1);
    _minMatch = MIN_MATCH3;
    _matches = new uint32[ROLZCodec::HASH_SIZE << _logPosChecks];
    memset(&_counters[0], 0, sizeof(_counters));
}

// return position index (_logPosChecks bits) + length (16 bits) or -1
int ROLZCodec2::findMatch(const byte buf[], int pos, int end, uint32 key)
{
    const int counter = _counters[key];
    uint32* matches = &_matches[key << _logPosChecks];
    prefetchRead(matches);
    const byte* curBuf = &buf[pos];
    const uint32 hash32 = ROLZCodec::hash(curBuf);
    int bestLen = 0;
    int bestIdx = -1;
    const int maxMatch = min(ROLZCodec2::MAX_MATCH, end - pos) - 8;

    // Check all recorded positions
    for (int i = counter; i > counter - _posChecks; i--) {
        uint32 ref = matches[i & _maskChecks];

        // Hash check may save a memory access ...
        if ((ref & ROLZCodec::HASH_MASK) != hash32)
            continue;

        ref &= ~ROLZCodec::HASH_MASK;

        if (buf[ref + bestLen] != curBuf[bestLen])
            continue;

        int n = 0;

        while (n < maxMatch) {
            const int64 diff = LittleEndian::readLong64(&buf[ref + n]) ^ LittleEndian::readLong64(&curBuf[n]);

            if (diff != 0) {
                n += (Global::trailingZeros(uint64(diff)) >> 3);
                break;
            }

            n += 8;
        }

        if (n > bestLen) {
            bestIdx = counter - i;
            bestLen = n;

            if (bestLen == maxMatch)
                break;
        }
    }

    // Register current position
    _counters[key] = (_counters[key] + 1) & _maskChecks;
    matches[_counters[key]] = hash32 | int32(pos);
    return (bestLen < _minMatch) ? -1 : (bestIdx << 16) | (bestLen - _minMatch);
}

bool ROLZCodec2::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (output._length < getMaxEncodedLength(count))
        return false;

    const int srcEnd = count - 4;
    const byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    BigEndian::writeInt32(&dst[0], count);
    _minMatch = MIN_MATCH3;
    int flags = 0;
    int delta = 2;

    if (_pCtx != nullptr) {
        Global::DataType dt = (Global::DataType) _pCtx->getInt("dataType", Global::UNDEFINED);

        if (dt == Global::UNDEFINED) {
            uint freqs0[256] = { 0 };
            Global::computeHistogram(&src[0], count, freqs0);
            dt = Global::detectSimpleType(count, freqs0);

            if (dt != Global::UNDEFINED)
                _pCtx->putInt("dataType", dt);
        }

        if (dt == Global::EXE) {
            delta = 3;
            flags |= 8;
        } else if (dt == Global::DNA) {
            delta = 8;
            _minMatch = MIN_MATCH7;
            flags |= 4;
        }
    }
	
    const int dt = delta;
    const bool cond = _minMatch == MIN_MATCH3;
    dst[4] = byte(flags);
    int srcIdx = 0;
    int dstIdx = 5;
    int sizeChunk = min(count, ROLZCodec::CHUNK_SIZE);
    int startChunk = 0;
    ROLZEncoder re(9, _logPosChecks, &dst[0], dstIdx);
    memset(&_counters[0], 0, sizeof(_counters));

    while (startChunk < srcEnd) {
        memset(&_matches[0], 0, sizeof(uint32) * size_t(ROLZCodec::HASH_SIZE << _logPosChecks));
        const int endChunk = min(startChunk + sizeChunk, srcEnd);
        sizeChunk = endChunk - startChunk;
        re.reset();
        src = &input._array[startChunk];
        srcIdx = 0;

        // First literals
        const int n = min(srcEnd - startChunk, 8);
        re.setContext(LITERAL_CTX, byte(0));

        for (int j = 0; j < n; j++) {
            re.encode9Bits((LITERAL_FLAG << 8) | int(src[srcIdx]));
            srcIdx++;
        }

        while (srcIdx < sizeChunk) {
            re.setContext(LITERAL_CTX, src[srcIdx - 1]);
            uint32 key = (cond == true) ? ROLZCodec::getKey1(&src[srcIdx - dt]) : ROLZCodec::getKey2(&src[srcIdx - dt]);
            const int match = findMatch(src, srcIdx, sizeChunk, key);

            if (match < 0) {
                // Emit one literal
                re.encode9Bits((LITERAL_FLAG << 8) | int(src[srcIdx]));
                srcIdx++;
                continue;
            }

            // Emit one match length and index
            const int matchLen = match & 0xFFFF;
            re.encode9Bits((MATCH_FLAG << 8) | matchLen);
            const int matchIdx = match >> 16;
            re.setContext(MATCH_CTX, src[srcIdx - 1]);
            re.encodeBits(matchIdx, _logPosChecks);
            srcIdx += (matchLen + _minMatch);
        }

        startChunk = endChunk;
    }

    // Emit last literals
    for (int i = 0; i < 4; i++, srcIdx++) {
        re.setContext(LITERAL_CTX, src[srcIdx - 1]);
        re.encode9Bits((LITERAL_FLAG << 8) | int(src[srcIdx]));
    }

    re.dispose();
    input._index = startChunk - sizeChunk + srcIdx;
    output._index = dstIdx;
    return (input._index == count) && (output._index < count);
}

bool ROLZCodec2::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (input._array == output._array)
        return false;

    byte* src = &input._array[input._index];
    const int dstEnd = BigEndian::readInt32(&src[0]);

    if ((dstEnd <= 0) || (dstEnd > output._length - output._index))
        return false;

    int srcIdx = 5;
    int sizeChunk = min(dstEnd, ROLZCodec::CHUNK_SIZE);
    int startChunk = 0;
    _minMatch = MIN_MATCH3;
    const int flags = int(src[4]);
    int delta = 2;

    if ((flags & 0x0E) == 8) {
        delta = 3;
    } else if ((flags & 0x0E) == 4) {
        delta = 8;
        _minMatch = MIN_MATCH7;
    }

    const bool cond = _minMatch == MIN_MATCH3;
    ROLZDecoder rd(9, _logPosChecks, &src[0], srcIdx);
    memset(&_counters[0], 0, sizeof(_counters));

    while (startChunk < dstEnd) {
        memset(&_matches[0], 0, sizeof(uint32) * (ROLZCodec::HASH_SIZE << _logPosChecks));
        const int endChunk = min(startChunk + sizeChunk, dstEnd);
        sizeChunk = endChunk - startChunk;
        rd.reset();
        byte* dst = &output._array[output._index];
        byte* refBuf = &output._array[output._index - delta];
        int dstIdx = 0;

        // First literals
        rd.setContext(LITERAL_CTX, byte(0));
        const int n = min(dstEnd - output._index, 8);

        for (int j = 0; j < n; j++) {
            int val = rd.decode9Bits();

            // Sanity check
            if ((val >> 8) == MATCH_FLAG) {
                output._index += dstIdx;
                return false;
            }

            dst[dstIdx++] = byte(val);
        }

        // Next chunk
        while (dstIdx < sizeChunk) {
            const int savedIdx = dstIdx;
            const uint32 key = (cond == true) ? ROLZCodec::getKey1(&refBuf[dstIdx]) : ROLZCodec::getKey2(&refBuf[dstIdx]);
            uint32* matches = &_matches[key << _logPosChecks];
            rd.setContext(LITERAL_CTX, dst[dstIdx - 1]);
            int val = rd.decode9Bits();

            if ((val >> 8) == LITERAL_FLAG) {
                dst[dstIdx++] = byte(val);
            }
            else {
                // Read one match length and index
                const int matchLen = val & 0xFF;
                prefetchRead(&_counters[key]);

                // Sanity check
                if (dstIdx + matchLen + 3 > dstEnd) {
                    output._index += dstIdx;
                    return false;
                }

                rd.setContext(MATCH_CTX, dst[dstIdx - 1]);
                const int32 matchIdx = int32(rd.decodeBits(_logPosChecks));
                const int32 ref = matches[(_counters[key] - matchIdx) & _maskChecks];
                dstIdx = ROLZCodec::emitCopy(dst, dstIdx, ref, matchLen + _minMatch);
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
