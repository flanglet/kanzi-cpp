/*
Copyright 2011-2024 Frederic Langlet
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
#ifndef _Context_
#define _Context_

#include <sstream>
#include <string>
#include "concurrent.hpp"
#include "util/strings.hpp"

#if __cplusplus >= 201103L
   #include <unordered_map>
   #define CTX_MAP std::unordered_map
#else
   #include <map>
   #define CTX_MAP std::map
#endif

namespace kanzi
{

   class Context
   {
   public:

#ifdef CONCURRENCY_ENABLED
    #if defined(WIN32) || defined(_WIN32) || defined(_WIN64)
       // Windows already has a built-in threadpool. Using it is better for performance.
       Context(const ThreadPool*) { _pool = nullptr; }
       Context(const Context>& c, const ThreadPool*) : _map(c._map) { _pool = nullptr; }
       Context() { _pool = nullptr; }
       Context(const Context& c) : _map(c._map) { _pool = nullptr; }
#else
           Context(ThreadPool* p = nullptr) : _pool(p) {}
           Context(const Context& c, ThreadPool* p = nullptr) : _map(c._map), _pool(p) {}
    #endif
#else
       Context() {}
       Context(const Context& c) : _map(c._map) {}
#endif

       bool has(const std::string& key) const;
       int getInt(const std::string& key, int defValue = 0) const;
       int64 getLong(const std::string& key, int64 defValue = 0) const;
       std::string getString(const std::string& key, const std::string& defValue = "") const;
       void putInt(const std::string& key, int value);
       void putLong(const std::string& key, int64 value);
       void putString(const std::string& key, const std::string& value);

#ifdef CONCURRENCY_ENABLED
       ThreadPool* getPool() const { return _pool; }
#endif

   private:
       CTX_MAP<std::string, std::string> _map;

#ifdef CONCURRENCY_ENABLED
       ThreadPool* _pool;
#endif
   };


   inline bool Context::has(const std::string& key) const
   {
      return _map.find(key) != _map.end();
   }


   inline int Context::getInt(const std::string& key, int defValue) const
   {
      CTX_MAP<std::string, std::string>::const_iterator it = _map.find(key);

      if (it == _map.end())
          return defValue;

      std::stringstream ss;
      int res;
      ss << it->second;
      ss >> res;
      return res;
   }


   inline int64 Context::getLong(const std::string& key, int64 defValue) const
   {
      CTX_MAP<std::string, std::string>::const_iterator it = _map.find(key);

      if (it == _map.end())
          return defValue;

      std::stringstream ss;
      int64 res;
      ss << it->second;
      ss >> res;
      return res;
   }


   inline std::string Context::getString(const std::string& key, const std::string& defValue) const
   {
      CTX_MAP<std::string, std::string>::const_iterator it = _map.find(key);
      return (it != _map.end()) ? it->second : defValue;
   }


   inline void Context::putInt(const std::string& key, int value)
   {
      _map[key] = TOSTR(value);
   }


   inline void Context::putLong(const std::string& key, int64 value)
   {
      _map[key] = TOSTR(value);
   }


   inline void Context::putString(const std::string& key, const std::string& value)
   {
      _map[key] = value;
   }

}
#endif

