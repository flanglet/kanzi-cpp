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

using namespace std;

namespace kanzi 
{

   class Context
   {
   public:
       Context() {};
       Context(Context& ctx);
       Context(map<string, string>& ctx);
       ~Context() {};

       bool has(const string& key);
       int getInt(const string& key, int defValue=0);
       const char* getString(const string& key, const string& defValue="");
       void putInt(const string& key, int value);
       void putString(const string& key, const string& value);

   private:
       map<string, string> _map;

   };


   inline Context::Context(Context& ctx)
      : _map(ctx._map)
   {
   }


   inline Context::Context(map<string, string>& ctx)
      : _map(ctx)
   {
   }


   inline bool Context::has(const string& key) 
   {
      map<string, string>::iterator it = _map.find(key);
      return it != _map.end();
   }


   inline int Context::getInt(const string& key, int defValue) 
   {
      map<string, string>::iterator it = _map.find(key);
      return (it == _map.end()) ? defValue : atoi(it->second.c_str());
   }


   inline const char* Context::getString(const string& key, const string& defValue) 
   {
      map<string, string>::iterator it = _map.find(key);
      return (it == _map.end()) ? defValue.c_str() :  it->second.c_str();
   }


   inline void Context::putInt(const string& key, int value) 
   {
      stringstream ss;
      ss << value;
      _map[key] = ss.str();
   }


   inline void Context::putString(const string& key, const string& value)
   {
      _map[key] = value;
   }
}
#endif