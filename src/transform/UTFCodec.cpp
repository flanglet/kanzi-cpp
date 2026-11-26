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
#include <vector>

#include "UTFCodec.hpp"
#include "../Global.hpp"
#include "../Memory.hpp"
#include "../types.hpp"

using namespace kanzi;
using namespace std;

const int UTFCodec::MIN_BLOCK_SIZE = 1024;
const uint64 UTFCodec::HIGH_BIT_MASK = (uint64(0x80808080u) << 32) | uint64(0x80808080u);

// Valid second-byte ranges per UTF-8 Table 3.7, indexed by (b0 & 0x1F) for b0 in [0xE0..0xF4].
const uint8 UTFCodec::MIN_B1[32] = {
    0xA0, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x90, 0x80, 0x80, 0x80, 0x80, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

const uint8 UTFCodec::SPAN_B1[32] = {
    0x1F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x1F, 0x3F, 0x3F,
    0x2F, 0x3F, 0x3F, 0x3F, 0x0F, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Every valid continuation byte has MSB=1, so fewer high-bit bytes than
// the threshold rules out passing UTFCodec validation without touching histograms.
uint64 UTFCodec::countHighBitBytes(const kanzi::byte src[], int start, int srcEnd)
{
    static const uint64 LANE_MASK_16 = (uint64(0x00FF00FFu) << 32) | uint64(0x00FF00FFu);
    static const uint64 LANE_MASK_32 = (uint64(0x0000FFFFu) << 32) | uint64(0x0000FFFFu);
    uint64 total = 0;
    int j = start;

    while (j + 8 <= srcEnd) {
        // Each 8-bit lane in acc holds up to 255 before overflowing into the neighbor lane.
        const int batchEnd = j + min(255, (srcEnd - j) >> 3) * 8;
        uint64 acc = 0;

        while (j < batchEnd) {
            const uint64 w = uint64(LittleEndian::readLong64(&src[j]));
            acc += (w & HIGH_BIT_MASK) >> 7;
            j += 8;
        }

        acc = (acc & LANE_MASK_16) + ((acc >> 8) & LANE_MASK_16);
        acc = (acc & LANE_MASK_32) + ((acc >> 16) & LANE_MASK_32);
        total += uint32(acc) + uint32(acc >> 32);
    }

    while (j < srcEnd) {
        total += uint8(src[j]) >> 7;
        j++;
    }

    return total;
}

bool UTFCodec::forward(SliceArray<kanzi::byte>& input, SliceArray<kanzi::byte>& output, int count)
{
    if (count == 0)
        return true;

    if (count < MIN_BLOCK_SIZE)
        return false;

    if (!SliceArray<kanzi::byte>::isValid(input))
        throw invalid_argument("UTFCodec: Invalid input block");

    if (!SliceArray<kanzi::byte>::isValid(output))
        throw invalid_argument("UTFCodec: Invalid output block");

    if ((count > input._length - input._index) ||
        (output._length - output._index < getMaxEncodedLength(count)))
        return false;

    const kanzi::byte* src = &input._array[input._index];
    kanzi::byte* dst = &output._array[output._index];
    bool mustValidate = true;

    if (_pCtx != nullptr) {
        Global::DataType dt = (Global::DataType)_pCtx->getInt("dataType", Global::UNDEFINED);

        if ((dt != Global::UNDEFINED) && (dt != Global::UTF8))
            return false;

        mustValidate = dt != Global::UTF8;
    }

    int start = 0;

    if ((count >= 3) && (src[0] == kanzi::byte(0xEF)) && (src[1] == kanzi::byte(0xBB)) && (src[2] == kanzi::byte(0xBF))) {
        // Byte Order Mark (BOM)
        start = 3;
    }
    else {
        // Skip up to 3 trailing continuation bytes left over from a split sequence.
        while ((start < 3) && ((uint8(src[start]) & 0xC0) == 0x80))
            start++;
    }

    const int srcEnd = count - 4;

    if (mustValidate && (countHighBitBytes(src, start, srcEnd) < uint64((count - start - 4) / 8)))
        return false;

    // 1-3 bit size + (7 or 11 or 16 or 21) bit payload
    // 3 MSBs indicate symbol size (limit map size to 22 bits)
    // 000 -> 7 bits
    // 001 -> 11 bits
    // 010 -> 16 bits
    // 1xx -> 21 bits
    // Value-initialize once with () so unused 4-byte pages can stay demand-zeroed;
    // AliasMapGuard clears touched slots on exit to avoid a 16 MiB memset per block.
    if (_aliasMap == nullptr)
        _aliasMap = new uint32[1 << 22]();

    if (_tokensCap < count) {
        delete[] _tokens;
        _tokens = new uint32[count];
        _tokensCap = count;
    }

    // Separate L1 counters avoid store-to-load stalls on repeated ASCII bytes.
    uint f0[128] = { 0 };
    uint f1[128] = { 0 };
    uint f2[128] = { 0 };
    uint f3[128] = { 0 };

    struct AliasMapGuard {
        uint32* map;
        vector<uint32>& mbKeys;

        AliasMapGuard(uint32* m, vector<uint32>& k) : map(m), mbKeys(k) {}

        ~AliasMapGuard()
        {
            memset(map, 0, 128 * sizeof(uint32));

            for (size_t i = 0; i < mbKeys.size(); i++)
                map[mbKeys[i]] = 0;
        }
    };

    vector<uint32> mbKeys;
    mbKeys.reserve(max(count >> 9, 256));
    AliasMapGuard guard(_aliasMap, mbKeys);

    uint32* const tokens = _tokens;
    int ntok = 0;
    uint contBytes = 0;
    bool res = true;
    int i = start;

    // Pass 1: validate UTF-8 sequences, count symbol frequencies, and record tokens.
    while (i < srcEnd) {
        const uint8 b0 = uint8(src[i]);

        if (b0 < 0x80) {
            if ((i + 8 <= srcEnd) &&
                ((uint64(LittleEndian::readLong64(&src[i])) & HIGH_BIT_MASK) == 0)) {
                do {
                    const uint64 w = uint64(LittleEndian::readLong64(&src[i]));

                    if ((w & HIGH_BIT_MASK) != 0)
                        break;

                    const uint8 c0 = uint8(w), c1 = uint8(w >> 8), c2 = uint8(w >> 16), c3 = uint8(w >> 24);
                    const uint8 c4 = uint8(w >> 32), c5 = uint8(w >> 40), c6 = uint8(w >> 48), c7 = uint8(w >> 56);
                    f0[c0]++; f1[c1]++; f2[c2]++; f3[c3]++;
                    f0[c4]++; f1[c5]++; f2[c6]++; f3[c7]++;
                    tokens[ntok + 0] = c0; tokens[ntok + 1] = c1; tokens[ntok + 2] = c2; tokens[ntok + 3] = c3;
                    tokens[ntok + 4] = c4; tokens[ntok + 5] = c5; tokens[ntok + 6] = c6; tokens[ntok + 7] = c7;
                    ntok += 8;
                    i += 8;
                } while (i + 8 <= srcEnd);

                continue;
            }

            f0[b0]++;
            tokens[ntok++] = b0;
            i++;
            continue;
        }

        uint32 val;
        int s;

        if (b0 < 0xE0) {
            const uint8 b1 = uint8(src[i + 1]);

            if ((b0 < 0xC2) || ((b1 & 0xC0) != 0x80)) {
                res = false;
                break;
            }

            val = (1u << 19) | (uint32(b0) << 8) | uint32(b1);
            s = 2;
            contBytes += 1;
        }
        else if (b0 < 0xF0) {
            // Loop stops at count - 4, so 4 bytes are always readable here.
            const uint32 w = uint32(LittleEndian::readInt32(&src[i]));
            const uint8 b1 = uint8(w >> 8);
            const uint8 slot = b0 & 0x1F;

            if ((uint8(b1 - MIN_B1[slot]) > SPAN_B1[slot]) || ((w & 0x00C00000u) != 0x00800000u)) {
                res = false;
                break;
            }

            val = (2u << 19) | ((w & 0x0Fu) << 12) | ((w >> 2) & 0x0FC0u) | ((w >> 16) & 0x3Fu);
            s = 3;
            contBytes += 2;
        }
        else {
            if (b0 > 0xF4) {
                res = false;
                break;
            }

            const uint32 w = uint32(LittleEndian::readInt32(&src[i]));
            const uint8 b1 = uint8(w >> 8);
            const uint8 slot = b0 & 0x1F;

            if ((uint8(b1 - MIN_B1[slot]) > SPAN_B1[slot]) || ((w & 0xC0C00000u) != 0x80800000u)) {
                res = false;
                break;
            }

            val = (4u << 19) | ((w & 0x07u) << 18) | ((w & 0x3F00u) << 4) | ((w >> 10) & 0x0FC0u) | ((w >> 24) & 0x3Fu);
            s = 4;
            contBytes += 3;
        }

        if (_aliasMap[val] == 0) {
            if (mbKeys.size() >= 32767) {
                res = false;
                break;
            }

            mbKeys.push_back(val);
        }

        _aliasMap[val]++;
        tokens[ntok++] = val;
        i += s;
    }

    const int srcIdxEnd = i;

    if (res == false)
        return false;

    if (mustValidate && (contBytes < uint((count - start - 4) / 8)))
        return false;

    vector<uint64> items;
    items.reserve(mbKeys.size() + 128);

    for (uint32 c = 0; c < 128; c++) {
        const uint freq = f0[c] + f1[c] + f2[c] + f3[c];

        if (freq != 0)
            items.push_back((uint64(freq) << 24) | uint64(c));
    }

    for (size_t k = 0; k < mbKeys.size(); k++) {
        const uint32 val = mbKeys[k];
        items.push_back((uint64(_aliasMap[val]) << 24) | uint64(val));
    }

    const int n = int(items.size());
    const int maxTarget = count - (count / 10);

    if ((n == 0) || (n >= 32768))
        return false;

    if (_pCtx != nullptr)
        _pCtx->putInt("dataType", Global::UTF8);

    if ((3 * n + 6) >= maxTarget)
        return false;

    // Sort ranks by decreasing frequencies (ties broken by higher val, matching 3-byte code order).
    sort(items.begin(), items.end(), greater<uint64>());

    int dstIdx = 2;

    // Emit map length then map data
    dst[dstIdx++] = kanzi::byte(n >> 8);
    dst[dstIdx++] = kanzi::byte(n);

    // Minimum possible encoded length = 4 + 3*n (map) + start + 1 (min tail) + body
    int estimate = dstIdx + 3 * n + start + 1;

    for (int r = 0; r < n; r++) {
        const uint64 it = items[size_t(r)];
        const uint freq = uint(it >> 24);
        const uint32 s = uint32(it & 0xFFFFFFu);
        estimate += int((r < 128) ? freq : 2 * freq);

        _aliasMap[s] = (r < 128) ? uint32(r) : uint32(0x10080 | ((r << 1) & 0xFF00) | (r & 0x7F));
        dst[dstIdx] = kanzi::byte(s >> 16);
        dst[dstIdx + 1] = kanzi::byte(s >> 8);
        dst[dstIdx + 2] = kanzi::byte(s);
        dstIdx += 3;
    }

    if (estimate >= maxTarget)
        return false;

    // Emit first (possibly invalid) symbols (due to block truncation)
    for (int k = 0; k < start; k++)
        dst[dstIdx++] = src[k];

    // Pass 2: emit aliases directly from the recorded token stream.
    // A 2-byte write is always in-bounds because estimate < maxTarget guarantees > 8 KiB
    // of trailing capacity, and any unused high byte is overwritten by the next write.
    int tk = 0;
    const uint32* const map = _aliasMap;

    for (; tk + 4 <= ntok; tk += 4) {
        const uint32 a0 = map[tokens[tk + 0]];
        const uint32 a1 = map[tokens[tk + 1]];
        const uint32 a2 = map[tokens[tk + 2]];
        const uint32 a3 = map[tokens[tk + 3]];
        LittleEndian::writeInt16(&dst[dstIdx], int16(a0));
        dstIdx += 1 + int(a0 >> 16);
        LittleEndian::writeInt16(&dst[dstIdx], int16(a1));
        dstIdx += 1 + int(a1 >> 16);
        LittleEndian::writeInt16(&dst[dstIdx], int16(a2));
        dstIdx += 1 + int(a2 >> 16);
        LittleEndian::writeInt16(&dst[dstIdx], int16(a3));
        dstIdx += 1 + int(a3 >> 16);
    }

    for (; tk < ntok; tk++) {
        const uint32 a = map[tokens[tk]];
        LittleEndian::writeInt16(&dst[dstIdx], int16(a));
        dstIdx += 1 + int(a >> 16);
    }

    int srcIdx = srcIdxEnd;
    dst[0] = kanzi::byte(start);
    dst[1] = kanzi::byte(srcIdx - srcEnd);

    // Emit last (possibly invalid) symbols (due to block truncation)
    while (srcIdx < count)
        dst[dstIdx++] = src[srcIdx++];

    input._index += srcIdx;
    output._index += dstIdx;
    return dstIdx < maxTarget;
}

bool UTFCodec::inverse(SliceArray<kanzi::byte>& input, SliceArray<kanzi::byte>& output, int count)
{
    if (count == 0)
        return true;

    if (count < 4)
        return false;

    if (!SliceArray<kanzi::byte>::isValid(input))
        throw invalid_argument("UTFCodec: Invalid input block");

    if (!SliceArray<kanzi::byte>::isValid(output))
        throw invalid_argument("UTFCodec: Invalid output block");

    if (input._index + count > input._length)
        return false;

    const kanzi::byte* src = &input._array[input._index];
    kanzi::byte* dst = &output._array[output._index];
    const int start = int(src[0]) & 0x03;
    const int adjust = int(src[1]) & 0x03; // adjust end of regular processing
    const int n = (int(src[2]) << 8) + int(src[3]);

    // Protect against invalid map size value
    if ((n == 0) || (n >= 32768) || (3 * n > count - 4))
        return false;

    struct symb {
        uint32 val;
        uint8 len;
    };

    symb m[32768];
    int srcIdx = 4;
    int maxLen = 1;

    // Build inverse mapping
    for (int i = 0; i < n; i++) {
        const uint32 s = (uint32(src[srcIdx]) << 16) | (uint32(src[srcIdx + 1]) << 8) | uint32(src[srcIdx + 2]);
        m[i].val = 0; // avoid copying uninitialized stack bytes in 4-byte stores below
        const int sl = unpack(s, reinterpret_cast<kanzi::byte*>(&m[i].val));

        if (sl == 0)
            return false;

        m[i].len = uint8(sl);

        if (sl > maxLen)
            maxLen = sl;

        srcIdx += 3;
    }

    int dstIdx = 0;
    const int srcEnd = count - 4 + adjust;
    const int dstCap = output._length - output._index;

    if (dstCap < 4)
        return false;

    if ((srcEnd > count) || (srcIdx + start > srcEnd) || (dstIdx + start > dstCap))
        return false;

    for (int i = 0; i < start; i++)
        dst[dstIdx++] = src[srcIdx++];

    // Emit data
    if (n <= 128) {
        // Reserve 4 bytes before dstCap so unconditional 4-byte stores cannot overflow.
        while ((srcIdx < srcEnd) && (dstCap - dstIdx >= 4)) {
            const int safeSteps = min(srcEnd - srcIdx, (dstCap - dstIdx - 4) / maxLen);

            if (safeSteps < 4)
                break;

            const int batchEnd = srcIdx + (safeSteps & ~3);

            while (srcIdx < batchEnd) {
                const uint a0 = uint(src[srcIdx + 0]);
                const uint a1 = uint(src[srcIdx + 1]);
                const uint a2 = uint(src[srcIdx + 2]);
                const uint a3 = uint(src[srcIdx + 3]);
                srcIdx += 4;

                if (KANZI_UNLIKELY(max(max(a0, a1), max(a2, a3)) >= uint(n)))
                    return false;

                const symb& s0 = m[a0];
                const symb& s1 = m[a1];
                const symb& s2 = m[a2];
                const symb& s3 = m[a3];

                memcpy(&dst[dstIdx], &s0.val, 4);
                dstIdx += s0.len;
                memcpy(&dst[dstIdx], &s1.val, 4);
                dstIdx += s1.len;
                memcpy(&dst[dstIdx], &s2.val, 4);
                dstIdx += s2.len;
                memcpy(&dst[dstIdx], &s3.val, 4);
                dstIdx += s3.len;
            }
        }

        while (srcIdx < srcEnd) {
            const uint alias = uint(src[srcIdx++]);

            if (KANZI_UNLIKELY(alias >= uint(n)))
                return false;

            const symb& s = m[alias];

            if (KANZI_LIKELY(dstIdx + 4 <= dstCap))
                memcpy(&dst[dstIdx], &s.val, 4);
            else if (dstIdx + int(s.len) <= dstCap)
                memcpy(&dst[dstIdx], &s.val, size_t(s.len));
            else
                return false;

            dstIdx += s.len;
        }
    }
    else {
        const int pairStride = 2 * maxLen;

        // Reserve 4 bytes of src input and 4 bytes of dst slack beyond pairStride
        // so inner iterations avoid per-symbol bounds checks.
        while ((srcEnd - srcIdx >= 4) && (dstCap - dstIdx >= 4)) {
            const int safePairs = min((srcEnd - srcIdx) >> 2, (dstCap - dstIdx - 4) / pairStride);

            if (safePairs == 0)
                break;

            for (int p = 0; p < safePairs; p++) {
                const uint32 w4 = uint32(LittleEndian::readInt32(&src[srcIdx]));

                if ((p + 1 < safePairs) && ((w4 & 0x80808080u) == 0)) {
                    // Four 1-byte aliases in a row are always < 128 < n.
                    const symb& s0 = m[uint8(w4)];
                    const symb& s1 = m[uint8(w4 >> 8)];
                    const symb& s2 = m[uint8(w4 >> 16)];
                    const symb& s3 = m[uint8(w4 >> 24)];
                    srcIdx += 4;
                    memcpy(&dst[dstIdx], &s0.val, 4);
                    dstIdx += s0.len;
                    memcpy(&dst[dstIdx], &s1.val, 4);
                    dstIdx += s1.len;
                    memcpy(&dst[dstIdx], &s2.val, 4);
                    dstIdx += s2.len;
                    memcpy(&dst[dstIdx], &s3.val, 4);
                    dstIdx += s3.len;
                    p++;
                    continue;
                }

                uint a0 = uint(src[srcIdx++]);

                if (a0 >= 128)
                    a0 = (uint(src[srcIdx++]) << 7) + (a0 & 0x7F);

                if (KANZI_UNLIKELY(a0 >= uint(n)))
                    return false;

                const symb& s0 = m[a0];
                const uint32 val0 = s0.val;
                const uint8 len0 = s0.len;

                uint a1 = uint(src[srcIdx++]);

                if (a1 >= 128)
                    a1 = (uint(src[srcIdx++]) << 7) + (a1 & 0x7F);

                if (KANZI_UNLIKELY(a1 >= uint(n)))
                    return false;

                const symb& s1 = m[a1];
                memcpy(&dst[dstIdx], &val0, 4);
                dstIdx += len0;
                memcpy(&dst[dstIdx], &s1.val, 4);
                dstIdx += s1.len;
            }
        }

        while (srcIdx < srcEnd) {
            uint alias = uint(src[srcIdx++]);

            if (alias >= 128) {
                if (KANZI_UNLIKELY(srcIdx >= srcEnd))
                    return false;

                alias = (uint(src[srcIdx++]) << 7) + (alias & 0x7F);
            }

            if (KANZI_UNLIKELY(alias >= uint(n)))
                return false;

            const uint32 val0 = m[alias].val;
            const uint8 len0 = m[alias].len;

            if (srcIdx >= srcEnd) {
                if (KANZI_LIKELY(dstIdx + 4 <= dstCap))
                    memcpy(&dst[dstIdx], &val0, 4);
                else if (dstIdx + int(len0) <= dstCap)
                    memcpy(&dst[dstIdx], &val0, size_t(len0));
                else
                    return false;

                dstIdx += len0;
                break;
            }

            alias = uint(src[srcIdx++]);

            if (alias >= 128) {
                if (KANZI_UNLIKELY(srcIdx >= srcEnd))
                    return false;

                alias = (uint(src[srcIdx++]) << 7) + (alias & 0x7F);
            }

            if (KANZI_UNLIKELY(alias >= uint(n)))
                return false;

            const uint32 val1 = m[alias].val;
            const uint8 len1 = m[alias].len;

            if (KANZI_LIKELY(dstCap - dstIdx >= int(len0) + int(len1) + 4)) {
                memcpy(&dst[dstIdx], &val0, 4);
                dstIdx += len0;
                memcpy(&dst[dstIdx], &val1, 4);
                dstIdx += len1;
            }
            else {
                if (KANZI_UNLIKELY(dstCap - dstIdx < int(len0) + int(len1)))
                    return false;

                memcpy(&dst[dstIdx], &val0, size_t(len0));
                dstIdx += len0;
                memcpy(&dst[dstIdx], &val1, size_t(len1));
                dstIdx += len1;
            }
        }
    }

    if (srcIdx == srcEnd) {
        if ((srcIdx + 4 - adjust > count) || (dstIdx + 4 - adjust > dstCap))
            return false;

        for (int i = 0; i < 4 - adjust; i++)
            dst[dstIdx++] = src[srcIdx++];
    }

    input._index += srcIdx;
    output._index += dstIdx;
    return srcIdx == count;
}
