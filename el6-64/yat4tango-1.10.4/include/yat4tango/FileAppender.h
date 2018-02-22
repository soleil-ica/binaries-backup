//----------------------------------------------------------------------------
// Copyright (c) 2004-2016 The Tango Community
// All rights reserved. This program and the accompanying materials
// are made available under the terms of the GNU Lesser Public License v3
// which accompanies this distribution, and is available at
// http://www.gnu.org/licenses/lgpl.html
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
// YAT4Tango LIBRARY
//----------------------------------------------------------------------------
//
// Copyright (C) 2006-2016 The Tango Community
//
// The YAT4Tango library is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as published
// by the Free Software Foundation; either version 2 of the License, or (at
// your option) any later version.
//
// The YAT4Tango library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
// Public License for more details.
//
// See COPYING file for license details
//
// Contact:
//      Nicolas Leclercq
//      Synchrotron SOLEIL
//------------------------------------------------------------------------------
/*!
 * \authors See AUTHORS file
 */

#ifndef _FILE_APPENDER_H_
#define _FILE_APPENDER_H_

// ============================================================================
// DEPENDENCIES
// ============================================================================
#include <deque>
#include <map>
#include <tango.h>
#include <yat/threading/Mutex.h>
#include <yat/time/Time.h>
#include <yat/memory/UniquePtr.h>
#include <yat4tango/DynamicAttributeManager.h>

namespace yat4tango
{

// ============================================================================
//! Default log buffer depth
// ============================================================================
#define DEFAULT_LOG_DEPTH 512

// ============================================================================
//! Default log files path
// ============================================================================
#define DEFAULT_LOG_PATH "/var/log/tango_devices"

// ============================================================================
//! \class FileAppender
//! \brief The appender for a Tango Device internal activity.
//!
//! This class provides a way to create an appender to log the internal activity
//! of a Tango Device in a cyclic buffer. This buffer is implemented as a
//! dynamic attribute of the Device (named "log", which type is SPECTRUM of strings).
//!
//! \b Usage:
//! - Initialize the appender in the device initialization function (i.e. the *init_device()*
//! function). This is the first thing to do before any other action. For instance:
//! \verbatim yat4tango::FileAppender::initialize(this); \endverbatim
//!
//! - Release the appender in the device destructor (i.e. the *delete_device()* function).
//! This is the last thing to do before removing the device. For instance:
//! \verbatim yat4tango::FileAppender::release(this); \endverbatim
//!
//! Inherits from log4tango::Appender class.
//!
// ============================================================================
class YAT4TANGO_DECL FileAppender : public log4tango::Appender
{
public:
  //! \brief Initialization of the inner appender.
  //!
  //! \param associated_device The Tango device to log.
  //! \param max_log_buffer_depth
  //! \exception DEVICE_ERROR Thrown when an error occurs while initializing the logger.
  static void initialize(Tango::DeviceImpl* associated_device,
                          std::size_t max_log_depth = DEFAULT_LOG_DEPTH,
                          const std::string& path = DEFAULT_LOG_PATH);

  //! \brief Termination of the inner appender.
  //!
  //! \param associated_device The associated Tango device.
  //! \exception DEVICE_ERROR Thrown when an error occurs while releasing the logger.
  static void release(Tango::DeviceImpl* associated_device);

  //! \brief Always returns false!
  //!
  //! Inherited from log4tango::Appender virtual interface.
  virtual bool requires_layout() const;

  //! \brief Does nothing else than deleting the specified layout!
  //!
  //! Inherited from log4tango::Appender virtual interface.
  //! \param layout The layout to set.
  virtual void set_layout(log4tango::Layout* layout);

  //! \brief Does nothing!
  //!
  //! Inherited from log4tango::Appender virtual interface.
  virtual void close();

protected:
  //! \brief Log appender method.
  //! \param event The event to log.
  virtual int _append(const log4tango::LoggingEvent& event);

private:
  //- dedicate a type to the log buffer
  typedef std::deque<std::string> LogBuffer;

  //- FileAppender repository
  typedef std::map<Tango::DeviceImpl*, FileAppender*> FileAppenderRepository;
  typedef FileAppenderRepository::value_type FileAppenderEntry;
  typedef FileAppenderRepository::iterator FileAppenderIterator;
  typedef FileAppenderRepository::const_iterator FileAppenderConstIterator;

  //- provide the yat4tango::DynamicAttributeReadCallback with access to read_callback
  friend class DynamicAttributeReadCallback;

  //- provide the log4tango::Logger with access to the dtor
  friend class log4tango::Logger;

  //- Ctor
  FileAppender();

  //- Dtor
  virtual ~FileAppender();

  //- Initialization
  void initialize_i(Tango::DeviceImpl* hd, std::size_t max_log_days,
                    const std::string& path);

  //- the host device
  Tango::DeviceImpl* m_dev;

  //- FileAppender repository
  static FileAppenderRepository m_rep;

  //- Base file name
  std::string m_name;

  //- Log files location
  std::string m_path;

  //- thread safety
  yat::Mutex  m_mtx;

  //- Watcher
  yat::UniquePtr<class FileAppenderWatcher, yat::TaskExiter> m_watcher_ptr;

  //- thread safety
  static yat::Mutex m_rep_lock;

  //- disabled methods
  FileAppender (const FileAppender&);
  const FileAppender& operator= (const FileAppender&);
};

} // namespace

#endif // _FILE_APPENDER_H_
