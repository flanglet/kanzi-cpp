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

#ifndef _IOUtil_
#define _IOUtil_

#include <errno.h>
#include <sys/stat.h>
#include <algorithm>
#include <vector>
#include "IOException.hpp"
#include "../types.hpp"
#include "../Error.hpp"

#ifdef _MSC_VER
#include "../msvc_dirent.hpp"
#include <direct.h>
#else
#include <dirent.h>
#endif


using namespace kanzi;

class FileData {
   public:
      std::string _fullPath;
      std::string _path;
      std::string _name;
      int64 _size;

      FileData(std::string& path, int64 size) : _fullPath(path), _size(size)
      {
         int idx = int(_fullPath.find_last_of(PATH_SEPARATOR));

         if (idx > 0) {
            _path = _fullPath.substr(0, idx+1);
            _name = _fullPath.substr(idx+1);
         } else {
            _path = "";
            _name = _fullPath;
         }
      }

      ~FileData() {}
};


static void createFileList(std::string& target, std::vector<FileData>& files) THROW
{
    struct stat buffer;

    if (target[target.size()-1] == PATH_SEPARATOR)
        target = target.substr(0, target.size()-1);

    if (stat(target.c_str(), &buffer) != 0) {
        std::stringstream ss;
        ss << "Cannot access input file '" << target << "'";
        throw IOException(ss.str(), Error::ERR_OPEN_FILE);
    }

    if ((buffer.st_mode & S_IFREG) != 0) {
        // Target is regular file
        if (target[0] != '.')
           files.push_back(FileData(target, buffer.st_size));

        return;
    }

    if ((buffer.st_mode & S_IFDIR) == 0) {
        // Target is neither regular file nor directory
        std::stringstream ss;
        ss << "Invalid file type '" << target << "'";
        throw IOException(ss.str(), Error::ERR_OPEN_FILE);
    }

    bool isRecursive = (target.size() <= 2) || (target[target.size()-1] != '.') ||
               (target[target.size()-2] != PATH_SEPARATOR);

    if (isRecursive) {
       if (target[target.size()-1] != PATH_SEPARATOR) {
          std::stringstream ss;
          ss << target << PATH_SEPARATOR;
          target = ss.str();
       }
    } else {
       target = target.substr(0, target.size()-1);
    }

    DIR* dir = opendir(target.c_str());

    if (dir != nullptr) {
        struct dirent* ent;

        while ((ent = readdir(dir)) != nullptr) {
            std::string fullpath = target + ent->d_name;

            if (stat(fullpath.c_str(), &buffer) != 0) {
                std::stringstream ss;
                ss << "Cannot access input file '" << target << ent->d_name << "'";
                throw IOException(ss.str(), Error::ERR_OPEN_FILE);
            }

            if (ent->d_name[0] != '.')
            {
               if ((buffer.st_mode & S_IFREG) != 0){
                   files.push_back(FileData(fullpath, buffer.st_size));
               }
               else if ((isRecursive) && ((buffer.st_mode & S_IFDIR) != 0)) {
                   createFileList(fullpath, files);
               }
            }
        }

        closedir(dir);
    }
    else {
        std::stringstream ss;
        ss << "Cannot read directory '" << target << "'";
        throw IOException(ss.str(), Error::ERR_READ_FILE);
    }
}


struct FileDataComparator
{
    bool _sortBySize;

    bool operator() (const FileData& f1, const FileData& f2)
    {
        if (_sortBySize == false)
           return f1._fullPath < f2._fullPath;

        // First, compare parent directory paths
        if (f1._path != f2._path)
           return f1._path < f2._path;

        // Then compare file sizes (decreasing order)
        return f1._size > f2._size;
    }
};


static void sortFilesByPathAndSize(std::vector<FileData>& files, bool sortBySize = false)
{
    FileDataComparator c = { sortBySize };
    sort(files.begin(), files.end(), c);
}


static int mkdirAll(const std::string& path) {
    errno = 0;

    // Scan path, ignoring potential PATH_SEPARATOR at position 0
    for (uint i=1; i<path.size(); i++) {
        if (path[i] == PATH_SEPARATOR) {
            std::string curPath = path.substr(0, i);

#if defined(_MSC_VER)
            if (_mkdir(curPath.c_str()) != 0) {
#elif defined(__MINGW32__)
            if (mkdir(curPath.c_str()) != 0) {
#else
            if (mkdir(curPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0) {
#endif
                if (errno != EEXIST)
                    return -1;
            }
        }
    }

#if defined(_MSC_VER)
    if (_mkdir(path.c_str()) != 0) {
#elif defined(__MINGW32__)
    if (mkdir(path.c_str()) != 0) {
#else
    if (mkdir(path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0) {
#endif
        if (errno != EEXIST)
            return -1;
    }

    return 0;
}

#endif