/*
Copyright 2011-2021 Frederic Langlet
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

#include <map>
#include <vector>
#include "BWT.hpp"
#include "../Global.hpp"

#ifdef CONCURRENCY_ENABLED
#include <future>
#endif

using namespace kanzi;
using namespace std;

BWT::BWT(int jobs) THROW
{
    _buffer = nullptr;
    _sa = nullptr;
    _bufferSize = 0;

#ifndef CONCURRENCY_ENABLED
    if (jobs > 1)
        throw invalid_argument("The number of jobs is limited to 1 in this version");
#endif

    _jobs = jobs;
    memset(_primaryIndexes, 0, sizeof(int) * 8);
}

BWT::~BWT()
{
    if (_buffer != nullptr)
        delete[] _buffer;

    if (_sa != nullptr)
        delete[] _sa;
}

bool BWT::setPrimaryIndex(int n, int primaryIndex)
{
    if ((primaryIndex < 0) || (n < 0) || (n >= 8))
        return false;

    _primaryIndexes[n] = primaryIndex;
    return true;
}

bool BWT::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count) THROW
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("Invalid output block");

    if (count > maxBlockSize()) {
        // Not a recoverable error: instead of silently fail the transform,
        // issue a fatal error.
        stringstream ss;
        ss << "The max BWT block size is " << maxBlockSize() << ", got " << count;
        throw invalid_argument(ss.str());
    }

    if (count < 2) {
        if (count == 1)
            output._array[output._index++] = input._array[input._index++];

        return true;
    }

    byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];

    // Lazy dynamic memory allocation
    if ((_sa == nullptr) || (_bufferSize < count)) {
        if (_sa != nullptr)
            delete[] _sa;

        _bufferSize = count;
        _sa = new int[_bufferSize];
    }

    bool res = true;
    int* sa = _sa;
    const int chunks = getBWTChunks(count);

    if (chunks == 1) {
        const int pIdx = _saAlgo.computeBWT(src, dst, sa, 0, count);
        res = setPrimaryIndex(0, pIdx);
    }
    else {
        _saAlgo.computeSuffixArray(src, sa, 0, count);
        const int st = count / chunks;
        const int step = (chunks * st == count) ? st : st + 1;
        dst[0] = src[count - 1];

        for (int i = 0, idx = 0; i < count; i++) {
            if ((sa[i] % step) != 0)
                continue;

            if (setPrimaryIndex(sa[i] / step, i + 1) == true) {
                idx++;

                if (idx == chunks)
                    break;
            }
        }

        const int pIdx0 = getPrimaryIndex(0);

        for (int i = 0; i < pIdx0 - 1; i++)
            dst[i + 1] = src[sa[i] - 1];

        for (int i = pIdx0; i < count; i++)
            dst[i] = src[sa[i] - 1];
    }

    input._index += count;
    output._index += count;
    return res;
}

bool BWT::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count) THROW
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("Invalid output block");

    if (count > maxBlockSize()) {
        // Not a recoverable error: instead of silently fail the transform,
        // issue a fatal error.
        stringstream ss;
        ss << "The max BWT block size is " << maxBlockSize() << ", got " << count;
        throw invalid_argument(ss.str());
    }

    if (count < 2) {
        if (count == 1)
            output._array[output._index++] = input._array[input._index++];

        return true;
    }

    // Find the fastest way to implement inverse based on block size
    if ((count <= BLOCK_SIZE_THRESHOLD2) && (_jobs == 1))
        return inverseMergeTPSI(input, output, count);

    return inverseBiPSIv2(input, output, count);
}

// When count <= BLOCK_SIZE_THRESHOLD2, mergeTPSI algo
bool BWT::inverseMergeTPSI(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    // Lazy dynamic memory allocation
    if ((_buffer == nullptr) || (_bufferSize < count)) {
        if (_buffer != nullptr)
            delete[] _buffer;

        _bufferSize = max(count, 64);
        _buffer = new uint[_bufferSize];
    }

    const int pIdx = getPrimaryIndex(0);

    if ((pIdx < 0) || (pIdx > count))
        return false;

    // Build array of packed index + value (assumes block size < 1<<24)
    uint buckets[256] = { 0 };
    Global::computeHistogram(&input._array[input._index], count, buckets, true);

    for (int i = 0, sum = 0; i < 256; i++) {
        const int tmp = buckets[i];
        buckets[i] = sum;
        sum += tmp;
    }

    byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    uint* data = _buffer;

    for (int i = 0; i < pIdx; i++) {
        const uint8 val = uint8(src[i]);
        data[buckets[val]] = ((i - 1) << 8) | val;
        buckets[val]++;
    }

    for (int i = pIdx; i < count; i++) {
        const uint8 val = uint8(src[i]);
        data[buckets[val]] = (i << 8) | val;
        buckets[val]++;
    }

    if (count < BLOCK_SIZE_THRESHOLD1) {
        for (int i = 0, t = pIdx - 1; i < count; i++) {
            const uint ptr = data[t];
            dst[i] = byte(ptr);
            t = ptr >> 8;
        }
    }
    else {
        const int ckSize = ((count & 7) == 0) ? count >> 3 : (count >> 3) + 1;
        int t0 = getPrimaryIndex(0) - 1;
        int t1 = getPrimaryIndex(1) - 1;
        int t2 = getPrimaryIndex(2) - 1;
        int t3 = getPrimaryIndex(3) - 1;
        int t4 = getPrimaryIndex(4) - 1;
        int t5 = getPrimaryIndex(5) - 1;
        int t6 = getPrimaryIndex(6) - 1;
        int t7 = getPrimaryIndex(7) - 1;
        int n = 0;

        while (true) {
            const int ptr0 = data[t0];
            dst[n] = byte(ptr0);
            t0 = ptr0 >> 8;
            const int ptr1 = data[t1];
            dst[n + ckSize * 1] = byte(ptr1);
            t1 = ptr1 >> 8;
            const int ptr2 = data[t2];
            dst[n + ckSize * 2] = byte(ptr2);
            t2 = ptr2 >> 8;
            const int ptr3 = data[t3];
            dst[n + ckSize * 3] = byte(ptr3);
            t3 = ptr3 >> 8;
            const int ptr4 = data[t4];
            dst[n + ckSize * 4] = byte(ptr4);
            t4 = ptr4 >> 8;
            const int ptr5 = data[t5];
            dst[n + ckSize * 5] = byte(ptr5);
            t5 = ptr5 >> 8;
            const int ptr6 = data[t6];
            dst[n + ckSize * 6] = byte(ptr6);
            t6 = ptr6 >> 8;
            const int ptr7 = data[t7];
            dst[n + ckSize * 7] = byte(ptr7);
            t7 = ptr7 >> 8;
            n++;

            if (ptr7 < 0)
                break;
        }

        while (n < ckSize) {
            const int ptr0 = data[t0];
            dst[n] = byte(ptr0);
            t0 = ptr0 >> 8;
            const int ptr1 = data[t1];
            dst[n + ckSize * 1] = byte(ptr1);
            t1 = ptr1 >> 8;
            const int ptr2 = data[t2];
            dst[n + ckSize * 2] = byte(ptr2);
            t2 = ptr2 >> 8;
            const int ptr3 = data[t3];
            dst[n + ckSize * 3] = byte(ptr3);
            t3 = ptr3 >> 8;
            const int ptr4 = data[t4];
            dst[n + ckSize * 4] = byte(ptr4);
            t4 = ptr4 >> 8;
            const int ptr5 = data[t5];
            dst[n + ckSize * 5] = byte(ptr5);
            t5 = ptr5 >> 8;
            const int ptr6 = data[t6];
            dst[n + ckSize * 6] = byte(ptr6);
            t6 = ptr6 >> 8;
            n++;
        }
    }

    input._index += count;
    output._index += count;
    return true;
}

// When count > BLOCK_SIZE_THRESHOLD2, biPSIv2 algo
bool BWT::inverseBiPSIv2(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    // Lazy dynamic memory allocations
    if ((_buffer == nullptr) || (_bufferSize < count + 1)) {
        if (_buffer != nullptr)
            delete[] _buffer;

        _bufferSize = max(count + 1, 64);
        _buffer = new uint[_bufferSize];
    }

    byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    const int pIdx = getPrimaryIndex(0);

    if ((pIdx < 0) || (pIdx > count))
        return false;

    uint* buckets = new uint[65536];
    memset(&buckets[0], 0, 65536 * sizeof(uint));
    uint freqs[256];
    Global::computeHistogram(&input._array[input._index], count, freqs, true);

    for (int sum = 1, c = 0; c < 256; c++) {
        const int f = sum;
        sum += int(freqs[c]);
        freqs[c] = f;

        if (f != sum) {
            uint* ptr = &buckets[c << 8];
            const int hi = min(sum, pIdx);

            for (int i = f; i < hi; i++)
                ptr[int(src[i])]++;

            const int lo = max(f - 1, pIdx);

            for (int i = lo; i < sum - 1; i++)
                ptr[int(src[i])]++;
        }
    }

    const int lastc = int(src[0]);
    uint16* fastBits = new uint16[MASK_FASTBITS + 1];
    memset(&fastBits[0], 0, (MASK_FASTBITS + 1) * sizeof(uint16));
    int shift = 0;

    while ((count >> shift) > MASK_FASTBITS)
        shift++;

    for (int v = 0, sum = 1, c = 0; c < 256; c++) {
        if (c == lastc)
            sum++;

        uint* ptr = &buckets[c];

        for (int d = 0; d < 256; d++) {
            const int s = sum;
            sum += ptr[d << 8];
            ptr[d << 8] = s;

            if (s == sum)
                continue;

            for (; v <= ((sum - 1) >> shift); v++)
                fastBits[v] = uint16((c << 8) | d);
        }
    }

    uint* data = &_buffer[0];

    for (int i = 0; i < pIdx; i++) {
        const int c = int(src[i]);
        const int p = freqs[c];
        freqs[c]++;

        if (p < pIdx)
            data[buckets[(c << 8) | int(src[p])]++] = i;
        else if (p > pIdx)
            data[buckets[(c << 8) | int(src[p - 1])]++] = i;
    }

    for (int i = pIdx; i < count; i++) {
        const int c = int(src[i]);
        const int p = freqs[c];
        freqs[c]++;

        if (p < pIdx)
            data[buckets[(c << 8) | int(src[p])]++] = i + 1;
        else if (p > pIdx)
            data[buckets[(c << 8) | int(src[p - 1])]++] = i + 1;
    }

    for (int c = 0; c < 256; c++) {
        const int c256 = c << 8;

        for (int d = 0; d < c; d++) {
            const int tmp = buckets[(d << 8) | c];
            buckets[(d << 8) | c] = buckets[c256 | d];
            buckets[c256 | d] = tmp;
        }
    }

    const int chunks = getBWTChunks(count);

    // Build inverse
    const int st = count / chunks;
    const int ckSize = (chunks * st == count) ? st : st + 1;
    const int nbTasks = (_jobs < chunks) ? _jobs : chunks;

    if (nbTasks == 1) {
        InverseBiPSIv2Task<int> task(data, buckets, fastBits, dst, _primaryIndexes,
            count, 0, ckSize, 0, chunks);
        task.run();
    }
    else {
#ifdef CONCURRENCY_ENABLED
        // Several chunks may be decoded concurrently (depending on the availability
        // of jobs per block).
        int* jobsPerTask = new int[nbTasks];
        Global::computeJobsPerTask(jobsPerTask, chunks, nbTasks);
        vector<future<int> > futures;
        vector<InverseBiPSIv2Task<int>*> tasks;

        // Create one task per job
        for (int j = 0, c = 0; j < nbTasks; j++) {
            // Each task decodes jobsPerTask[j] chunks
            const int start = c * ckSize;

            InverseBiPSIv2Task<int>* task = new InverseBiPSIv2Task<int>(data, buckets, fastBits, dst, _primaryIndexes,
                count, start, ckSize, c, c + jobsPerTask[j]);
            tasks.push_back(task);
            futures.push_back(async(launch::async, &InverseBiPSIv2Task<int>::run, task));
            c += jobsPerTask[j];
        }

        // Wait for completion of all concurrent tasks
        for (int j = 0; j < nbTasks; j++)
            futures[j].get();

        // Cleanup
        for (InverseBiPSIv2Task<int>* task : tasks)
            delete task;

        delete[] jobsPerTask;
#else
        // nbTasks > 1 but concurrency is not enabled (should never happen)
        throw invalid_argument("Error during BWT inverse: concurrency not supported");
#endif
    }

    dst[count - 1] = byte(lastc);
    delete[] fastBits;
    delete[] buckets;
    input._index += count;
    output._index += count;
    return true;
}

template <class T>
InverseBiPSIv2Task<T>::InverseBiPSIv2Task(uint* buf, uint* buckets, uint16* fastBits, byte* output,
    int* primaryIndexes, int total, int start, int ckSize, int firstChunk, int lastChunk)
{
    _data = buf;
    _fastBits = fastBits;
    _buckets = buckets;
    _primaryIndexes = primaryIndexes;
    _dst = output;
    _total = total;
    _start = start;
    _ckSize = ckSize;
    _firstChunk = firstChunk;
    _lastChunk = lastChunk;
}

template <class T>
T InverseBiPSIv2Task<T>::run() THROW
{
    int shift = 0;

    while ((_total >> shift) > BWT::MASK_FASTBITS)
        shift++;

    for (int c = _firstChunk; c < _lastChunk; c++) {
        const int end = min(_start + _ckSize, _total - 1);
        uint p = _primaryIndexes[c];

        for (int i = _start + 1; i <= end; i += 2) {
            uint16 s = _fastBits[p >> shift];

            while (_buckets[s] <= (const uint)p)
                s++;

            _dst[i - 1] = byte(s >> 8);
            _dst[i] = byte(s);
            p = _data[p];
        }

        _start = end;
    }

    return T(0);
}
