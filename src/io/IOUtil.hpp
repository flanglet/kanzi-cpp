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

#pragma once
#ifndef _IOUtil_
#define _IOUtil_

#include <algorithm>
#include <errno.h>
#include <ios>
#include <vector>

#ifdef _MSC_VER
#include "../msvc_dirent.hpp"
#include <direct.h>
#else
#include <dirent.h>
#endif

#include "../util.hpp"


struct FileData {
      std::string _path;
      std::string _name;
      int64 _size;
      int64 _modifTime;

      FileData(std::string& path, int64 size, int64 _modifTime = 0) 
         : _size(size)
         , _modifTime(_modifTime)
      {
         int idx = int(path.find_last_of(PATH_SEPARATOR));

         if (idx > 0) {
            _path = path.substr(0, idx + 1);
            _name = path.substr(idx + 1);
         } 
         else {
            _path = "";
            _name = path;
         }
      }

      std::string fullPath() const {
         return (_path.length() == 0) ? _name : _path + _name;
      }
};


struct FileListConfig
{
   bool _recursive;
   bool _followLinks;
   bool _continueOnErrors;
   bool _ignoreDotFiles;
};


static inline void createFileList(std::string& target, std::vector<FileData>& files, FileListConfig cfg, 
                                  std::vector<std::string>& errors)
{
    if (target.size() == 0)
        return;

    if (target[target.size() - 1] == PATH_SEPARATOR)
        target = target.substr(0, target.size() - 1);

    struct STAT buffer;
    int res = (cfg._followLinks) ? STAT(target.c_str(), &buffer) : LSTAT(target.c_str(), &buffer);

    if (res != 0) {
        std::stringstream ss;
        ss << "Cannot access input file '" << target << "'";
        errors.push_back(ss.str());

        if (cfg._continueOnErrors)
           return;
    }

    if ((buffer.st_mode & S_IFREG) != 0) {
        // Target is regular file
        if (cfg._ignoreDotFiles == true) {
           int idx = target.rfind(PATH_SEPARATOR);

           if (idx > 0) {
               std::string shortName = target.substr(idx + 1);

               if (shortName[0] == '.')
                  return;
           }
        }

        files.push_back(FileData(target, buffer.st_size, buffer.st_mtime));
        return;
    }

    if ((buffer.st_mode & S_IFDIR) == 0) {
        // Target is neither regular file nor directory, ignore
        return;
    }

    if (cfg._recursive) {
       target += PATH_SEPARATOR;
    } 
    else {
       target = target.substr(0, target.size() - 1);
    }

    DIR* dir = opendir(target.c_str());

    if (dir != nullptr) {
        struct dirent* ent;

        while ((ent = readdir(dir)) != nullptr) {
            std::string dirName = ent->d_name;

            if ((dirName == ".") || (dirName == ".."))
               continue;

            std::string fullpath = target + dirName;
            res = (cfg._followLinks) ? STAT(fullpath.c_str(), &buffer) :
               LSTAT(fullpath.c_str(), &buffer);

            if (res != 0) {
                std::stringstream ss;
                ss << "Cannot access input file '" << fullpath << "'";
                errors.push_back(ss.str());

                if (cfg._continueOnErrors)
                    return;
            }

            //if ((dirName != ".") && (dirName != ".."))
            {
               if ((buffer.st_mode & S_IFREG) != 0) {
                  // Target is regular file
                  if (cfg._ignoreDotFiles == true) {
                     int idx = fullpath.rfind(PATH_SEPARATOR);

                     if (idx > 0) {
                         std::string shortName = fullpath.substr(idx + 1);

                         if (shortName[0] == '.')
                            continue;
                     }
                  }
                 
                  files.push_back(FileData(fullpath, buffer.st_size, buffer.st_mtime));
               }
               else if ((cfg._recursive) && ((buffer.st_mode & S_IFDIR) != 0)) {
                  if (cfg._ignoreDotFiles == true) {
                     int idx = fullpath.rfind(PATH_SEPARATOR);

                     if (idx > 0) {
                         std::string shortName = fullpath.substr(idx + 1);

                         if (shortName[0] == '.')
                            continue;
                     }
                  }
                   
                  createFileList(fullpath, files, cfg, errors);
               }
            }
        }

        closedir(dir);
    }
    else {
        std::stringstream ss;
        ss << "Cannot read directory '" << target << "'";
        errors.push_back(ss.str());
    }
}


struct FileDataComparator
{
    bool _sortBySize;

    bool operator() (const FileData& f1, const FileData& f2)
    {
        if (_sortBySize == false)
           return f1.fullPath() < f2.fullPath();

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
    for (uint i = 1; i < path.size(); i++) {
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