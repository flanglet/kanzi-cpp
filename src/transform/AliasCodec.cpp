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
#include <cstring>
#include <functional>
#include <stdexcept>

#include "AliasCodec.hpp"
#include "../Global.hpp"
#include "../Memory.hpp"

using namespace kanzi;
using namespace std;


namespace kanzi {

// Fixed-capacity stack min-heap keeping the top n0 (< 240) digrams.
// Keys pack (freq << 16) | val so std::greater orders the worst candidate at
// items[0] with identical tie-breaking to ssAlias::operator<.
struct AliasMinHeap
{
    uint64 items[256];
    int size;
    int capacity;
    uint64 minKey;

    AliasMinHeap(int cap) : size(0), capacity(cap), minKey(uint64(1) << 16) {}

    KANZI_ALWAYS_INLINE void push(uint32 val, uint freq)
    {
        const uint64 key = (uint64(freq) << 16) | uint64(val);

        if (size < capacity) {
            items[size++] = key;

            if (size == capacity) {
                make_heap(items, items + capacity, greater<uint64>());
                minKey = items[0];
            }
        }
        else if (key > minKey) {
            pop_heap(items, items + capacity, greater<uint64>());
            items[capacity - 1] = key;
            push_heap(items, items + capacity, greater<uint64>());
            minKey = items[0];
        }
    }
};

} // namespace kanzi

#define ALIAS_STEP_STREAM(sp, dp) do {                               \
    const uint16 p = uint16(LittleEndian::readInt16(sp));            \
    const uint8 m = map8[p];                                         \
    *(dp)++ = kanzi::byte(m);                                        \
    (sp) += 1 + (m != uint8(p));                                     \
} while (0)

// Encode src in 8 parallel streams so the CPU is not bottlenecked by a
// single serial loop. Since digrams advance by 1 or 2 bytes, chunk k - 1
// stops at either m or m + 1; stepping from both offsets until they land on
// the same byte conv[k] gives a safe starting point for chunk k (or returns -1
// if they do not meet in 64 steps). Writing chunk k at dst + dstIdx + conv[k]
// leaves enough room for every preceding chunk since output <= input.
int AliasCodec::encodeDigram8(const kanzi::byte* src, kanzi::byte* dst,
                             int dstIdx, int count, const uint8* map8)
{
    const int srcEnd = count - 1;
    const int step = srcEnd >> 3;
    int end[8];
    int conv[8];
    conv[0] = 0;

    // Chunk k - 1 stops at or just past split, so chunk k can safely start
    // wherever parses from split and split + 1 first meet.
    for (int k = 1; k < 8; k++) {
        const int split = k * step;
        end[k - 1] = split;
        int pos0 = split;
        int pos1 = split + 1;

        for (int steps = 64; (pos0 != pos1) && (steps > 0) && (pos0 < srcEnd) && (pos1 < srcEnd); steps--) {
            if (pos0 < pos1) {
                const uint16 p = uint16(LittleEndian::readInt16(&src[pos0]));
                pos0 += 1 + (map8[p] != uint8(p));
            }
            else {
                const uint16 p = uint16(LittleEndian::readInt16(&src[pos1]));
                pos1 += 1 + (map8[p] != uint8(p));
            }
        }

        if ((pos0 != pos1) || (pos0 >= srcEnd))
            return -1;

        conv[k] = pos0;
    }

    end[7] = srcEnd;

    const kanzi::byte* sp0 = src;
    const kanzi::byte* sp1 = src + conv[1];
    const kanzi::byte* sp2 = src + conv[2];
    const kanzi::byte* sp3 = src + conv[3];
    const kanzi::byte* sp4 = src + conv[4];
    const kanzi::byte* sp5 = src + conv[5];
    const kanzi::byte* sp6 = src + conv[6];
    const kanzi::byte* sp7 = src + conv[7];

    kanzi::byte* dp0 = dst + dstIdx;
    kanzi::byte* dp1 = dst + dstIdx + conv[1];
    kanzi::byte* dp2 = dst + dstIdx + conv[2];
    kanzi::byte* dp3 = dst + dstIdx + conv[3];
    kanzi::byte* dp4 = dst + dstIdx + conv[4];
    kanzi::byte* dp5 = dst + dstIdx + conv[5];
    kanzi::byte* dp6 = dst + dstIdx + conv[6];
    kanzi::byte* dp7 = dst + dstIdx + conv[7];

    const kanzi::byte* const e0 = src + end[0];
    const kanzi::byte* const e1 = src + end[1];
    const kanzi::byte* const e2 = src + end[2];
    const kanzi::byte* const e3 = src + end[3];
    const kanzi::byte* const e4 = src + end[4];
    const kanzi::byte* const e5 = src + end[5];
    const kanzi::byte* const e6 = src + end[6];
    const kanzi::byte* const e7 = src + end[7];

    // Run all 8 streams in parallel while every chunk still has input
    while ((sp0 < e0) && (sp1 < e1) && (sp2 < e2) && (sp3 < e3) &&
           (sp4 < e4) && (sp5 < e5) && (sp6 < e6) && (sp7 < e7)) {
        ALIAS_STEP_STREAM(sp0, dp0);
        ALIAS_STEP_STREAM(sp1, dp1);
        ALIAS_STEP_STREAM(sp2, dp2);
        ALIAS_STEP_STREAM(sp3, dp3);
        ALIAS_STEP_STREAM(sp4, dp4);
        ALIAS_STEP_STREAM(sp5, dp5);
        ALIAS_STEP_STREAM(sp6, dp6);
        ALIAS_STEP_STREAM(sp7, dp7);
    }

    const kanzi::byte* sp[8] = { sp0, sp1, sp2, sp3, sp4, sp5, sp6, sp7 };
    kanzi::byte* dp[8] = { dp0, dp1, dp2, dp3, dp4, dp5, dp6, dp7 };
    const kanzi::byte* const ep[8] = { e0, e1, e2, e3, e4, e5, e6, e7 };

    // Drain the leftover tail of each chunk up to its split boundary
    for (int k = 0; k < 8; k++) {
        while (sp[k] < ep[k]) {
            ALIAS_STEP_STREAM(sp[k], dp[k]);
        }
    }

    // Stitch chunks together: encode the short bridge [sp[k-1], conv[k]) and shift chunk k down
    kanzi::byte* curDst = dp[0];

    for (int k = 1; k < 8; k++) {
        const kanzi::byte* pos = sp[k - 1];
        const kanzi::byte* const cEnd = src + conv[k];

        while (pos < cEnd) {
            ALIAS_STEP_STREAM(pos, curDst);
        }

        kanzi::byte* const ds = dst + dstIdx + conv[k];
        const size_t len = size_t(dp[k] - ds);
        memmove(curDst, ds, len);
        curDst += len;
    }

    if (sp[7] != src + count) {
        dst[1] = kanzi::byte(1);
        *curDst++ = *sp[7];
    }

    return int(curDst - dst);
}

#undef ALIAS_STEP_STREAM


const int AliasCodec::MIN_BLOCK_SIZE = 1024;


AliasCodec::AliasCodec(Context& ctx) :
          _pCtx(&ctx)
{
   _onlyDNA = _pCtx->getInt("packOnlyDNA", 0) != 0;
}


bool AliasCodec::forward(SliceArray<kanzi::byte>& input, SliceArray<kanzi::byte>& output, int count)
{
    if (count == 0)
        return true;

    if (count < MIN_BLOCK_SIZE)
        return false;

    if (!SliceArray<kanzi::byte>::isValid(input))
        throw invalid_argument("Alias codec: Invalid input block");

    if (!SliceArray<kanzi::byte>::isValid(output))
        throw invalid_argument("Alias codec: Invalid output block");

    if (output._length - output._index < getMaxEncodedLength(count))
        return false;

    Global::DataType dt = Global::UNDEFINED;

    if (_pCtx != nullptr) {
        dt = (Global::DataType) _pCtx->getInt("dataType", Global::UNDEFINED);

        if ((dt == Global::MULTIMEDIA) || (dt == Global::UTF8))
            return false;

        if ((dt == Global::EXE) || (dt == Global::BIN))
            return false;

        if ((_onlyDNA == true) && (dt != Global::UNDEFINED) && (dt != Global::DNA))
            return false;
    }

    kanzi::byte* dst = &output._array[output._index];
    const kanzi::byte* src = &input._array[input._index];

    // Find missing 1-byte symbols
    uint freqs0[256] = { 0 };
    Global::computeHistogram(&src[0], count, freqs0, true);
    int n0 = 0;
    int absent[256];
    int present[256];
    int nPres = 0;

    for (int i = 0; i < 256; i++) {
        if (freqs0[i] == 0)
            absent[n0++] = i;
        else
            present[nPres++] = i;
    }

    if (n0 < 16)
        return false;

    if (dt == Global::UNDEFINED) {
        dt = Global::detectSimpleType(count, freqs0);

        if ((_pCtx != nullptr) && (dt != Global::UNDEFINED))
            _pCtx->putInt("dataType", dt);

        if ((dt != Global::DNA) && (_onlyDNA == true))
            return false;
    }

    int srcIdx = 0;
    int dstIdx = 0;

    if (n0 >= 240) {
        // Small alphabet => pack bits
        dst[0] = kanzi::byte(n0);
        dstIdx = 1;

        if (n0 == 255) {
            // One symbol
            dst[1] = src[0];
            LittleEndian::writeInt32(&dst[2], count);
            dstIdx = 6;
            srcIdx = count;
        }
        else {
            uint8 map8[256] = { 0 };

            for (int i = 0, j = 0; i < 256; i++) {
                if (freqs0[i] != 0) {
                    dst[dstIdx++] = kanzi::byte(i);
                    map8[i] = uint8(j++);
                }
            }

            if (n0 >= 252) {
                // 4 symbols or less
                const int c3 = count & 3;
                dst[dstIdx++] = kanzi::byte(c3);
                memcpy(&dst[dstIdx], &src[srcIdx], size_t(c3));
                srcIdx += c3;
                dstIdx += c3;

                const int end16 = count - 15;

                while (srcIdx < end16) {
                    const uint32 b0 = uint32((map8[int(src[srcIdx + 0])] << 6) | (map8[int(src[srcIdx + 1])] << 4) |
                                             (map8[int(src[srcIdx + 2])] << 2) | map8[int(src[srcIdx + 3])]);
                    const uint32 b1 = uint32((map8[int(src[srcIdx + 4])] << 6) | (map8[int(src[srcIdx + 5])] << 4) |
                                             (map8[int(src[srcIdx + 6])] << 2) | map8[int(src[srcIdx + 7])]);
                    const uint32 b2 = uint32((map8[int(src[srcIdx + 8])] << 6) | (map8[int(src[srcIdx + 9])] << 4) |
                                             (map8[int(src[srcIdx + 10])] << 2) | map8[int(src[srcIdx + 11])]);
                    const uint32 b3 = uint32((map8[int(src[srcIdx + 12])] << 6) | (map8[int(src[srcIdx + 13])] << 4) |
                                             (map8[int(src[srcIdx + 14])] << 2) | map8[int(src[srcIdx + 15])]);
                    LittleEndian::writeInt32(&dst[dstIdx], int32(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)));
                    srcIdx += 16;
                    dstIdx += 4;
                }

                while (srcIdx < count) {
                    dst[dstIdx++] = kanzi::byte((map8[int(src[srcIdx + 0])] << 6) | (map8[int(src[srcIdx + 1])] << 4) |
                                                (map8[int(src[srcIdx + 2])] << 2) | map8[int(src[srcIdx + 3])]);
                    srcIdx += 4;
                }
            }
            else {
                // 16 symbols or less
                dst[dstIdx++] = kanzi::byte(count & 1);

                if ((count & 1) != 0)
                    dst[dstIdx++] = src[srcIdx++];

                const int end8 = count - 7;

                while (srcIdx < end8) {
                    const uint32 b0 = uint32((map8[int(src[srcIdx + 0])] << 4) | map8[int(src[srcIdx + 1])]);
                    const uint32 b1 = uint32((map8[int(src[srcIdx + 2])] << 4) | map8[int(src[srcIdx + 3])]);
                    const uint32 b2 = uint32((map8[int(src[srcIdx + 4])] << 4) | map8[int(src[srcIdx + 5])]);
                    const uint32 b3 = uint32((map8[int(src[srcIdx + 6])] << 4) | map8[int(src[srcIdx + 7])]);
                    LittleEndian::writeInt32(&dst[dstIdx], int32(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)));
                    srcIdx += 8;
                    dstIdx += 4;
                }

                while (srcIdx < count) {
                    dst[dstIdx++] = kanzi::byte((map8[int(src[srcIdx])] << 4) | map8[int(src[srcIdx + 1])]);
                    srcIdx += 2;
                }
            }
        }
    }
    else {
        // Digram encoding
        AliasMinHeap heap(n0);

        {
            uint* const freqs1 = new uint[65536];

            // Clear only active rows (+ row 0 if byte 0 is absent, as computeHistogram records (0, src[0]))
            for (int i = 0; i < nPres; i++)
                memset(&freqs1[present[i] << 8], 0, 256 * sizeof(uint));

            if (freqs0[0] == 0)
                memset(&freqs1[0], 0, 256 * sizeof(uint));

            Global::computeHistogram(&src[0], count, freqs1, false);

            // Scan present symbol pairs to keep only the top n0 most frequent digrams
            for (int hiIdx = 0; hiIdx < nPres; hiIdx++) {
                const int hi = present[hiIdx];
                const uint* row = &freqs1[hi << 8];
                const uint32 base = uint32(hi) << 8;

                for (int loIdx = 0; loIdx < nPres; loIdx++) {
                    const int lo = present[loIdx];

                    if (row[lo] != 0)
                        heap.push(base | uint32(lo), row[lo]);
                }
            }

            if ((freqs0[0] == 0) && (freqs1[uint(src[0])] != 0))
                heap.push(uint32(src[0]), freqs1[uint(src[0])]);

            delete[] freqs1;
        }

        if (heap.size < n0) {
            n0 = heap.size;

            if (n0 < 16)
                return false;

            make_heap(heap.items, heap.items + n0, greater<uint64>());
        }

        int savings = 0;

        for (int i = 0; i < n0; i++)
            savings += int(heap.items[i] >> 16);

        // Worth it?
        if (savings < count / 20)
            return false;

        // Order top digrams descending so the most frequent get the first aliases
        sort_heap(heap.items, heap.items + n0, greater<uint64>());

        // Unaliased entries store p[0] (present byte) != absent[i]
        uint8 map8[65536];
        uint8 row0[256];

        for (int j = 0; j < 256; j++)
            row0[j] = uint8(j);

        for (int j = 0; j < nPres; j++)
            memcpy(&map8[present[j] << 8], row0, 256);

        dst[0] = kanzi::byte(n0);
        dst[1] = kanzi::byte(0);
        dstIdx = 2;

        // Header: emit map data
        for (int i = 0; i < n0; i++) {
            const uint32 idx = uint32(heap.items[i] & 0xFFFF);
            const int hi = int(idx >> 8);
            const int lo = int(idx & 0xFF);
            map8[(lo << 8) | hi] = uint8(absent[i]);
            dst[dstIdx] = kanzi::byte(hi);
            dst[dstIdx + 1] = kanzi::byte(lo);
            dst[dstIdx + 2] = kanzi::byte(absent[i]);
            dstIdx += 3;
        }

        // encodeDigram8 stages chunks ahead in dst, so require disjoint src and dst buffers
        const bool nonOverlap = (dst + getMaxEncodedLength(count) <= src) || (src + count <= dst);
        const int encodedLen = ((count >= 4096) && nonOverlap)
            ? encodeDigram8(src, dst, dstIdx, count, map8) : -1;

        if (encodedLen >= 0) {
            dstIdx = encodedLen;
            srcIdx = count;
        }
        else {
            const int srcEnd = count - 1;

            while (srcIdx < srcEnd) {
                const uint16 p = uint16(LittleEndian::readInt16(&src[srcIdx]));
                const uint8 m = map8[p];
                dst[dstIdx++] = kanzi::byte(m);
                srcIdx += 1 + (m != uint8(p));
            }

            if (srcIdx != count) {
                dst[1] = kanzi::byte(1);
                dst[dstIdx++] = src[srcIdx++];
            }
        }
    }

    input._index += srcIdx;
    output._index += dstIdx;
    return dstIdx < count;
}


bool AliasCodec::inverse(SliceArray<kanzi::byte>& input, SliceArray<kanzi::byte>& output, int count)
{
    if (count == 0)
        return true;

    if (!SliceArray<kanzi::byte>::isValid(input))
        throw invalid_argument("Alias codec: Invalid input block");

    if (!SliceArray<kanzi::byte>::isValid(output))
        throw invalid_argument("Alias codec: Invalid output block");

    if (input._index + count > input._length)
        return false;

    kanzi::byte* dst = &output._array[output._index];
    const kanzi::byte* src = &input._array[input._index];
    const int dstEnd = output._length - output._index;
    int n = int(src[0]);

    if (n < 16)
        return false;

    int srcIdx;
    int dstIdx = 0;

    if (n >= 240) {
        n = 256 - n;
        srcIdx = 1;

        if (n == 1) {
            // One symbol
            if (count < 6)
                return false;

            const kanzi::byte val = src[1];
            const int oSize = LittleEndian::readInt32(&src[2]);

            if ((oSize < 0) || (oSize > dstEnd))
                return false;

            memset(&dst[0], int(val), size_t(oSize));
            srcIdx = count;
            dstIdx = oSize;
        }
        else {
            // Rebuild map alias -> symbol
            kanzi::byte idx2symb[16] = { kanzi::byte(0) };

            if (srcIdx + n + 1 > count)
                return false;

            for (int i = 0; i < n; i++)
                idx2symb[i] = src[srcIdx++];

            const int adjust = int(src[srcIdx++]);

            if ((adjust < 0) || (adjust >= 4))
                return false;

            if (n <= 4) {
                // 4 symbols or less
                uint32 decodeMap[256];

                for (int i = 0; i < 256; i++) {
                    uint32 val;
                    val  = uint32(idx2symb[(i >> 0) & 0x03]);
                    val <<= 8;
                    val |= uint32(idx2symb[(i >> 2) & 0x03]);
                    val <<= 8;
                    val |= uint32(idx2symb[(i >> 4) & 0x03]);
                    val <<= 8;
                    val |= uint32(idx2symb[(i >> 6) & 0x03]);
                    decodeMap[i] = val;
                }

                if ((srcIdx + adjust > count) || (dstIdx + adjust > dstEnd))
                    return false;

                memcpy(&dst[dstIdx], &src[srcIdx], size_t(adjust));
                srcIdx += adjust;
                dstIdx += adjust;

                if ((count - srcIdx) > ((dstEnd - dstIdx) >> 2))
                    return false;

                const int count2 = count - 1;

                while (srcIdx < count2) {
                    const uint64 q = uint64(decodeMap[int(src[srcIdx + 0])]) |
                                     (uint64(decodeMap[int(src[srcIdx + 1])]) << 32);
                    LittleEndian::writeLong64(&dst[dstIdx], int64(q));
                    srcIdx += 2;
                    dstIdx += 8;
                }

                if (srcIdx < count) {
                    LittleEndian::writeInt32(&dst[dstIdx], int32(decodeMap[int(src[srcIdx++])]));
                    dstIdx += 4;
                }
            }
            else
            {
                // 16 symbols or less
                uint16 decodeMap[256];

                for (int i = 0; i < 256; i++) {
                    uint16 val = uint16(idx2symb[i & 0x0F]);
                    val = uint16((val << 8) | uint16(idx2symb[i >> 4]));
                    decodeMap[i] = val;
                }

                if (adjust != 0) {
                    if ((srcIdx >= count) || (dstIdx >= dstEnd))
                        return false;

                    dst[dstIdx++] = src[srcIdx++];
                }

                if ((count - srcIdx) > ((dstEnd - dstIdx) >> 1))
                    return false;

                const int count4 = count - 3;

                while (srcIdx < count4) {
                    const uint64 q = uint64(decodeMap[int(src[srcIdx + 0])]) |
                                     (uint64(decodeMap[int(src[srcIdx + 1])]) << 16) |
                                     (uint64(decodeMap[int(src[srcIdx + 2])]) << 32) |
                                     (uint64(decodeMap[int(src[srcIdx + 3])]) << 48);
                    LittleEndian::writeLong64(&dst[dstIdx], int64(q));
                    srcIdx += 4;
                    dstIdx += 8;
                }

                while (srcIdx < count) {
                    LittleEndian::writeInt16(&dst[dstIdx], int16(decodeMap[int(src[srcIdx++])]));
                    dstIdx += 2;
                }
            }
        }
    }
    else {
        if (count < 2)
            return false;

        // Rebuild map alias -> symbol
        int map16[256] = { 0 };
        const int adjust = int(src[1]);

        if ((adjust < 0) || (adjust > 1))
            return false;

        const int srcEnd = count - adjust;
        srcIdx = 2;

        for (int i = 0; i < 256; i++)
            map16[i] = 0x10000 | i;

        if (srcIdx + 3 * n > srcEnd)
            return false;

        for (int i = 0; i < n; i++) {
            map16[int(src[srcIdx + 2])] = 0x20000 | int(src[srcIdx]) | (int(src[srcIdx + 1]) << 8);
            srcIdx += 3;
        }

        const int nbAliases = srcEnd - srcIdx;
        const int dstAvail = dstEnd - dstIdx;

        if (nbAliases <= (dstAvail >> 1)) {
            // Safe to prefix-sum and write 16 bits unconditionally for each 8-symbol batch
            const int srcEnd8 = srcEnd - 7;

            while (srcIdx < srcEnd8) {
                const int v0 = map16[int(src[srcIdx + 0])];
                const int v1 = map16[int(src[srcIdx + 1])];
                const int v2 = map16[int(src[srcIdx + 2])];
                const int v3 = map16[int(src[srcIdx + 3])];
                const int v4 = map16[int(src[srcIdx + 4])];
                const int v5 = map16[int(src[srcIdx + 5])];
                const int v6 = map16[int(src[srcIdx + 6])];
                const int v7 = map16[int(src[srcIdx + 7])];
                const int o1 = (v0 >> 16);
                const int o2 = o1 + (v1 >> 16);
                const int o3 = o2 + (v2 >> 16);
                const int o4 = o3 + (v3 >> 16);
                const int o5 = o4 + (v4 >> 16);
                const int o6 = o5 + (v5 >> 16);
                const int o7 = o6 + (v6 >> 16);
                LittleEndian::writeInt16(&dst[dstIdx], int16(v0));
                LittleEndian::writeInt16(&dst[dstIdx + o1], int16(v1));
                LittleEndian::writeInt16(&dst[dstIdx + o2], int16(v2));
                LittleEndian::writeInt16(&dst[dstIdx + o3], int16(v3));
                LittleEndian::writeInt16(&dst[dstIdx + o4], int16(v4));
                LittleEndian::writeInt16(&dst[dstIdx + o5], int16(v5));
                LittleEndian::writeInt16(&dst[dstIdx + o6], int16(v6));
                LittleEndian::writeInt16(&dst[dstIdx + o7], int16(v7));
                dstIdx += o7 + (v7 >> 16);
                srcIdx += 8;
            }

            while (srcIdx < srcEnd) {
                const int val = map16[int(src[srcIdx++])];
                LittleEndian::writeInt16(&dst[dstIdx], int16(val));
                dstIdx += (val >> 16);
            }
        }
        else {
            while ((srcIdx < srcEnd) && (dstIdx + 1 < dstEnd)) {
                const int val = map16[int(src[srcIdx++])];
                LittleEndian::writeInt16(&dst[dstIdx], int16(val));
                dstIdx += (val >> 16);
            }

            while (srcIdx < srcEnd) {
                const int val = map16[int(src[srcIdx++])];
                const int inc = val >> 16;

                if (dstIdx + inc > dstEnd)
                    return false;

                dst[dstIdx + inc - 1] = kanzi::byte(val >> 8);
                dst[dstIdx] = kanzi::byte(val);
                dstIdx += inc;
            }
        }

        if (adjust != 0) {
            if ((srcIdx >= count) || (dstIdx >= dstEnd))
                return false;

            dst[dstIdx++] = src[srcIdx++];
        }
    }

    input._index += srcIdx;
    output._index += dstIdx;
    return srcIdx == count;
}
