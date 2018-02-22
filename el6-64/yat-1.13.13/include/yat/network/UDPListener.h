// ============================================================================
// = CONTEXT
//		Flyscan Project - HW to SW Trigger Service
// = File
//		UDPListener.h
// = AUTHOR
//    N.Leclercq - SOLEIL
// ============================================================================

#pragma once 

#include <iostream>
#include <yat/network/ClientSocket.h>
#include <yat/threading/Task.h>
#include <yat/utils/StringTokenizer.h>
#include <yat/utils/Logging.h>
#include <yat/utils/Callback.h>

namespace yat
{
  
  
//-----------------------------------------------------------------------------
// PSEUDO CONST
//-----------------------------------------------------------------------------
#define UDP_LISTENER_NOTIF_DISABLED 0

  
// ============================================================================
//! Defines callbacks type.
// ============================================================================
//- U(dp)E(vent)R(eceived) callback
YAT_DEFINE_CALLBACK(UERCallback, yat::uint32);
//- E(nd)O(f)S(equence) callback
YAT_DEFINE_CALLBACK(EOSCallback, yat::uint32);
//- T(i)M(e)O(ut) callback
YAT_DEFINE_CALLBACK(TMOCallback, yat::uint32);

//-----------------------------------------------------------------------------
//! \brief class UDPListener
//! 
//! UDP Listener: FORWARDS THE UDP PACKETS (SENT BY THE SPI BOARD) TO A SPITASK
//--------------------------------------------------------
//! how to spawn a udp listener (end of seq. notif. only)
//--------------------------------------------------------
//! UDPListener::Config cfg;
//! cfg.task_to_notify = this;
//! cfg.udp_addr = 225.0.0.1; //- e.g. obtained from UDPAddress property 
//! cfg.udp_port = 30001;     //- e.g. obtained from UDPPort property
//! cfg.eos_notification_msg_id = (yat::FIRST_USER_MSG + 10001)
//! my_udp_listener = new UDPListener(cfg);
//! my_udp_listener->start_undetached();
//! ...
//! process notifications 
//! ...
//! my_udp_listener->exit();
//-----------------------------------------------------------------------------
template <typename TaskType = yat::Task>
class UDPListener : public yat::Thread
{
private:

  //- internal/private running mode
  enum Mode
  {
    UDP_INFINITE,
    UDP_FINITE,
    UDP_STANDBY
  };
  
public:
  //--------------------------------------------------------
  //! \brief Configuration struct
  //--------------------------------------------------------
  struct Config
  {
    //! the udp group address (use host addr if empty)
    std::string udp_addr;
    //! the udp port on which this listener waits for incoming data
    yat::uint32 udp_port;
    //! the udp timeout in ms (to avoid to block the thread till end of time - defaults to 1000 ms)
    yat::uint32 udp_tmo_ms;
    //! the task to which this listener posts incoming data
    TaskType * task_to_notify;
    //! id of the message to be posted to the 'task_to_notify' each time a 'UDP event is received'
    //! defaults to UDP_LISTENER_NOTIF_DISABLED which means 'disabled/no notifaction'
    size_t uer_notification_msg_id;
    //! id of the message to be posted at 'end of the sequence' - i.e. when the expected number of UDP events has beeen received
    //! defaults to UDP_LISTENER_NOTIF_DISABLED which means 'disabled/no notifaction'
    size_t eos_notification_msg_id;
    //! id of the message to be posted on timeout - i.e. when no UDP events has beeen received after 'udp_tmo_ms' ms
    //! defaults to UDP_LISTENER_NOTIF_DISABLED which means 'disabled/no notifaction'
    size_t tmo_notification_msg_id;
    //! optional callback called for each time a 'UDP event is received'
    UERCallback uer_callback;
    //! optional callback called when the expected number of UDP events has beeen received
    EOSCallback eos_callback;
    //! optional callback called when no UDP events has beeen received after 'udp_tmo_ms' ms
    TMOCallback tmo_callback;
    //-----------------------------------
    
    //! \brief default constructor
    Config () 
        : udp_addr(),
          udp_port(0),
          udp_tmo_ms(1000),
          task_to_notify(0),
          uer_notification_msg_id(UDP_LISTENER_NOTIF_DISABLED),
          eos_notification_msg_id(UDP_LISTENER_NOTIF_DISABLED),
          tmo_notification_msg_id(UDP_LISTENER_NOTIF_DISABLED),
          uer_callback(),
          eos_callback(),
          tmo_callback()
    {}

    //! \brief copy constructor
    Config (const Config& src) 
        : udp_addr(src.udp_addr),
          udp_port(src.udp_port),
          udp_tmo_ms(src.udp_tmo_ms),
          task_to_notify(src.task_to_notify),
          uer_notification_msg_id(src.uer_notification_msg_id),
          eos_notification_msg_id(src.eos_notification_msg_id),
          uer_callback(src.uer_callback),
          eos_callback(src.eos_callback),
          tmo_callback(src.tmo_callback)
    {}
  }; 
  
  //--------------------------------------------------------
  //! \brief c-tor
  //! 
  //! once instanciated and configured, the UDPListener must be started by a call to
  //! yat::Thread::start_undetached (inherited method)
  UDPListener (const UDPListener::Config & cfg)
      : Thread (),
      m_mode(UDP_STANDBY),
      m_go_on(true),
      m_received_events(0),
      m_expected_events(0),
      m_ignored_events(0),
      m_total_ignored_events(0),
      m_cfg(cfg)
  {
  }

  //--------------------------------------------------------
  //! \brief  start the UDP listener in FINITE mode (n > 0) or INFINITE mod (n = 0)
  //! 
  //! in FINITE mode, 'n' is the expected number of UDP events (i.e. sequence length)
  void start (yat::uint32 n)
  {
    m_received_events = 0;
    m_ignored_events = 0;
    m_expected_events = n;
    m_mode = n ? UDP_FINITE : UDP_INFINITE;
  }

  //--------------------------------------------------------
  //! \brief stop the UDP listener
  //! 
  //! any UDP received after a call to stop will be ignored 
  void stop ()
  {
    m_mode = UDP_STANDBY;
  }

  //--------------------------------------------------------
  //- ask the underlying thread to exit
  virtual void exit ()
  {  
    m_go_on = false;
    Thread::IOArg dummy = 0;
    join(&dummy);
  }

  //--------------------------------------------------------
  //! \brief total number of ignored UDP events since last call to UDPListener::start 
  inline yat::uint32 events_ignored () const { return m_ignored_events; }

  //--------------------------------------------------------
  //! \brief total number of ignored UDP events since call to yat::Thread::start_undetached 
  inline yat::uint32 total_events_ignored () const { return m_total_ignored_events; }
  
  //--------------------------------------------------------
  //! \brief number of UDP events since last call to start
  inline yat::uint32 events_received () const { return m_received_events; }
  
  //--------------------------------------------------------
  //! \brief return true if the listener received the expected number of UDP events, return false otherwise
  inline bool expected_events_received () const { return m_received_events == m_expected_events; }
  
protected:
  //--------------------------------------------------------
  //- to not call directly! call 'exit' instead (the undelying impl. will clenup everything for you)
  virtual ~UDPListener ()
  {}

  //--------------------------------------------------------
  void setup_udp_socket (yat::ClientSocket& sock)
  {
    yat::StringTokenizer st(m_cfg.udp_addr, ".");

    if ( st.count_tokens() != 4 )
    {
      yat::OSStream oss;
      oss << "invalid UDP address specified: " << m_cfg.udp_addr;
      throw yat::Exception("INVALID_ARGUMENT", oss.str().c_str(), "UDPListener::send_udp_info_i");
    }
    //- first byte of the udp addr
    yat::uint8 b = static_cast<yat::uint8>(st.next_long_token());

    //- addresses in the range 224.0.0.1 to 239.255.255.255 identifies a multicast group
    bool multicast = (b >= 224 && b <= 239 )
                   ? true
                   : false;

    //- unicast case...
    if ( ! multicast )
    {
      //-  just bind to udp port, then return
      sock.bind(m_cfg.udp_port);
      return;
    }
    
    //- multicast case...
    
    //- enable SOCK_OPT_REUSE_ADDRESS to allow multiple instances of this 
    //- device to receive copies of the multicast datagrams
    sock.set_option(yat::Socket::SOCK_OPT_REUSE_ADDRESS, 1);
      
    //- bind to the udp port
    sock.bind(m_cfg.udp_port);
    
    //- join the multicast group
    yat::Address multicast_grp_addr(m_cfg.udp_addr, 0);
    sock.join_multicast_group(multicast_grp_addr);
  }

  //--------------------------------------------------------
  //- the thread entry point - calle by yat::Thread::start_undetached
  virtual yat::Thread::IOArg run_undetached (yat::Thread::IOArg)
  {
    m_go_on = true;
    
    //- instanciate the udp socket
    yat::ClientSocket sock(yat::Socket::UDP_PROTOCOL);
      
    //- setup our udp socket
    setup_udp_socket(sock);

    //- input data buffer
    yat::Socket::Data ib(2048);
    
    //- (almost) infinite reading loop  
    yat::uint32 udp_evt_number = 0;
    while ( m_go_on )
    {
      //- wait for some input data
      if ( sock.wait_input_data(m_cfg.udp_tmo_ms, false) )
      {
        //- read input data
        yat::uint32  rb = sock.receive_from(ib);
        if ( rb )
        {
          if ( m_mode == UDP_STANDBY )
          {
            ++m_ignored_events;
            ++m_total_ignored_events;
            continue;
          }
          //- extract UDP event number (identifier) from the UDP packet
          //- this is set by SpiUdpTimebase (i.e. the UDP event emitter)
          udp_evt_number = *(reinterpret_cast<yat::uint32*>(ib.base()));
          //- post data to the task 
          if ( m_cfg.task_to_notify )
          {
            //- post UDP notification to the 'task_to_notify'?
            if ( m_cfg.uer_notification_msg_id > UDP_LISTENER_NOTIF_DISABLED )
            {
              //- post a 'uer_notification_msg_id' msg to the 'task_to_notify'
              m_cfg.task_to_notify->post(m_cfg.uer_notification_msg_id, udp_evt_number, 500);
            }
          }
          //- call "udp event received" callback (if any)
          if ( ! m_cfg.uer_callback.is_empty() )
          {
            m_cfg.uer_callback(udp_evt_number);
          }
          //- end of sequence...
          if ( m_mode == UDP_FINITE && udp_evt_number == m_expected_events )
          {
            //- done, post a 'end of sequence' to the 'task_to_notify'
            if ( m_cfg.task_to_notify && m_cfg.eos_notification_msg_id > UDP_LISTENER_NOTIF_DISABLED )
            {
              m_cfg.task_to_notify->post(m_cfg.eos_notification_msg_id, udp_evt_number, 500);
            }
            //- call "end of sequence" callback (if any)
            if ( ! m_cfg.eos_callback.is_empty() )
            {
              m_cfg.eos_callback(m_expected_events);
            }
            //- done, swicth to STANDBY mode
            m_mode = UDP_STANDBY;
          }
        }
      }
      //- tmo expired
      else
      {
        //- are we running?
        if ( m_mode == UDP_STANDBY )
          continue;
        //- call 'tmo' callback (if any)
        if ( ! m_cfg.tmo_callback.is_empty() )
        {
          m_cfg.tmo_callback(udp_evt_number);
        }
        //- done, post a 'tmo' to the 'task_to_notify'
        if ( m_cfg.task_to_notify && m_cfg.tmo_notification_msg_id > UDP_LISTENER_NOTIF_DISABLED )
        {
          m_cfg.task_to_notify->post(m_cfg.tmo_notification_msg_id, udp_evt_number, 500);
        }
      }
    }

    return 0;
  }

private:
  Mode m_mode;
  bool m_go_on;
  yat::uint32 m_received_events;
  yat::uint32 m_expected_events;
  yat::uint32 m_ignored_events;
  yat::uint32 m_total_ignored_events;
  UDPListener::Config m_cfg;
};

} //- namespace yat

