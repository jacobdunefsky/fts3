/* Copyright @ Members of the EMI Collaboration, 2010.
See www.eu-emi.eu for details on the copyright holders.

Licensed under the Apache License, Version 2.0 (the "License"); 
you may not use this file except in compliance with the License. 
You may obtain a copy of the License at 

    http://www.apache.org/licenses/LICENSE-2.0 

Unless required by applicable law or agreed to in writing, software 
distributed under the License is distributed on an "AS IS" BASIS, 
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
See the License for the specific language governing permissions and 
limitations under the License. */

/** \file threadtraits.h
 * Define the synchronization/threading Strategy 
 * (http://en.wikipedia.org/wiki/Strategy_pattern). It isolates the common
 * threading constructs (mutexes, locks, etc.) from the implementing technology
 * (for example, Boost threads, pthreads, etc.).
 *
 * Usage: include the file that implements the
 * traits. The rest of the code should include only this file, and
 * use the ThreadTraits type as a template parameter, typedef, etc. 
 * 
 * The file implementing the strategy should check if the __THREADTRAITS_H_GUARD__
 * is defined or not. If not defined, it indicates that the file was included
 * from elsewhere than this file. It is normally an unwanted thing.
 *  
 * For example, see the BoostThreadTraits.h.
 */

#ifndef FTS_COMMON_THREADTRAITS_H
#define FTS_COMMON_THREADTRAITS_H

#ifdef __FTS3_COMMON_THREADTRAITS_H_GUARD__
  #error __FTS3_COMMON_THREADTRAITS_H_GUARD__ should not be defined here!
#endif

#define __FTS3_COMMON_THREADTRAITS_H_GUARD__
// include the file defining the threading strategy here. Including it 
// elsewhere should result in compilation error.
#include "boostthreadtraits.h"
#undef __FTS3_COMMON_THREADTRAITS_H_GUARD__

#endif /* FTS_COMMON_THREADTRAITS_H */

