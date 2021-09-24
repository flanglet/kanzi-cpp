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

#include <sstream>
#include "CompressedOutputStream.hpp"
#include "IOException.hpp"
#include "../Error.hpp"
#include "../bitstream/DefaultOutputBitStream.hpp"
#include "../entropy/EntropyCodecFactory.hpp"
#include "../entropy/EntropyUtils.hpp"
#include "../transform/TransformFactory.hpp"

#ifdef CONCURRENCY_ENABLED
#include <future>
#endif

using namespace kanzi;
using namespace std;

CompressedOutputStream::CompressedOutputStream(OutputStream& os, const string& entropyCodec, const string& transform, int bSize, int tasks, bool checksum)
    : OutputStream(os.rdbuf())
    , _os(os)
{
#ifdef CONCURRENCY_ENABLED
    if ((tasks <= 0) || (tasks > MAX_CONCURRENCY)) {
        stringstream ss;
        ss << "The number of jobs must be in [1.." << MAX_CONCURRENCY << "]";
        throw invalid_argument(ss.str());
    }
#else
    if (tasks != 1)
        throw invalid_argument("The number of jobs is limited to 1 in this version");
#endif

    if (bSize > MAX_BITSTREAM_BLOCK_SIZE) {
        std::stringstream ss;
        ss << "The block size must be at most " << (MAX_BITSTREAM_BLOCK_SIZE >> 20) << " MB";
        throw invalid_argument(ss.str());
    }

    if (bSize < MIN_BITSTREAM_BLOCK_SIZE) {
        std::stringstream ss;
        ss << "The block size must be at least " << MIN_BITSTREAM_BLOCK_SIZE;
        throw invalid_argument(ss.str());
    }

    if ((bSize & -16) != bSize)
        throw invalid_argument("The block size must be a multiple of 16");

#ifdef CONCURRENCY_ENABLED
    // Limit concurrency with big blocks to avoid too much memory usage
    if (uint64(bSize) * uint64(tasks) >= (uint64(1) << 30))
        tasks = int((uint(1) << 30) / uint(bSize));
#endif

    _blockId = 0;
    _blockSize = bSize;
    _nbInputBlocks = 0;
    _initialized = false;
    _closed = false;
    _obs = new DefaultOutputBitStream(os, DEFAULT_BUFFER_SIZE);
    _entropyType = EntropyCodecFactory::getType(entropyCodec.c_str());
    _transformType = TransformFactory<byte>::getType(transform.c_str());
    _hasher = (checksum == true) ? new XXHash32(BITSTREAM_TYPE) : nullptr;
    _jobs = tasks;
    _sa = new SliceArray<byte>(new byte[0], 0);
    _buffers = new SliceArray<byte>*[2 * _jobs];

    for (int i = 0; i < 2 * _jobs; i++)
        _buffers[i] = new SliceArray<byte>(new byte[0], 0, 0);
}

#if __cplusplus >= 201103L
CompressedOutputStream::CompressedOutputStream(OutputStream& os, Context& ctx,
          std::function<OutputBitStream*(OutputStream&)>* createBitStream)
#else
CompressedOutputStream::CompressedOutputStream(OutputStream& os, Context& ctx)
#endif
    : OutputStream(os.rdbuf())
    , _os(os)
    , _ctx(ctx)
{
    int tasks = ctx.getInt("jobs");

#ifdef CONCURRENCY_ENABLED
    if ((tasks <= 0) || (tasks > MAX_CONCURRENCY)) {
        stringstream ss;
        ss << "The number of jobs must be in [1.." << MAX_CONCURRENCY << "]";
        throw invalid_argument(ss.str());
    }
#else
    if (tasks != 1)
        throw invalid_argument("The number of jobs is limited to 1 in this version");
#endif

    string entropyCodec = ctx.getString("codec");
    string transform = ctx.getString("transform");
    int bSize = ctx.getInt("blockSize");

    if (bSize > MAX_BITSTREAM_BLOCK_SIZE) {
        std::stringstream ss;
        ss << "The block size must be at most " << (MAX_BITSTREAM_BLOCK_SIZE >> 20) << " MB";
        throw invalid_argument(ss.str());
    }

    if (bSize < MIN_BITSTREAM_BLOCK_SIZE) {
        std::stringstream ss;
        ss << "The block size must be at least " << MIN_BITSTREAM_BLOCK_SIZE;
        throw invalid_argument(ss.str());
    }

    if ((bSize & -16) != bSize)
        throw invalid_argument("The block size must be a multiple of 16");

#ifdef CONCURRENCY_ENABLED
    // Limit concurrency with big blocks to avoid too much memory usage
    if (uint64(bSize) * uint64(tasks) >= (uint64(1) << 30))
        tasks = int((uint(1) << 30) / uint(bSize));
#endif

    _blockId = 0;
    _blockSize = bSize;

    // If input size has been provided, calculate the number of blocks
    // in the input data else use 0. A value of 63 means '63 or more blocks'.
    // This value is written to the bitstream header to let the decoder make
    // better decisions about memory usage and job allocation in concurrent
    // decompression scenario.
    const int64 fileSize = ctx.getLong("fileSize", 0);
    const int64 nbBlocks = (fileSize + int64(bSize - 1)) / int64(bSize);
    _nbInputBlocks = (nbBlocks > 63) ? 63 : uint8(nbBlocks);

    _initialized = false;
    _closed = false;
#if __cplusplus >= 201103L
    // A hook can be provided by the caller to customize the instantiation of the
    // output bitstream.
    _obs = (createBitStream == nullptr) ? new DefaultOutputBitStream(os, DEFAULT_BUFFER_SIZE) : (*createBitStream)(_os);
#else
    _obs = new DefaultOutputBitStream(os, DEFAULT_BUFFER_SIZE);
#endif
    _entropyType = EntropyCodecFactory::getType(entropyCodec.c_str());
    _transformType = TransformFactory<byte>::getType(transform.c_str());
    string str = ctx.getString("checksum");
    bool checksum = str == STR_TRUE;
    _hasher = (checksum == true) ? new XXHash32(BITSTREAM_TYPE) : nullptr;
    _jobs = tasks;
    _ctx.putInt("bsVersion", BITSTREAM_FORMAT_VERSION);
    _sa = new SliceArray<byte>(new byte[0], 0);
    _buffers = new SliceArray<byte>*[2 * _jobs];

    for (int i = 0; i < 2 * _jobs; i++)
        _buffers[i] = new SliceArray<byte>(new byte[0], 0, 0);
}

CompressedOutputStream::~CompressedOutputStream()
{
    try {
        close();
    }
    catch (exception&) {
        // Ignore and continue
    }

    for (int i = 0; i < 2 * _jobs; i++) {
        delete[] _buffers[i]->_array;
        delete _buffers[i];
    }

    delete[] _buffers;
    delete _obs;
    delete[] _sa->_array;
    delete _sa;

    if (_hasher != nullptr) {
        delete _hasher;
        _hasher = nullptr;
    }
}

void CompressedOutputStream::writeHeader() THROW
{
    if (_obs->writeBits(BITSTREAM_TYPE, 32) != 32)
        throw IOException("Cannot write bitstream type to header", Error::ERR_WRITE_FILE);

    if (_obs->writeBits(BITSTREAM_FORMAT_VERSION, 4) != 4)
        throw IOException("Cannot write bitstream version to header", Error::ERR_WRITE_FILE);

    if (_obs->writeBits((_hasher != nullptr) ? 1 : 0, 1) != 1)
        throw IOException("Cannot write checksum to header", Error::ERR_WRITE_FILE);

    if (_obs->writeBits(_entropyType, 5) != 5)
        throw IOException("Cannot write entropy type to header", Error::ERR_WRITE_FILE);

    if (_obs->writeBits(_transformType, 48) != 48)
        throw IOException("Cannot write transform types to header", Error::ERR_WRITE_FILE);

    if (_obs->writeBits(_blockSize >> 4, 28) != 28)
        throw IOException("Cannot write block size to header", Error::ERR_WRITE_FILE);

    if (_obs->writeBits(_nbInputBlocks, 6) != 6)
        throw IOException("Cannot write number of blocks to header", Error::ERR_WRITE_FILE);

    if (_obs->writeBits(uint64(0), 4) != 4)
        throw IOException("Cannot write reserved bits to header", Error::ERR_WRITE_FILE);
}

bool CompressedOutputStream::addListener(Listener& bl)
{
    _listeners.push_back(&bl);
    return true;
}

bool CompressedOutputStream::removeListener(Listener& bl)
{
    std::vector<Listener*>::iterator it = find(_listeners.begin(), _listeners.end(), &bl);

    if (it == _listeners.end())
        return false;

    _listeners.erase(it);
    return true;
}

ostream& CompressedOutputStream::write(const char* data, streamsize length) THROW
{
    int remaining = int(length);

    if (remaining < 0)
        throw IOException("Invalid buffer size");

    if (_closed.load(memory_order_relaxed) == true)
        throw ios_base::failure("Stream closed");

    int off = 0;

    while (remaining > 0) {
        // Limit to number of available bytes in buffer
        const int lenChunk = (_sa->_index + remaining < _sa->_length) ? remaining : _sa->_length - _sa->_index;

        if (lenChunk > 0) {
            // Process a chunk of in-buffer data. No access to bitstream required
            memcpy(&_sa->_array[_sa->_index], &data[off], lenChunk);
            _sa->_index += lenChunk;
            off += lenChunk;
            remaining -= lenChunk;

            if (remaining == 0)
                break;
        }

        // Buffer full, time to encode
        put(data[off]);
        off++;
        remaining--;
    }

    return *this;
}

void CompressedOutputStream::close() THROW
{
    if (_closed.exchange(true, memory_order_acquire))
        return;

    if (_sa->_index > 0)
        processBlock(true);

    try {
        // Write end block of size 0
        _obs->writeBits(uint64(0), 5); // write length-3 (5 bits max)
        _obs->writeBits(uint64(0), 3); // write 0 (3 bits)
        _obs->close();
    }
    catch (exception& e) {
        setstate(ios::badbit);
        throw ios_base::failure(e.what());
    }

    setstate(ios::eofbit);

    // Release resources
    // Force error on any subsequent write attempt
    delete[] _sa->_array;
    _sa->_array = new byte[0];
    _sa->_length = 0;
    _sa->_index = -1;

    for (int i = 0; i < 2 * _jobs; i++) {
        delete[] _buffers[i]->_array;
        _buffers[i]->_array = new byte[0];
        _buffers[i]->_length = 0;
    }
}

void CompressedOutputStream::processBlock(bool force) THROW
{
    if (force == false) {
        const int bufSize = min(_jobs, max(int(_nbInputBlocks), 1)) * _blockSize;

        if (_sa->_length < bufSize) {
            // Grow byte array until max allowed
            byte* buf = new byte[bufSize];
            memcpy(buf, _sa->_array, _sa->_length);
            delete[] _sa->_array;
            _sa->_array = buf;
            _sa->_length = bufSize;
            return;
        }
    }

    if (_sa->_index == 0)
        return;

    if (!_initialized.exchange(true, memory_order_acquire))
        writeHeader();

    vector<EncodingTask<EncodingTaskResult>*> tasks;

    try {

        // Protect against future concurrent modification of the list of block listeners
        vector<Listener*> blockListeners(_listeners);
        const int dataLength = _sa->_index;
        _sa->_index = 0;
        int firstBlockId = _blockId.load(memory_order_relaxed);
        int nbTasks = _jobs;
        int jobsPerTask[MAX_CONCURRENCY];

        // Assign optimal number of tasks and jobs per task
        if (nbTasks > 1) {
            // If the number of input blocks is available, use it to optimize
            // memory usage
            if (_nbInputBlocks != 0) {
                // Limit the number of jobs if there are fewer blocks that _jobs
                // It allows more jobs per task and reduces memory usage.
                nbTasks = min(nbTasks, int(_nbInputBlocks));
            }

            Global::computeJobsPerTask(jobsPerTask, _jobs, nbTasks);
        }
        else {
            jobsPerTask[0] = _jobs;
        }

        // Create as many tasks as required
        for (int taskId = 0; taskId < nbTasks; taskId++) {
            const int sz = (_sa->_index + _blockSize > dataLength) ? dataLength - _sa->_index : _blockSize;

            if (sz == 0)
                break;

            _buffers[2 * taskId]->_index = 0;
            _buffers[2 * taskId + 1]->_index = 0;

            // Add padding for incompressible data
            const int length = max(sz + (sz >> 6), 65536);

            // Grow encoding buffer if required
            if (_buffers[2 * taskId]->_length < length) {
                delete[] _buffers[2 * taskId]->_array;
                _buffers[2 * taskId]->_array = new byte[length];
                _buffers[2 * taskId]->_length = length;
            }

            Context copyCtx(_ctx);
            copyCtx.putInt("jobs", jobsPerTask[taskId]);
            memcpy(&_buffers[2 * taskId]->_array[0], &_sa->_array[_sa->_index], sz);

            EncodingTask<EncodingTaskResult>* task = new EncodingTask<EncodingTaskResult>(_buffers[2 * taskId],
                _buffers[2 * taskId + 1], sz, _transformType,
                _entropyType, firstBlockId + taskId + 1,
                _obs, _hasher, &_blockId,
                blockListeners, copyCtx);
            tasks.push_back(task);
            _sa->_index += sz;
        }

        if (tasks.size() == 1) {
            // Synchronous call
            EncodingTask<EncodingTaskResult>* task = tasks.back();
            tasks.pop_back();
            EncodingTaskResult res = task->run();

            if (res._error != 0)
                throw IOException(res._msg, res._error); // deallocate in catch block

            delete task;
        }
#ifdef CONCURRENCY_ENABLED
        else {
            vector<future<EncodingTaskResult> > futures;

            // Register task futures and launch tasks in parallel
            for (uint i = 0; i < tasks.size(); i++) {
                futures.push_back(async(launch::async, &EncodingTask<EncodingTaskResult>::run, tasks[i]));
            }

            // Wait for tasks completion and check results
            for (uint i = 0; i < tasks.size(); i++) {
                EncodingTaskResult status = futures[i].get();

                if (status._error != 0)
                    throw IOException(status._msg, status._error); // deallocate in catch block
            }
        }

        for (vector<EncodingTask<EncodingTaskResult>*>::iterator it = tasks.begin(); it != tasks.end(); ++it)
            delete *it;

        tasks.clear();
#endif
        _sa->_index = 0;
    }
    catch (IOException&) {
        for (vector<EncodingTask<EncodingTaskResult>*>::iterator it = tasks.begin(); it != tasks.end(); ++it)
            delete *it;

        tasks.clear();
        throw; // rethrow
    }
    catch (BitStreamException& e) {
        for (vector<EncodingTask<EncodingTaskResult>*>::iterator it = tasks.begin(); it != tasks.end(); ++it)
            delete *it;

        tasks.clear();
        throw IOException(e.what(), e.error());
    }
    catch (exception& e) {
        for (vector<EncodingTask<EncodingTaskResult>*>::iterator it = tasks.begin(); it != tasks.end(); ++it)
            delete *it;

        tasks.clear();
        throw IOException(e.what(), Error::ERR_UNKNOWN);
    }
}

// Return the number of bytes written so far
uint64 CompressedOutputStream::getWritten()
{
    return (_obs->written() + 7) >> 3;
}

void CompressedOutputStream::notifyListeners(vector<Listener*>& listeners, const Event& evt)
{
    vector<Listener*>::iterator it;

    for (it = listeners.begin(); it != listeners.end(); ++it)
        (*it)->processEvent(evt);
}

template <class T>
EncodingTask<T>::EncodingTask(SliceArray<byte>* iBuffer, SliceArray<byte>* oBuffer, int length,
    uint64 transformType, short entropyType, int blockId,
    OutputBitStream* obs, XXHash32* hasher,
    atomic_int* processedBlockId, vector<Listener*>& listeners,
    const Context& ctx)
    : _obs(obs)
    , _listeners(listeners)
    , _ctx(ctx)
{
    _data = iBuffer;
    _buffer = oBuffer;
    _blockLength = length;
    _transformType = transformType;
    _entropyType = entropyType;
    _blockId = blockId;
    _hasher = hasher;
    _processedBlockId = processedBlockId;
}

// Encode mode + transformed entropy coded data
// mode | 0b10000000 => copy block
//      | 0b0yy00000 => size(size(block))-1
//      | 0b000y0000 => 1 if more than 4 transforms
//  case 4 transforms or less
//      | 0b0000yyyy => transform sequence skip flags (1 means skip)
//  case more than 4 transforms
//      | 0b00000000
//      then 0byyyyyyyy => transform sequence skip flags (1 means skip)
template <class T>
T EncodingTask<T>::run() THROW
{
    EntropyEncoder* ee = nullptr;

    try {
        if (_blockLength == 0) {
            (*_processedBlockId)++;
            return T(_blockId, 0, "Success");
        }

        byte mode = byte(0);
        int postTransformLength = _blockLength;
        int checksum = 0;

        // Compute block checksum
        if (_hasher != nullptr)
            checksum = _hasher->hash(&_data->_array[_data->_index], _blockLength);

        if (_listeners.size() > 0) {
            // Notify before transform
            Event evt(Event::BEFORE_TRANSFORM, _blockId,
                int64(_blockLength), checksum, _hasher != nullptr, clock());

            CompressedOutputStream::notifyListeners(_listeners, evt);
        }

        if (_blockLength <= CompressedOutputStream::SMALL_BLOCK_SIZE) {
            _transformType = TransformFactory<byte>::NONE_TYPE;
            _entropyType = EntropyCodecFactory::NONE_TYPE;
            mode |= CompressedOutputStream::COPY_BLOCK_MASK;
        }
        else {
            if (_ctx.has("skipBlocks")) {
                string str = _ctx.getString("skipBlocks");

                if (str == STR_TRUE) {
                    uint histo[256];
                    Global::computeHistogram(&_data->_array[_data->_index], _blockLength, histo);
                    const int entropy = Global::computeFirstOrderEntropy1024(_blockLength, histo);
                    //_ctx.putString("histo0", toString(histo, 256));

                    if (entropy >= EntropyUtils::INCOMPRESSIBLE_THRESHOLD) {
                        _transformType = TransformFactory<byte>::NONE_TYPE;
                        _entropyType = EntropyCodecFactory::NONE_TYPE;
                        mode |= CompressedOutputStream::COPY_BLOCK_MASK;
                    }
                }
            }
        }

        _ctx.putInt("size", _blockLength);
        TransformSequence<byte>* transform = TransformFactory<byte>::newTransform(_ctx, _transformType);
        const int requiredSize = transform->getMaxEncodedLength(_blockLength);

        if (_buffer->_length < requiredSize) {
            _buffer->_length = requiredSize;
            delete[] _buffer->_array;
            _buffer->_array = new byte[_buffer->_length];
        }

        // Forward transform (ignore error, encode skipFlags)
        _buffer->_index = 0;

        // _data->_length is at least _blockLength
        transform->forward(*_data, *_buffer, _blockLength);
        const int nbTransforms = transform->getNbTransforms();
        const byte skipFlags = transform->getSkipFlags();
        delete transform;
        postTransformLength = _buffer->_index;

        if (postTransformLength < 0) {
            _processedBlockId->store(CompressedOutputStream::CANCEL_TASKS_ID, memory_order_release);
            return T(_blockId, Error::ERR_WRITE_FILE, "Invalid transform size");
        }

        _ctx.putInt("size", postTransformLength);
        const int dataSize = (postTransformLength < 256) ? 1 : (Global::_log2(uint32(postTransformLength)) >> 3) + 1;

        if (dataSize > 4) {
            _processedBlockId->store(CompressedOutputStream::CANCEL_TASKS_ID, memory_order_release);
            return T(_blockId, Error::ERR_WRITE_FILE, "Invalid block data length");
        }

        // Record size of 'block size' - 1 in bytes
        mode |= byte(((dataSize - 1) & 0x03) << 5);

        if (_listeners.size() > 0) {
            // Notify after transform
            Event evt(Event::AFTER_TRANSFORM, _blockId,
                int64(postTransformLength), checksum, _hasher != nullptr, clock());

            CompressedOutputStream::notifyListeners(_listeners, evt);
        }

        if (_data->_length < postTransformLength) {
            // Rare case where the transform expanded the input
            _data->_length = max(1024, postTransformLength + (postTransformLength >> 5));
            delete[] _data->_array;
            _data->_array = new byte[_data->_length];
        }

        _data->_index = 0;
        ostreambuf<char> buf(reinterpret_cast<char*>(&_data->_array[_data->_index]), streamsize(_data->_length));
        ostream os(&buf);
        DefaultOutputBitStream obs(os);

        // Write block 'header' (mode + compressed length);
        if (((mode & CompressedOutputStream::COPY_BLOCK_MASK) != byte(0)) || (nbTransforms <= 4)) {
            mode |= byte(skipFlags >> 4);
            obs.writeBits(uint64(mode), 8);
        }
        else {
            mode |= CompressedOutputStream::TRANSFORMS_MASK;
            obs.writeBits(uint64(mode), 8);
            obs.writeBits(uint64(skipFlags), 8);
        }

        obs.writeBits(postTransformLength, 8 * dataSize);

        // Write checksum
        if (_hasher != nullptr)
            obs.writeBits(checksum, 32);

        if (_listeners.size() > 0) {
            // Notify before entropy
            Event evt(Event::BEFORE_ENTROPY, _blockId,
                int64(postTransformLength), checksum, _hasher != nullptr, clock());

            CompressedOutputStream::notifyListeners(_listeners, evt);
        }

        // Each block is encoded separately
        // Rebuild the entropy encoder to reset block statistics
        ee = EntropyCodecFactory::newEncoder(obs, _ctx, _entropyType);

        // Entropy encode block
        if (ee->encode(_buffer->_array, 0, postTransformLength) != postTransformLength) {
            delete ee;
            _processedBlockId->store(CompressedOutputStream::CANCEL_TASKS_ID, memory_order_release);
            return T(_blockId, Error::ERR_PROCESS_BLOCK, "Entropy coding failed");
        }

        // Dispose before processing statistics (may write to the bitstream)
        ee->dispose();
        delete ee;
        ee = nullptr;
        obs.close();
        uint64 written = obs.written();

        // Lock free synchronization
        while (true) {
            const int taskId = _processedBlockId->load(memory_order_relaxed);

            if (taskId == CompressedOutputStream::CANCEL_TASKS_ID)
                return T(_blockId, 0, "Canceled");

            if (taskId == _blockId - 1)
                break;

            // Back-off improves performance
            CPU_PAUSE();
        }

        if (_listeners.size() > 0) {
            // Notify after entropy
            Event evt(Event::AFTER_ENTROPY,
                int64(_blockId), int((written + 7) >> 3), checksum, _hasher != nullptr, clock());

            CompressedOutputStream::notifyListeners(_listeners, evt);
        }

        // Emit block size in bits (max size pre-entropy is 1 GB = 1 << 30 bytes)
        const uint lw = (written < 8) ? 3 : uint(Global::log2(uint32(written >> 3)) + 4);
        _obs->writeBits(lw - 3, 5); // write length-3 (5 bits max)
        _obs->writeBits(written, lw);

        // Emit data to shared bitstream
        for (int n = 0; written > 0; ) {
            uint chkSize = uint(min(written, uint64(1) << 30));
            _obs->writeBits(&_data->_array[n], chkSize);
            n += ((chkSize + 7) >> 3);
            written -= uint64(chkSize);
        }

        // After completion of the entropy coding, increment the block id.
        // It unblocks the task processing the next block (if any).
        (*_processedBlockId)++;

        return T(_blockId, 0, "Success");
    }
    catch (exception& e) {
        // Make sure to unfreeze next block
        if (_processedBlockId->load(memory_order_relaxed) == _blockId - 1)
            (*_processedBlockId)++;

        if (ee != nullptr)
            delete ee;

        return T(_blockId, Error::ERR_PROCESS_BLOCK, e.what());
    }
}
