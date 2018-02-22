//*****************************************************************************
/// Synchrotron SOLEIL
///
/// Nexus API for Tango servers
///
/// Creation : 2011/01/12
/// Authors   : Stephane Poirier, Clement Rodriguez
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

// system
#include <cstdlib>
#include <climits>

// YAT
#include <yat/file/FileName.h>
#include <yat/threading/Task.h>
#include <yat/utils/Logging.h>

// NexusCPP
#include <nexuscpp/nxbuffer.h>

namespace nxcpp
{

//==============================================================================
// Free function
//==============================================================================
template<typename T>
static std::string VectorToStr( const std::vector<T> &v, char left='[', char sep=',', char right=']' )
{
    std::ostringstream oss;
    oss << left;
    typename std::vector<T>::const_iterator it;
    it = v.begin();

    while( it != v.end() )
    {
        oss << *it;
        ++it;
        if( it != v.end() )
            oss << sep;
    }
    oss << right;
    return oss.str();
}

//==============================================================================
// Class StreamBuffer
//==============================================================================
class StreamBuffer
{
private:
  std::string        m_sTargetDir;
  std::string        m_sFileName;
  std::string        m_sTempFileName;
    bool                       m_bDone;
    std::string                m_file_path;
    std::string                m_nxentry_name;
    NexusFileWriter::WriteMode m_wmWritingMode;

public:
    StreamBuffer(const std::string &sTargetDir, const std::string &sFileName,
                 const std::string &sEntryName = "entry");
    ~StreamBuffer();

    void FilePath(const std::string &sPath) { m_file_path = sPath; }
    void NXentryName(const std::string &sName) { m_nxentry_name = sName; }
    const std::string &FilePath() { return m_file_path; }
    const std::string &NXentryName() { return m_nxentry_name; }

    // Define the file writer's mode of writing
    void WriteMode(const NexusFileWriter::WriteMode &mode) { m_wmWritingMode = mode; }
    NexusFileWriter::WriteMode WriteMode() { return m_wmWritingMode; }

    void PostWritingProcess();
    bool Done() { return m_bDone; }

private:
    void FinalizeBuffer();
};
// Referenced pointer definition
typedef yat::SharedPtr<StreamBuffer, yat::Mutex> StreamBufferPtr;

typedef std::pair<StreamBufferPtr, NexusFileWriterPtr> CtrlWriterPair;
typedef std::vector<CtrlWriterPair> CtrlWriterList;

//==============================================================================
// StreamBufferTask
// Used to let the nexus file being still written (in a background 
// process) by the API while the end user want to keep pushing data into another
// files. The queue is then a simple task permiting writing process to finish
// properly and not block the user in his process.
//==============================================================================
class StreamBufferTask : public yat::Task, NexusFileWriter::INotify, IExceptionHandler
{
public:
    enum MessageType
    {
        ABORT = yat::FIRST_USER_MSG+55,
        PROCESS_CACHE,
        RESET_STATS
    };

    typedef struct Statistics
    {
        Statistics();

        // NeXusWriters active objects count
        yat::uint16 writer_count;

        // Total number of bytes to be written on all NeXusWriter instances
        yat::uint64 total_bytes;

        // Total number of written bytes on all NeXusWriter instances
        yat::uint64 written_bytes;

        // Instant write rates
        float instant_rate_mb_per_sec;

        // Average write rates
        float average_rate_mb_per_sec;
    } Statistics;

    struct DataWritten
    {
        NexusFileWriter* source_p;
        std::string      dataset_path;
        std::size_t      write_pos;
    };

private:
    CtrlWriterList      m_lstCtrlWriter; // map containing IStreamBuffer => NexusFileWriter
    bool                m_bCacheNotEmpty;
    mutable yat::Mutex  m_mtxLock;       // thread safety...
    bool                m_bAborted;      //  true if writing process has been aborted
    bool                m_bWantAbort;    // true if process abort is requested
    StreamBufferPtr m_ptrCurRecCtrl;
    NexusFileWriterPtr  m_ptrCurFileWriter;
    Statistics          m_Stats;
    yat::Timer          m_tmAverage;
    yat::Timer          m_tmInstant;
    yat::uint64         m_ui64WrittenBytesLastInstant;
    std::map<std::string, std::size_t> m_item_written_counter;
    IExceptionHandler  *m_pHandler;

    void Add(StreamBufferPtr ptrRecCtrl, NexusFileWriterPtr ptrFileWriter);

public:
    StreamBufferTask(NexusFileWriter::WriteMode WritingMode);
    ~StreamBufferTask();

    void Init(NexusFileWriter::WriteMode WritingMode);
    void SetStreamBuffer(StreamBufferPtr ptrRecCtrl);
    void Synchronize();
    void Abort();
    bool Done();

    void SetExceptionHandler(IExceptionHandler *pHandler);

    StreamBufferPtr StreamBuffer();
    NexusFileWriterPtr FileWriter();

    // Statistics
    void ResetStatistics();
    Statistics GetStatistics() const;
    void UpdateStatistics();

protected:
    void handle_message (yat::Message& msg);

    // interface NexusFileWriter::INotify
    void OnWriteSubSet(NexusFileWriter* source_p, const std::string& dataset_path, int start_pos[MAX_RANK], int dim[MAX_RANK]);
    void OnWrite(NexusFileWriter* source_p, const std::string& dataset_path);
    void OnCloseFile(NexusFileWriter* source_p, const std::string& file_path);
    void data_written(DataWritten data);

    void ProcessCache();

    void Abort_i();

    // interface IExceptionHandler
    void OnNexusException(const NexusException &e);
};

//==============================================================================
// StreamBuffer
//==============================================================================
//------------------------------------------------------------------------------
// StreamBuffer::StreamBuffer
//------------------------------------------------------------------------------
StreamBuffer::StreamBuffer(const std::string &sTargetDir, const std::string &sFileName,
                           const std::string &sEntryName)
{
    m_bDone = false;
    m_sFileName = sFileName;
  yat::StringUtil::printf(&m_sTempFileName, "temp_%s.%d", PSZ(sFileName), yat::Time::unix_time());
    m_sTargetDir = sTargetDir;
  FilePath( yat::StringUtil::str_format("%s/%s", PSZ(sTargetDir), PSZ(m_sTempFileName)) );
    NXentryName(sEntryName);
    WriteMode(NexusFileWriter::ASYNCHRONOUS);
}

//------------------------------------------------------------------------------
// StreamBuffer::~StreamBuffer
//------------------------------------------------------------------------------
StreamBuffer::~StreamBuffer() 
{
    if( !Done() )
        FinalizeBuffer();
}

//------------------------------------------------------------------------------
// StreamBuffer::FinalizeBuffer
//------------------------------------------------------------------------------
void StreamBuffer::FinalizeBuffer()
{
    NX_SCOPE_DBG("StreamBuffer::FinalizeBuffer");

    NX_DBG(m_sTempFileName << " -> " << m_sFileName);

    if( !m_bDone )
    {
        // Rename temp file name to final name

        if( yat::FileName( (m_sTargetDir + "/" + m_sTempFileName).c_str() ).file_exist() )
        {
            yat::FileName fnFile(m_sTargetDir + "/" + m_sTempFileName);
            try
            {
                fnFile.rename(m_sTargetDir + "/" + m_sFileName);
            }
            catch( yat::Exception &e )
            {
                e.push_error("OPERATION_FAILED", PSZ_FMT("Cannot rename file %s", PSZ(m_sTempFileName)), "StreamBuffer::FinalizeBuffer");
                throw e;
            }
        }
        else
        {
            NX_DBG("!!! Exception !!! Failed to find one buffer file: " << (m_sTargetDir + "/" + m_sTempFileName));
            throw NexusException(PSZ_FMT("Failed to find one buffer file: %s", (m_sTargetDir + "/" + m_sFileName).c_str()),
                                 "StreamBuffer::FinalizeBuffer");
        }

        m_bDone = true;
    }
}

//------------------------------------------------------------------------------
// StreamBuffer::PostWritingProcess
//------------------------------------------------------------------------------
void StreamBuffer::PostWritingProcess()
{
    FinalizeBuffer();
}

//==============================================================================
// StreamBufferTask
//==============================================================================

//------------------------------------------------------------------------------
// StreamBufferTask::StreamBufferTask
//------------------------------------------------------------------------------
StreamBufferTask::StreamBufferTask(NexusFileWriter::WriteMode WritingMode)
{
    Init(WritingMode);
}

//------------------------------------------------------------------------------
// StreamBufferTask::Init
//------------------------------------------------------------------------------
void StreamBufferTask::Init(NexusFileWriter::WriteMode WritingMode)
{
    m_Stats.writer_count = 0;
    m_bAborted = false;
    m_bWantAbort = false;
    m_ui64WrittenBytesLastInstant = 0;
    m_pHandler = this;

    if (NexusFileWriter::ASYNCHRONOUS == WritingMode)
    {
        // Start task
        enable_periodic_msg(true);
        set_periodic_msg_period(1000);

        //- setup low and high watermarks
        msgq_lo_wm(0x007FFFFF);
        msgq_hi_wm(0x00FFFFFF); 
        message_queue().throw_on_post_msg_timeout(true);

        go();
    }
}

//------------------------------------------------------------------------------
// StreamBufferTask::~StreamBufferTask
//------------------------------------------------------------------------------
StreamBufferTask::~StreamBufferTask()
{
    NX_DBG("StreamBufferTask::~StreamBufferTask");
    Synchronize();
    m_lstCtrlWriter.clear();
}

//------------------------------------------------------------------------------
// StreamBufferTask::SetExceptionHandler
//------------------------------------------------------------------------------
void StreamBufferTask::SetExceptionHandler(IExceptionHandler *pHandler)
{
    m_pHandler = pHandler;
}

//------------------------------------------------------------------------------
// StreamBufferTask::OnNexusException
//------------------------------------------------------------------------------
void StreamBufferTask::OnNexusException(const NexusException &e)
{
    YAT_LOG_EXCEPTION( e );
}

//------------------------------------------------------------------------------
// StreamBufferTask::data_written
//------------------------------------------------------------------------------
void StreamBufferTask::data_written(DataWritten data)
{
    yat::AutoMutex<> _lock(m_mtxLock);
    NX_SCOPE_DBG("StreamBufferTask::data_written");

    //## Send message...
    if( data.source_p->Done() && !m_lstCtrlWriter.empty() && data.source_p != m_lstCtrlWriter.back().second.get() )
    {
        // Previous file writer has completed its job => unhold the next
        for( std::size_t i = 1; i < m_lstCtrlWriter.size(); ++i )
        {
            if( data.source_p == m_lstCtrlWriter[i-1].second.get() )
            {
                m_lstCtrlWriter[i].second->Hold(false);
                break;
            }
        }
    }
}

//------------------------------------------------------------------------------
// StreamBufferTask::OnWriteSubSet
//------------------------------------------------------------------------------
void StreamBufferTask::OnWriteSubSet(NexusFileWriter* source_p, const std::string& dataset_path, int start_pos[MAX_RANK], int /*dim*/[MAX_RANK])
{
    NX_SCOPE_DBG("StreamBufferTask::OnWriteSubSet");
    DataWritten data;
    data.source_p = source_p;
    data.write_pos = start_pos[0];
    data.dataset_path = dataset_path;
    data_written(data);
}

//------------------------------------------------------------------------------
// StreamBufferTask::OnWrite
//------------------------------------------------------------------------------
void StreamBufferTask::OnWrite(NexusFileWriter* source_p, const std::string& dataset_path)
{
    NX_SCOPE_DBG("StreamBufferTask::OnWrite");
    DataWritten data;
    data.source_p = source_p;
    data.write_pos = -1;
    data.dataset_path = dataset_path;
    data_written(data);
}

//------------------------------------------------------------------------------
// StreamBufferTask::OnCloseFile
//------------------------------------------------------------------------------
void StreamBufferTask::OnCloseFile(NexusFileWriter* source_p, const std::string& file_path)
{
    NX_SCOPE_DBG("StreamBufferTask::OnCloseFile");
}

//------------------------------------------------------------------------------
// StreamBufferTask::StreamBuffer
//------------------------------------------------------------------------------
StreamBufferPtr StreamBufferTask::StreamBuffer()
{
    yat::AutoMutex<> _lock(m_mtxLock);
    return m_ptrCurRecCtrl;
}

//------------------------------------------------------------------------------
// StreamBufferTask::FileWriter
//------------------------------------------------------------------------------
NexusFileWriterPtr StreamBufferTask::FileWriter()
{
    yat::AutoMutex<> _lock(m_mtxLock);
    return m_ptrCurFileWriter;
}

//------------------------------------------------------------------------------
// StreamBufferTask::SetStreamBuffer
//------------------------------------------------------------------------------
void StreamBufferTask::SetStreamBuffer(StreamBufferPtr ptrRecCtrl)
{
    NX_SCOPE_DBG("StreamBufferTask::SetStreamBuffer");

    yat::AutoMutex<> _lock(m_mtxLock);

    if( ptrRecCtrl.get() != m_ptrCurRecCtrl.get() )
    {
        if( !ptrRecCtrl.is_null() )
        {
            // Allocate a new NexusFileWriter, e.g. a new writing thread
            NX_DBG("write mode: " << ptrRecCtrl->WriteMode());

            NexusFileWriterPtr ptrCurFileWriter = new NexusFileWriter(ptrRecCtrl->FilePath(), ptrRecCtrl->WriteMode());
            ptrCurFileWriter->SetNotificationHandler(this);

            // To improve performance the file is not closed after each write access
            ptrCurFileWriter->SetFileAutoClose(false);

            if( m_pHandler )
                ptrCurFileWriter->SetExceptionHandler(m_pHandler);

            if( !m_lstCtrlWriter.empty() && !m_lstCtrlWriter.back().second->Done() )
                // If it is not the 1st, then force it to delay the writing process in order to try
                // to serialize the write actions
                ptrCurFileWriter->Hold(true);

            m_ptrCurFileWriter = ptrCurFileWriter;
            m_ptrCurRecCtrl = ptrRecCtrl;
            Add(m_ptrCurRecCtrl, m_ptrCurFileWriter);
        }
        else
        {
            m_ptrCurFileWriter.reset();
        }
    }
}

//------------------------------------------------------------------------------
// StreamBufferTask::Add
//------------------------------------------------------------------------------
void StreamBufferTask::Add(StreamBufferPtr ptrRecCtrl, NexusFileWriterPtr ptrFileWriter)
{
    NX_SCOPE_DBG("StreamBufferTask::Add");
    yat::AutoMutex<> _lock(m_mtxLock);

    int iCount = m_lstCtrlWriter.size();
    m_lstCtrlWriter.push_back(CtrlWriterPair(ptrRecCtrl, ptrFileWriter));
    m_Stats.writer_count++;
    if( iCount == 0 )
    {
        if( STATE_RUNNING == state() )
            post(StreamBufferTask::PROCESS_CACHE);

        else
        {
            UpdateStatistics();
            ProcessCache();
        }
    }

}

//------------------------------------------------------------------------------
// StreamBufferTask::Abort
//------------------------------------------------------------------------------
void StreamBufferTask::Abort()
{
    m_bWantAbort = true;
    if( STATE_RUNNING == state() )
    {
        post(StreamBufferTask::ABORT);
    }
    else
    {
        Abort_i();
    }
}

//------------------------------------------------------------------------------
// StreamBufferTask::Abort
//------------------------------------------------------------------------------
void StreamBufferTask::Abort_i()
{
    while( !m_lstCtrlWriter.empty() )
    {
        m_lstCtrlWriter.begin()->second->Abort();
        m_lstCtrlWriter.erase(m_lstCtrlWriter.begin());
        m_Stats.writer_count--;
    }
    m_bAborted = true;    
}


//------------------------------------------------------------------------------
// StreamBufferTask::Done
//------------------------------------------------------------------------------
bool StreamBufferTask::Done()
{
    yat::AutoMutex<> _lock(m_mtxLock);

    int iSize = m_lstCtrlWriter.size();
    bool bFileW = m_ptrCurFileWriter ? true : false;
    bool bDone = false;
    if( bFileW )
        bDone = m_ptrCurFileWriter->Done();

    return ( (m_bAborted || (iSize <= 0)) && (!bFileW || (bFileW && bDone)) );
}

//------------------------------------------------------------------------------
// StreamBufferTask::handle_message
//------------------------------------------------------------------------------
void StreamBufferTask::handle_message (yat::Message& msg)
{
    switch( msg.type() )
    {
    case yat::TASK_INIT:
        break;

    case yat::TASK_EXIT:
        if( !m_bAborted && !m_bWantAbort )
        {
            NX_DBG("[TASK_EXIT] Processing cache: " << m_lstCtrlWriter.size());
            // Do the job
            try
            {
                ProcessCache();
            }
            catch( yat::Exception &e )
            {
                e.dump();
            }
        }
        break;

    case ABORT:
    {
        NX_DBG("StreamBufferTask::handle_message ABORT");
        yat::AutoMutex<> _lock(m_mtxLock);
        Abort_i();
    }
    break;

    case yat::TASK_PERIODIC:
        UpdateStatistics();
    case PROCESS_CACHE:
        if( !m_bAborted && !m_bWantAbort )
        {
            NX_DBG("[PROCESS_CACHE] Processing cache: " << m_lstCtrlWriter.size());
            // Do the job
            try
            {
                ProcessCache();
            }
            catch( yat::Exception &e )
            {
                e.dump();
            }
        }
        break;

    case RESET_STATS:
        ::memset(&m_Stats, 0, sizeof(Statistics));
        break;

    default:
        break;
    }
}

//-----------------------------------------------------------------------------
// StreamBufferTask::ProcessCache
//-----------------------------------------------------------------------------
void StreamBufferTask::ProcessCache()
{
    NX_SCOPE_DBG("StreamBufferTask::ProcessCache");

    while( true )
    {
        bool unlocked = false;
        m_mtxLock.lock();
        if( !m_lstCtrlWriter.empty() )
        {
            try 
            {

                StreamBufferPtr ptrRecCtrl = m_lstCtrlWriter.begin()->first;
                NexusFileWriterPtr ptrFile = m_lstCtrlWriter.begin()->second;

                if(  ptrFile->Done() && ptrFile.get() != m_ptrCurFileWriter.get() )
                {
                    if( m_lstCtrlWriter.size() > 1 )
                    {
                        // File writer has completed its job => unhold the next
                        if( m_lstCtrlWriter[1].second->IsHold() )
                            m_lstCtrlWriter[1].second->Hold(false);
                    }

                    NX_SCOPE_DBG("Erase writer");
                    m_lstCtrlWriter.erase(m_lstCtrlWriter.begin());

                    m_mtxLock.unlock();
                    unlocked = true;

                    try
                    {
                        ptrFile->CloseFile();
                        ptrRecCtrl->PostWritingProcess();
                    }
                    catch( yat::Exception &e )
                    {
                        if( m_pHandler )
                        {
                            NexusException ex;
                            ex.errors = e.errors;
                            m_pHandler->OnNexusException(ex);
                        }
                    }
                    m_Stats.writer_count--;
                }
                else
                {
                    m_mtxLock.unlock();
                    break;
                }
            }
            catch( ... )
            {
                if( !unlocked )
                {
                    NX_SCOPE_DBG("!!!!!!!!!!!!!!!!!!!!!!!!     PREVENTED DEAD LOCK !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
                    m_mtxLock.unlock();
                }
                throw;
            }
        }
        else
        {
            m_mtxLock.unlock();
            break;
        }
    }
}

//-----------------------------------------------------------------------------
// StreamBufferTask::Synchronize
//-----------------------------------------------------------------------------
void StreamBufferTask::Synchronize()
{
  NX_SCOPE_DBG("[BEGIN] StreamBufferTask::Synchronize");

  SetStreamBuffer(NULL);

  if (STATE_RUNNING == state() )
  {
      // Task was started : asynchronous mode
      while( !Done() )
        yat::ThreadingUtilities::sleep(0,50000000);
  }
  else
  {
      ProcessCache();
  }
}

//-----------------------------------------------------------------------------
// StreamBufferTask::UpdateStatistics
//-----------------------------------------------------------------------------
void StreamBufferTask::UpdateStatistics()
{
    double average_elapsed = m_tmAverage.elapsed_sec();
    double instant_elapsed = m_tmInstant.elapsed_sec();

    yat::AutoMutex<> _lock(m_mtxLock);
    yat::uint64 total_bytes = 0;
    yat::uint64 written_bytes = 0;
    for( CtrlWriterList::iterator it = m_lstCtrlWriter.begin(); it != m_lstCtrlWriter.end(); it++ )
    {
        NexusFileWriter::Statistics stats = it->second->GetStatistics();
        total_bytes += stats.ui64TotalBytes;
        written_bytes += stats.ui64WrittenBytes;
    }

    // Volumes
    m_Stats.total_bytes = total_bytes;
    m_Stats.written_bytes = written_bytes;

    // Instant rate
    if( instant_elapsed > 1.0 )
    {
        m_Stats.instant_rate_mb_per_sec = float((written_bytes - m_ui64WrittenBytesLastInstant) / instant_elapsed);
        m_ui64WrittenBytesLastInstant = written_bytes;
        m_tmInstant.restart();
    }

    // Average rate
    m_Stats.average_rate_mb_per_sec = float(written_bytes / average_elapsed);
}

//------------------------------------------------------------------------------
// NexusFileWriter::ResetStatistics
//------------------------------------------------------------------------------
void StreamBufferTask::ResetStatistics()
{
    if (STATE_RUNNING == state() )
    {
        post(new yat::Message(RESET_STATS));
    }
    else
    {
        ::memset(&m_Stats, 0, sizeof(Statistics));
    }
}

//------------------------------------------------------------------------------
// NexusFileWriter::GetStatistics
//------------------------------------------------------------------------------
StreamBufferTask::Statistics StreamBufferTask::GetStatistics() const
{
    yat::AutoMutex<> _lock(m_mtxLock);
    return m_Stats;
}

//-----------------------------------------------------------------------------
// StreamBufferTask::Statistics
//-----------------------------------------------------------------------------
StreamBufferTask::Statistics::Statistics()
{
    writer_count = 0;
    total_bytes = 0;
    written_bytes = 0;
}

//==============================================================================
// DataStreamer
//==============================================================================
std::map<std::string, long> DataStreamer::s_mapFileIndex;
std::map<std::string, yat::uint64> DataStreamer::s_mapStartIndex;
yat::Mutex DataStreamer::s_indexLock;
yat::Time DataStreamer::s_tmLastWriteAccess;

//------------------------------------------------------------------------------
// DataStreamer::DataStreamer
//------------------------------------------------------------------------------
DataStreamer::DataStreamer(const std::string &sBufferName, std::size_t nAcquisitionSize,
                           std::size_t nBufferSize)
{ 
    init(sBufferName);
    m_nAcquisitionSize = nAcquisitionSize;
    m_nBufferSize = nBufferSize;
}

//------------------------------------------------------------------------------
// DataStreamer::DataStreamer
//------------------------------------------------------------------------------
DataStreamer::DataStreamer(const std::string &sBufferName, std::size_t nBufferSize)
{ 
    init(sBufferName);
    m_nBufferSize = nBufferSize;
    m_nAcquisitionSize = nBufferSize;
}

//------------------------------------------------------------------------------
// DataStreamer::DataStreamer
//------------------------------------------------------------------------------
DataStreamer::DataStreamer(const std::string &sBufferName, int iAcquisitionSize, int iBufferSize)
{ 
    init(sBufferName);
    m_nBufferSize = static_cast<std::size_t>(iBufferSize);
    m_nAcquisitionSize =  static_cast<std::size_t>(iAcquisitionSize);
}

//------------------------------------------------------------------------------
// DataStreamer::DataStreamer
//------------------------------------------------------------------------------
DataStreamer::DataStreamer(const std::string &sBufferName, int iBufferSize)
{ 
    init(sBufferName);
    m_nBufferSize = static_cast<std::size_t>(iBufferSize);
    m_nAcquisitionSize =  static_cast<std::size_t>(iBufferSize);
}

//------------------------------------------------------------------------------
// DataStreamer::init
//------------------------------------------------------------------------------
void DataStreamer::init(const std::string &sBufferName)
{
    m_sName = sBufferName;
    m_wmWritingMode = NexusFileWriter::ASYNCHRONOUS;
    m_uiStepCompleted = 0;
    m_bInProgress = false;
    m_nBufferSize = 0;
    m_pStreamBufferTask = NULL;
    m_nBufferCount = 0;
    m_nTotalBufferCount = 0;
    m_pHandler = this;
}

//------------------------------------------------------------------------------
// DataStreamer::~DataStreamer
//------------------------------------------------------------------------------
DataStreamer::~DataStreamer() 
{
    NX_SCOPE_DBG("DataStreamer::~DataStreamer");
    if( m_pStreamBufferTask )
    {
        Finalize();
        m_pStreamBufferTask->exit();
    }
}

//------------------------------------------------------------------------------
// DataStreamer::Initialize
//------------------------------------------------------------------------------
void DataStreamer::Initialize(const std::string &sTargetPath, const std::string &sDataSource)
{
    m_sTargetDir = sTargetPath;

    yat::FileName fn(sTargetPath);
    if( !fn.path_exist() )
        throw NexusException(PSZ_FMT("Target path (%s) doesn't exists", PSZ(sTargetPath)),
                             "DataStreamer::Initialize");

    m_pStreamBufferTask = new StreamBufferTask(m_wmWritingMode);

    if( m_pHandler )
        m_pStreamBufferTask->SetExceptionHandler(m_pHandler);

    m_strPrefix = sDataSource;

  yat::StringUtil::replace(&m_strPrefix, "/", "__");
  yat::StringUtil::replace(&m_strPrefix, ",", "_");

    // Thread safety
    yat::AutoMutex<> _lock(s_indexLock);

    NX_DBG("DataStreamer::Initialize s_mapFileIndex[" << m_sName << "] = " << s_mapFileIndex[m_sName]);
    if( s_mapFileIndex[m_sName] == 0 )
    {
        s_mapFileIndex[m_sName] = 1;
    }
}

//------------------------------------------------------------------------------
// DataStreamer::EndRecording
//------------------------------------------------------------------------------
void DataStreamer::SetExceptionHandler(IExceptionHandler *pHandler)
{
    m_pHandler = pHandler;
    if( m_pStreamBufferTask )
        m_pStreamBufferTask->SetExceptionHandler(pHandler);
}

//------------------------------------------------------------------------------
// DataStreamer::EndRecording
//------------------------------------------------------------------------------
void DataStreamer::EndRecording()
{
    Finalize();
}

//------------------------------------------------------------------------------
// DataStreamer::Finalize
//------------------------------------------------------------------------------
void DataStreamer::Finalize()
{
   // Thread safety
    yat::AutoMutex<> _lock(m_mtxLock);

    PrivCloseBuffer();

    std::map<std::string, DataItemInfo>::iterator itDataItem = m_mapDataItem.begin();
    for( ; itDataItem != m_mapDataItem.end(); itDataItem++ )
    {
        itDataItem->second.ptrDatasetWriter->Stop();
    }

    if( m_pStreamBufferTask )
    {
        m_pStreamBufferTask->Synchronize();
    }
}

//------------------------------------------------------------------------------
// DataStreamer::Reset
//------------------------------------------------------------------------------
void DataStreamer::Reset()
{
    Synchronize();
}

//------------------------------------------------------------------------------
// DataStreamer::SetDataItemMemoryMode
//------------------------------------------------------------------------------
void DataStreamer::SetDataItemMemoryMode(const std::string &sItemName, MemoryMode mode)
{
    // Thread safety
    yat::AutoMutex<> _lock(m_mtxLock);

  NX_SCOPE_DBG("DataStreamer::SetDataItemMemoryMode");

    if( m_mapDataItem.count(sItemName) == 0 )
        throw NexusException(PSZ_FMT("Item %s is not defined!", PSZ(sItemName)),
                             "DataStreamer::SetDataItemMemoryMode");

    // Don't throw exception if it's not applicable due to data item rank
    if( m_mapDataItem[sItemName].nRank >= 1 )
        m_mapDataItem[sItemName].eMemoryMode = mode;
}

//------------------------------------------------------------------------------
// DataStreamer::AddDataItem
//------------------------------------------------------------------------------
void DataStreamer::AddDataItem(const std::string &sItemName, const std::vector<int>& viDimSize,
                               bool bDataSignal)
{
    DataStreamer::AddDataItem(sItemName, viDimSize, bDataSignal ? NX_DATA_SIGNAL : NX_DATA);
}

//------------------------------------------------------------------------------
// DataStreamer::AddDataItem0D
//------------------------------------------------------------------------------
void DataStreamer::AddDataItem0D(const std::string &sItemName, bool bDataSignal)

{
    std::vector<int> viDimSize;
    DataStreamer::AddDataItem(sItemName, viDimSize, bDataSignal);
}

//------------------------------------------------------------------------------
// DataStreamer::AddDataItem1D
//------------------------------------------------------------------------------
void DataStreamer::AddDataItem1D(const std::string &sItemName, int iDimSize, bool bDataSignal)

{
    std::vector<int> viDimSize = std::vector<int>(1);
    viDimSize[0] = iDimSize;
    DataStreamer::AddDataItem(sItemName, viDimSize, bDataSignal);
}

//------------------------------------------------------------------------------
// DataStreamer::AddDataItem2D
//------------------------------------------------------------------------------
void DataStreamer::AddDataItem2D(const std::string &sItemName, int iSizeDim1, int iSizeDim2,
                                 bool bDataSignal)
{
    std::vector<int> viDimSize = std::vector<int>(2);
    viDimSize[0] = iSizeDim1;
    viDimSize[1] = iSizeDim2;
    DataStreamer::AddDataItem(sItemName, viDimSize, bDataSignal);
}

//------------------------------------------------------------------------------
// DataStreamer::AddDataItem
//------------------------------------------------------------------------------
void DataStreamer::AddDataItem(const std::string &sItemName, const std::vector<int>& viDataSize,
                               DataItemCategory nxCat)
{
   // Thread safety
    yat::AutoMutex<> _lock(m_mtxLock);

  NX_SCOPE_DBG("DataStreamer::AddDataItem");
    static int sSignalId = 1;

    if( m_mapDataItem.count(sItemName) != 0 )
        throw NexusException(PSZ_FMT("Can't have two sensors with same name (%s) !", PSZ(sItemName)),
                             "DataStreamer::AddDataItem");
    DataItemInfo aItemInfo;
    aItemInfo.uiPushCount = 0;
    aItemInfo.nPendingData = 0;
    aItemInfo.strDatasetName = sItemName;
    aItemInfo.iBatchIndex = 1;
    aItemInfo.bAttributesWritten = false;
    aItemInfo.eMemoryMode = COPY;
    aItemInfo.nRank = viDataSize.size();

    // without type, allocated a untyped datasetWriter
    NX_DBG("Allocating DatasetWriter...");
    aItemInfo.ptrDatasetWriter.reset(new DatasetWriter(viDataSize, m_nBufferSize, 64));
    NX_DBG("DatasetWriter allocated");
    aItemInfo.ptrDatasetWriter->SetNexusFileWriter(m_pStreamBufferTask->FileWriter());
    aItemInfo.ptrDatasetWriter->SetFlushListener(this);

    // vector containing the absolute position inside the final dataset
    aItemInfo.viCurrentStart.resize(viDataSize.size() + 1, 0);
    aItemInfo.viNextStart.resize(viDataSize.size() + 1, 0);
    aItemInfo.viTotalSize.resize(viDataSize.size() + 1, 0);

    // Initial value
    aItemInfo.viNextStart[0] = s_mapStartIndex[sItemName];

    aItemInfo.viTotalSize[0] = m_nAcquisitionSize;
    for( yat::uint8 ui = 1; ui < viDataSize.size() + 1; ui++)
        aItemInfo.viTotalSize[ui] = viDataSize[ui - 1];

    if( nxCat == NX_DATA_SIGNAL )
        aItemInfo.ptrDatasetWriter->AddIntegerAttribute("signal", sSignalId++);
    
    m_mapDataItem[sItemName] = aItemInfo;

    if( !m_sDevice.empty() )
        aItemInfo.ptrDatasetWriter->AddStringAttribute("data_source", m_sDevice);
}

//------------------------------------------------------------------------------
// DataStreamer::AddDataAxis
//------------------------------------------------------------------------------
void DataStreamer::AddDataAxis(const std::string &sItemName, int iDimension, int iOrder)
{
    // Thread safety
    yat::AutoMutex<> _lock(m_mtxLock);

    std::vector<int> viDataDim;
    AddDataItem(sItemName, viDataDim, NX_AXIS);
    DataItemInfo &aItemInfo = GetDataItemInfo(sItemName);
    aItemInfo.ptrDatasetWriter->AddIntegerAttribute("axis", iDimension);
    aItemInfo.ptrDatasetWriter->AddIntegerAttribute("primary", iOrder);
}

//------------------------------------------------------------------------------
// DataStreamer::IsExistingItem
//------------------------------------------------------------------------------
bool DataStreamer::IsExistingItem(const std::string &sItemName)
{
    // Thread safety
    yat::AutoMutex<> _lock(m_mtxLock);

  NX_SCOPE_DBG("DataStreamer::IsExistingItem");
    if( m_mapDataItem.count(sItemName) > 0 )
        return true;
    else
        return false;
}

//------------------------------------------------------------------------------
// DataStreamer::SetPath
//------------------------------------------------------------------------------
void DataStreamer::SetPath(const std::string & /*sName*/, const std::string & /*sPath*/) 
{
}

//------------------------------------------------------------------------------
// DataStreamer::SetDataItemNodeName
//------------------------------------------------------------------------------
void DataStreamer::SetDataItemNodeName(const std::string & /*sName*/, const std::string & /*sNodeName*/) 
{
}

//==============================================================================
// Tools methods
//==============================================================================

//------------------------------------------------------------------------------
// DataStreamer::PushData
//------------------------------------------------------------------------------
void DataStreamer::PrivPushDataItems(DataItemInfo& ItemInfo, const void *tData,
                                     std::size_t nCount, bool bNoCopy)
{
    //  NX_SCOPE_DBG("DataStreamer::PrivPushDataItems");

    s_mapStartIndex[ItemInfo.strDatasetName] += nCount;
    ItemInfo.viNextStart[0] += nCount;

    //  NX_DBG("ItemInfo.ptrDatasetWriter->PushData: " << ItemInfo.uiPushCount << " + " << nCount);
    ItemInfo.ptrDatasetWriter->PushData((void*)tData, nCount,
                                        (bNoCopy && ItemInfo.eMemoryMode == NO_COPY) ? true : false);
    ItemInfo.uiPushCount += nCount;

    s_tmLastWriteAccess.set_current();

    // Update statistics
    m_Stats.ui64TotalBytes += nCount * ItemInfo.ptrDatasetWriter->DataItemSize();
}

//------------------------------------------------------------------------------
// DataStreamer::PushData
//------------------------------------------------------------------------------
void DataStreamer::PushPendingData(DataItemInfo& ItemInfo)
{
    NX_SCOPE_DBG("DataStreamer::PushPendingData");

    std::size_t nPush = 0;
    std::size_t nPendingData = ItemInfo.nPendingData;
    std::size_t nCurrentBufferPushedData = ItemInfo.uiPushCount - (m_nBufferCount * m_nBufferSize);
    if( nCurrentBufferPushedData + nPendingData <= m_nBufferSize )
        nPush = nPendingData;
    else
        nPush = m_nBufferSize - nCurrentBufferPushedData;

    // Push as much data as possible
    PrivPushDataItems(ItemInfo, (void *)(ItemInfo.mbPendingData.buf()), nPush, false);

    // Update pending data buffer
    if( nPush != nPendingData )
    {
        ItemInfo.mbPendingData.move_bloc(0, nPush * ItemInfo.ptrDatasetWriter->DataItemSize(),
                                         (nPendingData - nPush) * ItemInfo.ptrDatasetWriter->DataItemSize());
        ItemInfo.mbPendingData.set_len((nPendingData - nPush) * ItemInfo.ptrDatasetWriter->DataItemSize());
    }
    else
    {
        ItemInfo.mbPendingData.set_len(0);
    }
    ItemInfo.nPendingData = nPendingData - nPush;
}

//------------------------------------------------------------------------------
// DataStreamer::PrivOpenNewbuffer
//------------------------------------------------------------------------------
void DataStreamer::PrivOpenNewbuffer()
{
    NX_SCOPE_DBG("DataStreamer::PrivOpenNewbuffer");

    if(!m_pStreamBufferTask )
         throw NexusException("Uninitialized object", "DataStreamer::GetDataItemInfo");

    std::string sFileName;
    {
        yat::AutoMutex<> _lock(s_indexLock);
        sFileName = GenerateBufferName(m_sName, s_mapFileIndex[m_sName], m_strPrefix);
        s_mapFileIndex[m_sName] += 1;
    }

    yat::AutoMutex<> _lock2(m_mtxLock);
    StreamBufferPtr ptr(new StreamBuffer(m_sTargetDir, PSZ(sFileName)));
    ptr->WriteMode(m_wmWritingMode);

    m_pStreamBufferTask->SetStreamBuffer(ptr);

    // To get notified when some data is written
    m_pStreamBufferTask->FileWriter()->AddNotificationHandler(this);


    std::map<std::string, DataItemInfo>::iterator itDataItem = m_mapDataItem.begin();
    for( ; itDataItem != m_mapDataItem.end(); itDataItem++ )
    {
        DataItemInfo &ItemInfo = itDataItem->second;
        if( m_nAcquisitionSize - m_uiStepCompleted < m_nBufferSize )
        {
            // Last file containing a fraction of previous buffers
            nxcpp::DataShape shape;
            shape.push_back(m_nAcquisitionSize - m_uiStepCompleted);
            ItemInfo.ptrDatasetWriter->SetMatrix(shape);
        }

        ItemInfo.ptrDatasetWriter->SetNexusFileWriter(m_pStreamBufferTask->FileWriter());
        std::string strPath = yat::StringUtil::str_format("/%s<NXentry>/scan_data<NXdata>",
                                                      PSZ(m_pStreamBufferTask->StreamBuffer()->NXentryName()));

        ItemInfo.ptrDatasetWriter->SetPath(strPath, itDataItem->second.strDatasetName);
        ItemInfo.bAttributesWritten = false;

        if( !ItemInfo.mbPendingData.is_empty() )
        {
            // Push pending data
            PushPendingData(ItemInfo);
        }
    }
}

//------------------------------------------------------------------------------
// DataStreamer::PrivIsBufferOpen
//------------------------------------------------------------------------------
bool DataStreamer::PrivIsBufferOpen()
{
    return m_pStreamBufferTask->FileWriter();
}

//------------------------------------------------------------------------------
// DataStreamer::PrivCloseBuffer
//------------------------------------------------------------------------------
void DataStreamer::PrivCloseBuffer(bool bStopMark)
{
    // Flush all remaining data
    std::map<std::string, DataItemInfo>::iterator itDataItem = m_mapDataItem.begin();
    for( ; itDataItem != m_mapDataItem.end(); itDataItem++ )
    {
        if( m_uiStepCompleted >= (unsigned int)m_nAcquisitionSize )
        {
            // Reset data pushed counter for infinite mode
            itDataItem->second.uiPushCount = 0;

            // Increasing data batch counter for infinite mode
            itDataItem->second.iBatchIndex++;

            // Add the 'end buffer' mark
            itDataItem->second.ptrDatasetWriter->AddIntegerAttribute(NX_ATTR_SLAB_END, 1);
        }

        if( bStopMark )
            itDataItem->second.ptrDatasetWriter->AddIntegerAttribute(NX_ATTR_SLAB_STOP, 1);

        itDataItem->second.ptrDatasetWriter->Stop();
    }

    if( m_pStreamBufferTask->FileWriter() )
    {
        // m_pStreamBufferTask->FileWriter()->SynchronizeMsg();

        // Release current nexus file writer
        m_pStreamBufferTask->SetStreamBuffer(NULL);
    }

    m_nTotalBufferCount++;

    if( m_uiStepCompleted >= (unsigned int)m_nAcquisitionSize )
    { // For next batch in infinite mode
        m_uiStepCompleted = 0;
        m_nBufferCount = 0;
    }
    else
    {
        m_nBufferCount++;
    }
}

//------------------------------------------------------------------------------
// DataStreamer::BufferingControl
//------------------------------------------------------------------------------
void DataStreamer::BufferingControl()
{
    // Check push counter and open a new buffer if needed
    unsigned int uiStepCompleted = UINT_MAX;
    std::map<std::string, DataItemInfo>::iterator it;
    for( it = m_mapDataItem.begin(); it != m_mapDataItem.end(); it++ )
    {
        if( it->second.uiPushCount < uiStepCompleted )
            uiStepCompleted = it->second.uiPushCount;
    }

    // Another acquisition step completed ?
    if( uiStepCompleted > m_uiStepCompleted )
    {
        m_uiStepCompleted = uiStepCompleted;
        // Completed a number of acquisition steps multiple of m_nBufferSize ?
        if( uiStepCompleted % m_nBufferSize == 0 || uiStepCompleted >= (unsigned int)m_nAcquisitionSize )
        {
            NX_DBG("Need to open new buffer file (" << uiStepCompleted << "; " << m_nAcquisitionSize << ')');
            PrivCloseBuffer();
        }
    }
}

//------------------------------------------------------------------------------
// DataStreamer::GetDataItemInfo
//------------------------------------------------------------------------------
DataStreamer::DataItemInfo &DataStreamer::GetDataItemInfo(const std::string &sItemName)

{
    std::map<std::string, DataItemInfo>::iterator it = m_mapDataItem.find(sItemName);
    if( it == m_mapDataItem.end() )
    {
        throw NexusException(PSZ_FMT("Invalid sensor name (%s)", sItemName.c_str()),
                             "DataStreamer::GetDataItemInfo");
    }
    return it->second;
}

//------------------------------------------------------------------------------
// DataStreamer::GetDataItemInfoFromDatasetName
//------------------------------------------------------------------------------
DataStreamer::DataItemInfo &DataStreamer::GetDataItemInfoFromDatasetName(const std::string &strDataset)
{
    std::map<std::string, DataItemInfo>::iterator it = m_mapDataItem.begin();
    for( ; it != m_mapDataItem.end(); it++ )
    {
    if( yat::StringUtil::is_equal(strDataset, it->second.strDatasetName) )
            return it->second;
    }

    // Not found
    throw NexusException(PSZ_FMT("Invalid sensor name (%s)", strDataset.c_str()),
                         "DataStreamer::GetDataItemInfoFromDatasetName");
}

//------------------------------------------------------------------------------
// DataStreamer::PushIntegerAttribute
//------------------------------------------------------------------------------
void DataStreamer::PushIntegerAttribute(const std::string &sItemName, const std::string &sName, long lValue)
{
    // Thread safety
    yat::AutoMutex<> _lock(m_mtxLock);

    GetDataItemInfo(sItemName).ptrDatasetWriter->AddIntegerAttribute(sName, lValue);
}

//------------------------------------------------------------------------------
// DataStreamer::PushFloatAttribute
//------------------------------------------------------------------------------
void DataStreamer::PushFloatAttribute(const std::string &sItemName, const std::string &sName, double dValue)
{
    // Thread safety
    yat::AutoMutex<> _lock(m_mtxLock);

    GetDataItemInfo(sItemName).ptrDatasetWriter->AddFloatAttribute(sName, dValue);
}

//------------------------------------------------------------------------------
// DataStreamer::PushStringAttribute
//------------------------------------------------------------------------------
void DataStreamer::PushStringAttribute(const std::string &sItemName, const std::string &sName,
                                       const std::string &strValue)
{
    // Thread safety
    yat::AutoMutex<> _lock(m_mtxLock);
    GetDataItemInfo(sItemName).ptrDatasetWriter->AddStringAttribute(sName, strValue);
}

//------------------------------------------------------------------------------
// DataStreamer::Abort
//------------------------------------------------------------------------------
void DataStreamer::Abort(bool bSynchronize)
{
    m_pStreamBufferTask->Abort();

    if( bSynchronize )
        Synchronize();
}

//------------------------------------------------------------------------------
// DataStreamer::Stop
//------------------------------------------------------------------------------
void DataStreamer::Stop()
{
    // Thread safety
    yat::AutoMutex<> _lock(m_mtxLock);

    NX_SCOPE_DBG("DataStreamer::Stop");
    PrivCloseBuffer(true);
    m_pStreamBufferTask->Synchronize();
}

//------------------------------------------------------------------------------
// DataStreamer::IsDone
//------------------------------------------------------------------------------
bool DataStreamer::IsDone()
{
    // Thread safety
    yat::AutoMutex<> _lock(m_mtxLock);

    return m_pStreamBufferTask->Done();
}

//------------------------------------------------------------------------------
// DataStreamer::Synchronize
//------------------------------------------------------------------------------
void DataStreamer::Synchronize()
{
    // Thread safety
    yat::AutoMutex<> _lock(m_mtxLock);

  NX_SCOPE_DBG("DataStreamer::Synchronize");
    m_pStreamBufferTask->Synchronize();
}

//------------------------------------------------------------------------------
// DataStreamer::IsBufferPathAvailable
//------------------------------------------------------------------------------
bool DataStreamer::IsBufferPathAvailable(const std::string &sBaseName, long lIndex)
{
  yat::FileName fn(m_sTargetDir, yat::StringUtil::str_format("%s_%06d.nxs", PSZ(sBaseName), lIndex));
    bool bExists = fn.file_exist();
    return !bExists;
}

//------------------------------------------------------------------------------
// DataStreamer::CheckBufferDirectory
//------------------------------------------------------------------------------
bool DataStreamer::CheckBufferDirectory(const std::string &sBaseName, const std::string &strPrefix)
{
    std::string sFileNameWithoutIndex, strFileName;

    // Construct a standard file name for buffer. The name has the following
    // form: "" + nom_buffer + "_" + index + ".nxs"
    if( !strPrefix.empty() )
        yat::StringUtil::printf(&sFileNameWithoutIndex, "%s_%s", PSZ(strPrefix), PSZ(sBaseName));
    else
        yat::StringUtil::printf(&sFileNameWithoutIndex, "%s", PSZ(sBaseName));
    yat::StringUtil::to_lower(&sFileNameWithoutIndex);

    yat::FileName fn(m_sTargetDir);
    yat::FileEnum fe(fn.full_name(), yat::FileEnum::ENUM_FILE);
    while( fe.find() )
    {
        strFileName = fe.name();
        if( yat::StringUtil::start_with(strFileName, PSZ(sFileNameWithoutIndex)) )
        {
            NX_DBG("DataStreamer::CheckBufferDirectory file found: " << strFileName);
            return false;
        }
    }

    // No buffer file found
    NX_DBG("DataStreamer::CheckBufferDirectory no file found!");
    return true;
}

//------------------------------------------------------------------------------
// DataStreamer::ResetBufferIndex
//------------------------------------------------------------------------------
void DataStreamer::ResetBufferIndex()
{
    // Thread safety
    yat::AutoMutex<> _lock(s_indexLock);

    s_mapFileIndex.clear();
    s_mapStartIndex.clear();
}

//------------------------------------------------------------------------------
// DataStreamer::GenerateBufferName
//------------------------------------------------------------------------------
std::string DataStreamer::GenerateBufferName(const std::string &sBaseName, long lIndex,
                                             const std::string &strPrefix)
{
    std::string sFileName;

    // Construct a standard file name for buffer. The name has the following
    // form: "" + nom_buffer + "_" + index + ".nxs"
    if( !strPrefix.empty() )
        yat::StringUtil::printf(&sFileName, "%s_%s_%06d.nxs", PSZ(strPrefix), PSZ(sBaseName), lIndex);
    else
        yat::StringUtil::printf(&sFileName, "%s_%06d.nxs", PSZ(sBaseName), lIndex);
    yat::StringUtil::to_lower(&sFileName);
    return sFileName;
}

//------------------------------------------------------------------------------
// DataStreamer::Clean
//------------------------------------------------------------------------------
void DataStreamer::Clean()
{
    // Thread safety
    yat::AutoMutex<> _lock(m_mtxLock);

  NX_SCOPE_DBG("DataStreamer::Clean");
    std::string sFileName;
    std::string sFileNamePattern;

    if( !m_strPrefix.empty() )
        sFileNamePattern = m_strPrefix + "_" + m_sName + "_??????.nxs";
    else
        sFileNamePattern = m_sName + "_??????.nxs";

    yat::StringUtil::to_lower(&sFileNamePattern);
    std::vector<std::string> vFileNames;

    yat::FileEnum fTgtFolder (m_sTargetDir + '/');

    while( fTgtFolder.find() )
    {
        sFileName = fTgtFolder.name_ext();
        yat::StringUtil::to_lower(&sFileName);
        if( yat::StringUtil::match(sFileName, PSZ(sFileNamePattern)) )
        {
            vFileNames.push_back(fTgtFolder.full_name());
        }
    }

    for( std::size_t i = 0; i < vFileNames.size(); ++i )
    {
        NX_DBG("Delete file: " << vFileNames[i]);
        yat::FileName(vFileNames[i]).remove();
    }
}

//------------------------------------------------------------------------------
// DataStreamer::OnFlushData
//------------------------------------------------------------------------------
void DataStreamer::OnFlushData(DatasetWriter* pWriter)
{
    DataItemInfo &aItemInfo = GetDataItemInfoFromDatasetName(pWriter->DatasetName());

    if( !aItemInfo.bAttributesWritten )
    {
        pWriter->AddStringAttribute(NX_ATTR_NODE_NAME, pWriter->DatasetName());
        pWriter->AddStringAttribute(NX_ATTR_DATA_SIZE, VectorToStr<int>(aItemInfo.viTotalSize));
        pWriter->AddStringAttribute(NX_ATTR_SLAB_START, VectorToStr<int>(aItemInfo.viCurrentStart));
        pWriter->AddIntegerAttribute(NX_ATTR_ACQ_BATCH, aItemInfo.iBatchIndex);
        aItemInfo.bAttributesWritten = true;
    }
    aItemInfo.viCurrentStart = aItemInfo.viNextStart;
}

//------------------------------------------------------------------------------
// DataStreamer::OnWriteSubSet
//------------------------------------------------------------------------------
void DataStreamer::OnWriteSubSet(NexusFileWriter* source_p, const std::string& dataset_path, int start_pos[MAX_RANK], int dim[MAX_RANK])
{
    NX_SCOPE_DBG("DataStreamer::OnWriteSubSet");

    NX_DBG("Data written for " << dataset_path << " (" << source_p << "): " << dim[0] << " items");

    if( !m_write_notif_cb.is_empty() )
    {
        // extracting item name
        std::string item, path = dataset_path;
        yat::StringUtil::extract_token_right(&item, '/', &path);

        WriteNotification notif;
        notif.item_name = item;
        notif.write_count = dim[0];

        // invoke callback
        m_write_notif_cb(notif);
    }
}

//------------------------------------------------------------------------------
// DataStreamer::OnCloseFile
//------------------------------------------------------------------------------
void DataStreamer::OnCloseFile(NexusFileWriter* source_p, const std::string& file_path)
{
    NX_SCOPE_DBG("DataStreamer::OnCloseFile");
    NX_DBG("Nexus file closed: " << file_path);
}

//------------------------------------------------------------------------------
// DataStreamer::OnWrite
//------------------------------------------------------------------------------
void DataStreamer::OnWrite(NexusFileWriter* /*source_p*/, const std::string& /*dataset_path*/)
{
}

//==============================================================================
// Tools methods
//==============================================================================
//------------------------------------------------------------------------------
// DataStreamer::Statistics::Statistics()
//------------------------------------------------------------------------------
DataStreamer::Statistics::Statistics()
{
    ::memset(this, 0, sizeof(Statistics));
}

//------------------------------------------------------------------------------
// DataStreamer::ResetStatistics()
//------------------------------------------------------------------------------
void DataStreamer::ResetStatistics()
{
    // Thread safety
    yat::AutoMutex<> _lock(m_mtxLock);

    ::memset(&m_Stats, 0, sizeof(DataStreamer::Statistics));
    if( m_pStreamBufferTask )
        m_pStreamBufferTask->ResetStatistics();
}

//------------------------------------------------------------------------------
// DataStreamer::GetetStatistics()
//------------------------------------------------------------------------------
DataStreamer::Statistics DataStreamer::GetStatistics() const
{
    // Thread safety
    yat::AutoMutex<> _lock(m_mtxLock);

    if( m_pStreamBufferTask )
    {
        StreamBufferTask::Statistics manager_statistics = m_pStreamBufferTask->GetStatistics();
        m_Stats.ui64WrittenBytes = manager_statistics.written_bytes;
        m_Stats.ui16ActiveWriters = manager_statistics.writer_count;
        if( m_Stats.ui16ActiveWriters > m_Stats.ui16MaxSimultaneousWriters )
            m_Stats.ui16MaxSimultaneousWriters = m_Stats.ui16ActiveWriters;

        m_Stats.ui64PendingBytes = m_Stats.ui64TotalBytes - m_Stats.ui64WrittenBytes;
        if( m_Stats.ui64PendingBytes > m_Stats.ui64MaxPendingBytes )
            m_Stats.ui64MaxPendingBytes = m_Stats.ui64PendingBytes;
    }
    return m_Stats;
}

//------------------------------------------------------------------------------
// DataStreamer::OnNexusException
//------------------------------------------------------------------------------
void DataStreamer::OnNexusException(const NexusException &e)
{
    YAT_LOG_EXCEPTION( e );
}



} // namespace


////////////////////////////////////////////////////////////////////////////////// 
// python ctypes wrapper (using ctype to avoid to use boost)
////////////////////////////////////////////////////////////////////////////////// 
// DO NOT CHANGE ANYTHING WITHOUT VALIDATING YOUR MODS WITH A FLYSCAN EXPERT
////////////////////////////////////////////////////////////////////////////////// 

//- ctor --------------------------------------------
nxcpp::DataStreamer * nds_new (const char* dsn, yat::uint32 nas, yat::uint32 nbs)
{
    nxcpp::DataStreamer * ds = 0;

    try
    {
        ds = ( nas != 0 )
                ? new nxcpp::DataStreamer(std::string(dsn), std::size_t(nas), std::size_t(nbs))
                : new nxcpp::DataStreamer(std::string(dsn), std::size_t(nbs));
    }
    catch (nxcpp::NexusException& e)
    {
        e.PrintMessage();
        if ( ds ) delete ds;
        ds = 0;
    }
    catch (...)
    {
        if ( ds ) delete ds;
        ds = 0;
    }

    return ds;
}

//- dtor --------------------------------------------
void nds_delete (nxcpp::DataStreamer* ds)
{
    try
    {
        delete ds;
    }
    catch (nxcpp::NexusException& e)
    {
        e.PrintMessage();
    }
    catch (...)
    {
        //- noop
    }
}

//- init --------------------------------------------
int nds_init (nxcpp::DataStreamer* ds, const char* tp, const char* dp)
{
    if ( ds )
    {
        try
        {
            ds->Initialize(std::string(tp), std::string(dp));
        }
        catch (nxcpp::NexusException& e)
        {
            e.PrintMessage();
            return -1;
        }
        catch (...)
        {
            return -1;
        }
    }
    
    return 0;
}


//- stop -------------------------------------------
int nds_stop (nxcpp::DataStreamer* ds)
{
    
    if ( ds )
    {
        try
        {
            ds->Stop();
        }
        catch (nxcpp::NexusException& e)
        {
            e.PrintMessage();
            return -1;
        }
        catch (...)
        {
            return -1;
        }
    }

    return 0;
}

//- fini --------------------------------------------
int nds_fini (nxcpp::DataStreamer* ds)
{
    if ( ds )
    {
        try
        {
            ds->Finalize();
        }
        catch (nxcpp::NexusException& e)
        {
            e.PrintMessage();
            return -1;
        }
        catch (...)
        {
            return -1;
        }
    }

    return 0;
}

//- 0D data item ------------------------------------
int nds_add_data_item_0D (nxcpp::DataStreamer* ds,
                          const char* in)
{
    
    if ( ds )
    {
        try
        {
            ds->AddDataItem0D(std::string(in));
        }
        catch (nxcpp::NexusException& e)
        {
            e.PrintMessage();
            return -1;
        }
        catch (...)
        {
            return -1;
        }
    }

    return 0;
}

//- 1D data item ------------------------------------
int nds_add_data_item_1D (nxcpp::DataStreamer* ds,
                          const char* in,
                          yat::uint32 sd1)
{
    if ( ds )
    {
        try
        {
            ds->AddDataItem1D(std::string(in), sd1);
        }
        catch (nxcpp::NexusException& e)
        {
            e.PrintMessage();
            return -1;
        }
        catch (...)
        {
            return -1;
        }
    }
    return 0;
}

//- 2D data item ------------------------------------
int nds_add_data_item_2D (nxcpp::DataStreamer* ds,
                          const char* in,
                          yat::uint32 sd1,
                          yat::uint32 sd2)
{
    
    if ( ds )
    {
        try
        {
            ds->AddDataItem2D(std::string(in), sd1, sd2);
        }
        catch (nxcpp::NexusException& e)
        {
            e.PrintMessage();
            return -1;
        }
        catch (...)
        {
            return -1;
        }
    }
    return 0;
}

//- init --------------------------------------------
int nds_set_data_item_memory_mode (nxcpp::DataStreamer* ds,
                                   const char* in,
                                   yat::uint32 mem_mode)
{
    //- static const yat::int32 MEM_MODE_COPY    =  0;
    //- static const yat::int32 MEM_MODE_NO_COPY =  1;

    if ( ds )
    {
        try
        {
            nxcpp::DataStreamer::MemoryMode mm = mem_mode
                    ? nxcpp::DataStreamer::NO_COPY
                    : nxcpp::DataStreamer::COPY;
            ds->SetDataItemMemoryMode(std::string(in), mm);
        }
        catch (nxcpp::NexusException& e)
        {
            e.PrintMessage();
            return -1;
        }
        catch (...)
        {
            return -1;
        }
    }
    return 0;
}

//- push data into the stream -----------------------
int nds_push_data (nxcpp::DataStreamer* ds,
                   const char* data_item_name,
                   yat::uint32 data_type,
                   yat::uint32 num_items_in_data_buffer,
                   void* data_buffer)
{
    static const yat::int32 NUMPY_TYPE_BOOL    =  0;
    static const yat::int32 NUMPY_TYPE_INT8    =  1;
    static const yat::int32 NUMPY_TYPE_UINT8   =  2;
    static const yat::int32 NUMPY_TYPE_INT16   =  3;
    static const yat::int32 NUMPY_TYPE_UINT16  =  4;
    static const yat::int32 NUMPY_TYPE_INT32   =  5;
    static const yat::int32 NUMPY_TYPE_UINT32  =  6;
    static const yat::int32 NUMPY_TYPE_INT64   =  7;
    static const yat::int32 NUMPY_TYPE_UINT64  =  8;
    static const yat::int32 NUMPY_TYPE_FLOAT32 =  9;
    static const yat::int32 NUMPY_TYPE_FLOAT64 = 10;

    if ( ! ds )
        return 0;

    
#define NUMPY_TYPE_CASE(ID, DT) \
    case ID: \
    try \
    { \
    ds->PushData(std::string(data_item_name), reinterpret_cast<DT*>(data_buffer), num_items_in_data_buffer); \
} \
    catch (nxcpp::NexusException& e) \
    { \
    e.PrintMessage(); \
    return -1; \
} \
    catch ( ... ) \
    { \
    return -1; \
} \
    break

    switch ( data_type )
    {
    NUMPY_TYPE_CASE(NUMPY_TYPE_BOOL, yat::uint8);
    NUMPY_TYPE_CASE(NUMPY_TYPE_INT8, yat::int8);
    NUMPY_TYPE_CASE(NUMPY_TYPE_UINT8, yat::uint8);
    NUMPY_TYPE_CASE(NUMPY_TYPE_INT16, yat::int16);
    NUMPY_TYPE_CASE(NUMPY_TYPE_UINT16, yat::uint16);
    NUMPY_TYPE_CASE(NUMPY_TYPE_INT32, yat::int32);
    NUMPY_TYPE_CASE(NUMPY_TYPE_UINT32, yat::uint32);
    NUMPY_TYPE_CASE(NUMPY_TYPE_INT64, yat::int64);
    NUMPY_TYPE_CASE(NUMPY_TYPE_UINT64, yat::uint64);
    NUMPY_TYPE_CASE(NUMPY_TYPE_FLOAT32, float);
    NUMPY_TYPE_CASE(NUMPY_TYPE_FLOAT64, double);
    default:
        return -1;
        break;
    }

    return 0;
}

