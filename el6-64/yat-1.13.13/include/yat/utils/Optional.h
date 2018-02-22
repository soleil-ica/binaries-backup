//----------------------------------------------------------------------------
// Copyright (c) 2004-2015 Synchrotron SOLEIL
// All rights reserved. This program and the accompanying materials
// are made available under the terms of the GNU Lesser Public License v3
// which accompanies this distribution, and is available at
// http://www.gnu.org/licenses/lgpl.html
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
// YAT LIBRARY
//----------------------------------------------------------------------------
//
// Copyright (C) 2006-2014 The Tango Community
//
// Part of the code comes from the ACE Framework (asm bytes swaping code)
// see http://www.cs.wustl.edu/~schmidt/ACE.html for more about ACE
//
// The thread native implementation has been initially inspired by omniThread
// - the threading support library that comes with omniORB. 
// see http://omniorb.sourceforge.net/ for more about omniORB.
// The YAT library is free software; you can redistribute it and/or modify it 
// under the terms of the GNU General Public License as published by the Free 
// Software Foundation; either version 2 of the License, or (at your option) 
// any later version.
//
// The YAT library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
// Public License for more details.
//
// See COPYING file for license details 
//
// Contact:
//    Nicolas Leclercq
//    Synchrotron SOLEIL
//------------------------------------------------------------------------------
/*!
 * \author See AUTHORS file
 */

#ifndef _YAT_OPTIONAL_H_
#define _YAT_OPTIONAL_H_

namespace yat
{

// ============================================================================
//! \class Optional<T>
//! \brief Manage an optional contained value, i.e. a value that may or may not be present.
//! This class is directly inspired by the std::optional class defined in C++17
//! See http://en.cppreference.com/w/cpp/experimental/optional
// ============================================================================
template<typename T>
class Optional 
{

  typedef void (Optional::*bool_type)() const;

  void this_type_does_not_support_comparisons() const {}

public:

  Optional() : m_value(), m_set(false) {}
  template<typename U> Optional(const U& value) : m_value(value), m_set(true) {}
  template<typename U> Optional(const Optional<U>& opt) : m_value(*opt), m_set(opt.is_set()) {}

  inline bool is_set() const { return m_set; }

  operator bool_type() const
  {
    return m_set ? &Optional::this_type_does_not_support_comparisons : NULL;
  }

  inline const T& operator*() const { return m_value; }
  inline T& operator*() { return m_value; }
  inline const T* operator->() const { return &m_value; }
  inline T* operator->() { return &m_value; }

  //! \brief Returns the contained value 
  const T& value() const
  {
    if( !m_set )
      throw yat::Exception("NO_VALUE", "Optional value is empty", "Optional::value");
    return m_value;
  }

  //! \brief Returns the contained value if available, another value otherwise 
  const T& value_or(const T& default_value) const
  {
    return m_set ? m_value : default_value;
  }

  template<typename U>
  void set_default(const U& value)
  {
    if( !m_set )
      *this = value;
  }

private:

  T m_value;
  bool m_set;
};

template <typename U, typename V>
bool operator!=(const Optional<U>& lhs, const V&)
{
  lhs.this_type_does_not_support_comparisons();
  return false;
}

template <typename U, typename V>
bool operator==(const Optional<U>& lhs, const V&)
{
  lhs.this_type_does_not_support_comparisons();
  return false;
};

} // namespace

#endif // _YAT_OPTIONAL_H_
