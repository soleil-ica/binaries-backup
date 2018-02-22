//*****************************************************************************
/// Synchrotron SOLEIL
///
/// NeXus C++ API over NAPI
///
/// Creation : 2010/07/18
/// Author   : Stephane Poirier
///
/// This program is free software; you can redistribute it and/or modify it under
/// the terms of the GNU General Public License as published by the Free Software
/// Foundation; version 2 of the License.
/// 
/// This program is distributed in the hope that it will be useful, but WITHOUT 
/// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
/// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
///
//*****************************************************************************

#ifndef __NX_TOOLS_H__
#define __NX_TOOLS_H__

// standard library objets
#include <iostream>
#include <iomanip>
#include <vector>

// YAT
#include "yat/threading/Utilities.h"
#include "yat/threading/Mutex.h"

namespace nxcpp
{

#ifdef NX_DEBUG
  #define DBG_PREFIX(x) "[" << std::hex << std::setfill(' ') << std::setw(10)\
                     << (void*)(x) << "][" << std::setfill('0') << std::setw(8)\
                     << yat::ThreadingUtilities::self() << "] " << std::dec

  class dbg_helper
  {
    private:
      std::string _s;
      void *_this_object;
    public:
      dbg_helper(const std::string &s, void* this_object) : _s(s), _this_object(this_object)
      {
        yat::AutoMutex<> lock(mutex());
        std::cout << DBG_PREFIX(_this_object) << indent() << "> " << _s << std::endl;
        indent().append(2, ' ');
      }
      ~dbg_helper()
      {
        yat::AutoMutex<> lock(mutex());
        indent().erase(0,2);
        std::cout << DBG_PREFIX(_this_object) << indent() << "< " << _s << std::endl;
      }
      static std::string& indent()
      {
        static std::string s_indent;
        return s_indent; 
      }
      static yat::Mutex& mutex()
      { 
        static yat::Mutex s_mtx;
        return s_mtx; 
      }
  };
  #define NX_SCOPE_DBG(s) dbg_helper _scope_dbg(s, (void*)this)
  #define NX_DBG(s) \
  { \
    yat::AutoMutex<> lock(dbg_helper::mutex()); \
    std::cout << DBG_PREFIX((void*)this) << dbg_helper::indent() << s << std::endl; \
  }

#else
  #define NX_DBG(s)
  #define NX_SCOPE_DBG(s)
#endif

}

#endif
