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

#include <cstdlib>
#include <iomanip>
#include <ios>
#include <sstream>

#include "InfoPrinter.hpp"
#include "../util/strings.hpp"

using namespace kanzi;
using namespace std;

InfoPrinter::InfoPrinter(int infoLevel, InfoPrinter::Type type, OutputStream& os)
    : _os(os)
    , _type(type)
    , _level(infoLevel)
    , _headerInfo(0)
{
    STORE_ATOMIC(_lastEmittedBlockId, 0);

    // Select the ONLY ordered phase
    if (type == InfoPrinter::COMPRESSION) {
        _thresholds[0] = Event::COMPRESSION_START;
        _thresholds[1] = Event::BEFORE_TRANSFORM;
        _thresholds[2] = Event::AFTER_TRANSFORM;
        _thresholds[3] = Event::BEFORE_ENTROPY;
        _thresholds[4] = Event::AFTER_ENTROPY;
        _thresholds[5] = Event::COMPRESSION_END;
        _orderedPhase = Event::AFTER_ENTROPY;
    }
    else if (type == InfoPrinter::DECOMPRESSION) {
        _thresholds[0] = Event::DECOMPRESSION_START;
        _thresholds[1] = Event::BEFORE_ENTROPY;
        _thresholds[2] = Event::AFTER_ENTROPY;
        _thresholds[3] = Event::BEFORE_TRANSFORM;
        _thresholds[4] = Event::AFTER_TRANSFORM;
        _thresholds[5] = Event::DECOMPRESSION_END;
        _orderedPhase = Event::BEFORE_ENTROPY;
    }
    else {
        _orderedPhase = Event::BLOCK_INFO; // unused
    }
}


void InfoPrinter::processEvent(const Event& evt)
{
    if (_type == InfoPrinter::INFO) {
        processHeaderInfo(evt);
        return;
    }

    if (evt.getType() == _orderedPhase) {
        processOrderedPhase(evt);
        return;
    }

    processEventOrdered(evt);
}


void InfoPrinter::processOrderedPhase(const Event& evt)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _orderedPending.insert(std::make_pair(evt.getId(), evt));
    }

    for (;;)
    {
        WallTimer timer;
        Event next(Event::BLOCK_INFO, 0, "", timer.getCurrentTime());

        {
            std::lock_guard<std::mutex> lock(_mutex);

            int nextId = LOAD_ATOMIC(_lastEmittedBlockId) + 1;
            std::unordered_map<int, Event>::iterator it =
                _orderedPending.find(nextId);

            if (it == _orderedPending.end())
                return;

            next = it->second;
            _orderedPending.erase(it);
            STORE_ATOMIC(_lastEmittedBlockId, nextId);
        }

        // Compression: AFTER_ENTROPY emitted in-order
        // Decompression: BEFORE_TRANSFORM emitted in-order
        processEventOrdered(next);
    }
}


void InfoPrinter::processEventOrdered(const Event& evt)
{
    const int blockId = evt.getId();
    const Event::Type type = evt.getType();

    if (type == _thresholds[1])
    {
        BlockInfo* bi = new BlockInfo();
        bi->_timeStamp1 = evt.getTime();
        bi->_stage0Size = evt.getSize();
        _blocks[blockId] = bi;

        if (_level >= 5)
            _os << evt.toString() << std::endl;

        _os.flush();
    }
    else if (type == _thresholds[2])
    {
        std::unordered_map<int, BlockInfo*>::iterator it = _blocks.find(blockId);

        if (it == _blocks.end())
            return;

        BlockInfo& bi = *it->second;
        bi._timeStamp2 = evt.getTime();

        if (_level >= 5) {
            double elapsed = WallTimer::calculateDifference(bi._timeStamp1, bi._timeStamp2);
            std::stringstream ss;
            ss << evt.toString() << " [" << int64(elapsed) << " ms]";
            _os << ss.str() << std::endl;
        }

        _os.flush();
    }
    else if (type == _thresholds[3])
    {
        std::unordered_map<int, BlockInfo*>::iterator it = _blocks.find(blockId);

        if (it == _blocks.end())
            return;

        BlockInfo& bi = *it->second;
        bi._timeStamp3 = evt.getTime();
        bi._stage1Size = evt.getSize();

        if (_level >= 5)
            _os << evt.toString() << std::endl;

        _os.flush();
    }
    else if (type == _thresholds[4])
    {
        std::unordered_map<int, BlockInfo*>::iterator it = _blocks.find(blockId);

        if (it == _blocks.end())
            return;

        BlockInfo& bi = *it->second;
        stringstream ss;

        if (_level >= 5) {
            ss << evt.toString() << endl;
        }

        // Display block info
        if (_level >= 4) {
            double elapsed1 = WallTimer::calculateDifference(bi._timeStamp1, bi._timeStamp2);
            double elapsed2 = WallTimer::calculateDifference(bi._timeStamp3, evt.getTime());
            ss << "Block " << blockId << ": "
               << bi._stage0Size
               << " => "
               << bi._stage1Size
               << " [" << int64(elapsed1) << " ms] => " << evt.getSize()
               << " [" << int64(elapsed2) << " ms]";

            // Add compression ratio for encoding
            if ((_type == InfoPrinter::COMPRESSION) && (bi._stage0Size != 0)) {
                ss << " (" << uint(double(evt.getSize()) * 100.0 / double(bi._stage0Size))
                   << "%)";
            }

            // Optionally add hash
            if (evt.getHash() != 0) {
                ss << std::uppercase << std::hex << " [" << evt.getHash() << "]";
            }

            _os << ss.str() << std::endl;
        }

        delete it->second;
        _blocks.erase(it);

        _os.flush();
    }
    else if ((evt.getType() == Event::AFTER_HEADER_DECODING) && (_level >= 3)) {
        // Special CSV format
        stringstream ss;
        vector<string> tokens;
        string s = evt.toString();
        const int nbTokens = tokenizeCSV(s, tokens);

        if (_level >= 5) {
            // JSON output
            ss << "{ \"type\":\"" << evt.getTypeAsString() << "\"";

            if (nbTokens > 1)
                ss << ", \"bsversion\":" << tokens[1];

            if (nbTokens > 2)
                ss << ", \"checksize\":" << tokens[2];

            if (nbTokens > 3)
                ss << ", \"blocksize\":" << tokens[3];

            if (nbTokens > 4)
                ss << ", \"entropy\":" << (tokens[4] == "" ? "\"NONE\"" : "\"" + tokens[4] + "\"");

            if (nbTokens > 5)
                ss << ", \"transforms\":" << (tokens[5] == "" ? "\"NONE\"" : "\"" + tokens[5] + "\"");

            if (nbTokens > 6)
                ss << ", \"compressed\":" << (tokens[6] == "" ? "N/A" : tokens[6]);

            if (nbTokens > 7)
                ss << ", \"original\":" << (tokens[7] == "" ? "N/A" : tokens[7]);

            ss << " }";
        }
        else {
            // Raw text output
            if (nbTokens > 1)
                ss << "Bitstream version: " << tokens[1] << endl;

            if (nbTokens > 2)
                ss << "Block checksum: " << (tokens[2] == "0" ? "NONE" : tokens[2] + " bits") << endl;

            if (nbTokens > 3)
                ss << "Block size: " << tokens[3] << " bytes" << endl;

            if (nbTokens > 4)
                ss << "Using " << (tokens[4] == "" ? "no" : tokens[4]) << " entropy codec (stage 1)" << endl;

            if (nbTokens > 5)
                ss << "Using " << (tokens[5] == "" ? "no" : tokens[5]) << " transform (stage 2)" << endl;

            if ((nbTokens > 7) && (tokens[7] != ""))
                ss << "Original size: " << tokens[7] << " byte(s)" << endl;
        }

        _os << ss.str() << endl;
        _os.flush();
    }
    else if (_level >= 5) {
        _os << evt.toString() << endl;
        _os.flush();
    }
}


void InfoPrinter::processHeaderInfo(const Event& evt)
{
    if ((_level == 0) || (evt.getType() != Event::AFTER_HEADER_DECODING))
        return;

    stringstream ss(evt.toString());
    vector<string> tokens;
    tokenizeCSV(ss.str(), tokens);
    ss.str(string());

    if (_headerInfo++ == 0) {
        ss << endl;
        ss << "|" << "     File Name      ";
        ss << "|" << "Ver";
        ss << "|" << "Check";
        ss << "|" << "Block Size";
        ss << "|" << "  File Size ";
        ss << "|" << " Orig. Size ";
        ss << "|" << " Ratio ";

        if (_level >= 4) {
            ss << "|" << " Entropy";
            ss << "|" << "        Transforms        ";
        }

        ss << "|" << endl;
    }

    ss << "|";

    if (!tokens.empty()) {
        string inputName = tokens[0];
        size_t idx = inputName.find_last_of(PATH_SEPARATOR);

        if (idx != string::npos)
            inputName.erase(0, idx + 1);

        if (inputName.length() > 20)
            inputName.replace(18, string::npos, "..");

        ss << left << setw(20) << inputName << "|" << right;
    }

    if (tokens.size() > 1) ss << setw(3) << tokens[1] << "|";
    if (tokens.size() > 2) ss << setw(5) << tokens[2] << "|";
    if (tokens.size() > 3) ss << setw(10) << tokens[3] << "|";

    if (tokens.size() > 6)
        ss << setw(12) << formatSize(tokens[6]) << "|";
    else
        ss << setw(12) << "    N/A    |";

    if (tokens.size() > 7)
        ss << setw(12) << formatSize(tokens[7]) << "|";
    else
        ss << setw(12) << "    N/A    |";

    if (tokens.size() > 7 && tokens[6] != "" && tokens[7] != "") {
        double compSz = atof(tokens[6].c_str());
        double origSz = atof(tokens[7].c_str());

        if (origSz == 0.0)
            ss << setw(7) << "  N/A  |";
        else
            ss << setw(7) << fixed << setprecision(3)
               << (compSz / origSz) << "|";
    }
    else {
        ss << setw(7) << "  N/A  |";
    }

    if (_level >= 4 && tokens.size() > 4)
        ss << setw(8) << (tokens[4] == "" ? "NONE" : tokens[4]) << "|";

    if (_level >= 4 && tokens.size() > 5) {
        string t = tokens[5];

        if (t.length() > 26)
            t.replace(24, string::npos, "..");

        ss << setw(26) << (t == "" ? "NONE" : t) << "|";
    }

    _os << ss.str() << endl;
}

