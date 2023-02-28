/*
Copyright 2011-2022 Frederic Langlet
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
#include <iostream>
#include <map>

#include "BlockCompressor.hpp"
#include "BlockDecompressor.hpp"
#include "../util.hpp"
#include "../Error.hpp"

using namespace kanzi;
using namespace std;

static const string CMD_LINE_ARGS[14] = {
    "-c", "-d", "-i", "-o", "-b", "-t", "-e", "-j", "-v", "-l", "-s", "-x", "-f", "-h"
};

//static const int ARG_IDX_COMPRESS = 0;
//static const int ARG_IDX_DECOMPRESS = 1;
static const int ARG_IDX_INPUT = 2;
static const int ARG_IDX_OUTPUT = 3;
static const int ARG_IDX_BLOCK = 4;
static const int ARG_IDX_TRANSFORM = 5;
static const int ARG_IDX_ENTROPY = 6;
static const int ARG_IDX_JOBS = 7;
static const int ARG_IDX_VERBOSE = 8;
static const int ARG_IDX_LEVEL = 9;
//static const int ARG_IDX_FROM = 10;
//static const int ARG_IDX_TO = 11;

static const char* APP_HEADER = "Kanzi 2.1 (c) Frederic Langlet";


#ifdef CONCURRENCY_ENABLED
   mutex Printer::_mtx;
#endif

void printHelp(Printer& log, const string& mode)
{
   log.println("", true);
   log.println("Credits: Matt Mahoney, Yann Collet, Jan Ondrus, Yuta Mori, Ilya Muravyov,", true);
   log.println("         Neal Burns, Fabian Giesen, Jarek Duda, Ilya Grebnov", true);
   log.println("", true);
   log.println("   -h, --help", true);
   log.println("        display this message\n", true);

   if ((mode.compare(0, 1, "c") != 0) && (mode.compare(0, 1, "d") != 0)) {
       log.println("   -c, --compress", true);
       log.println("        compress mode\n", true);
       log.println("   -d, --decompress", true);
       log.println("        decompress mode\n", true);
   }

   log.println("   -i, --input=<inputName>", true);
   log.println("        mandatory name of the input file or directory or 'stdin'", true);
   log.println("        When the source is a directory, all files in it will be processed.", true);
   stringstream ss;
   ss << "        Provide " << PATH_SEPARATOR << ". at the end of the directory name to avoid recursion";
   log.println(ss.str().c_str(), true);
   ss.str(string());
   ss << "        (EG: myDir" << PATH_SEPARATOR << ". => no recursion)\n";
   log.println(ss.str().c_str(), true);
   ss.str(string());
   log.println("   -o, --output=<outputName>", true);

   if (mode.compare(0, 1, "c") == 0) {
       log.println("        optional name of the output file or directory (defaults to", true);
       log.println("        <inputName.knz>) or 'none' or 'stdout'. 'stdout' is not valid", true);
       log.println("        when the number of jobs is greater than 1.\n", true);
   }
   else if (mode.compare(0, 1, "d") == 0) {
       log.println("        optional name of the output file or directory (defaults to", true);
       log.println("        <inputName.bak>) or 'none' or 'stdout'. 'stdout' is not valid", true);
       log.println("        when the number of jobs is greater than 1.\n", true);
   }
   else {
       log.println("        optional name of the output file or 'none' or 'stdout'.\n", true);
   }

   if (mode.compare(0, 1, "c") == 0) {
       log.println("   -b, --block=<size>", true);
       log.println("        size of blocks (default 4|8|16 MB based on level, max 1 GB, min 1 KB).\n", true);
       log.println("   -l, --level=<compression>", true);
       log.println("        set the compression level [0..9]", true);
       log.println("        Providing this option forces entropy and transform.", true);
       log.println("        0=None&None (store), 1=LZ&HUFFMAN, 2=TEXT+UTF+FSD+LZX&HUFFMAN", true);
       log.println("        3=TEXT+UTF+FSD+ROLZ, 4=TEXT+UTF+FSD+ROLZX, 5=TEXT+UTF+BWT+RANK+ZRLT&ANS0", true);
       log.println("        6=TEXT+UTF+BWT+SRT+ZRLT&FPAQ, 7=LZP+TEXT+UTF+BWT+LZP&CM", true);
       log.println("        8=EXE+RLT+TEXT+UTF&TPAQ, 9=EXE+RLT+TEXT+UTF&TPAQX\n", true);
       log.println("   -e, --entropy=<codec>", true);
       log.println("        entropy codec [None|Huffman|ANS0|ANS1|Range|FPAQ|TPAQ|TPAQX|CM]", true);
       log.println("        (default is ANS0)\n", true);
       log.println("   -t, --transform=<codec>", true);
       log.println("        transform [None|BWT|BWTS|LZ|LZX|LZP|ROLZ|ROLZX|RLT|ZRLT]", true);
       log.println("                  [MTFT|RANK|SRT|TEXT|FSD|EXE|UTF]", true);
       log.println("        EG: BWT+RANK or BWTS+MTFT (default is BWT+RANK+ZRLT)\n", true);
       log.println("   -x, --checksum", true);
       log.println("        enable block checksum\n", true);
       log.println("   -s, --skip", true);
       log.println("        copy blocks with high entropy instead of compressing them.\n", true);
   }

   log.println("   -j, --jobs=<jobs>", true);
   log.println("        maximum number of jobs the program may start concurrently", true);
   #ifdef CONCURRENCY_ENABLED
      log.println("        (default is half of available cores, maximum is 64).\n", true);
   #else
      log.println("        (always 1 in this version).\n", true);
   #endif
   log.println("   -v, --verbose=<level>", true);
   log.println("        0=silent, 1=default, 2=display details, 3=display configuration,", true);
   log.println("        4=display block size and timings, 5=display extra information", true);
   log.println("        Verbosity is reduced to 1 when files are processed concurrently", true);
   log.println("        Verbosity is reduced to 0 when the output is 'stdout'\n", true);
   log.println("   -f, --force", true);
   log.println("        overwrite the output file if it already exists\n", true);

   if (mode.compare(0, 1, "d") == 0) {
       log.println("   --from=blockId\n", true);
       log.println("        Decompress starting from the provided block (included).\n", true);
       log.println("        The first block ID is 1.\n", true);
       log.println("   --to=blockId\n", true);
       log.println("        Decompress ending at the provided block (excluded).\n", true);
   }

   if (mode.compare(0, 1, "d") != 0) {
       log.println("", true);
       log.println("EG. kanzi -c -i foo.txt -o none -b 4m -l 4 -v 3\n", true);
       log.println("EG. kanzi -c -i foo.txt -f -t BWT+MTFT+ZRLT -b 4m -e FPAQ -j 4\n", true);
       log.println("EG. kanzi --compress --input=foo.txt --output=foo.knz --force", true);
       log.println("          --transform=BWT+MTFT+ZRLT --block=4m --entropy=FPAQ --jobs=4\n", true);
   }

   if (mode.compare(0, 1, "c") != 0) {
       log.println("", true);
       log.println("EG. kanzi -d -i foo.knz -f -v 2 -j 2\n", true);
       log.println("EG. kanzi --decompress --input=foo.knz --force --verbose=2 --jobs=2\n", true);
   }
}

int processCommandLine(int argc, const char* argv[], map<string, string>& map)
{
    string inputName;
    string outputName;
    string strLevel = "-1";
    string strVerbose = "1";
    string strTasks = "0";
    string strBlockSize = "";
    string strFrom = "";
    string strTo = "";
    string strOverwrite = STR_FALSE;
    string strChecksum = STR_FALSE;
    string strSkip = STR_FALSE;
    string strReorder = STR_TRUE;
    string codec;
    string transf;
    int verbose = 1;
    int ctx = -1;
    int level = -1;
    int from = -1;
    int to = -1;
    string mode = " ";
    Printer log(&cout);

    for (int i = 1; i < argc; i++) {
        string arg(argv[i]);
        arg = trim(arg);

        if (arg.compare(0, 2, "-o") == 0) {
            ctx = ARG_IDX_OUTPUT;
            continue;
        }

        if (arg.compare(0, 2, "-v") == 0) {
            ctx = ARG_IDX_VERBOSE;
            continue;
        }

        // Extract verbosity, output and mode first
        if ((arg.compare(0, 10, "--compress") == 0) || (arg.compare(0, 2, "-c") == 0)) {
            if (mode == "d") {
                cerr << "Both compression and decompression options were provided." << endl;
                return Error::ERR_INVALID_PARAM;
            }

            mode = 'c';
            continue;
        }

        if ((arg.compare(0, 12, "--decompress") == 0) || (arg.compare(0, 2, "-d") == 0)) {
            if (mode == "c") {
                cerr << "Both compression and decompression options were provided." << endl;
                return Error::ERR_INVALID_PARAM;
            }

            mode = 'd';
            continue;
        }

        if ((arg.compare(0, 10, "--verbose=") == 0) || (ctx == ARG_IDX_VERBOSE)) {
            strVerbose = (arg.compare(0, 10, "--verbose=") == 0) ? arg.substr(10) : arg;
            strVerbose = trim(strVerbose);

            if (strVerbose.length() != 1) {
                cerr << "Invalid verbosity level provided on command line: " << arg << endl;
                return Error::ERR_INVALID_PARAM;
            }

            verbose = atoi(strVerbose.c_str());

            if ((verbose < 0) || (verbose > 5)) {
                cerr << "Invalid verbosity level provided on command line: " << arg << endl;
                return Error::ERR_INVALID_PARAM;
            }
        }
        else if ((arg.compare(0, 9, "--output=") == 0) || (ctx == ARG_IDX_OUTPUT)) {
            arg = (arg.compare(0, 9, "--output=") == 0) ? arg.substr(9) : arg;
            outputName = trim(arg);
        }

        ctx = -1;
    }

    // Overwrite verbosity if the output goes to stdout
    if (outputName.length() != 0) {
        string str = outputName;
        transform(str.begin(), str.end(), str.begin(), ::toupper);

        if (str == "STDOUT") {
            verbose = 0;
            strVerbose = '0';
        }
    }

    if (verbose >= 1) {
        log.println("", true);
        log.println(APP_HEADER, true);
        stringstream extraHeader;

#ifdef __clang__
       extraHeader << "\nCompiled with clang version ";
       extraHeader << __clang_major__ << "." << __clang_minor__;
#else
   #ifdef _MSC_VER
         extraHeader << "\nCompiled with Visual Studio";
      #ifdef _MSC_VER_STR // see types.h
         extraHeader << " " << _MSC_VER_STR;
      #endif
   #else
      #ifdef  __INTEL_COMPILER
         extraHeader << "\nCompiled with Intel compiler ";
         extraHeader << "(" << __INTEL_COMPILER_BUILD_DATE << ")";
      #else
         #ifdef  __GNUC__
            extraHeader << "\nCompiled with gcc version ";
            extraHeader << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__;
         #endif
      #endif
   #endif
#endif

        if (extraHeader.str().length() > 0) {
#if defined(__AVX2__)
            extraHeader << " - AVX2";
#elif defined(__AVX__)
            extraHeader << " - AVX";
#elif defined(__AVX512F__)
            extraHeader << " - AVX512";
#elif defined(__SSE4_1__)
            extraHeader << " - SSE4.1";
#elif defined(__SSE3__)
            extraHeader << " - SSE3";
#elif defined(__SSE2__)
            extraHeader << " - SSE2";
#elif defined(__SSE__)
            extraHeader << " - SSE";
#endif
            log.println(extraHeader.str().c_str(), verbose >= 3);
        }

        log.println("", true);
    }

    outputName = "";
    ctx = -1;

    if (argc == 1) {
        printHelp(log, mode);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        string arg(argv[i]);
        size_t k = 0;

        // Left trim limited to spaces (due to possible unicode chars in names)
        while ((k < arg.length()) && (arg[k] == 0x20))
           k++;

        arg = arg.substr(k);

        if ((arg == "--help") || (arg == "-h")) {
            printHelp(log, mode);
            return 0;
        }

        if ((arg == "--compress") || (arg == "-c") || (arg == "--decompress") || (arg == "-d")) {
            if (ctx != -1) {
                stringstream ss;
                ss << "Warning: ignoring option [" << CMD_LINE_ARGS[ctx] << "] with no value.";
                log.println(ss.str().c_str(), verbose > 0);
            }

            ctx = -1;
            continue;
        }

        if ((arg == "--force") || (arg == "-f")) {
            if (ctx != -1) {
                stringstream ss;
                ss << "Warning: ignoring option [" << CMD_LINE_ARGS[ctx] << "] with no value.";
                log.println(ss.str().c_str(), verbose > 0);
            }

            strOverwrite = STR_TRUE;
            ctx = -1;
            continue;
        }

        if ((arg == "--skip") || (arg == "-s")) {
            if (ctx != -1) {
                stringstream ss;
                ss << "Warning: ignoring option [" << CMD_LINE_ARGS[ctx] << "] with no value.";
                log.println(ss.str().c_str(), verbose > 0);
            }

            strSkip = STR_TRUE;
            ctx = -1;
            continue;
        }

        if ((arg == "--checksum") || (arg == "-x")) {
            if (ctx != -1) {
                stringstream ss;
                ss << "Warning: ignoring option [" << CMD_LINE_ARGS[ctx] << "] with no value.";
                log.println(ss.str().c_str(), verbose > 0);
            }

            strChecksum = STR_TRUE;
            ctx = -1;
            continue;
        }

        if (arg == "--no-file-reorder") {
            if (ctx != -1) {
                stringstream ss;
                ss << "Warning: ignoring option [" << CMD_LINE_ARGS[ctx] << "] with no value.";
                log.println(ss.str().c_str(), verbose > 0);
            }

            strReorder = STR_FALSE;
            ctx = -1;
            continue;
        }

        if (ctx == -1) {
            for (int j = 0; j < 10; j++) {
                if (arg == CMD_LINE_ARGS[j]) {
                    ctx = j;
                    break;
                }
            }

            if (ctx != -1) 
                continue;
        }

        if ((arg.compare(0, 9, "--output=") == 0) || (ctx == ARG_IDX_OUTPUT)) {
            string name = (arg.compare(0, 9, "--output=") == 0) ? arg.substr(9) : arg;
            int j = int(arg.length() - 1);

            while ((j > 0) && (arg[j] == 0x20)) // trim only spaces
                j--;

            arg = arg.substr(0, j + 1);

            if (outputName != "") {
                cout << "Warning: ignoring duplicate output name: " << name << endl;
            } else {
                if ((name[0] == '.') && (name[1] == PATH_SEPARATOR)) 
                    name = name.substr(2);

                outputName = name;
            }

            ctx = -1;
            continue;
        }

        if ((arg.compare(0, 8, "--input=") == 0) || (ctx == ARG_IDX_INPUT)) {
            string name = (arg.compare(0, 8, "--input=") == 0) ? arg.substr(8) : arg;
            int j = int(arg.length() - 1);

            while ((j > 0) && (arg[j] == 0x20)) // trim only spaces
                j--;

            arg = arg.substr(0, j + 1);

            if (inputName != "") {
                cout << "Warning: ignoring duplicate input name: " << name << endl;
            } else {
                if ((name[0] == '.') && (name[1] == PATH_SEPARATOR)) 
                    name = name.substr(2);

                inputName = name;
            }

            ctx = -1;
            continue;
        }

        if ((arg.compare(0, 10, "--entropy=") == 0) || (ctx == ARG_IDX_ENTROPY)) {
            string name = (arg.compare(0, 10, "--entropy=") == 0) ? arg.substr(10) : arg;
            name = trim(name);

            if (codec != "") {
                cout << "Warning: ignoring duplicate entropy: " << name << endl;
            } else {
                if (name.length() == 0) {
                    cerr << "Invalid empty entropy provided on command line" << endl;
                    return Error::ERR_INVALID_PARAM;
                }

                codec = name;
                transform(codec.begin(), codec.end(), codec.begin(), ::toupper);
            }

            ctx = -1;
            continue;
        }

        if ((arg.compare(0, 12, "--transform=") == 0) || (ctx == ARG_IDX_TRANSFORM)) {
            string name = (arg.compare(0, 12, "--transform=") == 0) ? arg.substr(12) : arg;
            name = trim(name);

            if (transf != "") {
                cout << "Warning: ignoring duplicate transform: " << name << endl;
            } else {
                if (name.length() == 0) {
                    cerr << "Invalid empty transform provided on command line" << endl;
                    return Error::ERR_INVALID_PARAM;
                }

                transf = name;
                transform(transf.begin(), transf.end(), transf.begin(), ::toupper);
            }

            while ((transf.length() > 0) && (transf[0] == '+')) {
                transf = transf.substr(1);
            }

            while ((transf.length() > 0) && (transf[transf.length() - 1] == '+')) {
                transf = transf.substr(0, transf.length() - 1);
            }

            ctx = -1;
            continue;
        }

        if ((arg.compare(0, 8, "--level=") == 0) || (ctx == ARG_IDX_LEVEL)) {
            string name = (arg.compare(0, 8, "--level=") == 0) ? arg.substr(8) : arg;
            name = trim(name);

            if (strLevel != "-1") {
                cout << "Warning: ignoring duplicate level: " << name << endl;
            } else {
                if (name.length() != 1) {
                    cerr << "Invalid compression level provided on command line: " << arg << endl;
                    return Error::ERR_INVALID_PARAM;
                }

                strLevel = name;
                level = atoi(strLevel.c_str());

                if (((level < 0) || (level > 9)) || ((level == 0) && (strLevel != "0"))) {
                    cerr << "Invalid compression level provided on command line: " << arg << endl;
                    return Error::ERR_INVALID_PARAM;
                }
            }

            ctx = -1;
            continue;
        }

        if ((arg.compare(0, 8, "--block=") == 0) || (ctx == ARG_IDX_BLOCK)) {
            string name = (arg.compare(0, 8, "--block=") == 0) ? arg.substr(8) : arg;
            name = trim(name);

            if (strBlockSize != "") {
                cout << "Warning: ignoring duplicate block size: " << name << endl;
                ctx = -1;
                continue;
            }

            transform(name.begin(), name.end(), name.begin(), ::toupper);
            char lastChar = (name.length() == 0) ? ' ' : name[name.length() - 1];
            uint64 scale = 1;

            // Process K or M or G suffix
            if ('K' == lastChar) {
                scale = 1024;
                name = name.substr(0, name.length() - 1);
            }
            else if ('M' == lastChar) {
                scale = 1024 * 1024;
                name = name.substr(0, name.length() - 1);
            }
            else if ('G' == lastChar) {
                scale = 1024 * 1024 * 1024;
                name = name.substr(0, name.length() - 1);
            }

            size_t k = 0;

            while ((k < name.length()) && (name[k] == '0'))
                k++;

            name = name.substr(k);

            if (name.length() == 0) {
                cerr << "Invalid block size provided on command line: " << arg << endl;
                return Error::ERR_INVALID_PARAM;
            }

            uint64 bk;
            stringstream ss1;
            ss1 << name;
            ss1 >> bk;

            if (scale != 1) {
                k = 0;

                while ((k < name.length()) && ((name[k] >= '0') && (name[k] <= '9')))
                    k++;
		
                if (k < name.length()) {
                    cerr << "Invalid block size provided on command line: " << arg << endl;
                    return Error::ERR_INVALID_PARAM;
                }
            }

            bk *= scale;
            stringstream ss2;
            ss2 << bk;
            ss2 >> strBlockSize;

            if ((scale == 1) && (strBlockSize.compare(ss1.str()) != 0)) {
                cerr << "Invalid block size provided on command line: " << arg << endl;
                return Error::ERR_INVALID_PARAM;
            }

            ctx = -1;
            continue;
        }

        if ((arg.compare(0, 7, "--jobs=") == 0) || (ctx == ARG_IDX_JOBS)) {
            string name = (arg.compare(0, 7, "--jobs=") == 0) ? arg.substr(7) : arg;
            name = trim(name);

            if (strTasks != "0") {
                cout << "Warning: ignoring duplicate jobs: " << name << endl;
                ctx = -1;
                continue;
            }

            if ((name.length() != 1) && (name.length() != 2)) {
                cerr << "Invalid number of jobs provided on command line: " << arg << endl;
                return Error::ERR_INVALID_PARAM;
            }

            strTasks = name;
            int tasks = atoi(strTasks.c_str());

            if (tasks < 1) {
                cerr << "Invalid number of jobs provided on command line: " << arg << endl;
                return Error::ERR_INVALID_PARAM;
            }

            ctx = -1;
            continue;
        }

        if ((arg.compare(0, 7, "--from=") == 0) && (ctx == -1)) {
            string name = (arg.compare(0, 7, "--from=") == 0) ? arg.substr(7) : arg;
            name = trim(name);

            if (strFrom != "") {
                cout << "Warning: ignoring duplicate start block: " << name << endl;
            } else {
                strFrom = name;
                from = atoi(strFrom.c_str());

                if ((from < 0) || ((from == 0) && (strFrom != "0"))) {
                    cerr << "Invalid start block provided on command line: " << arg << endl;

                    if (from == 0) {
                        cerr << "The first block ID is 1." << endl;
                    }

                    return Error::ERR_INVALID_PARAM;
                }
            }

            continue;
        }

        if ((arg.compare(0, 5, "--to=") == 0) && (ctx == -1)) {
            string name = (arg.compare(0, 5, "--to=") == 0) ? arg.substr(5) : arg;
            name = trim(name);

            if (strTo != "") {
                cout << "Warning: ignoring duplicate end block: " << name << endl;
            } else {
                strTo = name;
                to = atoi(strTo.c_str());

                if (to <= 0) { // Must be > 0 (0 means nothing to do)
                    cerr << "Invalid end block provided on command line: " << arg << endl;
                    return Error::ERR_INVALID_PARAM;
                }
            }

            continue;
        }

        if ((arg.compare(0, 10, "--verbose=") != 0) && (ctx == -1) && (arg.compare(0, 9, "--output=") != 0)) {
            stringstream ss;
            ss << "Warning: ignoring unknown option [" << arg << "]";
            log.println(ss.str().c_str(), verbose > 0);
        }

        ctx = -1;
    }

    if (inputName.length() == 0) {
        cerr << "Missing input file name, exiting ..." << endl;
        return Error::ERR_MISSING_PARAM;
    }

    if (ctx != -1) {
        stringstream ss;
        ss << "Warning: ignoring option with missing value [" << CMD_LINE_ARGS[ctx] << "]";
        log.println(ss.str().c_str(), verbose > 0);
    }

    if (level >= 0) {
        if (codec.length() > 0) {
            stringstream ss;
            ss << "Warning: providing the 'level' option forces the entropy codec. Ignoring [" << codec << "]";
            log.println(ss.str().c_str(), verbose > 0);
        }

        if (transf.length() > 0) {
            stringstream ss;
            ss << "Warning: providing the 'level' option forces the transform. Ignoring [" << transf << "]";
            log.println(ss.str().c_str(), verbose > 0);
        }
    }

    if ((strFrom != "") || (strTo != "")) {
        if (mode != "d"){
            log.println("Warning: ignoring start/end block (only valid for decompression)", verbose > 0);
            from = -1;
            to = -1;
        }
    }

    if (strBlockSize.length() > 0)
        map["block"] = strBlockSize;

    map["verbose"] = strVerbose;
    map["mode"] = mode;

    if ((mode == "c") && (strLevel != "-1"))
        map["level"] = strLevel;

    if (strOverwrite == STR_TRUE)
        map["overwrite"] = STR_TRUE;

    map["inputName"] = inputName;
    map["outputName"] = outputName;

    if (codec.length() > 0)
        map["entropy"] = codec;

    if (transf.length() > 0)
        map["transform"] = transf;

    if (strChecksum == STR_TRUE)
        map["checksum"] = STR_TRUE;

    if (strSkip == STR_TRUE)
        map["skipBlocks"] = STR_TRUE;

    if (strReorder == STR_FALSE)
        map["fileReorder"] = STR_FALSE;

    if (from >= 0)
        map["from"] = strFrom;

    if (to >= 0)
        map["to"] = strTo;

    map["jobs"] = strTasks;
    return 0;
}

int main(int argc, const char* argv[])
{
    map<string, string> args;
    int status = processCommandLine(argc, argv, args);

    // Command line processing error ?
    if (status != 0)
       exit(status);

    map<string, string>::iterator it = args.find("mode");

    // Help mode only ?
    if (it == args.end())
       exit(0);

    string mode = it->second;
    args.erase(it);

    if (mode == "c") {
        try {
            BlockCompressor bc(args);
            uint64 written = 0;
            int code = bc.compress(written);
            exit(code);
        }
        catch (exception& e) {
            cerr << "Could not create the compressor: " << e.what() << endl;
            exit(Error::ERR_CREATE_COMPRESSOR);
        }
    }

    if (mode == "d") {
        try {
            BlockDecompressor bd(args);
            uint64 read = 0;
            int code = bd.decompress(read);
            exit(code);
        }
        catch (exception& e) {
            cerr << "Could not create the decompressor: " << e.what() << endl;
            exit(Error::ERR_CREATE_DECOMPRESSOR);
        }
    }

    cout << "Missing arguments: try --help or -h" << endl;
    return Error::ERR_MISSING_PARAM;
}
