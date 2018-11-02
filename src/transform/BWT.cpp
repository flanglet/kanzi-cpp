
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

#include <map>
#include <vector>
#include "BWT.hpp"
#include "../Memory.hpp"
#include "../IllegalArgumentException.hpp"

#ifdef CONCURRENCY_ENABLED
#include <future>
#endif

using namespace kanzi;

BWT::BWT(int jobs) THROW
{
    _buffer1 = nullptr; // Only used in inverse
    _buffer2 = nullptr; // Only used for big blocks (size >= 1<<24)
    _buffer3 = nullptr; // Only used in forward
    _bufferSize = 0;

#ifndef CONCURRENCY_ENABLED
    if (jobs > 1)
        throw IllegalArgumentException("The number of jobs is limited to 1 in this version");
#endif

    _jobs = jobs;
    memset(_primaryIndexes, 0, sizeof(int) * 8);
}

BWT::~BWT() 
{
    if (_buffer1 != nullptr)
        delete[] _buffer1;

    if (_buffer2 != nullptr)
        delete[] _buffer2;

    if (_buffer3 != nullptr)
        delete[] _buffer3;
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
    if ((!SliceArray<byte>::isValid(input)) || (!SliceArray<byte>::isValid(output)))
        return false;

    if ((count < 0) || (count + input._index > input._length))
        return false;

    if (count > maxBlockSize()) {
        // Not a recoverable error: instead of silently fail the transform,
        // issue a fatal error.
        stringstream ss;
        ss << "The max BWT block size is " << maxBlockSize() << ", got " << count;
        throw IllegalArgumentException(ss.str());
    }

    if (count < 2) {
        if (count == 1)
            output._array[output._index++] = input._array[input._index++];

        return true;
    }

    byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];

    // Lazy dynamic memory allocation
    if ((_buffer3 == nullptr) || (_bufferSize < count)) {
        if (_buffer3 != nullptr)
            delete[] _buffer3;

        _bufferSize = count;
        _buffer3 = new int[_bufferSize];
    }

    int* sa = _buffer3;
    _saAlgo.computeSuffixArray(src, sa, 0, count);
    int n = 0;
    const int chunks = getBWTChunks(count);
    bool res = true;

    if (chunks == 1) {
        for (; n < count; n++) {
            if (sa[n] == 0) {
                res &= setPrimaryIndex(0, n);
                break;
            }

            dst[n] = src[sa[n] - 1];
        }

        dst[n] = src[count - 1];
        n++;

        for (; n < count; n++)
            dst[n] = src[sa[n] - 1];
    }
    else {
        const int st = count / chunks;
        const int step = (chunks*st == count) ? st : st + 1;

        for (; n < count; n++) {
            if ((sa[n] % step) == 0) {
                res &= setPrimaryIndex(sa[n] / step, n);

                if (sa[n] == 0)
                    break;
            }

            dst[n] = src[sa[n] - 1];
        }

        dst[n] = src[count - 1];
        n++;

        for (; n < count; n++) {
            if ((sa[n] % step) == 0)
                res &= setPrimaryIndex(sa[n] / step, n);

            dst[n] = src[sa[n] - 1];
        }
    }

    input._index += count;
    output._index += count;
    return res;
}

bool BWT::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count) THROW
{
    if ((!SliceArray<byte>::isValid(input)) || (!SliceArray<byte>::isValid(output)))
        return false;

    if ((count < 0) || (count + input._index > input._length))
        return false;

    if (count > maxBlockSize()) {
        // Not a recoverable error: instead of silently fail the transform,
        // issue a fatal error.
        stringstream ss;
        ss << "The max BWT block size is " << maxBlockSize() << ", got " << count;
        throw IllegalArgumentException(ss.str());
    }

    if (count < 2) {
        if (count == 1)
            output._array[output._index++] = input._array[input._index++];

        return true;
    }

    // Find the fastest way to implement inverse based on block size
    if (count < 1 << 24)
        return inverseRegularBlock(input, output, count);

    if (5 * uint64(count) >= (uint64(1) << 31))
        return inverseHugeBlock(input, output, count);

    return inverseBigBlock(input, output, count);
}

// When count < 1<<24
bool BWT::inverseRegularBlock(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];

    // Lazy dynamic memory allocation
    if ((_buffer1 == nullptr) || (_bufferSize < count)) {
        if (_buffer1 != nullptr)
            delete[] _buffer1;

        _bufferSize = count;
        _buffer1 = new uint32[_bufferSize];
    }

    // Aliasing
    uint32* data = _buffer1;

    int chunks = getBWTChunks(count);
    uint32 buckets[256] = { 0 };

    // Build array of packed index + value (assumes block size < 2^24)
    // Start with the primary index position
    int pIdx = getPrimaryIndex(0);
    const uint8 val0 = src[pIdx];
    data[pIdx] = val0;
    buckets[val0]++;

    for (int i = 0; i < pIdx; i++) {
        const uint8 val = src[i];
        data[i] = (buckets[val] << 8) | val;
        buckets[val]++;
    }

    for (int i = pIdx + 1; i < count; i++) {
        const uint8 val = src[i];
        data[i] = (buckets[val] << 8) | val;
        buckets[val]++;
    }

    uint32 sum = 0;

    // Create cumulative histogram
    for (int i = 0; i < 256; i++) {
        sum += buckets[i];
        buckets[i] = sum - buckets[i];
    }

    int idx = count - 1;

    // Build inverse
    if ((chunks == 1) || (_jobs == 1)) {
        // Shortcut for 1 chunk scenario
        uint32 ptr = data[pIdx];
        dst[idx--] = byte(ptr);

        for (; idx >= 0; idx--) {
            ptr = data[(ptr >> 8) + buckets[ptr & 0xFF]];
            dst[idx] = byte(ptr);
        }
    }
#ifdef CONCURRENCY_ENABLED
    else {
        // Several chunks may be decoded concurrently (depending on the availaibility
        // of jobs per block).
        const int st = count / chunks;
        const int step = (chunks*st == count) ? st : st + 1;
        const int nbTasks = (_jobs < chunks) ? _jobs : chunks;
        int* jobsPerTask = new int[nbTasks];
        Global::computeJobsPerTask(jobsPerTask, chunks, nbTasks);
        int c = chunks;
        vector<future<int> > futures;
        vector<InverseRegularChunkTask<int>*> tasks;

        // Create one task per job
        for (int j = 0; j < nbTasks; j++) {
            // Each task decodes jobsPerTask[j] chunks
            const int nc = c - jobsPerTask[j];
            const int end = nc * step;
            InverseRegularChunkTask<int>* task = new InverseRegularChunkTask<int>(data, buckets, dst, _primaryIndexes,
                pIdx, idx, step, c - 1, c - 1 - jobsPerTask[j]);
            tasks.push_back(task);
            futures.push_back(async(launch::async, &InverseRegularChunkTask<int>::call, task));
            c = nc;
            pIdx = getPrimaryIndex(c);
            idx = end - 1;
        }

        // Wait for completion of all concurrent tasks
        for (int j = 0; j < nbTasks; j++) {
            futures[j].get();
        }

        // Cleanup
        for (vector<InverseRegularChunkTask<int>*>::iterator it = tasks.begin(); it != tasks.end(); it++)
            delete *it;

        tasks.clear();
        delete[] jobsPerTask;
    }
#endif

    input._index += count;
    output._index += count;
    return true;
}

// When count >= 1<<24 and 5*count < 1<<31
bool BWT::inverseBigBlock(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];

    // Lazy dynamic memory allocations
    if ((_buffer2 == nullptr) || (_bufferSize < 5 * count)) {
        if (_buffer2 != nullptr)
            delete[] _buffer2;

        _bufferSize = 5 * count;
        _buffer2 = new byte[_bufferSize];
    }

    // Aliasing
    byte* data = _buffer2;

    int chunks = getBWTChunks(count);
    uint32 buckets[256] = { 0 };

    // Build arrays
    // Start with the primary index position
    int pIdx = getPrimaryIndex(0);
    const uint8 val0 = src[pIdx];
    LittleEndian::writeInt32(&data[5 * pIdx], buckets[val0]);
    data[5 * pIdx + 4] = val0;
    buckets[val0]++;

    for (int i = 0; i < pIdx; i++) {
        const uint8 val = src[i];
        LittleEndian::writeInt32(&data[5 * i], buckets[val]);
        data[5 * i + 4] = val;
        buckets[val]++;
    }

    for (int i = pIdx + 1; i < count; i++) {
        const uint8 val = src[i];
        LittleEndian::writeInt32(&data[5 * i], buckets[val]);
        data[5 * i + 4] = val;
        buckets[val]++;
    }

    uint32 sum = 0;

    // Create cumulative histogram
    for (int i = 0; i < 256; i++) {
        sum += buckets[i];
        buckets[i] = sum - buckets[i];
    }

    int idx = count - 1;

    // Build inverse
    if ((chunks == 1) || (_jobs == 1)) {
        // Shortcut for 1 chunk scenario
        uint8 val = data[5 * pIdx + 4];
        dst[idx--] = val;
        uint32 n = uint32(LittleEndian::readInt32(&data[5 * pIdx])) + buckets[val];

        for (; idx >= 0; idx--) {
            val = data[5 * n + 4];
            dst[idx] = val;
            n = uint32(LittleEndian::readInt32(&data[5 * n])) + buckets[val];
        }
    }
#ifdef CONCURRENCY_ENABLED
    else {
        // Several chunks may be decoded concurrently (depending on the availaibility
        // of jobs per block).
        const int st = count / chunks;
        const int step = (chunks*st == count) ? st : st + 1;
        const int nbTasks = (_jobs < chunks) ? _jobs : chunks;
        int* jobsPerTask = new int[nbTasks];
        Global::computeJobsPerTask(jobsPerTask, chunks, nbTasks);
        int c = chunks;
        vector<future<int> > futures;
        vector<InverseBigChunkTask<int>*> tasks;

        // Create one task per job
        for (int j = 0; j < nbTasks; j++) {
            // Each task decodes jobsPerTask[j] chunks
            const int nc = c - jobsPerTask[j];
            const int end = nc * step;

            InverseBigChunkTask<int>* task = new InverseBigChunkTask<int>(data, buckets, dst, _primaryIndexes,
                pIdx, idx, step, c - 1, c - 1 - jobsPerTask[j]);
            tasks.push_back(task);
            futures.push_back(async(launch::async, &InverseBigChunkTask<int>::call, task));
            c = nc;
            pIdx = getPrimaryIndex(c);
            idx = end - 1;
        }

        // Wait for completion of all concurrent tasks
        for (int j = 0; j < nbTasks; j++) {
            futures[j].get();
        }

        // Cleanup
        for (vector<InverseBigChunkTask<int>*>::iterator it = tasks.begin(); it != tasks.end(); it++)
            delete *it;

        tasks.clear();
        delete[] jobsPerTask;
    }
#endif

    input._index += count;
    output._index += count;
    return true;
}

// When 5*count >= 1<<31
bool BWT::inverseHugeBlock(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];

    // Lazy dynamic memory allocations
    if ((_buffer1 == nullptr) || (_bufferSize < count)) {
        if (_buffer1 != nullptr)
            delete[] _buffer1;

        if (_buffer2 != nullptr)
            delete[] _buffer2;

        _bufferSize = count;
        _buffer1 = new uint32[_bufferSize];
        _buffer2 = new byte[_bufferSize];
    }

    // Aliasing
    uint32* buckets = _buckets;
    uint32* data1 = _buffer1;
    byte* data2 = _buffer2;

    int chunks = getBWTChunks(count);

    // Initialize histogram
    memset(_buckets, 0, sizeof(_buckets));

    // Build arrays
    // Start with the primary index position
    int pIdx = getPrimaryIndex(0);
    const uint8 val0 = src[pIdx];
    data1[pIdx] = buckets[val0];
    data2[pIdx] = val0;
    buckets[val0]++;

    for (int i = 0; i < pIdx; i++) {
        const uint8 val = src[i];
        data1[i] = buckets[val];
        data2[i] = val;
        buckets[val]++;
    }

    for (int i = pIdx + 1; i < count; i++) {
        const uint8 val = src[i];
        data1[i] = buckets[val];
        data2[i] = val;
        buckets[val]++;
    }

    uint32 sum = 0;

    // Create cumulative histogram
    for (int i = 0; i < 256; i++) {
        sum += buckets[i];
        buckets[i] = sum - buckets[i];
    }

    int idx = count - 1;

    // Build inverse
    if ((chunks == 1) || (_jobs == 1)) {
        uint8 val = data2[pIdx];
        dst[idx--] = val;
        int n = data1[pIdx] + buckets[val];

        for (; idx >= 0; idx--) {
            val = data2[n];
            dst[idx] = val;
            n = data1[n] + buckets[val];
        }
    }
#ifdef CONCURRENCY_ENABLED
    else {
        // Several chunks may be decoded concurrently (depending on the availaibility
        // of jobs per block).
        const int st = count / chunks;
        const int step = (chunks*st == count) ? st : st + 1;
        const int nbTasks = (_jobs < chunks) ? _jobs : chunks;
        int* jobsPerTask = new int[nbTasks];
        Global::computeJobsPerTask(jobsPerTask, chunks, nbTasks);
        int c = chunks;
        vector<future<int> > futures;
        vector<InverseHugeChunkTask<int>*> tasks;

        // Create one task per job
        for (int j = 0; j < nbTasks; j++) {
            // Each task decodes jobsPerTask[j] chunks
            const int nc = c - jobsPerTask[j];
            const int end = nc * step;

            InverseHugeChunkTask<int>* task = new InverseHugeChunkTask<int>(data1, data2, buckets, dst, _primaryIndexes,
                pIdx, idx, step, c - 1, c - 1 - jobsPerTask[j]);
            tasks.push_back(task);
            futures.push_back(async(launch::async, &InverseHugeChunkTask<int>::call, task));
            c = nc;
            pIdx = getPrimaryIndex(c);
            idx = end - 1;
        }

        // Wait for completion of all concurrent tasks
        for (int j = 0; j < nbTasks; j++) {
            futures[j].get();
        }

        // Cleanup
        for (vector<InverseHugeChunkTask<int>*>::iterator it = tasks.begin(); it != tasks.end(); it++)
            delete *it;

        tasks.clear();
        delete[] jobsPerTask;
    }
#endif

    input._index += count;
    output._index += count;
    return true;
}

int BWT::getBWTChunks(int size)
{
    if (size < 1 << 23) // 8 MB
        return 1;

    const int res = (size + (1 << 22)) >> 23;
    return (res > BWT_MAX_CHUNKS) ? BWT_MAX_CHUNKS : res;
}

template <class T>
InverseRegularChunkTask<T>::InverseRegularChunkTask(uint32* buf, uint32* buckets, byte* output,
    int* primaryIndexes, int pIdx0, int startIdx, int step, int startChunk, int endChunk)
{
    _buffer = buf;
    _buckets = buckets;
    _primaryIndexes = primaryIndexes;
    _dst = output;
    _pIdx0 = pIdx0;
    _startIdx = startIdx;
    _step = step;
    _startChunk = startChunk;
    _endChunk = endChunk;
}

template <class T>
T InverseRegularChunkTask<T>::call() THROW
{
    int idx = _startIdx;
    int pIdx = _pIdx0;
    uint32* data = _buffer;
    byte* dst = _dst;

    for (int i = _startChunk; i > _endChunk; i--) {
        uint32 ptr = data[pIdx];
        dst[idx--] = byte(ptr);
        const int endIdx = i * _step;
        prefetchRead(&dst[idx]);

        for (; idx >= endIdx; idx--) {
            ptr = data[(ptr >> 8) + _buckets[ptr & 0xFF]];
            dst[idx] = byte(ptr);
        }

        pIdx = _primaryIndexes[i];
    }

    return T(0);
}

template <class T>
InverseHugeChunkTask<T>::InverseHugeChunkTask(uint32* buf1, byte* buf2, uint32* buckets, byte* output,
    int* primaryIndexes, int pIdx0, int startIdx, int step, int startChunk, int endChunk)
{
    _buffer1 = buf1;
    _buffer2 = buf2;
    _buckets = buckets;
    _primaryIndexes = primaryIndexes;
    _dst = output;
    _pIdx0 = pIdx0;
    _startIdx = startIdx;
    _step = step;
    _startChunk = startChunk;
    _endChunk = endChunk;
}

template <class T>
T InverseHugeChunkTask<T>::call() THROW
{
    int idx = _startIdx;
    int pIdx = _pIdx0;
    uint32* data1 = _buffer1;
    byte* data2 = _buffer2;
    byte* dst = _dst;

    for (int i = _startChunk; i > _endChunk; i--) {
        const int endIdx = i * _step;
        uint8 val = data2[pIdx];
        dst[idx--] = val;
        int n = data1[pIdx] + _buckets[val];

        for (; idx >= endIdx; idx--) {
            val = data2[n];
            dst[idx] = val;
            n = data1[n] + _buckets[val];
        }

        pIdx = _primaryIndexes[i];
    }

    return T(0);
}

template <class T>
InverseBigChunkTask<T>::InverseBigChunkTask(byte* buf, uint32* buckets, byte* output,
    int* primaryIndexes, int pIdx0, int startIdx, int step, int startChunk, int endChunk)
{
    _buffer = buf;
    _buckets = buckets;
    _primaryIndexes = primaryIndexes;
    _dst = output;
    _pIdx0 = pIdx0;
    _startIdx = startIdx;
    _step = step;
    _startChunk = startChunk;
    _endChunk = endChunk;
}

template <class T>
T InverseBigChunkTask<T>::call() THROW
{
    int idx = _startIdx;
    int pIdx = _pIdx0;
    byte* data = _buffer;
    byte* dst = _dst;

    for (int i = _startChunk; i > _endChunk; i--) {
        const int endIdx = i * _step;
        uint8 val = data[5 * pIdx + 4];
        dst[idx--] = val;
        uint32 n = uint32(LittleEndian::readInt32(&data[5 * pIdx])) + _buckets[val];
        prefetchRead(&dst[idx]);

        for (; idx >= endIdx; idx--) {
            val = data[5 * n + 4];
            dst[idx] = val;
            n = uint32(LittleEndian::readInt32(&data[5 * n])) + _buckets[val];
        }

        pIdx = _primaryIndexes[i];
    }

    return T(0);
}