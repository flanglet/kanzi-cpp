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

#ifndef _Context_
#define _Context_

#include <sstream>
#include <string>
#include "types.hpp"

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
       Context() {}
       Context(const Context& ctx);
       Context& operator=(const Context& ctx);
       virtual ~Context() {}

       bool has(const std::string& key);
       int getInt(const std::string& key, int defValue=0);
       int64 getLong(const std::string& key, int64 defValue=0);
       const char* getString(const std::string& key, const std::string& defValue="");
       void putInt(const std::string& key, int value);
       void putLong(const std::string& key, int64 value);
       void putString(const std::string& key, const std::string& value);


   private:
       CTX_MAP<std::string, std::string> _map;
   };


   inline Context::Context(const Context& ctx)
      : _map(ctx._map)
   {
   }


   inline Context& Context::operator=(const Context& ctx)
   {
      _map = ctx._map;
      return*this;
   }


   inline bool Context::has(const std::string& key)
   {
      return _map.find(key) != _map.end();
   }


   inline int Context::getInt(const std::string& key, int defValue)
   {
      CTX_MAP<std::string, std::string>::iterator it = _map.find(key);

      if (it == _map.end())
          return defValue;

      std::stringstream ss;
      int res;
      ss << it->second.c_str();
      ss >> res;
      return res;
   }


   inline int64 Context::getLong(const std::string& key, int64 defValue)
   {
      CTX_MAP<std::string, std::string>::iterator it = _map.find(key);

      if (it == _map.end())
          return defValue;

      std::stringstream ss;
      int64 res;
      ss << it->second.c_str();
      ss >> res;
      return res;
   }


   inline const char* Context::getString(const std::string& key, const std::string& defValue)
   {
      CTX_MAP<std::string, std::string>::iterator it = _map.find(key);
      return (it == _map.end()) ? defValue.c_str() : it->second.c_str();
   }


   inline void Context::putInt(const std::string& key, int value)
   {
      std::stringstream ss;
      ss << value;
      _map[key] = ss.str();
   }


   inline void Context::putLong(const std::string& key, int64 value)
   {
      std::stringstream ss;
      ss << value;
      _map[key] = ss.str();
   }


   inline void Context::putString(const std::string& key, const std::string& value)
   {
      _map[key] = value;
   }

}
#endif