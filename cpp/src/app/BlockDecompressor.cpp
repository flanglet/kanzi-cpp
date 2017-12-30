
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

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include "BlockDecompressor.hpp"
#include "InfoPrinter.hpp"
#include "../io/Error.hpp"
#include "../io/IOException.hpp"
#include "../IllegalArgumentException.hpp"
#include "../io/NullOutputStream.hpp"
#include "../SliceArray.hpp"
#ifdef CONCURRENCY_ENABLED
#include <future>
#endif

using namespace kanzi;

BlockDecompressor::BlockDecompressor(map<string, string>& args)
{
    _blockSize = 0;
    map<string, string>::iterator it;
    it = args.find("overwrite");

    if (it == args.end()) {
        _overwrite = false;
    }
    else {
        string str = it->second;
        transform(str.begin(), str.end(), str.begin(), ::toupper);
        _overwrite = str == "TRUE";
        args.erase(it);
    }

    it = args.find("inputName");
    _inputName = it->second;
    args.erase(it);
    it = args.find("outputName");
    _outputName = it->second;
    args.erase(it);
    it = args.find("jobs");
    int concurrency = atoi(it->second.c_str());
    _jobs = (concurrency == 0) ? DEFAULT_CONCURRENCY : concurrency;
    args.erase(it);

#ifndef CONCURRENCY_ENABLED
    if (_jobs > 1)
        throw IllegalArgumentException("The number of jobs is limited to 1 in this version");
#endif

    _cis = nullptr;
    _os = nullptr;

    it = args.find("verbose");
    _verbosity = atoi(it->second.c_str());
    args.erase(it);

    if ((_verbosity > 0) && (args.size() > 0)) {
        for (it = args.begin(); it != args.end(); it++) {
            stringstream ss;
            ss << "Ignoring invalid option [" << it->first << "]";
            printOut(ss.str().c_str(), _verbosity > 0);
        }
    }
}

BlockDecompressor::~BlockDecompressor()
{
    dispose();

    while (_listeners.size() > 0) {
        vector<Listener*>::iterator it = _listeners.begin();
        delete *it;
        _listeners.erase(it);
    }
}

void BlockDecompressor::dispose()
{
}

int BlockDecompressor::call()
{
    vector<string> files;
    uint64 read = 0;
    Clock clock;

    try {
        createFileList(_inputName, files);
    }
    catch (exception& e) {
        cerr << e.what() << endl;
        return Error::ERR_OPEN_FILE;
    }

    if (files.size() == 0) {
        cerr << "Cannot access input file '" << _inputName << "'" << endl;
        return Error::ERR_OPEN_FILE;
    }

    // Sort files by name to ensure same order each time
    sort(files.begin(), files.end());
    int nbFiles = int(files.size());

    // Limit verbosity level when files are processed concurrently
    if ((_jobs > 1) && (nbFiles > 1) && (_verbosity > 1)) {
        printOut("Warning: limiting verbosity to 1 due to concurrent processing of input files.\n", _verbosity > 1);
        _verbosity = 1;
    }

    if (_verbosity > 2)
        addListener(new InfoPrinter(_verbosity, InfoPrinter::DECODING, cout));

    bool printFlag = _verbosity > 2;
    printOut("\n", printFlag);

    stringstream ss;
    string strFiles = (nbFiles > 1) ? " files" : " file";
    ss << nbFiles << strFiles << " to decompress\n";
    printOut(ss.str().c_str(), _verbosity > 0);
    ss.str(string());
    ss << "Verbosity set to " << _verbosity;
    printOut(ss.str().c_str(), printFlag);
    ss.str(string());
    ss << "Overwrite set to " << (_overwrite ? "true" : "false");
    printOut(ss.str().c_str(), printFlag);
    ss.str(string());
    ss << "Using " << _jobs << " job" << ((_jobs > 1) ? "s" : "");
    printOut(ss.str().c_str(), printFlag);
    ss.str(string());

    string outputName = _outputName;
    transform(outputName.begin(), outputName.end(), outputName.begin(), ::toupper);

    if ((_jobs > 1) && (outputName.compare("STDOUT") == 0)) {
        cerr << "Cannot output to STDOUT with multiple jobs" << endl;
        return Error::ERR_CREATE_FILE;
    }

    int res = 0;

    // Run the task(s)
    if (nbFiles == 1) {
        string iName = files[0];
        string oName = _outputName;

        if (oName.size() == 0)
            oName = iName + ".bak";

        FileDecompressTask<FileDecompressResult> task(_verbosity, _overwrite, iName, oName, 1, _listeners);
        FileDecompressResult fdr = task.call();
        res = fdr._code;
        read = fdr._read;
    }
    else {
        if ((_outputName.length() > 0) && (outputName.compare("NONE") != 0)) {
            cerr << "Output file cannot be provided when input is a directory (except 'NONE')" << endl;
            return Error::ERR_CREATE_FILE;
        }

        vector<FileDecompressTask<FileDecompressResult>*> tasks;

        //  Create one task per file
        for (int i = 0; i < nbFiles; i++) {
            string iName = files[i];
            string oName = (_outputName.length() > 0) ? "NONE" : iName + ".bak";
            FileDecompressTask<FileDecompressResult>* task = new FileDecompressTask<FileDecompressResult>(_verbosity, _overwrite, iName, oName, 1, _listeners);
            tasks.push_back(task);
        }

        bool doConcurrent = _jobs > 1;

#ifdef CONCURRENCY_ENABLED
        if (doConcurrent) {
            vector<FileDecompressWorker<FileDecompressTask<FileDecompressResult>*, FileDecompressResult>*> workers;
            vector<future<FileDecompressResult> > results;
            BoundedConcurrentQueue<FileDecompressTask<FileDecompressResult>*, FileDecompressResult> queue(nbFiles, &tasks[0]);

            // Create one worker per job and run it. A worker calls several tasks sequentially.
            for (int i = 0; i < _jobs; i++) {
                workers.push_back(new FileDecompressWorker<FileDecompressTask<FileDecompressResult>*, FileDecompressResult>(&queue));
                results.push_back(async(launch::async, &FileDecompressWorker<FileDecompressTask<FileDecompressResult>*, FileDecompressResult>::call, workers[i]));
            }

            // Wait for results
            for (int i = 0; i < _jobs; i++) {
                FileDecompressResult fcr = results[i].get();
                res = fcr._code;
                read += fcr._read;

                if (res != 0) {
                    // Exit early by telling the workers that the queue is empty
                    queue.clear();
                    break;
                }
            }

            for (int i = 0; i < _jobs; i++)
                delete workers[i];
        }
#endif

        if (!doConcurrent) {
            for (uint i = 0; i < tasks.size(); i++) {
                FileDecompressResult fcr = tasks[i]->call();
                res = fcr._code;
                read += fcr._read;

                if (res != 0)
                    break;
            }
        }

        for (int i = 0; i < nbFiles; i++)
            delete tasks[i];
    }

    clock.stop();

    if (nbFiles > 1) {
        double delta = clock.elapsed();
        printOut("", _verbosity > 0);
        ss << "Total decoding time: " << uint64(delta) << " ms";
        printOut(ss.str().c_str(), _verbosity > 0);
        ss.str(string());
        ss << "Total input size: " << read << " byte" << ((read > 1) ? "s" : "");
        printOut(ss.str().c_str(), _verbosity > 0);
        ss.str(string());
    }

    return res;
}

bool BlockDecompressor::addListener(Listener* bl)
{
    if (bl == nullptr)
        return false;

    _listeners.push_back(bl);
    return true;
}

bool BlockDecompressor::removeListener(Listener* bl)
{
    std::vector<Listener*>::iterator it = find(_listeners.begin(), _listeners.end(), bl);

    if (it == _listeners.end())
        return false;

    _listeners.erase(it);
    return true;
}

void BlockDecompressor::notifyListeners(vector<Listener*>& listeners, const Event& evt)
{
    vector<Listener*>::iterator it;

    for (it = listeners.begin(); it != listeners.end(); it++)
        (*it)->processEvent(evt);
}

template <class T>
FileDecompressTask<T>::FileDecompressTask(int verbosity, bool overwrite,
    const string& inputName, const string& outputName,
    int jobs, vector<Listener*> listeners)
{
    _verbosity = verbosity;
    _overwrite = overwrite;
    _inputName = inputName;
    _outputName = outputName;
    _jobs = jobs;
    _listeners = listeners;
    _os = nullptr;
    _cis = nullptr;
}

template <class T>
FileDecompressTask<T>::~FileDecompressTask()
{
    dispose();

    if (_cis != nullptr) {
        delete _cis;
        _cis = nullptr;
    }

    try {
        if ((_os != nullptr) && (_os != &cout)) {
            delete _os;
        }

        _os = nullptr;
    }
    catch (exception ioe) {
    }
}

template <class T>
T FileDecompressTask<T>::call()
{
    bool printFlag = _verbosity > 2;
    stringstream ss;
    ss << "Input file name set to '" << _inputName << "'";
    printOut(ss.str().c_str(), printFlag);
    ss.str(string());
    ss << "Output file name set to '" << _outputName << "'";
    printOut(ss.str().c_str(), printFlag);
    ss.str(string());

    int64 read = 0;
    printFlag = _verbosity > 1;
    ss << "\nDecoding " << _inputName << " ...";
    printOut(ss.str().c_str(), printFlag);
    printOut("\n", _verbosity > 3);

    if (_listeners.size() > 0) {
        Event evt(Event::DECOMPRESSION_START, -1, int64(0));
        BlockDecompressor::notifyListeners(_listeners, evt);
    }

    string str = _outputName;
    transform(str.begin(), str.end(), str.begin(), ::toupper);

    if (str.compare(0, 4, "NONE") == 0) {
        _os = new NullOutputStream();
    }
    else if (str.compare(0, 6, "STDOUT") == 0) {
        _os = &cout;
    }
    else {
        try {
            if (samePaths(_inputName, _outputName)) {
                cerr << "The input and output files must be different" << endl;
                return T(Error::ERR_CREATE_FILE, 0);
            }

            struct stat buffer;

            if (stat(_outputName.c_str(), &buffer) == 0) {
                if ((buffer.st_mode & S_IFDIR) != 0) {
                    cerr << "The output file is a directory" << endl;
                    return T(Error::ERR_OUTPUT_IS_DIR, 0);
                }

                if (_overwrite == false) {
                    cerr << "The output file exists and the 'force' command "
                         << "line option has not been provided" << endl;
                    return T(Error::ERR_OVERWRITE_FILE, 0);
                }
            }

            _os = new ofstream(_outputName.c_str(), ofstream::binary);

            if (!*_os) {
                cerr << "Cannot open output file '" << _outputName + "' for writing: " << endl;
                return T(Error::ERR_CREATE_FILE, 0);
            }
        }
        catch (exception& e) {
            cerr << "Cannot open output file '" << _outputName << "' for writing: " << e.what() << endl;
            return T(Error::ERR_CREATE_FILE, 0);
        }
    }

    InputStream* is;

    try {
        str = _inputName;
        transform(str.begin(), str.end(), str.begin(), ::toupper);

        if (str.compare(0, 5, "STDIN") == 0) {
            is = &cin;
        }
        else {
            ifstream* ifs = new ifstream(_inputName.c_str(), ifstream::binary);

            if (!*ifs) {
                cerr << "Cannot open input file '" << _inputName << "'" << endl;
                return T(Error::ERR_OPEN_FILE, 0);
            }

            is = ifs;
        }

        try {
            map<string, string> ctx;
            stringstream ss;
            ss << _jobs;
            ctx["jobs"] = ss.str();
            _cis = new CompressedInputStream(*is, ctx);

            for (uint i = 0; i < _listeners.size(); i++)
                _cis->addListener(*_listeners[i]);
        }
        catch (IllegalArgumentException& e) {
            cerr << "Cannot create compressed stream: " << e.what() << endl;
            return T(Error::ERR_CREATE_DECOMPRESSOR, 0);
        }
    }
    catch (exception& e) {
        cerr << "Cannot open input file '" << _inputName << "': " << e.what() << endl;
        return T(Error::ERR_OPEN_FILE, _cis->getRead());
    }

    Clock clock;
    byte* buf = new byte[DEFAULT_BUFFER_SIZE];

    try {
        SliceArray<byte> sa(buf, DEFAULT_BUFFER_SIZE, 0);
        int decoded = 0;

        // Decode next block
        do {
            _cis->read((char*)&sa._array[0], sa._length);
            decoded = (int)_cis->gcount();

            if (decoded < 0) {
                delete[] buf;
                cerr << "Reached end of stream" << endl;
                return T(Error::ERR_READ_FILE, _cis->getRead());
            }

            try {
                if (decoded > 0) {
                    _os->write((const char*)&sa._array[0], decoded);
                    read += decoded;
                }
            }
            catch (exception& e) {
                delete[] buf;
                cerr << "Failed to write decompressed block to file '" << _outputName << "': ";
                cerr << e.what() << endl;
                return T(Error::ERR_READ_FILE, _cis->getRead());
            }
        } while (decoded == sa._length);
    }
    catch (IOException& e) {
        // Close streams to ensure all data are flushed
        dispose();
        delete[] buf;

        if (_cis->eof()) {
            cerr << "Reached end of stream" << endl;
            return T(Error::ERR_READ_FILE, _cis->getRead());
        }

        cerr << e.what() << endl;
        return T(e.error(), _cis->getRead());
    }
    catch (exception& e) {
        // Close streams to ensure all data are flushed
        dispose();
        delete[] buf;

        if (_cis->eof()) {
            cerr << "Reached end of stream" << endl;
            return T(Error::ERR_READ_FILE, _cis->getRead());
        }

        cerr << "An unexpected condition happened. Exiting ..." << endl
             << e.what() << endl;
        return T(Error::ERR_UNKNOWN, _cis->getRead());
    }

    // Close streams to ensure all data are flushed
    dispose();

    if (is != &cin) {
        ifstream* ifs = dynamic_cast<ifstream*>(is);

        if (ifs) {
            try {
                ifs->close();
            }
            catch (exception&) {
                // Ignore
            }
        }

        delete is;
    }

    clock.stop();
    double delta = clock.elapsed();
    printOut("", _verbosity > 1);
    ss.str(string());
    ss << "Decoding:          " << uint64(delta) << " ms";
    printOut(ss.str().c_str(), printFlag);
    ss.str(string());
    ss << "Input size:        " << _cis->getRead();
    printOut(ss.str().c_str(), printFlag);
    ss.str(string());
    ss << "Output size:       " << read;
    printOut(ss.str().c_str(), printFlag);
    ss.str(string());
    ss << "Decoding " << _inputName << ": " << _cis->getRead() << " => " << read;
    ss << " bytes in " << delta << " ms";
    printOut(ss.str().c_str(), _verbosity == 1);

    if (delta > 0) {
        double b2KB = double(1000) / double(1024);
        ss.str(string());
        ss << "Throughput (KB/s): " << uint(read * b2KB / delta);
        printOut(ss.str().c_str(), printFlag);
    }

    printOut("", _verbosity > 1);

    if (_listeners.size() > 0) {
        Event evt(Event::DECOMPRESSION_END, -1, int64(_cis->getRead()));
        BlockDecompressor::notifyListeners(_listeners, evt);
    }

    delete[] buf;
    return T(0, read);
}

// Close and flush streams. Do not deallocate resources. Idempotent.
template <class T>
void FileDecompressTask<T>::dispose()
{
    try {
        if (_cis != nullptr) {
            _cis->close();
        }
    }
    catch (exception& e) {
        cerr << "Decompression failure: " << e.what() << endl;
        exit(Error::ERR_WRITE_FILE);
    }

    if (_os != &cout) {
        ofstream* ofs = dynamic_cast<ofstream*>(_os);

        if (ofs) {
            try {
                ofs->close();
            }
            catch (exception&) {
                // Ignore
            }
        }
    }
}

#ifdef CONCURRENCY_ENABLED
template <class T, class R>
R FileDecompressWorker<T, R>::call()
{
    int res = 0;
    uint64 read = 0;

    while (res == 0) {
        T* task = _queue->get();

        if (task == nullptr)
            break;

        R result = (*task)->call();
        res = result._code;
        read += result._read;
    }

    return R(res, read);
}
#endif
