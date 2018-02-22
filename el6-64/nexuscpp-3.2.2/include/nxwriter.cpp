//*****************************************************************************
/// Synchrotron SOLEIL
///
/// Nexus C++ API over NAPI
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

#ifdef WIN32
  #pragma warning(disable:4786)
#endif

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <list>
#include <map>
#include <vector>
#include <stack>
#include <set>

// YAT
#include "yat/LogHelper.h"
#include "yat/threading/Task.h"
#include "yat/threading/Utilities.h"
#include "yat/utils/String.h"
#include "yat/utils/Logging.h"
#include "yat/file/FileName.h"
#include "yat/time/Time.h"

#include <nexuscpp/nxwriter.h>

#ifdef WRITER_DEBUG
  #define DBG_PREFIX "[" <<  std::setfill(' ') << std::setw(10) << (void*)(this) << "]["\
                     << std::setfill('0') << std::setw(8)\
                     << yat::ThreadingUtilities::self() << "] - " << std::dec

  #define WRITER_DBG(s) std::cout << DBG_PREFIX << s << std::endl
#else
  #define WRITER_DBG(s)
#endif

// Error msgs

const char CANNOT_PUSH_DATASET[]    = "Cannot push dataset";
const char CANNOT_PUSH_ATTR[]       = "Cannot push Nexus attribute";

const char CANNOT_PUSH_DATA[]       = "Cannot push data";

// Reasons
const char BAD_POLICY[]             = "Bad policy!";
const char DATA_OVERFLOW[]          = "Data overflow";

#define MEGA_BYTE (1024 * 1024)
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define MAX(x,y) ((x) > (y) ? (x) : (y))

typedef std::vector<int> DimVector;

namespace nxcpp
{

const DataShape g_empty_shape;

//=============================================================================
// Object containing a Nexus dataset
//
//=============================================================================
class NexusDatasetContainer
{
private:
  std::string     m_strPath;
  NexusDataSetPtr m_ptrDataSet;

public:
  NexusDatasetContainer(const std::string &strPath, NexusDataSetPtr ptrDataSet): m_strPath(strPath)
  {
    m_ptrDataSet = ptrDataSet;
  }

  NexusDataSetPtr Content() const { return m_ptrDataSet; }
  const std::string &Path()    const { return m_strPath; }
};
typedef yat::SharedPtr<NexusDatasetContainer, yat::Mutex> NexusDatasetContainerRef;
typedef std::list<NexusDatasetContainerRef> NexusDatasetContainerRefList;

//=============================================================================
// Object containing a Nexus attribute
//
//=============================================================================
class NexusAttributeContainer
{
private:
  std::string   m_strPath;
  NexusAttrPtr  m_ptrAttr;

public:
  NexusAttributeContainer(const std::string &strPath, NexusAttrPtr ptrAttr): m_strPath(strPath)
  {
    m_ptrAttr = ptrAttr;
  }

  NexusAttrPtr Content() const { return m_ptrAttr; }
  const std::string &Path() const { return m_strPath; }
};
typedef yat::SharedPtr<NexusAttributeContainer, yat::Mutex> NexusAttributeContainerPtr;
typedef std::list<NexusAttributeContainerPtr> NexusAttributeContainerPtrList;

//=============================================================================
// Object containing a Nexus dataset information for creation
//=============================================================================
struct DatasetInfo
{
  std::string    strPath;
  NexusDataType  eDataType;    // data type
  int            iRank;        // Data set rank
  DimVector      vecDim;       // size of each dimensions
};

//=============================================================================
// Object managing exceptions
//=============================================================================
class ExceptionListener: public yat::Task, public IExceptionHandler
{
public:
  // Default handler implementation: output exception on console
  void OnException(const NexusException &e);
  
protected:
  virtual void handle_message (yat::Message& msg) throw (yat::Exception);
};

typedef std::vector<NexusFileWriter::INotify*> WriteNotifHandlerVector;

//=============================================================================
// Nexus writer class implementation
//
//=============================================================================
class NexusFileWriterImpl: public yat::Task, public IExceptionHandler
{
public:
  enum MessageType
  {
    ABORT = yat::FIRST_USER_MSG + 183,
    PUSH_DATASET,
    PUSH_ATTRIBUTE,
    CREATE_DATASET,
    PROCESS_CACHE,
    SYNC
  };

private:
  NexusFileWriter*               m_parent;
  NexusFileWriter::WriteMode     m_eWriteMode;
  mutable yat::Mutex             m_mtxLock;
  std::string                    m_strNexusFilePath;
  NexusDatasetContainerRefList   m_lstDataset;
  NexusAttributeContainerPtrList m_lstAttr;
  bool                           m_bAborted;             // true if writing process has been aborted
  bool                           m_bWantAbort;           // true if process abort is requested
  std::set<std::string>          m_setCreatedGroupPaths;
  yat::uint16                    m_usMaxCacheSize;       // max size of dataset cache in MBytes
  yat::uint32                    m_uiCurrentCacheSize;   // current size of dataset cache in Bytes
  bool                           m_bFileCreated;
  bool                           m_bSync;                // Synchronization flag
  std::set<std::string>          m_setCreatedDataSets;
  NexusFileWriter::Statistics    m_Stats;
  yat::uint64                    m_uiWrittenSinceLastInstant;
  yat::Timer                     m_tmAverage;
  yat::Timer                     m_tmInstant;
  bool                           m_bStatisticsFirstAccess; // True at first call to UpdateStatistics()
  IExceptionHandler*             m_pExceptionHandler; 
  WriteNotifHandlerVector        m_NotificationHandlers;
  yat::uint16                    m_uiWritePeriod;
  bool                           m_bHold;                // No data is wrote on disk while set to 'true'
  std::size_t                    m_msg_counter;          // message counter used for asynchronous data writing
  bool                           m_is_writing;
  YAT_SHARED_PTR(NexusFile)      m_nexus_file_ptr;
  bool                           m_auto_close;
  bool                           m_use_lock;             // If 'true' => use the file locking mechanism

public:
  // Constructor
  NexusFileWriterImpl(NexusFileWriter* parent_p, const std::string &strNexusFilePath, NexusFileWriter::WriteMode eWriteMode, yat::uint16 uiWritePeriod);

  // Destructor
  ~NexusFileWriterImpl() 
  {
    WRITER_DBG("Delete NexusFileWriterImpl: " << m_strNexusFilePath);
    CloseFile();
  }

  // Use the lock file mecanism
  void set_use_lock() { m_use_lock = true; }

  /// Accessors
  const std::string &NexusFilePath() const { return m_strNexusFilePath; }
  
  /// Access to the nexus file
  YAT_SHARED_PTR(NexusFile) File(const std::string& file_name )
  {
    if( !m_nexus_file_ptr )
      m_nexus_file_ptr = new NexusFile(PSZ(file_name), NexusFile::WRITE, m_use_lock);
    return m_nexus_file_ptr;
  }
  
  /// Close nexus file
  void CloseFile()
  {
    if( m_nexus_file_ptr )
      m_nexus_file_ptr->Close();
    m_nexus_file_ptr.reset();
  }
  
  /// If true, the file should be automatically closed after each write action
  void SetAutoClose(bool b=true) { m_auto_close = b; }
  bool AutoClose() { return m_auto_close; }
  
  // Call OnCloseFile on the notification handlers
  void send_close_file_notification();

  /// Abort process: delete all remaining data, no more access to file
  void Abort();

  /// Returns true if there is no more attribute to write
  bool IsAttributeCacheEmpty() const;

  /// Returns true if there is no more dataset to write
  bool IsDatasetCacheEmpty() const ;

  /// Returns true if there is no more data to write
  bool IsCacheEmpty() const;

  /// Returns true if writing process is aborted
  bool IsAborted() const { return m_bAborted; }

  /// Is all done ?
  bool Done() const;
  
  /// Returns true if synchronisation is currently in process
  bool IsSyncFlag() const 
  {
    yat::AutoMutex<> _lock(m_mtxLock);
    return m_bSync;
  }

  /// Force cache using
  void Hold(bool b)
  {
    WRITER_DBG( "Hold(" << b << ") for " << m_strNexusFilePath );
    m_bHold = b;
    if( !b )
    {
      // Flush all accumulated data if revert 'hold' flag to false
      post(PROCESS_CACHE);
    }
  }

  /// Check 'hold' flag
  bool IsHold() const { return m_bHold; }
  
  void SetExceptionHandler(IExceptionHandler* pHandler)
  {
    m_pExceptionHandler = pHandler;
  }

  void SetNotificationHandler(NexusFileWriter::INotify *pHandler)
  {
    m_NotificationHandlers.clear();
    m_NotificationHandlers.push_back(pHandler);
  }

  void AddNotificationHandler(NexusFileWriter::INotify *pHandler)
  {
    m_NotificationHandlers.push_back(pHandler);
  }

  void SetCacheSize(yat::uint16 usCacheSize) 
  {
    m_usMaxCacheSize = usCacheSize;
  }

  bool UseCache() 
  {
    return NexusFileWriter::ASYNCHRONOUS == m_eWriteMode && m_usMaxCacheSize > 0 ? true : false;
  }

  void CancelSyncFlag() { m_bSync = false; }
  
  /// Set the WantAbort flags to TRUE
  void SetWantAbort()
  {
    {  // Clear cache
      yat::AutoMutex<> _lock(m_mtxLock);
      m_lstDataset.clear();
      m_lstAttr.clear();
    }
    m_bWantAbort = true; 
  }

  // Post a message asking for asynchronous data writing
  void PostMessageData( yat::Message* msg_p )
  {
    post(msg_p);
    
    // incremente message counter
    yat::AutoMutex<> _lock(m_mtxLock);
    m_msg_counter++;
  }
  
  // Decremente message counter
  void DecMsgCounter()
  {
    yat::AutoMutex<> _lock(m_mtxLock);
    m_msg_counter--;
  }
  
  // All isn't done while this flag is up 
  void SetWritingFlag(bool b)
  {
    yat::AutoMutex<> _lock(m_mtxLock);
    m_is_writing = b;
  }
  
  bool IsWritingFlag() const
  {
    yat::AutoMutex<> _lock(m_mtxLock);
    return m_is_writing;
  }
  
  // All messages handled ?
  bool HasPendingDataMsg() const
  {
    yat::AutoMutex<> _lock(m_mtxLock);
    return m_msg_counter > 0 ? true : false;
  }
  
  // Write dataset into nexus file
  void WriteDataset(NexusFile *pnxf, const std::string &strPath, const NexusDataSet &aDataset);

  // Write attribute into nexus file
  void WriteAttribute(NexusFile *pnxf, const std::string &strPath, const NexusAttr &aAttr);

  // Create a new dataset: this action may not be cached
  void CreateDataset(NexusFile *pnxf, const DatasetInfo &info);

  // Returns the writing mode
  NexusFileWriter::WriteMode WriteMode() const { return m_eWriteMode; }

  // Creates nexus file if needed
  void CheckFile();

  // Returns a copy of the statistics struct
  NexusFileWriter::Statistics GetStatistics() const;

  // Update statistics
  void UpdateStats(yat::int64 bytes_delta);

  // Reset statistics
  void ResetStatistics();

  // Default handler implementation: output exception on console
  void OnNexusException(const NexusException &e);

private:
  // Extract first dataset from cache
  NexusDatasetContainerRef PopFirstDataset();

  // Extract first attribute from cache
  NexusAttributeContainerPtr PopFirstAttribute();

  void ProcessCache();

  void ProcessCacheData(yat::Message& msg);
  void ProcessPushData(yat::Message& msg);
  
  // Utility methods
  void VectorToArray(const DimVector &vec, int *piArray);
  std::string GroupPath(const std::string &strPath);
  std::string DatasetName(const std::string &strPath);
  std::string AttrName(const std::string &strPath);

protected:
  virtual void handle_message (yat::Message& msg);
};

//=============================================================================
// AutoNexusFile
//
//=============================================================================
class AutoNexusFile
{
public:
  AutoNexusFile(NexusFileWriterImpl* p, const std::string& file_name)
  {
    WRITER_DBG("AutoNexusFile::AutoNexusFile " << file_name );

    m_impl_p = p;
    m_file_name = file_name;
  }
  
  ~AutoNexusFile()
  {
    WRITER_DBG("AutoNexusFile::~AutoNexusFile ");
    if( m_impl_p->AutoClose() )
    {
      try
      {
        m_impl_p->CloseFile();
      }
      catch( const yat::Exception& e )
      {
        YAT_LOG_EXCEPTION( e );
        // drop exception...
      }
      m_impl_p->send_close_file_notification();
    }
  }

  NexusFile* get()
  {
    return m_impl_p->File(m_file_name).get();
  }
 
 private:
   NexusFileWriterImpl* m_impl_p;
   std::string          m_file_name;
};

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::NexusFileWriterImpl
//-----------------------------------------------------------------------------
NexusFileWriterImpl::NexusFileWriterImpl(NexusFileWriter* parent_p, 
                                         const std::string &strNexusFilePath,
                                         NexusFileWriter::WriteMode eWriteMode,
                                         yat::uint16 uiWritePeriod)
{
  m_parent = parent_p;
//  // PrintfDebug("[BEGIN] NexusFileWriterImpl::NexusFileWriterImpl path: %s", PSZ(strNexusFilePath));

  m_eWriteMode = eWriteMode;
  m_usMaxCacheSize = 0; // 0 means no cache
  if( NexusFileWriter::IMMEDIATE == eWriteMode )
  {
    m_eWriteMode = NexusFileWriter::ASYNCHRONOUS;
  }
  if( NexusFileWriter::DELAYED == eWriteMode )
  {
    m_usMaxCacheSize = 20; // 20 MBytes
    m_eWriteMode = NexusFileWriter::ASYNCHRONOUS;
  }

  m_strNexusFilePath = strNexusFilePath;
  m_bAborted = false;
  m_bWantAbort = false;
  m_bSync = false;
  m_uiCurrentCacheSize = 0;
  m_bFileCreated = false;
  m_bStatisticsFirstAccess = false;
  m_uiWrittenSinceLastInstant = 0;
  m_pExceptionHandler = this;
  m_uiWritePeriod = uiWritePeriod;
  m_bHold = false;
  m_msg_counter = 0;
  m_is_writing = false;
  m_auto_close = true;
  m_use_lock = false;

  //- setup low and high watermarks
  msgq_lo_wm(0x007FFFFF);
  msgq_hi_wm(0x00FFFFFF); 
  message_queue().throw_on_post_msg_timeout(true);
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::OnNexusException
//-----------------------------------------------------------------------------
void NexusFileWriterImpl::OnNexusException(const NexusException &e)
{
  YAT_LOG_EXCEPTION( e );
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::send_close_file_notification
//-----------------------------------------------------------------------------
void NexusFileWriterImpl::send_close_file_notification()
{
  for( std::size_t i = 0; i < m_NotificationHandlers.size(); ++i )
    m_NotificationHandlers[i]->OnCloseFile(m_parent, m_strNexusFilePath);
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::CheckFile
//-----------------------------------------------------------------------------
void NexusFileWriterImpl::CheckFile()
{
  yat::FileName fn(m_strNexusFilePath);
  WRITER_DBG("check file " << fn.full_name() );
  if( !m_bFileCreated && !fn.file_access() )
  {
    WRITER_DBG("File " << fn.full_name() << " not found (" << m_bFileCreated << "). Must be created");
    NexusFile nf (m_strNexusFilePath.c_str());
    nf.Create(m_strNexusFilePath.c_str());
    m_bFileCreated = true;
  }
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::IsCacheEmpty
//-----------------------------------------------------------------------------
bool NexusFileWriterImpl::IsCacheEmpty() const
{
  return IsDatasetCacheEmpty() && IsAttributeCacheEmpty();
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::IsDatasetCacheEmpty
//-----------------------------------------------------------------------------
bool NexusFileWriterImpl::IsDatasetCacheEmpty() const 
{
  yat::AutoMutex<> _lock(m_mtxLock);

  if( m_lstDataset.empty() )
    return true;

  return false;
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::IsAttributeCacheEmpty
//-----------------------------------------------------------------------------
bool NexusFileWriterImpl::IsAttributeCacheEmpty() const 
{
  yat::AutoMutex<> _lock(m_mtxLock);

  if( m_lstAttr.empty() )
    return true;

  return false;
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::WriteDataset
//-----------------------------------------------------------------------------
void NexusFileWriterImpl::WriteDataset(NexusFile *pnxf, const std::string &strPath, const NexusDataSet &aDataset)
{
  WRITER_DBG("NexusFileWriterImpl::WriteDataset <");
  try
  {
    // Split path into group path and dataset name
    std::string strGroupPath = strPath, strDatasetName;
    yat::StringUtil::extract_token_right(&strGroupPath, '/', &strDatasetName);
    strGroupPath += '/';

    // Check group path
    bool bGroupOk = pnxf->CreateGroupPath(PSZ(strGroupPath));
    if( bGroupOk )
    {
      bool bDatasetOpened = pnxf->OpenDataSet(PSZ(strDatasetName), false);
      if( !bDatasetOpened )
      {
	    WRITER_DBG("NexusFileWriterImpl::WriteDataset -> CreateDataSet " << strPath );
        // Create and open dataset
        pnxf->CreateDataSet(PSZ(strDatasetName), aDataset.DataType(), aDataset.TotalRank(), aDataset.TotalDimArray(), true);
      }
    }

    if( aDataset.IsSubset() )
    {
      WRITER_DBG("NexusFileWriterImpl::WriteDataset -> PutDataSubSet" );
      pnxf->PutDataSubSet(aDataset.Data(), aDataset.StartArray(), aDataset.DimArray());

//## bug TANGODEVIC-1706: memory leak : to be confirmed
//      pnxf->Flush();

      SetWritingFlag(false);

      for( std::size_t i = 0; i < m_NotificationHandlers.size(); ++i )
        m_NotificationHandlers[i]->OnWriteSubSet(m_parent, m_strNexusFilePath + "#" + strPath, aDataset.StartArray(), aDataset.DimArray());
    }
    else
    {
      pnxf->PutData(aDataset.Data());
      pnxf->Flush();
      
      SetWritingFlag(false);
      
      for( std::size_t i = 0; i < m_NotificationHandlers.size(); ++i )
        m_NotificationHandlers[i]->OnWrite(m_parent, strPath);
    }
  }
  catch( NexusException &ex )
  {
    SetWritingFlag(false);
    ex.dump();
    throw ex;
  }
  catch(std::exception& e)
  {
    throw NexusException(e.what(), "NexusFileWriterImpl::WriteDataset");
  }
  catch( ... )
  {
    throw NexusException("Unknown error", "NexusFileWriterImpl::WriteDataset");
  }

  WRITER_DBG("NexusFileWriterImpl::WriteDataset >");
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::VectorToArray
//-----------------------------------------------------------------------------
void NexusFileWriterImpl::VectorToArray(const DimVector &vec, int *piArray)
{
  for(yat::uint32 ui = 0; ui < vec.size(); ui++)
    piArray[ui] = vec[ui];
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::GroupPath
//-----------------------------------------------------------------------------
std::string NexusFileWriterImpl::GroupPath(const std::string &strPath)
{
  std::string strGroupPath = strPath, strDatasetName;
  yat::StringUtil::extract_token_right(&strGroupPath, '/', &strDatasetName);
  return strGroupPath;
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::DatasetName
//-----------------------------------------------------------------------------
std::string NexusFileWriterImpl::DatasetName(const std::string &strPath)
{
  std::string strGroupPath = strPath, strDatasetName;
  yat::StringUtil::extract_token_right(&strGroupPath, '/', '.', &strDatasetName);
  if( strDatasetName.empty() )
  {
    yat::StringUtil::extract_token_right(&strGroupPath, '/', &strDatasetName);
  }
  
  return strDatasetName;
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::AttrName
//-----------------------------------------------------------------------------
std::string NexusFileWriterImpl::AttrName(const std::string &strPath)
{
  std::string strGroupPath = strPath, strDatasetName, strAttrName;
  yat::StringUtil::extract_token_right(&strGroupPath, '/', &strDatasetName);
  yat::StringUtil::extract_token_right(&strDatasetName, '.', &strAttrName);
  return strAttrName;
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::CreateDataset
//-----------------------------------------------------------------------------
void NexusFileWriterImpl::CreateDataset(NexusFile *pnxf, const DatasetInfo &info)
{
  try
  {
    // Check group path
    bool bGroupOk = pnxf->CreateGroupPath(PSZ(GroupPath(info.strPath)));
    if( bGroupOk )
    {
      bool bDatasetOpened = pnxf->OpenDataSet(PSZ(DatasetName(info.strPath)), false);
      if( !bDatasetOpened )
      {
        int aiDim[32];
        VectorToArray(info.vecDim, aiDim); 

        // Create and open dataset
        pnxf->CreateDataSet(PSZ(DatasetName(info.strPath)), info.eDataType, info.iRank, aiDim, true);
      }
    }
  }
  catch( NexusException &ex )
  {
    ex.dump();
    throw ex;
  }
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::WriteAttribute
//-----------------------------------------------------------------------------
void NexusFileWriterImpl::WriteAttribute(NexusFile *pnxf, const std::string &strPath, const NexusAttr &aAttr)
{
  // Check group path
  bool bGroupOk = pnxf->CreateGroupPath(PSZ(GroupPath(strPath)));
  if( bGroupOk )
    // Open the already existing dataset, if not: a exception is throwed
    pnxf->OpenDataSet(PSZ(DatasetName(strPath)), true);

  pnxf->PutAttr(aAttr.AttrName(), aAttr.RawValue(), aAttr.Len(), aAttr.DataType());
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::PopFirst
//-----------------------------------------------------------------------------
NexusDatasetContainerRef NexusFileWriterImpl::PopFirstDataset()
{
  yat::AutoMutex<> _lock(m_mtxLock);

  NexusDatasetContainerRef refContainer = m_lstDataset.front();
  m_lstDataset.pop_front();
  return refContainer;
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::PopFirst
//-----------------------------------------------------------------------------
NexusAttributeContainerPtr NexusFileWriterImpl::PopFirstAttribute()
{
  yat::AutoMutex<> _lock(m_mtxLock);

  NexusAttributeContainerPtr refContainer = m_lstAttr.front();
  m_lstAttr.pop_front();
  return refContainer;
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::ProcessCache
//-----------------------------------------------------------------------------
void NexusFileWriterImpl::ProcessCache()
{
  WRITER_DBG("ENTER NexusFileWriterImpl::ProcessCache for " << m_strNexusFilePath << " @ " << m_parent);
  
  if( !IsDatasetCacheEmpty() || !IsAttributeCacheEmpty() )
  {
    NexusGlobalLock __lock;

    CheckFile();
    AutoNexusFile nxf(this, m_strNexusFilePath);
    
    while( !IsDatasetCacheEmpty() )
    {
      SetWritingFlag(true);
      // pop first data item
      NexusDatasetContainerRef refContainer = PopFirstDataset();
      UpdateStats( yat::int64(refContainer->Content()->BufferSize()) );
      WriteDataset(nxf.get(), refContainer->Path(), *(refContainer->Content()));
      m_uiCurrentCacheSize -= refContainer->Content()->BufferSize();
      UpdateStats( -yat::int64(refContainer->Content()->BufferSize()) );
    }

    while( !IsAttributeCacheEmpty() )
    {
      // pop first data item
      NexusAttributeContainerPtr refContainer = PopFirstAttribute();
      WriteAttribute(nxf.get(), refContainer->Path(), *(refContainer->Content()));
    }
  }
  else
  {
    WRITER_DBG("NexusFileWriterImpl::ProcessCache Nothing to write");
  }

  WRITER_DBG("EXIT  NexusFileWriterImpl::ProcessCache");
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::UpdateStats
//-----------------------------------------------------------------------------
void NexusFileWriterImpl::UpdateStats(yat::int64 bytes_delta)
{
  WRITER_DBG("NexusFileWriterImpl::UpdateStats " << bytes_delta);
  if( !m_bStatisticsFirstAccess )
  {
    ResetStatistics();
    m_bStatisticsFirstAccess = true;
  }

  // Timers
  double average_elapsed = m_tmAverage.elapsed_sec();
  double instant_elapsed = m_tmInstant.elapsed_sec();

  yat::AutoMutex<> _lock(m_mtxLock);
  if( bytes_delta > 0 )
    m_Stats.ui64TotalBytes += bytes_delta;
  if( bytes_delta < 0 )
  {
    m_uiWrittenSinceLastInstant += (-bytes_delta);
    // Written bytes
    m_Stats.ui64WrittenBytes += (-bytes_delta);
    if( instant_elapsed > 0.1 )
    {
      m_Stats.fInstantMbPerSec = float(m_uiWrittenSinceLastInstant / instant_elapsed) / 1e6;
      m_uiWrittenSinceLastInstant = 0;
      m_tmInstant.restart();
    }
    if( average_elapsed > 0.0 )
      m_Stats.fAverageMbPerSec = float(m_Stats.ui64WrittenBytes / average_elapsed) / 1e6;
  }
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::ResetStatistics
//-----------------------------------------------------------------------------
void NexusFileWriterImpl::ResetStatistics()
{
  yat::AutoMutex<> _lock(m_mtxLock);
  ::memset(&m_Stats, 0, sizeof(NexusFileWriter::Statistics));
  m_tmAverage.restart();
  m_tmInstant.restart();
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::GetStatistics
//-----------------------------------------------------------------------------
NexusFileWriter::Statistics NexusFileWriterImpl::GetStatistics() const
{
  yat::AutoMutex<> _lock(m_mtxLock);
  // returns a copy
  return m_Stats;
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::ProcessPushData
//-----------------------------------------------------------------------------
void NexusFileWriterImpl::ProcessPushData(yat::Message& msg)
{
  WRITER_DBG("> NexusFileWriterImpl::ProcessPushData");
  
  NexusGlobalLock __lock;
  
  NexusDatasetContainerRef refContainer = msg.get_data<NexusDatasetContainerRef>();
  UpdateStats(refContainer->Content()->BufferSize());
  CheckFile();
  AutoNexusFile nxf(this, m_strNexusFilePath);
  WriteDataset(nxf.get(), refContainer->Path(), *(refContainer->Content()));
  UpdateStats(-yat::int64(refContainer->Content()->BufferSize()));
  WRITER_DBG("< NexusFileWriterImpl::ProcessPushData");
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::ProcessCacheData
//-----------------------------------------------------------------------------
void NexusFileWriterImpl::ProcessCacheData(yat::Message& msg)
{
  WRITER_DBG("NexusFileWriterImpl::ProcessCacheData");
  NexusDatasetContainerRef refContainer = msg.get_data<NexusDatasetContainerRef>();
  {
    yat::AutoMutex<> _lock(m_mtxLock);
    
    if( refContainer->Content()->IsOwner() )
      m_uiCurrentCacheSize += refContainer->Content()->BufferSize();
    
    m_lstDataset.push_back(refContainer);
  }
  // Buffer max size readched => emptying cache
  if( yat::uint16(m_uiCurrentCacheSize / MEGA_BYTE) >= m_usMaxCacheSize )
  {
    ProcessCache();
  }
}

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::Done
//-----------------------------------------------------------------------------
bool NexusFileWriterImpl::Done() const
{
  yat::AutoMutex<> _lock(m_mtxLock);

  bool synchronous_mode = NexusFileWriter::SYNCHRONOUS == WriteMode();
  bool aborted = IsAborted(); 
  bool cache_empty = IsCacheEmpty();
  bool no_pending_data_msg = ! HasPendingDataMsg();
  bool not_writting = ! IsWritingFlag();

  WRITER_DBG("NexusFileWriterImpl::Done:" << synchronous_mode << aborted <<  cache_empty << no_pending_data_msg << not_writting );

  if( synchronous_mode
   || aborted
   || (cache_empty && no_pending_data_msg && not_writting ) )
  {
    return true;
  }
  
  return false;
}

//-----------------------------------------------------------------------------
// HANDLE_RETRY_EXCEPTION
//-----------------------------------------------------------------------------
#define HANDLE_RETRY_EXCEPTION(origin) \
  catch(NexusException &ex) \
  { \
    ex.push_error("ERROR", "NexusException caught while recording NeXus data", origin ); \
    YAT_LOG_EXCEPTION(ex); \
    YAT_WARNING_STREAM("Attempt(s) left: " << attempt_left - 1); \
    if( --attempt_left <= 0 ) \
      throw; \
  } \
  catch(yat::Exception &ex) \
  { \
    YAT_LOG_EXCEPTION(ex); \
    YAT_WARNING_STREAM("Attempt(s) left: " << attempt_left - 1); \
    if( --attempt_left <= 0 ) \
      throw; \
  } \
  catch(const std::exception& e) \
  { \
    yat::Exception ex("STD_ERROR", "std exception caught while recording NeXus data", origin ); \
    YAT_LOG_EXCEPTION(ex); \
    YAT_WARNING_STREAM("Attempt(s) left: " << attempt_left - 1); \
    if( --attempt_left <= 0 ) \
      throw ex; \
  } \
  catch(...) \
  { \
    yat::Exception ex("STD_ERROR", "An unknown error occured while recording NeXus data", origin ); \
    YAT_LOG_EXCEPTION(ex); \
    YAT_WARNING_STREAM("Attempt(s) left: " << attempt_left - 1); \
    if( --attempt_left <= 0 ) \
      throw ex; \
  }

//-----------------------------------------------------------------------------
// TRY_ACTION
//-----------------------------------------------------------------------------
#define TRY_ACTION(action, error_origin) \
do \
{ \
  std::size_t attempt_left = NexusFileWriter::AttemptMax(); \
  while( attempt_left ) \
  { \
    try \
    { \
      action; \
      break; \
    } \
    HANDLE_RETRY_EXCEPTION(error_origin); \
    yat::Thread::sleep(NexusFileWriter::AttemptDelay()); \
  } \
} while(0)

//-----------------------------------------------------------------------------
// NexusFileWriterImpl::handle_message
//-----------------------------------------------------------------------------
void NexusFileWriterImpl::handle_message (yat::Message& msg)
{
  try
  {
    switch( msg.type() )
    {
      case yat::TASK_INIT:
        if( NexusFileWriter::ASYNCHRONOUS == m_eWriteMode )
        {
          set_periodic_msg_period(m_uiWritePeriod * 1000);
          enable_periodic_msg(true);
        }
        break;

      case yat::TASK_EXIT:
        WRITER_DBG("NexusFileWriterImpl::handle_message yat::TASK_EXIT");
        m_msg_counter = 0;
        break;

      case SYNC:
        WRITER_DBG("NexusFileWriterImpl::handle_message SYNC");
        if( !m_bAborted && !m_bWantAbort && UseCache() )
          TRY_ACTION( ProcessCache(), "SYNC" );
        m_bSync = true;
        break;
      
      case ABORT:
        m_bAborted = true;
        break;
      
      case CREATE_DATASET:
      {
        SetWritingFlag(true);
        DecMsgCounter();
        WRITER_DBG("NexusFileWriterImpl::handle_message CREATE_DATASET " << m_msg_counter);
        if( !m_bAborted && !m_bWantAbort )
        {
          DatasetInfo info = msg.get_data<DatasetInfo>();
          TRY_ACTION( \
                      CheckFile(); \
                      AutoNexusFile nxf(this, m_strNexusFilePath); \
                      CreateDataset(nxf.get(), info), \
                      "CREATE_DATASET"
                    );
        }
        break;
      }
            
      case PUSH_DATASET:
      {
        SetWritingFlag(true);
        DecMsgCounter();
        WRITER_DBG("NexusFileWriterImpl::handle_message PUSH_DATASET " << m_msg_counter);
        if( !m_bAborted && !m_bWantAbort )
        {
          TRY_ACTION( \
                      if( !m_bHold ) \
                        ProcessPushData(msg); \
                      else \
                        ProcessCacheData(msg),
                      "PUSH_DATASET"
                    );
        }
        break;
      }
      
      case PUSH_ATTRIBUTE:
      {
        SetWritingFlag(true);
        DecMsgCounter();
        WRITER_DBG("NexusFileWriterImpl::handle_message PUSH_ATTRIBUTE " << m_msg_counter);
        if( !m_bAborted && !m_bWantAbort )
        {
          TRY_ACTION( \
                      NexusAttributeContainerPtr refContainer = msg.get_data<NexusAttributeContainerPtr>(); \
                      CheckFile(); \
                      AutoNexusFile nxf(this, m_strNexusFilePath); \
                      WriteAttribute(nxf.get(), refContainer->Path(), *(refContainer->Content())),
                      "PUSH_ATTRIBUTE"
                    );
        }
        break;
      }
      
      case PROCESS_CACHE:
        WRITER_DBG("NexusFileWriterImpl::handle_message yat::PROCESS_CACHE");
      case yat::TASK_PERIODIC:
        WRITER_DBG("NexusFileWriterImpl::handle_message yat::TASK_PERIODIC");
        if( !m_bAborted && !m_bWantAbort && ( UseCache() || ( !m_bHold && !IsCacheEmpty() ) ) )
          // Do the job
          TRY_ACTION( ProcessCache(), "PROCESS_CACHE or yat::TASK_PERIODIC" );
        break;
      
      default:
        break;
    }
    SetWritingFlag(false);
  }
  catch(NexusException &e)
  {
    m_bAborted = true;
    SetWritingFlag(false);
    if( m_pExceptionHandler )
      m_pExceptionHandler->OnNexusException(e);
    else
      YAT_LOG_EXCEPTION( e );
  }
  catch(yat::Exception &e)
  {
    m_bAborted = true;
    SetWritingFlag(false);
    NexusException ex;
    ex.errors = e.errors;
    if( m_pExceptionHandler )
      m_pExceptionHandler->OnNexusException(ex);
    else
      YAT_LOG_EXCEPTION( e );
  }
  catch(std::exception& e)
  {
    m_bAborted = true;
    SetWritingFlag(false);
    if( m_pExceptionHandler )
      m_pExceptionHandler->OnNexusException(
       NexusException( PSZ_FMT("A system error occured while recording NeXus data: ", e.what()),
                      "NexusFileWriterImpl::handle_message") );
    else
    {
      yat::log_error( "A system error occured while recording NeXus data!" );
      yat::log_error( e.what() );
    }
  }
  catch(...)
  {
    m_bAborted = true;
    SetWritingFlag(false);
    if( m_pExceptionHandler )
      m_pExceptionHandler->OnNexusException(
       NexusException("UNKNOWN ERROR",
                      "A unknown error occured while recording NeXus data"
                      "NexusFileWriterImpl::handle_message") );
    else
    {
      yat::log_error( "A unknown error occured while recording NeXus data!" );
    }
  }
}

//=============================================================================
//
// NexusFileWriter Class
//
//=============================================================================
std::size_t NexusFileWriter::s_attempt_max_ = 10;
std::size_t NexusFileWriter::s_attempt_delay_ms_ = 250;

//-----------------------------------------------------------------------------
// NexusFileWriter::NexusFileWriter
//-----------------------------------------------------------------------------
NexusFileWriter::NexusFileWriter(const std::string &strFilePath, WriteMode eMode, unsigned int uiWritePeriod)
{
  m_pImpl = new NexusFileWriterImpl( this, strFilePath, eMode, yat::uint16(uiWritePeriod) );
  WRITER_DBG("New NexusFileWriter " << strFilePath);
  
  if( NexusFileWriter::SYNCHRONOUS != eMode )
  {
    // Start task
    m_pImpl->go();
  }
}

//-----------------------------------------------------------------------------
// NexusFileWriter::~NexusFileWriter
//-----------------------------------------------------------------------------
NexusFileWriter::~NexusFileWriter()
{
  WRITER_DBG("DELETE NexusFileWriter");

  if( NexusFileWriter::SYNCHRONOUS != m_pImpl->WriteMode() )
  {
    WRITER_DBG("asynchronous");
    m_pImpl->exit();
  }
  else
  {
    WRITER_DBG("synchronous");
    delete m_pImpl;
  }
}

//-----------------------------------------------------------------------------
// NexusFileWriter::Hold
//-----------------------------------------------------------------------------
void NexusFileWriter::Hold(bool b)
{
  m_pImpl->Hold(b);
}

//-----------------------------------------------------------------------------
// NexusFileWriter::IsHold
//-----------------------------------------------------------------------------
bool NexusFileWriter::IsHold() const
{
  return m_pImpl->IsHold();
}

//-----------------------------------------------------------------------------
// NexusFileWriter::SetExceptionHandler
//-----------------------------------------------------------------------------
void NexusFileWriter::SetExceptionHandler(IExceptionHandler* pHandler)
{
  m_pImpl->SetExceptionHandler(pHandler);
}

//-----------------------------------------------------------------------------
// NexusFileWriter::SetNotificationHandler
//-----------------------------------------------------------------------------
void NexusFileWriter::SetNotificationHandler(INotify* pHandler)
{
  m_pImpl->SetNotificationHandler(pHandler);
}

//-----------------------------------------------------------------------------
// NexusFileWriter::AddNotificationHandler
//-----------------------------------------------------------------------------
void NexusFileWriter::AddNotificationHandler(INotify* pHandler)
{
  m_pImpl->AddNotificationHandler(pHandler);
}

//-----------------------------------------------------------------------------
// NexusFileWriter::SetCacheSize
//-----------------------------------------------------------------------------
void NexusFileWriter::SetCacheSize(yat::uint16 usCacheSize) 
{
  m_pImpl->SetCacheSize(usCacheSize);
}

//-----------------------------------------------------------------------------
// NexusFileWriter::File
//-----------------------------------------------------------------------------
const std::string &NexusFileWriter::File() const 
{
  return m_pImpl->NexusFilePath();
}

//-----------------------------------------------------------------------------
// NexusFileWriter::CreateDataSet
//-----------------------------------------------------------------------------
void NexusFileWriter::CreateDataSet(const std::string &strPath, NexusDataType eDataType, int iRank, int *piDim)
{
  DatasetInfo info;
  info.strPath = strPath;
  info.eDataType = eDataType;
  info.iRank = iRank;
  for( int i = 0; i < iRank; i++ )
    info.vecDim.push_back(piDim[i]);

  switch( m_pImpl->WriteMode() )
  {
    case SYNCHRONOUS:
    {
      m_pImpl->CheckFile();
      AutoNexusFile nxf( m_pImpl, m_pImpl->NexusFilePath() );
      m_pImpl->CreateDataset(nxf.get(), info);
      break;
    }
    
    case ASYNCHRONOUS: // Dataset creation can't be cached
    {
      yat::Message *pMsg = new yat::Message(NexusFileWriterImpl::CREATE_DATASET);
      pMsg->attach_data(info);
      m_pImpl->PostMessageData(pMsg);
      break;
    }
    default:
        throw NexusException(BAD_POLICY, "NexusFileWriterImpl::PushDataSet");
  }
}

//-----------------------------------------------------------------------------
// NexusFileWriter::PushDataSet
//-----------------------------------------------------------------------------
void NexusFileWriter::PushDataSet(const std::string &strPath, NexusDataSetPtr ptrDataSet)
{
  switch( m_pImpl->WriteMode() )
  {
    case SYNCHRONOUS:
    {
      m_pImpl->CheckFile();
      AutoNexusFile nxf( m_pImpl, m_pImpl->NexusFilePath() );
      m_pImpl->WriteDataset(nxf.get(), strPath, *ptrDataSet);
      break;
    }
    case ASYNCHRONOUS:
    {
      NexusDatasetContainerRef refDataContainer = new NexusDatasetContainer(strPath, ptrDataSet);
      yat::Message *pMsg;
      pMsg = new yat::Message(NexusFileWriterImpl::PUSH_DATASET);
      pMsg->attach_data(refDataContainer);
      m_pImpl->PostMessageData(pMsg);
      break;
    }
    default:
      throw NexusException(BAD_POLICY, "NexusFileWriter::PushDataSet");
  }
}

//-----------------------------------------------------------------------------
// NexusFileWriter::PushDatasetAttr
//-----------------------------------------------------------------------------
void NexusFileWriter::PushDatasetAttr(const std::string &strPath, NexusAttrPtr ptrAttr)
{
  switch( m_pImpl->WriteMode() )
  {
    case SYNCHRONOUS:
    {
      m_pImpl->CheckFile();
      AutoNexusFile nxf( m_pImpl, m_pImpl->NexusFilePath() );
      m_pImpl->WriteAttribute(nxf.get(), strPath, *ptrAttr);
      break;
    }
    case ASYNCHRONOUS:
    {
      NexusAttributeContainerPtr refDataContainer = new NexusAttributeContainer(strPath, ptrAttr);
      yat::Message *pMsg = new yat::Message(NexusFileWriterImpl::PUSH_ATTRIBUTE);
      pMsg->attach_data(refDataContainer);
      m_pImpl->PostMessageData(pMsg);
      break;
    }
    default:
        throw NexusException(BAD_POLICY, "NexusFileWriter::PushDatasetAttr");
  }
}

//-----------------------------------------------------------------------------
// NexusFileWriter::Abort
//-----------------------------------------------------------------------------
void NexusFileWriter::Abort()
{
  if( NexusFileWriter::SYNCHRONOUS != m_pImpl->WriteMode() )
  {
    m_pImpl->SetWantAbort();
    
    // Send ABORT message to the task
    yat::Message *pMsg = new yat::Message(NexusFileWriterImpl::ABORT);
    m_pImpl->post(pMsg);
    
    while( !Done() )
      m_pImpl->yield();
  }
}

//-----------------------------------------------------------------------------
// NexusFileWriter::IsEmpty
//-----------------------------------------------------------------------------
bool NexusFileWriter::IsEmpty()
{
  if( m_pImpl->IsDatasetCacheEmpty() && m_pImpl->IsAttributeCacheEmpty() )
    return true;

  return false;
}

//-----------------------------------------------------------------------------
// NexusFileWriter::Done
//-----------------------------------------------------------------------------
bool NexusFileWriter::Done()
{
  return m_pImpl->Done();
}

//-----------------------------------------------------------------------------
// NexusFileWriter::Synchronize
//-----------------------------------------------------------------------------
void NexusFileWriter::Synchronize(bool bSendSyncMsg)
{
  WRITER_DBG("> NexusFileWriter::Synchronize");

  Hold(false);

  if( NexusFileWriter::SYNCHRONOUS != m_pImpl->WriteMode() )
  {
    if( NexusFileWriter::DELAYED == m_pImpl->WriteMode() ) 
    {
      m_pImpl->post(new yat::Message(NexusFileWriterImpl::PROCESS_CACHE));
    }
    if( bSendSyncMsg || !m_pImpl->IsCacheEmpty() )
    {
      m_pImpl->CancelSyncFlag();
      m_pImpl->post(new yat::Message(NexusFileWriterImpl::SYNC));
      while( !m_pImpl->IsCacheEmpty() || !m_pImpl->IsSyncFlag() )
      {
        m_pImpl->yield();
       
      }
    }
    else
    {
      while( !m_pImpl->IsCacheEmpty() )
      {
        m_pImpl->yield();
      }
    }
  }
  WRITER_DBG("< NexusFileWriter::Synchronize");
}

//------------------------------------------------------------------------------
// NexusFileWriter::Statistics::Statistics()
//------------------------------------------------------------------------------
NexusFileWriter::Statistics::Statistics()
{
  ::memset(this, 0, sizeof(NexusFileWriter::Statistics));
}

//------------------------------------------------------------------------------
// NexusFileWriter::GetStatistics()
//------------------------------------------------------------------------------
NexusFileWriter::Statistics NexusFileWriter::GetStatistics() const
{
  return m_pImpl->GetStatistics();
}

//------------------------------------------------------------------------------
// NexusFileWriter::ResetStatistics()
//------------------------------------------------------------------------------
void NexusFileWriter::ResetStatistics()
{
  m_pImpl->ResetStatistics();
}

//------------------------------------------------------------------------------
// NexusFileWriter::SetFileAutoClose
//------------------------------------------------------------------------------
void NexusFileWriter::SetFileAutoClose(bool b)
{
  m_pImpl->SetAutoClose(b);
}

//------------------------------------------------------------------------------
// NexusFileWriter::CloseFile
//------------------------------------------------------------------------------
void NexusFileWriter::CloseFile()
{
  m_pImpl->CloseFile();
}

//------------------------------------------------------------------------------
// NexusFileWriter::SetUseLock
//------------------------------------------------------------------------------
void NexusFileWriter::SetUseLock()
{
  m_pImpl->set_use_lock();
}

//=============================================================================
// DatasetWriter class
//=============================================================================
//------------------------------------------------------------------------------
// DatasetWriter::DatasetWriter
//------------------------------------------------------------------------------
DatasetWriter::DatasetWriter( const DataShape &shapeDataItem, const DataShape &shapeMatrix,
                             yat::uint16 usMaxMB, yat::uint16 m_usWriteTimeout )
{
  // The data buffer will not be deleted by the MemBuf destructor !
  init(usMaxMB, m_usWriteTimeout);
  SetShapes(shapeDataItem, shapeMatrix);
  Reset();
}
//------------------------------------------------------------------------------
// DatasetWriter::DatasetWriter
//------------------------------------------------------------------------------
DatasetWriter::DatasetWriter( const DataShape &shapeDataItem, std::size_t one_dim_size,
                             yat::uint16 usMaxMB, yat::uint16 m_usWriteTimeout )
{
  init(usMaxMB, m_usWriteTimeout);
  DataShape shape;
  shape.push_back(one_dim_size);
  SetShapes(shapeDataItem, shape);
  Reset();
}

//------------------------------------------------------------------------------
// DatasetWriter::DatasetWriter
// deprecated
//------------------------------------------------------------------------------
DatasetWriter::DatasetWriter( const std::vector<int> &shapeDataItem, const std::vector<int> &shapeMatrix,
                             yat::uint16 usMaxMB )
{
  // The data buffer will not be deleted by the MemBuf destructor !
  init(usMaxMB, 0);
  
  DataShape shapeDataItemNew, shapeMatrixNew;
  for( std::size_t i=0; i < shapeDataItem.size(); ++i )
    shapeDataItemNew.push_back(static_cast<std::size_t>(shapeDataItem[i]));
  for( std::size_t i=0; i < shapeMatrix.size(); ++i )
    shapeMatrixNew.push_back(static_cast<std::size_t>(shapeMatrix[i]));
  SetShapes(shapeDataItemNew, shapeMatrixNew);
  
  Reset();
}

//------------------------------------------------------------------------------
// DatasetWriter::DatasetWriter
// deprecated
//------------------------------------------------------------------------------
DatasetWriter::DatasetWriter( const std::vector<int> &shapeDataItem, std::size_t one_dim_size,
                             yat::uint16 usMaxMB )
{
  init(usMaxMB, 0);
  
  DataShape shapeDataItemNew;
  DataShape shape;
  shape.push_back(one_dim_size);
  for( std::size_t i=0; i < shapeDataItem.size(); ++i )
    shapeDataItemNew.push_back(static_cast<std::size_t>(shapeDataItem[i]));
  SetShapes(shapeDataItemNew, shape);
  
  Reset();
}

//------------------------------------------------------------------------------
// DatasetWriter::DatasetWriter
//------------------------------------------------------------------------------
DatasetWriter::DatasetWriter( yat::uint16 usMaxMB, yat::uint16 m_usWriteTimeout )
{
  // The data buffer will not be deleted by the MemBuf destructor !
  init(usMaxMB, m_usWriteTimeout);
  Reset();
}

//------------------------------------------------------------------------------
// DatasetWriter::DatasetWriter
//------------------------------------------------------------------------------
void DatasetWriter::init( yat::uint16 usMaxMB, yat::uint16 m_usWriteTimeout )
{
  m_usMaxMB = 64;
  if( usMaxMB > 0 )
    m_usMaxMB = MIN(1024, usMaxMB);
  
  m_pFlushListener = NULL;
  m_ui64TotalSize = 0;
  m_bUnlimited = false;
  m_NexusDataType = NX_NONE;
  m_ulDataItemSize = 0;
  m_nTotalRank = 0;
  m_ui64TotalSize = 0;
  m_bDatasetCreated = false;
  m_nDataItemCount = 0;
  m_nWrittenCount = 0;
  memset(m_aiTotalDim, 0, sizeof(int) * MAX_RANK);
  memset(m_aiDataItemDim, 0, sizeof(int) * MAX_RANK);
  m_nDataItemRank = 0;

  if( m_usWriteTimeout > 0 )
    m_tmWriteTimeout.set_value( 1000 * m_usWriteTimeout );
}

//------------------------------------------------------------------------------
// DatasetWriter::~DatasetWriter
//------------------------------------------------------------------------------
DatasetWriter::~DatasetWriter()
{
  WRITER_DBG("DELETE DatasetWriter");
}

//------------------------------------------------------------------------------
// DatasetWriter::SetShapes
//------------------------------------------------------------------------------
void DatasetWriter::SetShapes(const DataShape &shapeDataItem, const DataShape &shapeMatrix)
{
  /* No longer support of unlimited arrays ?
  if( shapeMatrix[0] < 0 )
    m_bUnlimited = true;
  */
  
  WRITER_DBG("DatasetWriter::SetShapes " << m_strDataset);
  m_nDataItemRank = shapeDataItem.size();
  
  for( std::size_t i = 0; i < m_nDataItemRank + shapeMatrix.size(); ++i )
  {
    if( i < shapeMatrix.size() )
      m_aiDataItemDim[i] = 1;
    else
      m_aiDataItemDim[i] = shapeDataItem[i - shapeMatrix.size()];

    WRITER_DBG("m_aiDataItemDim[" << i << "]= " << m_aiDataItemDim[i]);
  }

  SetMatrix(shapeMatrix);
}

//------------------------------------------------------------------------------
// DatasetWriter::SetMatrix
//------------------------------------------------------------------------------
void DatasetWriter::SetMatrix(const DataShape &shapeMatrix)
{
  WRITER_DBG("DatasetWriter::SetMatrix");
  m_shapeMatrix = shapeMatrix;
  
  m_nTotalRank = m_shapeMatrix.size() + m_nDataItemRank;

  for( std::size_t i = 0; i < m_nTotalRank; i++ )
  {
    if( i < m_shapeMatrix.size() )
      m_aiTotalDim[i] = m_shapeMatrix[i];
    else
      m_aiTotalDim[i] = m_aiDataItemDim[i];
    WRITER_DBG("m_aiTotalDim[" << i << "]= " << m_aiTotalDim[i]);
  }
}

//------------------------------------------------------------------------------
// DatasetWriter::DataItemShape
//------------------------------------------------------------------------------
DataShape DatasetWriter::DataItemShape() const
{
  DataShape shape;
  for( std::size_t i = 0; i < m_nDataItemRank; ++i )
    shape.push_back(m_aiDataItemDim[i + m_shapeMatrix.size()]);
  return shape;
}

//------------------------------------------------------------------------------
// DatasetWriter::SetNexusFileWriter
//------------------------------------------------------------------------------
void DatasetWriter::SetNexusFileWriter(NexusFileWriterPtr ptrWriter)
{
  if( !m_ptrNeXusFileWriter.is_null() )
  {
    WRITER_DBG("DatasetWriter::SetNexusFileWriter uc(m_ptrNeXusFileWriter) " << m_ptrNeXusFileWriter.get() << " " << m_ptrNeXusFileWriter.use_count());
  }
  m_ptrNeXusFileWriter = ptrWriter;
}

//------------------------------------------------------------------------------
// DatasetWriter::CheckWriter
//------------------------------------------------------------------------------
void DatasetWriter::CheckWriter()
{
  if( !m_ptrNeXusFileWriter )
    throw NexusException("No file writer !", "DatasetWriter::CheckWriter");
}

//------------------------------------------------------------------------------
// DatasetWriter::SetPath
//------------------------------------------------------------------------------
void DatasetWriter::SetPath(const std::string &strPath, const std::string &strDataset)
{
  m_strPath = strPath;
  m_strDataset = strDataset;
}

//------------------------------------------------------------------------------
// DatasetWriter::SetFullPath
//------------------------------------------------------------------------------
void DatasetWriter::SetFullPath(const std::string &strFullPath)
{
  m_strPath = strFullPath;
  yat::StringUtil::extract_token_right(&m_strPath, '/', &m_strDataset);
}

//------------------------------------------------------------------------------
// DatasetWriter::SetDatasetName
//------------------------------------------------------------------------------
void DatasetWriter::SetDatasetName(const std::string &strDataset)
{
  m_strDataset = strDataset;
}

//------------------------------------------------------------------------------
// DatasetWriter::AddAttribute
//------------------------------------------------------------------------------
void DatasetWriter::AddAttribute(const NexusAttrPtr &ptrAttr)
{
  switch( ptrAttr->DataType() )
  {
    case NX_INT32:
      AddMetadata(ptrAttr->AttrName(), ptrAttr->GetLong());
      break;
    case NX_FLOAT64:
      AddMetadata(ptrAttr->AttrName(), ptrAttr->GetDouble());
      break;
    case NX_CHAR:
      AddMetadata(ptrAttr->AttrName(), ptrAttr->GetString());
      break;
    default:
      break;
  }
}

//------------------------------------------------------------------------------
// DatasetWriter::AddIntegerAttribute
//------------------------------------------------------------------------------
void DatasetWriter::AddIntegerAttribute(const std::string &strName, long lValue)
{
  if( !m_bDatasetCreated )
    AddMetadata(strName, lValue);
  else
  {
    CheckWriter();
    NexusAttrPtr ptrAttr = new NexusAttr(strName);
    ptrAttr->SetLong(lValue);
    m_ptrNeXusFileWriter->PushDatasetAttr(FullPath(), ptrAttr);
  }
}

//------------------------------------------------------------------------------
// DatasetWriter::AddFloatAttribute
//------------------------------------------------------------------------------
void DatasetWriter::AddFloatAttribute(const std::string &strName, double dValue)
{
  if( !m_bDatasetCreated )
    AddMetadata(strName, dValue);
  else
  {
    CheckWriter();
    NexusAttrPtr ptrAttr = new NexusAttr(strName);
    ptrAttr->SetDouble(dValue);
    m_ptrNeXusFileWriter->PushDatasetAttr(FullPath(), ptrAttr);
  }
}

//------------------------------------------------------------------------------
// DatasetWriter::AddStringAttribute
//------------------------------------------------------------------------------
void DatasetWriter::AddStringAttribute(const std::string &strName, const std::string &strValue)
{
  if( !m_bDatasetCreated )
    AddMetadata(strName, strValue);
  else
  {
    CheckWriter();
    NexusAttrPtr ptrAttr = new NexusAttr(strName);
    ptrAttr->SetString(strValue);
    m_ptrNeXusFileWriter->PushDatasetAttr(FullPath(), ptrAttr);
  }
}

//------------------------------------------------------------------------------
// DatasetWriter::PreAllocBuffer
//------------------------------------------------------------------------------
void DatasetWriter::PreAllocBuffer()
{
  std::size_t buf_size = 0;

  if( m_shapeMatrix.size() == 0 )
  {
    buf_size = m_ulDataItemSize;
  }
  else
  {
    if( m_nDataItemRank > 1 )
    {
      // Images or hypothetic nD data items
      buf_size = MIN(10, m_shapeMatrix[0]) * m_ulDataItemSize;
    }
    else if( 1 == m_nDataItemRank )
    {
      // Spectrums
      buf_size = MIN(100, m_shapeMatrix[0]) * m_ulDataItemSize;
    }
    else
      // scalar values
      buf_size = MIN(1000, m_shapeMatrix[0]) * m_ulDataItemSize;
  }
  
  m_mbData.realloc( MIN( m_usMaxMB * 1024 * 1024, buf_size) );  
}

//------------------------------------------------------------------------------
// DatasetWriter::SetNeXusDataType
//------------------------------------------------------------------------------
void DatasetWriter::SetNeXusDataType(NexusDataType eDataType)
{
  m_ulDataItemSize = NexusDataSetInfo::DataTypeSize(eDataType);
  m_NexusDataType = eDataType;
  
  for( yat::uint32 i = 0; i < m_nTotalRank; i++ )
    m_ulDataItemSize *= m_aiDataItemDim[i];

  yat::int64 i64TotalSize = long(NexusDataSetInfo::DataTypeSize(eDataType));
  for( yat::uint32 i = 0; i < m_nTotalRank; i++ )
    i64TotalSize *= m_aiTotalDim[i];
    
  if( i64TotalSize < 0 )
  {
    // The result is negative in case of an unlimited dataset (first dimension fixed to '-1')
    m_ui64TotalSize = yat::uint64(-i64TotalSize);
    m_bUnlimited = true;
  }
  else
    m_ui64TotalSize = yat::uint64(i64TotalSize);

  // Pre-allocate data buffer
  yat::int64 alloc_size = m_ui64TotalSize + (m_ui64TotalSize / 2);
  if( alloc_size > (1024LL * 1024LL * m_usMaxMB ) )
    alloc_size = 1024 * 1024 * m_usMaxMB;
  m_mbData.realloc(static_cast<yat::uint32>(alloc_size));
}

//------------------------------------------------------------------------------
// DatasetWriter::PushData
//------------------------------------------------------------------------------
void DatasetWriter::PushData(const void *pData, std::size_t nDataCount, bool bNoCopy)
{
  //if( m_nDataItemCount * m_ulDataItemSize == m_i64TotalSize )
  //  throw ...

  if( !m_bUnlimited && yat::uint64(m_nDataItemCount + nDataCount) * m_ulDataItemSize > m_ui64TotalSize )
  {
    std::ostringstream oss;
    oss << "Data overflow detected!!!!!\n" \
        << "m_nDataItemCount = " << m_nDataItemCount << '\n' \
        << "nDataCount = " << nDataCount << '\n' \
        << "m_ulDataItemSize = " << m_ulDataItemSize << '\n' \
        << "m_nWrittenCount = " << m_nWrittenCount << '\n' \
        << "m_ui64TotalSize = " << m_ui64TotalSize << '\n' \
        << "m_nTotalRank = " << m_nTotalRank << '\n' \
        << "m_strDataset = " << m_strDataset << '\n';
    for( std::size_t i = 0; i < m_nTotalRank; ++i )
    {
      oss << "m_aiTotalDim[" << i << "] = " << m_aiTotalDim[i] << '\n';
    }

    YAT_ERROR_STREAM("Data overflow detected!!!!!");
    YAT_ERROR_STREAM("m_nDataItemCount = " << m_nDataItemCount);
    YAT_ERROR_STREAM("nDataCount = " << nDataCount);
    YAT_ERROR_STREAM("m_ulDataItemSize = " << m_ulDataItemSize);
    YAT_ERROR_STREAM("m_nWrittenCount = " << m_nWrittenCount);
    YAT_ERROR_STREAM("m_ui64TotalSize = " << m_ui64TotalSize);
    YAT_ERROR_STREAM("m_nTotalRank = " << m_nTotalRank);
    YAT_ERROR_STREAM("m_strDataset = " << m_strDataset);

    for( std::size_t i = 0; i < m_nTotalRank; ++i )
    {
      YAT_ERROR_STREAM("m_aiTotalDim[" << i << "] = " << m_aiTotalDim[i]);
    }

    throw NexusException(oss.str(), "DatasetWriter::PushData");
  }
  
  // Increment item counter
  m_nDataItemCount += nDataCount;
  
  bool direct_ok = false;
  bool call_on_flush = true; // To call listener->OnFlushData in Flush()
  if( bNoCopy )
  { // Try the 'direct' method, e.g. without any copy of the data
    direct_ok = DirectFlush(pData);
    if( !direct_ok )
      call_on_flush = false;
  }
  
  if( (bNoCopy && !direct_ok) || !bNoCopy )
  {
    try
    {
  	  if( m_mbData.len() == 0 )
  		  // Allocate memory
  		  PreAllocBuffer();

      // push back data block
      m_mbData.put_bloc((const void*)pData, m_ulDataItemSize * nDataCount);
    }
    catch( yat::Exception& ex )
    {
      RETHROW_YAT_ERROR( ex, "DATA_ERROR", 
        std::string("Unable to push ") + yat::StringUtil::to_string(nDataCount) + std::string(" data item(s)!"),
        std::string("DatasetWriter::PushData") );
    }
    
    if( m_tmWriteTimeout.get_value() > 1 && !m_tmWriteTimeout.enabled() )
      m_tmWriteTimeout.enable();

    if( m_tmWriteTimeout.expired() ||                                                                   // Timeout expired
        m_mbData.buf_len() / (1024*1024) > m_usMaxMB ||                                                 // Buffer full
        (m_nDataItemCount - m_nWrittenCount) >= (yat::uint32)m_shapeMatrix[m_shapeMatrix.size() - 1] || // Enough data
        (!m_bUnlimited && yat::uint64(m_nDataItemCount * m_ulDataItemSize) >= m_ui64TotalSize ) )
      // Cache limit size reached or dataset fully completed
      Flush(call_on_flush);
  }
}

//------------------------------------------------------------------------------
// DatasetWriter::CreateDataset
//------------------------------------------------------------------------------
void DatasetWriter::CreateDataset()
{
  m_ptrNeXusFileWriter->CreateDataSet(FullPath(), m_NexusDataType, m_nTotalRank, m_aiTotalDim);

  std::list<std::string> lstAttributesNames = MetadataKeys();
  for(std::list<std::string>::const_iterator it = lstAttributesNames.begin(); it != lstAttributesNames.end(); it++ )
  {
    NexusAttrPtr ptrAttr = new NexusAttr(*it);

    switch( GetMetadataType(*it) )
    {
      case MMetadata::STRING:
        ptrAttr->SetString(StringMetadata(*it));
        break;
      case MMetadata::LONGINT:
        ptrAttr->SetLong(LongIntegerMetadata(*it));
        break;
      case MMetadata::INT:
        ptrAttr->SetLong(IntegerMetadata(*it));
        break;
      case MMetadata::DOUBLE:
        ptrAttr->SetDouble(DoubleMetadata(*it));
        break;
      default:
        break;
    }
    m_ptrNeXusFileWriter->PushDatasetAttr(FullPath(), ptrAttr);
  }
  m_bDatasetCreated = true;
}

//------------------------------------------------------------------------------
// DatasetWriter::Flush
//------------------------------------------------------------------------------
void DatasetWriter::Flush(bool call_on_flush)
{
  WRITER_DBG("DatasetWriter::Flush " << FullPath() << "; buffer size: " << m_mbData.len());

  while( m_mbData.len() > 0 && m_nDataItemCount - m_nWrittenCount > 0 )
  {
    // PrintfDebug("DatasetWriter::Flush m_mbData.Len(): %d, m_mbData.Pos(): %d", m_mbData.Len(), m_mbData.Pos());
    CheckWriter();

    if( m_pFlushListener && call_on_flush )
      m_pFlushListener->OnFlushData(this);

    if( !m_bDatasetCreated )
      CreateDataset();

    // Calculate a slab range on the most quickly moving dimendion

    // Rank of acquisition (0 = 1 single measurement; 1 = 1D scan; 2 = 2D scan, ...)
    yat::uint32 uiMatrixRank = m_shapeMatrix.size();

    if( 0 == uiMatrixRank )
    {
      // Prepare the NeXus slab array dimension
      int aiSlabSize[MAX_RANK];
      // Set the lower dimensions to the dimensions of a single data item
      for( yat::uint8 ui = 0; ui < m_nTotalRank; ui++ )
        aiSlabSize[ui] = m_aiDataItemDim[ui];
      yat::MemBuf mb = m_mbData;
      mb.set_owner(false);
      NexusDataSetPtr ptrDataset = new NexusDataSet(m_NexusDataType, mb.buf(), m_nTotalRank, aiSlabSize, NULL);
      m_ptrNeXusFileWriter->PushDataSet(FullPath(), ptrDataset);
    }
    else
    {
      // Get size of the first acquistion dimension
      yat::int32 iFirstDimSize = m_aiTotalDim[uiMatrixRank - 1];

      // count of data items to be written
      yat::uint32 uiPendingDataItem = m_nDataItemCount - m_nWrittenCount;

      // number of data items to write
      yat::uint32 uiToBeWritten = 0;

      if( -1 != iFirstDimSize )
      {
        // Calculate the number of acquisition step to write for complete the first acquisition dimension, 
        // which is the max number of data item that can be writtent this time
        yat::uint32 uiMaxToWrite = (yat::uint32)iFirstDimSize - (m_nWrittenCount % (yat::uint32)iFirstDimSize);

        // Number of data items what can be written is limited by the size of the first dimension of the acquisition
        uiToBeWritten = MIN(uiMaxToWrite, uiPendingDataItem);
      }
      else
      {  // In case of unlimited 1D dataset we can write all the data
        uiToBeWritten = uiPendingDataItem;
      }

      // Prepare the NeXus slab array dimension
      int aiSlabSize[MAX_RANK];
      for( yat::uint8 ui = 0; ui < MAX_RANK; ui++ ) aiSlabSize[ui] = 1;

      // Set the number of item on the first acquisition dimension
      aiSlabSize[uiMatrixRank - 1] = uiToBeWritten;

      // Set the lower dimensions to the dimensions of a single data item
      for( yat::uint8 ui = m_shapeMatrix.size(); ui < m_nTotalRank; ui++ )
        aiSlabSize[ui] = m_aiDataItemDim[ui];

      // Prepare the NeXus 'start array'
      int aiStart[MAX_RANK];
      memset(aiStart, 0, MAX_RANK * sizeof(int));

      // Position order of the first data to be written
      yat::uint32 uiStartPos = m_nWrittenCount;

      for( int i = m_shapeMatrix.size() - 1; i >= 0; i-- )
      {
        if( 0 == i )
          aiStart[i] = uiStartPos;
        else
        {
          aiStart[i] = uiStartPos % m_shapeMatrix[i];
          uiStartPos /= m_shapeMatrix[i];
        }
      }

      yat::MemBuf mb;
      mb.put_bloc((char *)(m_mbData.buf() + m_mbData.pos()), uiToBeWritten * m_ulDataItemSize);
      mb.set_owner(false);
      NexusDataSetPtr ptrDataset = new NexusDataSet(m_NexusDataType, mb.buf(), m_nTotalRank, aiSlabSize, aiStart);
      m_ptrNeXusFileWriter->PushDataSet(FullPath(), ptrDataset);

      // Move the buffer position according to the number of flushed data items
      m_mbData.set_pos(m_mbData.pos() + uiToBeWritten * m_ulDataItemSize);

      // Sets the current number of written items
      m_nWrittenCount += uiToBeWritten;
    }
  }
  m_mbData.rewind();
  m_mbData.set_len(0);
}

//------------------------------------------------------------------------------
// DatasetWriter::DirectFlush
//------------------------------------------------------------------------------
bool DatasetWriter::DirectFlush(const void *pData)
{
  WRITER_DBG("DatasetWriter::DirectFlush " << FullPath());
  // PrintfDebug("DatasetWriter::Flush m_mbData.Len(): %d, m_mbData.Pos(): %d", m_mbData.Len(), m_mbData.Pos());
  CheckWriter();

  if( m_pFlushListener )
    m_pFlushListener->OnFlushData(this);

  if( !m_bDatasetCreated )
    CreateDataset();

  // Calculate a slab range on the most quickly moving dimendion

  // Rank of acquisition (0 = 1 single measurement; 1 = 1D scan; 2 = 2D scan, ...)
  std::size_t nMatrixRank = m_shapeMatrix.size();

  if( 0 == nMatrixRank )
  {
    // Prepare the NeXus slab array dimension
    int aiSlabSize[MAX_RANK];
    // Set the lower dimensions to the dimensions of a single data item
    for( yat::uint8 ui = 0; ui < m_nTotalRank; ui++ )
      aiSlabSize[ui] = m_aiDataItemDim[ui];

    NexusDataSetPtr ptrDataset = new NexusDataSet(m_NexusDataType, (void*)pData, m_nTotalRank, aiSlabSize, NULL);
    m_ptrNeXusFileWriter->PushDataSet(FullPath(), ptrDataset);
    
    // Sets the current number of written items
    m_nWrittenCount += 1;
    
    return true;
  }
  else
  {
    // Get size of the first acquistion dimension
    std::size_t first_dim_size = std::size_t(m_aiTotalDim[nMatrixRank - 1]);

    // count of data items to be written
    std::size_t nPendingDataItem = m_nDataItemCount - m_nWrittenCount;

    // number of data items to write
    std::size_t nToBeWritten = 0;

    if( -1 != m_aiTotalDim[nMatrixRank - 1] )
    {
      // Calculate the number of acquisition step to write for complete the first acquisition dimension, 
      // which is the max number of data item that can be writtent this time
      std::size_t nMaxToWrite = first_dim_size - (m_nWrittenCount % first_dim_size);

      // Number of data items what can be written is limited by the size of the first dimension of the acquisition
      nToBeWritten = MIN(nMaxToWrite, nPendingDataItem);
    }
    else
    {  // In case of unlimited 1D dataset we can write all the data
      nToBeWritten = nPendingDataItem;
    }

    // Prepare the NeXus slab array dimension
    int aiSlabSize[MAX_RANK];
    for( yat::uint8 ui = 0; ui < MAX_RANK; ui++ ) aiSlabSize[ui] = 1;

    // Set the number of item on the first acquisition dimension
    aiSlabSize[nMatrixRank - 1] = nToBeWritten;

    // Set the lower dimensions to the dimensions of a single data item
    for( yat::uint8 ui = m_shapeMatrix.size(); ui < m_nTotalRank; ui++ )
      aiSlabSize[ui] = m_aiDataItemDim[ui];

    // Prepare the NeXus 'start array'
    int aiStart[MAX_RANK];
    memset(aiStart, 0, MAX_RANK * sizeof(int));

    // Position order of the first data to be written
    std::size_t start_pos = m_nWrittenCount;

    for( int i = m_shapeMatrix.size() - 1; i >= 0; i-- )
    {
      if( 0 == i )
        aiStart[i] = start_pos;
      else
      {
        aiStart[i] = start_pos % m_shapeMatrix[i];
        start_pos /= m_shapeMatrix[i];
      }
    }

    if( nToBeWritten == nPendingDataItem )
    {
      NexusDataSetPtr ptrDataset = new NexusDataSet(m_NexusDataType, (void*)pData, m_nTotalRank, aiSlabSize, aiStart);
      ptrDataset->SetOwner(false);
      m_ptrNeXusFileWriter->PushDataSet(FullPath(), ptrDataset);
      
      // Sets the current number of written items
      m_nWrittenCount += nToBeWritten;
      
      // ok
      return true;
    }
    
    // Cannot flush all the data, use the standard method, with copy
    WRITER_DBG("no copy failed");
    return false;
  }
}

//------------------------------------------------------------------------------
// DatasetWriter::Stop
//------------------------------------------------------------------------------
void DatasetWriter::Stop()
{
  Flush(true);
  Reset();
}

//------------------------------------------------------------------------------
// DatasetWriter::Abort
//------------------------------------------------------------------------------
void DatasetWriter::Abort()
{
  // Purge memory
  Reset();
}

//------------------------------------------------------------------------------
// DatasetWriter::Reset
//------------------------------------------------------------------------------
void DatasetWriter::Reset()
{
  WRITER_DBG("DatasetWriter::Reset <");

  // Dataset must be not already present in the nexus file
  m_bDatasetCreated = false;
  
  // Total count of Pushdata calls
  m_nDataItemCount = 0;
  
  // No data written yet
  m_nWrittenCount = 0;
  if( !m_ptrNeXusFileWriter.is_null() )
  {
    // No more NeXus file writer object
    m_ptrNeXusFileWriter.reset();
  }

  // Don't delete buffer, just set cursor position
  m_mbData.rewind();
  
  ClearMetadata();
  
  m_NexusDataType = NX_NONE;
  WRITER_DBG("DatasetWriter::Reset >");
}

//------------------------------------------------------------------------------
// DatasetWriter::FullPath
//------------------------------------------------------------------------------
std::string DatasetWriter::FullPath() const
{
  return yat::StringUtil::str_format("%s/%s", PSZ(m_strPath), PSZ(m_strDataset));
}

//=============================================================================
// MMetadata
//=============================================================================
//------------------------------------------------------------------------
// MMetadata::AddMetadata
//------------------------------------------------------------------------
void MMetadata::AddMetadata(const std::string &strKey, const std::string &strValue)
{
  m_mapString[strKey] = strValue;
}
void MMetadata::AddMetadata(const std::string &strKey, const char *pszValue)
{
  if( pszValue )
    m_mapString[strKey] = std::string(pszValue);
  else
    throw yat::Exception("NULL_POINTER", "Null pointer passed as metadata value", "MMetadata::AddMetadata");
}
void MMetadata::AddMetadata(const std::string &strKey, int iValue)
{
  m_mapInteger[strKey] = iValue;
  // Also store metadata as string
  std::string strTmp;
  yat::StringUtil::printf(&strTmp, "%d", iValue);
  m_mapString[strKey] = strTmp;
}
void MMetadata::AddMetadata(const std::string &strKey, double dValue)
{
  m_mapDouble[strKey] = dValue;
  // Also store metadata as string
  std::string strTmp;
  yat::StringUtil::printf(&strTmp, "%g", dValue);
  m_mapString[strKey] = strTmp;
}
void MMetadata::AddMetadata(const std::string &strKey, long lValue)
{
  m_mapLong[strKey] = lValue;
  // Also store metadata as string
  std::string strTmp;
  yat::StringUtil::printf(&strTmp, "%ld", lValue);
  m_mapString[strKey] = strTmp;
}

//------------------------------------------------------------------------
// MMetadata::HasMetadata
//------------------------------------------------------------------------
bool MMetadata::HasMetadata(const std::string &strKey) const
{
  std::map<std::string, std::string>::const_iterator it = m_mapString.find(strKey);
  if( it != m_mapString.end() )
    return true;
  return false;
}

//------------------------------------------------------------------------
// MMetadata::GetMetadata
//------------------------------------------------------------------------
bool MMetadata::GetMetadata(const std::string &strKey, std::string *pstrValue, bool bThrow) const
{
  std::map<std::string, std::string>::const_iterator it = m_mapString.find(strKey);
  if( it == m_mapString.end() )
  {
    if( bThrow )
      throw yat::Exception("NO_DATA", "Metadata not found", "MMetadata::GetMetadata");
    else
      return false;
  }
  *pstrValue = it->second;
  return true;
}

//------------------------------------------------------------------------
// MMetadata::GetIntegerMetadata
//------------------------------------------------------------------------
bool MMetadata::GetIntegerMetadata(const std::string &strKey, int *piValue, bool bThrow) const
{
  std::map<std::string, int>::const_iterator it = m_mapInteger.find(strKey);
  if( it == m_mapInteger.end() )
  {
    if( bThrow )
      throw yat::Exception("NO_DATA", "Metadata not found", "MMetadata::GetIntegerMetadata");
    else
      return false;
  }
  *piValue = it->second;
  return true;
}

//------------------------------------------------------------------------
// MMetadata::IntegerMetadata
//------------------------------------------------------------------------
int MMetadata::IntegerMetadata(const std::string &strKey) const
{
  int i;
  GetIntegerMetadata(strKey, &i, true);
  return i;
}

//------------------------------------------------------------------------
// MMetadata::GetLongIntegerMetadata
//------------------------------------------------------------------------
bool MMetadata::GetLongIntegerMetadata(const std::string &strKey, long *plValue, bool bThrow) const
{
  std::map<std::string, long>::const_iterator it = m_mapLong.find(strKey);
  if( it == m_mapLong.end() )
  {
    if( bThrow )
      throw yat::Exception("NO_DATA", "Metadata not found", "MMetadata::GetLongIntegerMetadata");
    else
      return false;
  }
  *plValue = it->second;
  return true;
}

//------------------------------------------------------------------------
// MMetadata::LongIntegerMetadata
//------------------------------------------------------------------------
long MMetadata::LongIntegerMetadata(const std::string &strKey) const
{
  long l;
  GetLongIntegerMetadata(strKey, &l, true);
  return l;
}

//------------------------------------------------------------------------
// MMetadata::GetDoubleMetadata
//------------------------------------------------------------------------
bool MMetadata::GetDoubleMetadata(const std::string &strKey, double *pdValue, bool bThrow) const
{
  std::map<std::string, double>::const_iterator it = m_mapDouble.find(strKey);
  if( it == m_mapDouble.end() )
  {
    if( bThrow )
      throw yat::Exception("NO_DATA", "Metadata not found", "MMetadata::GetDoubleMetadata");
    else
      return false;
  }
  *pdValue = it->second;
  return true;
}

//------------------------------------------------------------------------
// MMetadata::DoubleMetadata
//------------------------------------------------------------------------
double MMetadata::DoubleMetadata(const std::string &strKey) const
{
  double d;
  GetDoubleMetadata(strKey, &d, true);
  return d;
}

//------------------------------------------------------------------------
// MMetadata::GetStringMetadata
//------------------------------------------------------------------------
bool MMetadata::GetStringMetadata(const std::string &strKey, std::string *pstrValue, bool bThrow) const
{
  std::map<std::string, std::string>::const_iterator it = m_mapString.find(strKey);
  if( it == m_mapString.end() )
  {
    if( bThrow )
      throw yat::Exception("NO_DATA", "Metadata not found", "MMetadata::GetDoubleMetadata");
    else
      return false;
  }
  *pstrValue = it->second;
  return true;
}

//------------------------------------------------------------------------
// MMetadata::StringMetadata
//------------------------------------------------------------------------
std::string MMetadata::StringMetadata(const std::string &strKey) const
{
  std::string str;
  GetStringMetadata(strKey, &str, true);
  return str;
}

//------------------------------------------------------------------------
// MMetadata::GetMetadataType
//------------------------------------------------------------------------
MMetadata::Type MMetadata::GetMetadataType(const std::string &strKey, bool bThrow) const
{
  std::map<std::string, double>::const_iterator itDouble = m_mapDouble.find(strKey);
  if( itDouble != m_mapDouble.end() )
  return DOUBLE; 
  
  std::map<std::string, int>::const_iterator itInteger = m_mapInteger.find(strKey);
  if( itInteger != m_mapInteger.end() )
  return INT; 
  
  std::map<std::string, long>::const_iterator itLong = m_mapLong.find(strKey);
  if( itLong != m_mapLong.end() )
  return LONGINT; 
  
  std::map<std::string, std::string>::const_iterator itString = m_mapString.find(strKey);
  if( itString != m_mapString.end() )
  return STRING; 

  if( bThrow )
  throw yat::Exception("NO_DATA", "metadata not found", "MMetadata::GetMetadataType");
  return NONE;
}

//------------------------------------------------------------------------
// MMetadata::Keys
//------------------------------------------------------------------------
std::list<std::string> MMetadata::MetadataKeys() const
{
  std::list<std::string> keys;
  
  std::map<std::string, int>::const_iterator itInteger = m_mapInteger.begin();
  for( ; itInteger != m_mapInteger.end(); itInteger++ )
  keys.push_back(itInteger->first);
  
  std::map<std::string, std::string>::const_iterator itString = m_mapString.begin();
  for( ; itString != m_mapString.end(); itString++ )
  keys.push_back(itString->first);
  
  std::map<std::string, double>::const_iterator itDouble = m_mapDouble.begin();
  for( ; itDouble != m_mapDouble.end(); itDouble++ )
  keys.push_back(itDouble->first);
  
  std::map<std::string, long>::const_iterator itLong = m_mapLong.begin();
  for( ; itLong != m_mapLong.end(); itLong++ )
  keys.push_back(itLong->first);
  
  return keys;
}

//------------------------------------------------------------------------
// MMetadata::Clear
//------------------------------------------------------------------------
void MMetadata::ClearMetadata()
{
  m_mapInteger.clear();
  m_mapLong.clear();
  m_mapDouble.clear();
  m_mapString.clear();
}

} // namespace nxcpp
