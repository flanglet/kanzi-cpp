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

#ifndef _Context_
#define _Context_

#include <map>
#include <sstream>
#include <string>
#include "types.hpp"

namespace kanzi 
{

   class Context
   {
   public:
       Context() {};
       Context(Context& ctx);
       Context(std::map<std::string, std::string>& ctx);
       ~Context() {};

       bool has(const std::string& key);
       int getInt(const std::string& key, int defValue=0);
       int64 getLong(const std::string& key, int64 defValue=0);
       const char* getString(const std::string& key, const std::string& defValue="");
       void putInt(const std::string& key, int value);
       void putLong(const std::string& key, int64 value);
       void putString(const std::string& key, const std::string& value);

   private:
       std::map<std::string, std::string> _map;

   };


   inline Context::Context(Context& ctx)
      : _map(ctx._map)
   {
   }


   inline Context::Context(std::map<std::string, std::string>& ctx)
      : _map(ctx)
   {
   }


   inline bool Context::has(const std::string& key) 
   {
      return _map.find(key) != _map.end();
   }


   inline int Context::getInt(const std::string& key, int defValue) 
   {
      std::map<std::string, std::string>::iterator it = _map.find(key);

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
      std::map<std::string, std::string>::iterator it = _map.find(key);

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
      std::map<std::string, std::string>::iterator it = _map.find(key);
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