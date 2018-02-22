// ============================================================================
// = CONTEXT
// Flyscan Project - HW to SW Trigger Service
// = File
// NexusDataStreamerFinalizerImpl.cpp
// = AUTHOR
// N.Leclercq - SOLEIL
// ============================================================================

#include <iostream>
#include <yat/Exception.h>
#include <yat/time/Timer.h>
#include <nexuscpp/nexuscpp.h>
#include <nexuscpp/nxfinalizer.h>

namespace nxcpp
{

//=============================================================================
// NexusDataStreamerFinalizer::NexusDataStreamerFinalizer
//=============================================================================
NexusDataStreamerFinalizer::NexusDataStreamerFinalizer ()
    : m_pulser(0),
      m_entries(),
      m_entries_mutex()
{
    //- noop
}

//=============================================================================
// NexusDataStreamerFinalizer::NexusDataStreamerFinalizer
//=============================================================================
NexusDataStreamerFinalizer::~NexusDataStreamerFinalizer ()
{
    try
    {
        terminate();
    }
    catch (...)
    {
        //- ignore error
    }
}

//=============================================================================
// NexusDataStreamerFinalizer::initialize
//=============================================================================
void NexusDataStreamerFinalizer::initialize ()
{
    try
    {
        //---------------------------------------
        yat::Pulser::Config pulser_cfg;
        pulser_cfg.period_in_msecs = 1000;
        pulser_cfg.num_pulses = 0;
        pulser_cfg.callback = yat::PulserCallback::instanciate(*this, &NexusDataStreamerFinalizer::pulse);
        pulser_cfg.user_data = 0;
        //---------------------------------------
        m_pulser = new yat::Pulser(pulser_cfg);
        if ( ! m_pulser )
            throw std::bad_alloc();
        //---------------------------------------
        m_pulser->start();
    }
    catch (const std::bad_alloc&)
    {
        THROW_YAT_ERROR("MEMORY_ERROR", "failed to instanciate yat::Pulser!", "NexusDataStreamerFinalizer::initialize");
    }
    catch (...)
    {
        THROW_YAT_ERROR("UNKNOWN_ERROR", "failed to instanciate yat::Pulser!", "NexusDataStreamerFinalizer::initialize");
    }
}

//=============================================================================
// NexusDataStreamerFinalizer::terminate
//=============================================================================
void NexusDataStreamerFinalizer::terminate ()
{
    if ( ! initialized() )
        return;

    if ( m_pulser )
    {
        m_pulser->stop();
        delete m_pulser;
        m_pulser = 0;
    }
    
    { //- critical section
        yat::MutexLock guard(m_entries_mutex);

        //- final all nexus data-streamers
        EntriesIterator it = m_entries.begin();
        for (; it != m_entries.end(); ++it)
        {
            try
            {
                if ( *it && (*it)->data_streamer )
                    (*it)->data_streamer->Finalize();
            }
            catch (...)
            {
                //- ignore error?
            }
            //- delete the data-streamer
            delete *it;
        }

        //- clear the list
        m_entries.clear();
    }
}

//=============================================================================
// NexusDataStreamerFinalizer::start
//=============================================================================
void NexusDataStreamerFinalizer::start ()
{
    //- initialized?
    if ( initialized() )
        return;

    initialize();
}

//=============================================================================
// NexusDataStreamerFinalizer::stop
//=============================================================================
void NexusDataStreamerFinalizer::stop ()
{
    //- initialized?
    if ( ! initialized() )
        return;

    terminate();
}

//=============================================================================
// NexusDataStreamerFinalizer::push
//=============================================================================
void NexusDataStreamerFinalizer::push (Entry* e)
{
    //- initialized?
    if ( ! initialized() )
        return;

    //- valid arg?
    if ( ! e )
        return;

    { //- critical section
        yat::MutexLock guard(m_entries_mutex);

        //- insert the data_streamer into the list
        m_entries.push_back(e);
    }
}

//=============================================================================
// NexusDataStreamerFinalizer::pulse
//=============================================================================
void NexusDataStreamerFinalizer::pulse (yat::Thread::IOArg)
{
    while (true)
    {
        //- access first element in the list then remove it from the list
        Entry* e = 0;

        {
            yat::MutexLock guard(m_entries_mutex);
            if ( m_entries.empty() )
                break;
            e = m_entries.front();
            m_entries.pop_front();
        }

        if ( ! e || ! e->data_streamer )
            continue;

        //- done, finalize then delete the DataStreamer
        NX_DBG("NexusDataStreamerFinalizer::pulse:finalizing datastreamer @" << std::hex << (void*)e << std::dec);
        yat::Timer t;

        try
        {
            e->data_streamer->Finalize();
        }
        catch (const yat::Exception& ex)
        {
            ex.dump();
        }
        catch ( ... )
        {

        }

        delete e;

        NX_DBG("NexusDataStreamerFinalizer::pulse:finalizing datatstreamer @"
               << std::hex
               << (void*)e
               << std::dec
               << " took "
               << t.elapsed_msec()
               << " ms");
    }
}

} //- namespace nxcpp

