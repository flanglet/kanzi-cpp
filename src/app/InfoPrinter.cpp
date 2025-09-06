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
{
    if (type == InfoPrinter::ENCODING) {
        _thresholds[0] = Event::COMPRESSION_START;
        _thresholds[1] = Event::BEFORE_TRANSFORM;
        _thresholds[2] = Event::AFTER_TRANSFORM;
        _thresholds[3] = Event::BEFORE_ENTROPY;
        _thresholds[4] = Event::AFTER_ENTROPY;
        _thresholds[5] = Event::COMPRESSION_END;
    }
    else {
        _thresholds[0] = Event::DECOMPRESSION_START;
        _thresholds[1] = Event::BEFORE_ENTROPY;
        _thresholds[2] = Event::AFTER_ENTROPY;
        _thresholds[3] = Event::BEFORE_TRANSFORM;
        _thresholds[4] = Event::AFTER_TRANSFORM;
        _thresholds[5] = Event::DECOMPRESSION_END;
    }
	
    for (int i = 0; i < 1024; i++)
        _map[i] = nullptr;

    _headerInfo = 0;
}

void InfoPrinter::processEvent(const Event& evt)
{
    if (_type == InfoPrinter::INFO) {
       processHeaderInfo(evt);
       return;
    }

    const int currentBlockId = evt.getId();

    if (evt.getType() == _thresholds[1]) {
        // Register initial block size
        BlockInfo* bi = new BlockInfo();
        _clock12.start();

        bi->_stage0Size = evt.getSize();
        _map[hash(currentBlockId)] = bi;

        if (_level >= 5) {
            _os << evt.toString() << endl;
        }
    }
    else if (evt.getType() == _thresholds[2]) {
        BlockInfo* bi = _map[hash(currentBlockId)];

        if (bi == nullptr)
            return;

        _clock12.stop();
        _clock23.start();

        if (_level >= 5) {
            stringstream ss;
            ss << evt.toString() << " [" << int64(_clock12.elapsed()) << " ms]";
            _os << ss.str() << endl;
        }
    }
    else if (evt.getType() == _thresholds[3]) {
        BlockInfo* bi = _map[hash(currentBlockId)];

        if (bi == nullptr)
            return;

        _clock23.stop();
        _clock34.start();
        bi->_stage1Size = evt.getSize();

        if (_level >= 5) {
            _os << evt.toString() << endl;
        }
    }
    else if (evt.getType() == _thresholds[4]) {
        BlockInfo* bi = _map[hash(currentBlockId)];

        if (bi == nullptr)
            return;

        if (_level < 3) {
            delete bi;
            _map[hash(currentBlockId)] = nullptr;
            return;
        }

        int64 stage2Size = evt.getSize();
        _clock34.stop();
        stringstream ss;

        if (_level >= 5) {
            ss << evt.toString() << endl;
        }

        // Display block info
        if (_level >= 4) {
            ss << "Block " << currentBlockId << ": " << bi->_stage0Size << " => ";
            ss << bi->_stage1Size << " [" << int64(_clock12.elapsed()) << " ms] => " << stage2Size;
            ss << " [" << int64(_clock34.elapsed()) << " ms]";

            // Add compression ratio for encoding
            if ((_type == InfoPrinter::ENCODING) && (bi->_stage0Size != 0)) {
                ss << " (" << uint(double(stage2Size) * double(100) / double(bi->_stage0Size));
                ss << "%)";
            }

            // Optionally add hash
            if (evt.getHash() != 0) {
                ss << std::uppercase << std::hex << " [" << evt.getHash() << "]";
            }

            _os << ss.str() << endl;
        }

        delete bi;
        _map[hash(currentBlockId)] = nullptr;
    }
    else if ((evt.getType() == Event::AFTER_HEADER_DECODING) && (_level >= 3)) {
        // Special CSV format
        stringstream ss;
        vector<string> tokens;
        string s = evt.toString();
        const int nbTokens = tokenizeCSV(s, tokens);

        if (_level >= 5) {
            // JSON text
            if (nbTokens > 1)
                ss << ", \"bsVersion\":" << tokens[1];

            if (nbTokens > 2)
                ss << ", \"checksize\":" << tokens[2];

            if (nbTokens > 3)
                ss << ", \"blocksize\":" << tokens[3];

            if (nbTokens > 4)
                ss << ", \"entropy\":" << (tokens[4] == "" ? "none" : "\"" + tokens[4] + "\"");

            if (nbTokens > 5)
                ss << ", \"transforms\":" << (tokens[5] == "" ? "none" : "\"" + tokens[5] + "\"");

            if (nbTokens > 6)
                ss << ", \"compressed\":" << (tokens[6] == "" ? "N/A" : tokens[6]);

            if (nbTokens > 7)
                ss << ", \"original\":" << (tokens[7] == "" ? "N/A" : tokens[7]);
        }
        else {
            // Raw text
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
    }
    else if (_level >= 5) {
        _os << evt.toString() << endl;
    }
}


void InfoPrinter::processHeaderInfo(const Event& evt)
{
   if ((_level == 0) || (evt.getType() != Event::AFTER_HEADER_DECODING))
      return;

   stringstream ss(evt.toString());
   string s = ss.str();
   vector<string> tokens;
   const int nbTokens = tokenizeCSV(s, tokens);
   ss.str(string());

   if (_headerInfo++ == 0) {
      // Display header
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

   if (nbTokens > 0) {
       string inputName = tokens[0];
       size_t idx = inputName.find_last_of(PATH_SEPARATOR);

       if (idx != string::npos)
          inputName = inputName.substr(idx + 1);

       if (inputName.length() > 20)
          inputName = inputName.substr(0, 18) + "..";

       ss << std::left << setw(20) << inputName << "|" << std::right; // inputName
   }

   if (nbTokens > 1)
       ss << setw(3) << tokens[1] << "|"; //bsVersion

   if (nbTokens > 2)
       ss << setw(5) << tokens[2] << "|"; // checksum

   if (nbTokens > 3)
       ss << setw(10) << tokens[3] << "|"; // block size

   if (nbTokens > 6)
       ss << setw(12) << formatSize(tokens[6]) << "|"; // compressed size
   else
       ss << setw(12) << "    N/A    " << "|";

   if (nbTokens > 7)
       ss << setw(12) << formatSize(tokens[7]) << "|"; // original size
   else
       ss << setw(12) << "    N/A    " << "|";

   if ((tokens[6] != "") && (tokens[7] != "")) {
      string compStr = tokens[6];
      string origStr = tokens[7];
      double compSz = atof(compStr.c_str());
      double origSz = atof(origStr.c_str());

      if (origSz == double(0)) {
        ss << setw(7) << "  N/A  " << "|";
      }
      else {
         double ratio = compSz / origSz;
         ss << setw(7) << std::fixed << std::setprecision(3) << ratio << "|"; // compression ratio
      }
   }
   else {
      ss << setw(7) << "  N/A  " << "|";
   }

   if ((_level >= 4) && (nbTokens > 4))
       ss << setw(8) << (tokens[4] == "" ? "NONE" : tokens[4]) << "|"; // entropy

   if ((_level >= 4) && (nbTokens > 5)) {
       string t = tokens[5];

       if (t.length() > 26)
          t = t.substr(0, 24) + "..";

       ss << setw(26) << (t == "" ? "NONE" : t) << "|"; // transforms
   }

   _os << ss.str() << endl;
}
