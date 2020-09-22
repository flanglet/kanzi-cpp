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

#include <sstream>
#include <iomanip>
#include "CompressedInputStream.hpp"
#include "IOException.hpp"
#include "../Error.hpp"
#include "../bitstream/DefaultInputBitStream.hpp"
#include "../entropy/EntropyCodecFactory.hpp"
#include "../function/FunctionFactory.hpp"

#ifdef CONCURRENCY_ENABLED
#include <future>
#endif

using namespace kanzi;

CompressedInputStream::CompressedInputStream(InputStream& is, int tasks)
    : InputStream(is.rdbuf())
    , _is(is)
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

    _blockId = 0;
    _blockSize = 0;
    _entropyType = EntropyCodecFactory::NONE_TYPE;
    _transformType = FunctionFactory<byte>::NONE_TYPE;
    _initialized = false;
    _closed = false;
    _maxIdx = 0;
    _gcount = 0;
    _ibs = new DefaultInputBitStream(is, DEFAULT_BUFFER_SIZE);
    _jobs = tasks;
    _sa = new SliceArray<byte>(new byte[0], 0, 0);
    _hasher = nullptr;
    _nbInputBlocks = 0;
    _buffers = new SliceArray<byte>*[2 * _jobs];

    for (int i = 0; i < 2 * _jobs; i++)
        _buffers[i] = new SliceArray<byte>(new byte[0], 0, 0);
}

CompressedInputStream::CompressedInputStream(InputStream& is, Context& ctx)
    : InputStream(is.rdbuf())
    , _is(is)
    , _ctx(ctx)
{
    int tasks = ctx.getInt("jobs", 1);

#ifdef CONCURRENCY_ENABLED
    if ((tasks <= 0) || (tasks > MAX_CONCURRENCY)) {
        stringstream ss;
        ss << "The number of jobs must be in [1.." << MAX_CONCURRENCY << "]";
        throw invalid_argument(ss.str());
    }
#else
    if ((tasks <= 0) || (tasks > 1))
        throw invalid_argument("The number of jobs is limited to 1 in this version");
#endif

    _blockId = 0;
    _blockSize = 0;
    _entropyType = EntropyCodecFactory::NONE_TYPE;
    _transformType = FunctionFactory<byte>::NONE_TYPE;
    _initialized = false;
    _closed = false;
    _maxIdx = 0;
    _gcount = 0;
    _ibs = new DefaultInputBitStream(is, DEFAULT_BUFFER_SIZE);
    _jobs = tasks;
    _sa = new SliceArray<byte>(new byte[0], 0, 0);
    _hasher = nullptr;
    _nbInputBlocks = 0;
    _buffers = new SliceArray<byte>*[2 * _jobs];

    for (int i = 0; i < 2 * _jobs; i++)
        _buffers[i] = new SliceArray<byte>(new byte[0], 0, 0);
}

CompressedInputStream::~CompressedInputStream()
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
    delete _ibs;
    delete[] _sa->_array;
    delete _sa;

    if (_hasher != nullptr) {
        delete _hasher;
        _hasher = nullptr;
    }
}

void CompressedInputStream::readHeader() THROW
{
    // Read stream type
    const int type = int(_ibs->readBits(32));

    // Sanity check
    if (type != BITSTREAM_TYPE) {
        stringstream ss;
        throw IOException("Invalid stream type", Error::ERR_INVALID_FILE);
    }

    // Read stream version
    int version = int(_ibs->readBits(5));

    // Sanity check
    if (version != BITSTREAM_FORMAT_VERSION) {
        stringstream ss;
        ss << "Invalid bitstream, cannot read this version of the stream: " << version;
        throw IOException(ss.str(), Error::ERR_STREAM_VERSION);
    }

    // Read block checksum
    if (_ibs->readBit() == 1)
        _hasher = new XXHash32(BITSTREAM_TYPE);

    // Read entropy codec
    _entropyType = uint(_ibs->readBits(5));
    _ctx.putString("codec", EntropyCodecFactory::getName(_entropyType));
    _ctx.putString("extra", _entropyType == EntropyCodecFactory::TPAQX_TYPE ? STR_TRUE : STR_FALSE);

    // Read transform: 8*6 bits
    _transformType = _ibs->readBits(48);
    _ctx.putString("transform", FunctionFactory<byte>::getName(_transformType));

    // Read block size
    _blockSize = int(_ibs->readBits(28)) << 4;
    _ctx.putInt("blockSize", _blockSize);

    if ((_blockSize < MIN_BITSTREAM_BLOCK_SIZE) || (_blockSize > MAX_BITSTREAM_BLOCK_SIZE)) {
        stringstream ss;
        ss << "Invalid bitstream, incorrect block size: " << _blockSize;
        throw IOException(ss.str(), Error::ERR_BLOCK_SIZE);
    }

#ifdef CONCURRENCY_ENABLED
    if (uint64(_blockSize) * uint64(_jobs) >= uint64(1 << 31))
        _jobs = (1 << 31) / _blockSize;
#endif

    // Read number of blocks in input. 0 means 'unknown' and 63 means 63 or more.
    _nbInputBlocks = uint8(_ibs->readBits(6));

    // Read reserved bits
    _ibs->readBits(3);

    if (_listeners.size() > 0) {
        stringstream ss;
        ss << "Checksum set to " << (_hasher != nullptr ? "true" : "false") << endl;
        ss << "Block size set to " << _blockSize << " bytes" << endl;

        try {
            string w1 = EntropyCodecFactory::getName(_entropyType);
            ss << "Using " << ((w1 == "NONE") ? "no" : w1) << " entropy codec (stage 1)" << endl;
        }
        catch (invalid_argument&) {
            stringstream err;
            err << "Invalid bitstream, unknown entropy codec type: " << _entropyType;
            throw IOException(err.str(), Error::ERR_INVALID_CODEC);
        }

        try {
            string w2 = FunctionFactory<byte>::getName(_transformType);
            ss << "Using " << ((w2 == "NONE") ? "no" : w2) << " transform (stage 2)" << endl;
        }
        catch (invalid_argument&) {
            stringstream err;
            err << "Invalid bitstream, unknown transform type: " << _transformType;
            throw IOException(err.str(), Error::ERR_INVALID_CODEC);
        }

        // Protect against future concurrent modification of the list of block listeners
        vector<Listener*> blockListeners(_listeners);
        Event evt(Event::AFTER_HEADER_DECODING, 0, ss.str(), clock());
        CompressedInputStream::notifyListeners(blockListeners, evt);
    }
}

bool CompressedInputStream::addListener(Listener& bl)
{
    _listeners.push_back(&bl);
    return true;
}

bool CompressedInputStream::removeListener(Listener& bl)
{
    std::vector<Listener*>::iterator it = find(_listeners.begin(), _listeners.end(), &bl);

    if (it == _listeners.end())
        return false;

    _listeners.erase(it);
    return true;
}

int CompressedInputStream::peek() THROW
{
    try {
        if (_sa->_index >= _maxIdx) {
            _maxIdx = processBlock();

            if (_maxIdx == 0) {
                // Reached end of stream
                setstate(ios::eofbit);
                return EOF;
            }
        }

        return int(_sa->_array[_sa->_index]);
    }
    catch (IOException& e) {
        setstate(ios::badbit);
        throw e;
    }
    catch (exception& e) {
        setstate(ios::badbit);
        throw e;
    }
}

int CompressedInputStream::get() THROW
{
    _gcount = 0;
    int res = peek();

    if (res != EOF) {
        _sa->_index++;
        _gcount++;
    }

    return res;
}

int CompressedInputStream::_get() THROW
{
    int res = peek();

    if (res != EOF) {
        _sa->_index++;
    }

    return res;
}

istream& CompressedInputStream::read(char* data, streamsize length) THROW
{
    int remaining = int(length);

    if (remaining < 0)
        throw ios_base::failure("Invalid buffer size");

    if (_closed.load() == true) {
        setstate(ios::badbit);
        throw ios_base::failure("Stream closed");
    }

    int off = 0;
    _gcount = 0;

    while (remaining > 0) {
        // Limit to number of available bytes in buffer
        const int lenChunk = (_sa->_index + remaining < _maxIdx) ? remaining : _maxIdx - _sa->_index;

        if (lenChunk > 0) {
            // Process a chunk of in-buffer data. No access to bitstream required
            memcpy(&data[off], &_sa->_array[_sa->_index], lenChunk);
            _sa->_index += lenChunk;
            off += lenChunk;
            remaining -= lenChunk;
            _gcount += lenChunk;

            if (remaining == 0)
                break;
        }

        // Buffer empty, time to decode
        int c2 = _get();

        // EOF ?
        if (c2 == EOF)
            break;

        data[off++] = char(c2);
        _gcount++;
        remaining--;
    }

    return *this;
}

streampos CompressedInputStream::tellg()
{
    return _is.tellg();
}

istream& CompressedInputStream::seekp(streampos) THROW
{
    setstate(ios::badbit);
    throw ios_base::failure("Not supported");
}

int CompressedInputStream::processBlock() THROW
{
    vector<DecodingTask<DecodingTaskResult>*> tasks;

    if (!_initialized.exchange(true, memory_order_acquire))
        readHeader();

    try {
        // Add a padding area to manage any block with header or temporarily expanded
        const int blkSize = max(_blockSize + EXTRA_BUFFER_SIZE, _blockSize + (_blockSize >> 4));
        
        // Protect against future concurrent modification of the list of block listeners
        vector<Listener*> blockListeners(_listeners);
        int decoded = 0;

        while (true) {
            _sa->_index = 0;
            const int firstBlockId = _blockId.load();
            int nbTasks = _jobs;
            int jobsPerTask[MAX_CONCURRENCY];

            // Assign optimal number of tasks and jobs per task
            if (nbTasks > 1) {
                // If the number of input blocks is available, use it to optimize
                // memory usage
                if (_nbInputBlocks != 0) {
                    // Limit the number of jobs if there are fewer blocks that _jobs
                    // It allows more jobs per task and reduces memory usage.
                    if (nbTasks > _nbInputBlocks) {
                        nbTasks = _nbInputBlocks;
                    }
                }

                Global::computeJobsPerTask(jobsPerTask, _jobs, nbTasks);
            }
            else {
                jobsPerTask[0] = _jobs;
            }

            // Create as many tasks as required
            for (int taskId = 0; taskId < nbTasks; taskId++) {
                _buffers[2 * taskId]->_index = 0;
                _buffers[2 * taskId + 1]->_index = 0;

                if (_buffers[2 * taskId]->_length < blkSize + 1024) {
                    // Lazy instantiation of input buffers this.buffers[2*taskId]
                    // Output buffers this.buffers[2*taskId+1] are lazily instantiated
                    // by the decoding tasks.
                    delete[] _buffers[2 * taskId]->_array;
                    _buffers[2 * taskId]->_array = new byte[blkSize + 1024];
                    _buffers[2 * taskId]->_length = blkSize + 1024;
                }

                Context copyCtx(_ctx);
                copyCtx.putInt("jobs", jobsPerTask[taskId]);

                DecodingTask<DecodingTaskResult>* task = new DecodingTask<DecodingTaskResult>(_buffers[2 * taskId],
                    _buffers[2 * taskId + 1], blkSize, _transformType,
                    _entropyType, firstBlockId + taskId + 1, _ibs, _hasher, &_blockId,
                    blockListeners, copyCtx);
                tasks.push_back(task);
            }

            int skipped = 0;

            if (tasks.size() == 1) {
                // Synchronous call
                DecodingTask<DecodingTaskResult>* task = tasks.back();
                tasks.pop_back();
                DecodingTaskResult res = task->run();
                delete task;
                decoded += res._decoded;

                if (res._error != 0)
                    throw IOException(res._msg, res._error); // deallocate in catch block

                if (res._skipped == true)
                    skipped++;

                const int size = _sa->_index + decoded;

                if (size > nbTasks * _blockSize)
                    throw IOException("Invalid data", Error::ERR_PROCESS_BLOCK); // deallocate in catch code

                if (_sa->_length < size) {
                    _sa->_length = size;
                    delete[] _sa->_array;
                    _sa->_array = new byte[_sa->_length];
                }

                memcpy(&_sa->_array[_sa->_index], &res._data[0], res._decoded);
                _sa->_index += res._decoded;

                if (blockListeners.size() > 0) {
                    // Notify after transform ... in block order !
                    Event evt(Event::AFTER_TRANSFORM, res._blockId,
                        int64(res._decoded), res._checksum, _hasher != nullptr, res._completionTime);
                    CompressedInputStream::notifyListeners(blockListeners, evt);
                }
            }
#ifdef CONCURRENCY_ENABLED
            else {
                vector<future<DecodingTaskResult> > futures;
                vector<DecodingTaskResult> results;

                // Register task futures and launch tasks in parallel
                for (uint i = 0; i < tasks.size(); i++) {
                    futures.push_back(async(&DecodingTask<DecodingTaskResult>::run, tasks[i]));
                }

                // Wait for tasks completion and check results
                for (uint i = 0; i < futures.size(); i++) {
                    DecodingTaskResult status = futures[i].get();
                    results.push_back(status);
                    decoded += status._decoded;

                    if (status._error != 0)
                        throw IOException(status._msg, status._error); // deallocate in catch block

                    if (status._skipped == true)
                        skipped++;
                }

                const int size = _sa->_index + decoded;

                if (size > nbTasks * _blockSize)
                    throw IOException("Invalid data", Error::ERR_PROCESS_BLOCK); // deallocate in catch code

                if (_sa->_length < size) {
                    _sa->_length = size;
                    delete[] _sa->_array;
                    _sa->_array = new byte[_sa->_length];
                }

                for (uint i = 0; i < results.size(); i++) {
                    DecodingTaskResult res = results[i];
                    memcpy(&_sa->_array[_sa->_index], &res._data[0], res._decoded);
                    _sa->_index += res._decoded;

                    if (blockListeners.size() > 0) {
                        // Notify after transform ... in block order !
                        Event evt(Event::AFTER_TRANSFORM, res._blockId,
                            int64(res._decoded), res._checksum, _hasher != nullptr, res._completionTime);
                        CompressedInputStream::notifyListeners(blockListeners, evt);
                    }
                }
            }

            for (vector<DecodingTask<DecodingTaskResult>*>::iterator it = tasks.begin(); it != tasks.end(); it++)
                delete *it;

            tasks.clear();
#endif

            // Unless all blocks were skipped, exit the loop (usual case)
            if (skipped != nbTasks)
                break;
        }

        _sa->_index = 0;
        return decoded;
    }
    catch (IOException&) {
        for (vector<DecodingTask<DecodingTaskResult>*>::iterator it = tasks.begin(); it != tasks.end(); it++)
            delete *it;

        tasks.clear();
        throw;
    }
    catch (exception& e) {
        for (vector<DecodingTask<DecodingTaskResult>*>::iterator it = tasks.begin(); it != tasks.end(); it++)
            delete *it;

        tasks.clear();
        throw IOException(e.what(), Error::ERR_UNKNOWN);
    }
}

void CompressedInputStream::close() THROW
{
    if (_closed.exchange(true, memory_order_acquire))
        return;

    try {
        _ibs->close();
    }
    catch (BitStreamException& e) {
        throw IOException(e.what(), e.error());
    }

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

// Return the number of bytes read so far
uint64 CompressedInputStream::getRead()
{
    return (_ibs->read() + 7) >> 3;
}

void CompressedInputStream::notifyListeners(vector<Listener*>& listeners, const Event& evt)
{
    vector<Listener*>::iterator it;

    for (it = listeners.begin(); it != listeners.end(); it++)
        (*it)->processEvent(evt);
}

template <class T>
DecodingTask<T>::DecodingTask(SliceArray<byte>* iBuffer, SliceArray<byte>* oBuffer, int blockSize,
    uint64 transformType, uint entropyType, int blockId,
    InputBitStream* ibs, XXHash32* hasher,
    atomic_int* processedBlockId, vector<Listener*>& listeners,
    Context& ctx)
    : _ctx(ctx)
{
    _blockLength = blockSize;
    _data = iBuffer;
    _buffer = oBuffer;
    _transformType = transformType;
    _entropyType = entropyType;
    _blockId = blockId;
    _ibs = ibs;
    _hasher = hasher;
    _listeners = listeners;
    _processedBlockId = processedBlockId;
}

// Decode mode + transformed entropy coded data
// mode | 0b10000000 => copy block
//      | 0b0yy00000 => size(size(block))-1
//      | 0b000y0000 => 1 if more than 4 transforms
//  case 4 transforms or less
//      | 0b0000yyyy => transform sequence skip flags (1 means skip)
//  case more than 4 transforms
//      | 0b00000000
//      then 0byyyyyyyy => transform sequence skip flags (1 means skip)
template <class T>
T DecodingTask<T>::run() THROW
{
    // Lock free synchronization
    while (true) {
        const int taskId = _processedBlockId->load();

        if (taskId == CompressedInputStream::CANCEL_TASKS_ID) {
            // Skip, an error occurred
            return T(*_data, _blockId, 0, 0, 0, "Canceled");
        }

        if (taskId == _blockId - 1)
            break;

        // Back-off improves performance
        CPU_PAUSE();
    }

    // Read shared bitstream sequentially (each task is gated by _processedBlockId)
    const uint lr = 3 + uint(_ibs->readBits(5));
    uint64 read = _ibs->readBits(lr);

    if (read == 0) {
        (*_processedBlockId)++;
        return T(*_data, _blockId, 0, 0, 0, "Success");
    }

    if (read > (uint64(1) << 34)) {
        _processedBlockId->store(CompressedInputStream::CANCEL_TASKS_ID);
        return T(*_data, _blockId, 0, 0, Error::ERR_BLOCK_SIZE, "Invalid block size");
    }

    const int r = int((read + 7) >> 3);

    if (_data->_length < max(_blockLength, r)) {
        _data->_length = max(_blockLength, r);
        delete[] _data->_array;
        _data->_array = new byte[_data->_length];
    }

    for (int n = 0; read > 0; ) {
        const uint chkSize = uint(min(read, uint64(1) << 30));
        _ibs->readBits(&_data->_array[n], chkSize);
        n += ((chkSize + 7) >> 3);
        read -= uint64(chkSize);
    }

    // After completion of the bitstream reading, increment the block id.
    // It unblocks the task processing the next block (if any)
    (*_processedBlockId)++;

    const int from = _ctx.getInt("from", 0);
    const int to = _ctx.getInt("to", CompressedInputStream::MAX_BLOCK_ID);

    // Check if the block must be skipped
    if ((_blockId < from) || (_blockId >= to)) {
        return T(*_data, _blockId, 0, 0, 0, "Skipped", true);
    }

    // Create an InputBitstream local to the task
    istreambuf<char> buf(reinterpret_cast<char*>(&_data->_array[0]), streamsize(r));
    iostream is(&buf);
    DefaultInputBitStream ibs(is);
    int checksum1 = 0;
    EntropyDecoder* ed = nullptr;

    try {
        // Extract block header from bitstream
        byte mode = byte(ibs.readBits(8));
        byte skipFlags = byte(0);

        if ((mode & CompressedInputStream::COPY_BLOCK_MASK) != byte(0)) {
            _transformType = FunctionFactory<byte>::NONE_TYPE;
            _entropyType = EntropyCodecFactory::NONE_TYPE;
        }
        else {
            if ((mode & CompressedInputStream::TRANSFORMS_MASK) != byte(0))
                skipFlags = byte(ibs.readBits(8));
            else
                skipFlags = (mode << 4) | byte(0x0F);
        }

        const int dataSize = 1 + (int(mode >> 5) & 0x03);
        const int length = dataSize << 3;
        const uint64 mask = (uint64(1) << length) - 1;
        const int preTransformLength = int(ibs.readBits(length) & mask);

        if (preTransformLength == 0) {
            // Error => cancel concurrent decoding tasks
            _processedBlockId->store(CompressedInputStream::CANCEL_TASKS_ID);
            return T(*_data, _blockId, 0, checksum1, 0, "Invalid transform block size");
        }

        if ((preTransformLength < 0) || (preTransformLength > CompressedInputStream::MAX_BITSTREAM_BLOCK_SIZE)) {
            // Error => cancel concurrent decoding tasks
            _processedBlockId->store(CompressedInputStream::CANCEL_TASKS_ID);
            stringstream ss;
            ss << "Invalid compressed block length: " << preTransformLength;
            return T(*_data, _blockId, 0, checksum1, Error::ERR_READ_FILE, ss.str());
        }

        // Extract checksum from bitstream (if any)
        if (_hasher != nullptr)
            checksum1 = int(ibs.readBits(32));

        if (_listeners.size() > 0) {
            // Notify before entropy (block size in bitstream is unknown)
            Event evt(Event::BEFORE_ENTROPY, _blockId, int64(-1), checksum1, _hasher != nullptr, clock());
            CompressedInputStream::notifyListeners(_listeners, evt);
        }

        const int bufferSize = (_blockLength >= preTransformLength + CompressedInputStream::EXTRA_BUFFER_SIZE) ? _blockLength : preTransformLength + CompressedInputStream::EXTRA_BUFFER_SIZE;

        if (_buffer->_length < bufferSize) {
            _buffer->_length = bufferSize;
            delete[] _buffer->_array;
            _buffer->_array = new byte[_buffer->_length];
        }

        const int savedIdx = _data->_index;
        _ctx.putInt("size", preTransformLength);

        // Each block is decoded separately
        // Rebuild the entropy decoder to reset block statistics
        ed = EntropyCodecFactory::newDecoder(ibs, _ctx, _entropyType);

        // Block entropy decode
        if (ed->decode(_buffer->_array, 0, preTransformLength) != preTransformLength) {
            // Error => cancel concurrent decoding tasks
            delete ed;
            _processedBlockId->store(CompressedInputStream::CANCEL_TASKS_ID);
            return T(*_data, _blockId, 0, checksum1, Error::ERR_PROCESS_BLOCK,
                "Entropy decoding failed");
        }

        delete ed;
        ed = nullptr;

        if (_listeners.size() > 0) {
            // Notify after entropy (block size set to size in bitstream)
            Event evt(Event::AFTER_ENTROPY, _blockId,
                int64(ibs.read() >> 3), checksum1, _hasher != nullptr, clock());

            CompressedInputStream::notifyListeners(_listeners, evt);
        }

        if (_listeners.size() > 0) {
            // Notify before transform (block size after entropy decoding)
            Event evt(Event::BEFORE_TRANSFORM, _blockId,
                int64(preTransformLength), checksum1, _hasher != nullptr, clock());

            CompressedInputStream::notifyListeners(_listeners, evt);
        }

        TransformSequence<byte>* transform = FunctionFactory<byte>::newFunction(_ctx, _transformType);
        transform->setSkipFlags(skipFlags);
        _buffer->_index = 0;

        // Inverse transform
        _buffer->_length = preTransformLength;
        bool res = transform->inverse(*_buffer, *_data, _buffer->_length);
        delete transform;

        if (res == false) {
            return T(*_data, _blockId, 0, checksum1, Error::ERR_PROCESS_BLOCK,
                "Transform inverse failed");
        }

        const int decoded = _data->_index - savedIdx;

        // Verify checksum
        if (_hasher != nullptr) {
            const int checksum2 = _hasher->hash(&_data->_array[savedIdx], decoded);

            if (checksum2 != checksum1) {
                stringstream ss;
                ss << "Corrupted bitstream: expected checksum " << hex << checksum1 << ", found " << hex << checksum2;
                return T(*_data, _blockId, decoded, checksum1, Error::ERR_CRC_CHECK, ss.str());
            }
        }

        return T(*_data, _blockId, decoded, checksum1, 0, "Success");
    }
    catch (exception& e) {
        // Make sure to unfreeze next block
        if (_processedBlockId->load() == _blockId - 1)
            (*_processedBlockId)++;

        if (ed != nullptr)
            delete ed;

        return T(*_data, _blockId, 0, checksum1, Error::ERR_PROCESS_BLOCK, e.what());
    }
}
