// ============================================================================
// = CONTEXT
// Flyscan Project - Nexus file generation optimizer
// = File
// NexusDataStreamerFinalizer.h
// = AUTHOR
// N.Leclercq - SOLEIL
// ============================================================================

#pragma once 

#include <list>
#include <yat/threading/Mutex.h>
#include <yat/threading/Pulser.h>
#include <yat/any/GenericContainer.h>

namespace nxcpp
{

//-----------------------------------------------------------------------------
// Forward declaration
//-----------------------------------------------------------------------------
class DataStreamer;
  
//-----------------------------------------------------------------------------
// NexusDataStreamerFinalizer
//-----------------------------------------------------------------------------
class NexusDataStreamerFinalizer
{
public:

  typedef struct Entry
  {  
    //----------------------------------------
    //- the nexuscpp::DataStreamer to finalize
    nxcpp::DataStreamer *data_streamer;
    //----------------------------------------
    //- opaque data holder  
    //-   for data-items making use of the nexuscpp::DataStreamer::NO_COPY mode
    //-   this will be simply deleted once the data streaming is done
    //-   use this to ensure that the memory blocks containing data are not released before streaming is done  
    yat::Container *data_holder;
    //----------------------------------------
    Entry ()
      : data_streamer(0), data_holder(0) {}
    //----------------------------------------
    Entry (Entry& src)
      : data_streamer(src.data_streamer), data_holder(src.data_holder) { src.data_streamer = 0; src.data_holder = 0; }
    //----------------------------------------
    void operator= (Entry& src)
      { data_streamer = src.data_streamer; src.data_streamer = 0; data_holder = src.data_holder; src.data_holder = 0; }
    //----------------------------------------
    ~Entry ()
      { delete data_streamer; delete data_holder; }
    //----------------------------------------
  } Entry;
  
  //-------------------------------------------------------
  NexusDataStreamerFinalizer ();
  
  //-------------------------------------------------------
  virtual ~NexusDataStreamerFinalizer ();
  
  //-------------------------------------------------------
  void start ();
  
  //-------------------------------------------------------
  void stop ();
  
  //-------------------------------------------------------
  void push (Entry* e);

private:
  //--------------------------------------------------------
  typedef std::list<Entry*>       Entries;
  typedef Entries::iterator       EntriesIterator;
  typedef Entries::const_iterator EntriesConstIterator;
  //--------------------------------------------------------
  void pulse (yat::Thread::IOArg arg);
  //--------------------------------------------------------
  void initialize ();
  void terminate ();
  //--------------------------------------------------------
  inline bool initialized () const { return m_pulser != 0; }
  //--------------------------------------------------------
  yat::Pulser*  m_pulser;
  Entries       m_entries;
  yat::Mutex    m_entries_mutex;
};

} 
