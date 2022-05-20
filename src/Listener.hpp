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

#pragma once
#ifndef _Listener_
#define _Listener_

#include "Event.hpp"

namespace kanzi
{

   class Listener
   {
   public:
       Listener(){}

       virtual void processEvent(const Event& evt) = 0;

       virtual ~Listener(){}
   };

}
#endif