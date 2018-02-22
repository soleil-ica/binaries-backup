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
// Copyright (C) 2006-2016 The Tango Community
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
//      Nicolas Leclercq
//      Synchrotron SOLEIL
//------------------------------------------------------------------------------
/*!
 * \author See AUTHORS file
 */

#ifndef _YAT_UDP_TRIGGER_H_
#define _YAT_UDP_TRIGGER_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <yat/network/ClientSocket.h>
#include <yat/threading/Mutex.h>
#include <yat/threading/Task.h>
#include <yat/threading/Pulser.h>
#include <yat/utils/Logging.h>
#include <yat/utils/Callback.h>

namespace yat
{
  
//-----------------------------------------------------------------------------
// PSEUDO CONST
//-----------------------------------------------------------------------------
#define YAT_UDP_TRIGGER_NOTIF_DISABLED 0

#define YAT_DEFAULT_MULTICAST_GROUP "225.0.0.1"
#define YAT_DEFAULT_MULTICAST_PORT  30001
#define YAT_DEFAULT_TRIGGER_PERIOD  100

//-----------------------------------------------------------------------------
//! \brief class UDPTrigger
//! 
//! UDP Trigger: Send an udp event
//--------------------------------------------------------
//! how to spawn a udp trigger
//--------------------------------------------------------
//! UDPTrigger::Config cfg;
//! cfg.task_to_notify = this;
//! cfg.udp_addr = 225.0.0.1;
//! cfg.udp_port = 30001;
//! cfg.trigger_period_ms = 100;
//! my_udp_trigger = new UDPTrigger(cfg);
//! my_udp_trigger->start();
//! ...
//-----------------------------------------------------------------------------
template <typename TaskType = yat::Task>
class UDPTrigger
{
public:

// ============================================================================
//! Defines callbacks type.
// ============================================================================
//- E(nd)O(f)S(equence) callback
YAT_DEFINE_CALLBACK(EOSCallback, Thread::IOArg);
//- Trig callback
YAT_DEFINE_CALLBACK(TrigCallback, Thread::IOArg);
//- Error callback
YAT_DEFINE_CALLBACK(ErrorCallback, Exception::ErrorList );

public:
  //--------------------------------------------------------
  //! \brief Configuration struct
  //--------------------------------------------------------
  struct Config
  {
    //! the udp group address (use host addr if empty)
    std::string udp_addr;
    //! the udp port on which the triggers are sent
    yat::uint32 udp_port;
    //! the trigger period in ms or 0 for sending trggers manually using send_next()
    yat::uint32 trigger_period_ms;
    //! the task to which this objet posts notifications
    TaskType* task_to_notify;
    //! id of the message to be posted at 'end of the sequence' - i.e. when all the triggers has been emitted
    //! defaults to YAT_UDP_TRIGGER_NOTIF_DISABLED which means 'disabled/no notifaction'
    size_t eos_notification_msg_id;
    //! id of the message to be posted each time a trig is emitted
    //! defaults to YAT_UDP_TRIGGER_NOTIF_DISABLED which means 'disabled/no notifaction'
    size_t trig_notification_msg_id;
    //! id of the message to be posted when triggering fail
    //! defaults to YAT_UDP_TRIGGER_NOTIF_DISABLED which means 'disabled/no notifaction'
    size_t error_notification_msg_id;
    //! optional callback called when all the triggers has been emitted
    EOSCallback eos_callback;
    //! optional callback called each time a trig is emitted
    TrigCallback trig_callback;
    //! optional callback called when triggering fail
    ErrorCallback error_callback;
    //-----------------------------------
    
    //! \brief default constructor
    Config() 
        : udp_port(0),
          trigger_period_ms(0),
          task_to_notify(0),
          eos_notification_msg_id(YAT_UDP_TRIGGER_NOTIF_DISABLED),
          trig_notification_msg_id(YAT_UDP_TRIGGER_NOTIF_DISABLED),
          error_notification_msg_id(YAT_UDP_TRIGGER_NOTIF_DISABLED),
          eos_callback(),
          trig_callback(),
          error_callback()
    {}

    //! \brief copy constructor
    Config( const Config& src )
        : udp_addr(src.udp_addr),
          udp_port(src.udp_port),
          trigger_period_ms(src.trigger_period_ms),
          task_to_notify(src.task_to_notify),
          eos_notification_msg_id(src.eos_notification_msg_id),
          trig_notification_msg_id(src.trig_notification_msg_id),
          error_notification_msg_id(src.error_notification_msg_id),
          eos_callback(src.eos_callback),
          trig_callback(src.trig_callback),
          error_callback(src.error_callback)
    {}
  }; 
  
  //--------------------------------------------------------
  //! \brief c-tor
  //! 
  //! once instanciated and configured, the UDPTrigger must be started by a call to
  //! yat::Thread::start_undetached (inherited method)
  UDPTrigger( const UDPTrigger::Config& cfg )
      : m_cfg(cfg), m_udp_socket(0), m_trigger_cnt(0), m_sequence_length(0)
  {
    setup_udp_socket();
  }

  //--------------------------------------------------------
  //! \brief  start the UDP trigger
  //!
  //! set n to 0 for a infinite sequence
  void start( std::size_t n=0 )
  {
    if( m_pulser_ptr && m_pulser_ptr->is_running() )
    {
      throw yat::Exception("ERROR", "Triggers sequence already in progress", "yat::UDPTrigger::start");
    }

    m_trigger_cnt = 0;
    m_sequence_length = n;

    if( m_cfg.trigger_period_ms > 0 )
    {
      yat::Pulser::Config cfg;
      cfg.period_in_msecs = m_cfg.trigger_period_ms;
      cfg.num_pulses = n;
      cfg.callback = yat::PulserCallback::instanciate(*this, &UDPTrigger::udp_callback);
      cfg.user_data = 0;
      m_pulser_ptr.reset( new yat::Pulser(cfg) );
      m_pulser_ptr->start();
    }
  }

  //--------------------------------------------------------
  //! \brief stop the UDP trigger
  //! 
  //! any UDP received after a call to stop will be ignored 
  void stop()
  {
    if( m_pulser_ptr )
      m_pulser_ptr->stop();
  }
  
  //--------------------------------------------------------
  //! \brief in manual mode, send the next UDP trigger
  //! 
  void next_trigger()
  {
    if( !m_pulser_ptr )
      send_next();
  }
  
  //--------------------------------------------------------
  //- d-tor
  virtual ~UDPTrigger()
  {}

protected:

  //--------------------------------------------------------
  //! \brief in manual mode, send the next UDP trigger
  //! 
  void send_next()
  {
    if( m_sequence_length > 0 && m_trigger_cnt == m_sequence_length )
    {
      yat::log_warning("yat::UDPTrigger::udp_callback: rejecting pulser trigger! [num. of expected notifications reached]");
      return;
    }
    
    //- inc. trigger counter before sending it to over the network
    ++m_trigger_cnt;
    
    //- send udp packet
    int cnt = ::sendto( m_udp_socket, 
                        &m_trigger_cnt, 
                        sizeof( m_trigger_cnt ), 
                        0, 
                        (struct sockaddr*)&m_multicast_addr, 
                        sizeof( m_multicast_addr )
                        );
    if( cnt < 0 ) 
    {
      m_completion_mtx.unlock();
      yat::OSStream oss;
      oss << "failed to send UDP packet to " 
          << m_cfg.udp_addr
          << ":"
          << m_cfg.udp_port;
      yat::Exception e( "SOCKET_ERROR", oss.str().c_str(), "yat::UDPTrigger::udp_callback" );

      //- call error callback (if any)...
      if( !m_cfg.error_callback.is_empty() )
      {
        m_cfg.error_callback( e.errors );
      }
      if( m_cfg.task_to_notify && m_cfg.error_notification_msg_id > YAT_UDP_TRIGGER_NOTIF_DISABLED )
      {
        yat::Message* msg_p = yat::Message::allocate( m_cfg.error_notification_msg_id );
        msg_p->attach_data( e.errors );
        m_cfg.task_to_notify->post( msg_p );
      }
      else if( m_cfg.error_callback.is_empty() )
      { //- ...or log a message
        yat::log_error( oss.str().c_str() );
      }
    }

      //- call "udp event emitted" callback (if any)
    if( !m_cfg.trig_callback.is_empty() )
    {
      m_cfg.trig_callback( this );
    }
    if( m_cfg.task_to_notify && m_cfg.trig_notification_msg_id > YAT_UDP_TRIGGER_NOTIF_DISABLED )
    {
      m_cfg.task_to_notify->post( m_cfg.trig_notification_msg_id );
    }

    //- sequence completed ?
    if( m_trigger_cnt == m_sequence_length )
    {
      m_completion_mtx.unlock();
      //- call "End Of Sequence" callback (if any)
      if( !m_cfg.eos_callback.is_empty() )
      {
        m_cfg.eos_callback( this );
      }
      if( m_cfg.task_to_notify && m_cfg.eos_notification_msg_id > YAT_UDP_TRIGGER_NOTIF_DISABLED )
      {
        m_cfg.task_to_notify->post( m_cfg.eos_notification_msg_id );
      }
    }  
  }
  

  //--------------------------------------------------------
  void udp_callback( yat::Thread::IOArg )
  { 
    send_next();
  }

  //--------------------------------------------------------
  void setup_udp_socket()
  {
    m_udp_socket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if ( m_udp_socket < 0 ) 
    {
      throw yat::Exception("SOCKET_ERROR", "failed to create UDP socket", "yat::UDPTrigger::start");
    }
    
    ::memset((char *)&m_multicast_addr, 0, sizeof(m_multicast_addr));
   
    m_multicast_addr.sin_family = AF_INET;
    m_multicast_addr.sin_port = htons( m_cfg.udp_port );
    if ( 0 == inet_aton( m_cfg.udp_addr.c_str(), &m_multicast_addr.sin_addr) )
    {
      throw yat::Exception("SOCKET_ERROR", "failed to create UDP socket", "yat::UDPTrigger::start");
    }
  }

private:
  UDPTrigger::Config          m_cfg;
  int                         m_udp_socket;
  struct sockaddr_in          m_multicast_addr;
  yat::UniquePtr<yat::Pulser> m_pulser_ptr;
  std::size_t                 m_trigger_cnt;
  yat::Mutex                  m_completion_mtx;
  yat::uint32                 m_sequence_length;
};

} //- namespace yat

#endif //- _YAT_UDP_TRIGGER_H_
