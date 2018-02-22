//*****************************************************************************
/// Synchrotron SOLEIL
///
/// NeXus C++ API over NAPI
///
/// Creation : 16/02/2005
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


#ifndef NEXUSAPI
  #ifdef WIN32
    #include "napi.h"
  #else // LINUX
    #include <napi.h>
  #endif
#endif

#ifdef WIN32
  #pragma warning(disable:4786)
#endif

// Use enum instead of #define for NeXus DataTypes
#undef NX_FLOAT32
#undef NX_FLOAT64
#undef NX_INT8   
#undef NX_UINT8  
#undef NX_INT16  
#undef NX_UINT16 
#undef NX_INT32  
#undef NX_UINT32 
#undef NX_INT64  
#undef NX_UINT64 
#undef NX_CHAR   
#undef NX_BINARY

// Use enum instead of #define for Nexus Status codes
#undef NX_OK
#undef NX_EOD

#include "stdio.h"
#include <iostream>
#include <sstream>

// yat
#include <yat/CommonHeader.h>
#include <yat/utils/String.h>
#include <yat/memory/MemBuf.h>
#include <yat/threading/Mutex.h>
#include <yat/threading/Semaphore.h>
#include <yat/utils/Logging.h>

#include <nexuscpp/nxfile.h>

#ifdef YAT_WIN32
  #define OUTPUT_DISABLE   freopen("NUL", "w", stdout)
  #define OUTPUT_ENABLE    freopen("CON", "w", stdout)
#else
  #define OUTPUT_DISABLE   do {FILE* fout = freopen("/dev/null", "w", stdout);\
                            FILE* ferr = freopen("/dev/null", "w", stderr);\
                            (void)fout; (void)ferr;} while (0)
  #define OUTPUT_ENABLE    do {FILE* fout = freopen("/dev/tty", "w", stdout);\
                            FILE* ferr = freopen("/dev/tty", "w", stderr);\
                            (void)fout; (void)ferr;} while(0)
#endif


using namespace nxcpp;

namespace nxcpp
{
// Error msgs
const char BAD_ACCESS_MODE[]        = "Bad access mode for file '%s'";
const char FILE_ALREADY_LOCKED[]    = "Same thread has already locked the file";
const char CREATE_FILE_ERR[]        = "Cannot create file '%s'";
const char OPENING_FILE_ERR[]       = "Cannot open file";
const char OPENING_FILE_READ_ERR[]  = "Cannot open file '%s' for reading";
const char OPENING_FILE_RW_ERR[]    = "Cannot open file for '%s' reading and writing";
const char CREATE_DATA_ERR[]        = "Cannot create dataset '%s' (group '%s', file '%s')";
const char CREATE_DATA_BAD_TYPE_ERR[] = "Cannot create dataset '%s' (group '%s', file '%s'): no datatype !!";
const char BAD_GROUP_NAME_ERR[]     = "Null value passed as group name";
const char OPEN_GROUP_ERR[]         = "Cannot open group '%s' (file '%s')";
const char MAKE_GROUP_ERR[]         = "Cannot create group '%s' (file '%s')";
const char OPEN_DATA_ERR[]          = "Cannot open dataset '%s' (group '%s', file '%s')";
const char CLOSE_DATA_ERR[]         = "Cannot close dataset (group '%s', file '%s')";
const char CLOSE_GROUP_ERR[]        = "Cannot close group '%s' in file '%s'";
const char PUT_DATA_ERR[]           = "Cannot put data in dataset '%s' (group '%s', file '%s')";
const char PUT_DATASUBSET_ERR[]     = "Cannot put data subset in dataset '%s' (group '%s', file '%s')";
const char PUT_ATTR_ERR[]           = "Cannot write attribute '%s' (group '%s', file '%s')";
const char GET_ATTR_ERR[]           = "Cannot read attribute '%s' (group '%s', file '%s')";
const char GETINFO_ERR[]            = "Cannot read data set infos (dataset '%s', group '%s', file '%s')";
const char GETDATA_ERR[]            = "Cannot read data set (dataset '%s', group '%s', file '%s')";
const char FLUSH_ERR[]              = "Cannot flush data (file '%s')";
const char GET_INIT_ENUM_ITEM[]     = "Cannot initialize group items enumeration (file '%s')";
const char GET_RETREIVE_ITEM[]      = "Cannot retreive group items information (file '%s')";
const char GET_INIT_ENUM_ATTR[]     = "Cannot initialize attributes enumeration (file '%s')";
const char GET_RETREIVE_ATTR[]      = "Cannot retreive attributes information (file '%s')";
const char LINK_ITEM_ERR[]          = "Cannot link item to current group (file '%s')";
const char GET_GROUPINFO_ERR[]      = "Cannot read group infos (group '%s', file '%s')";
const char GET_ATTRINFO_ERR[]       = "Cannot read attributes infos (group '%s', file '%s')";
const char CANNOT_MERGE_DATASETS[]  = "Cannot merge datasets";

// Reasons
const char MISSING_FILE_NAME[]      = "Missing file name !";
const char UNABLE_TO_OPEN_FILE[]    = "Unable to open '%s'";
const char NO_HANDLE[]              = "Missing file handle !";
const char OPERATION_FAILED[]       = "Operation failed !";
const char NO_DATASET[]             = "No data set !";
const char TYPES_MISMATCH[]         = "Type mismatch !";
const char BAD_DATASET[]            = "Bad dataset";
const char API_BUSY[]               = "Ressource unavalaible !";

const char ROOT_LEVEL[] = "root(NXroot)";    // Top level group of a NeXus file

//---------------------------------------------------------------------------
// Free function that return a char * from a std::string objet
//---------------------------------------------------------------------------
inline char *PCSZToPSZ(const char *pcsz)
{
  return (char *)((void *)pcsz);
}

} // namespace

// Commenter cette ligne si necessaire
#define MULTIPLE_ACCESS

namespace nxcpp
{
//=============================================================================
// NeXus File Class internal implementation
//
// Do the job
//=============================================================================
class NexusFileImpl
{
friend class NexusGlobalLock;
private:

#ifdef MULTIPLE_ACCESS
  struct SavedFileState // This object keep the file state in case of multiple access
  {
    std::string              strOpenDataSet; 
    std::vector<std::string> vecOpenGroup;
  };
  
  static yat::Mutex s_mtxMultiAccess;  // mutex used for multi-access
  
  static NexusFileImpl* s_pCurrentImpl;    // file currently accessed
  SavedFileState        m_FileState;       //  File state
#endif

  std::string                m_strOpenDataSet;   // name of the currently opened data set
  std::vector<std::string>   m_vecOpenGroup;  // stack of opened groups names
  
  void *                m_hFile;            // NeXus Handle
  int                   m_iLastAccessMode;  // NeXus file access mode: read or read/write
  std::string           m_strFullPath;      // Path to NeXus file
  bool                  m_bNeedFlush;       // if true flushing data is needed before closing

  static yat::Semaphore s_semGuardian;      // Access locking in multithreaded environment
  static yat::ThreadUID s_uidThread;        // Current granted Thread

  // Method called by CreateFile, OpenRead, OpenReadWrite
  void PrivOpenFile(const char *pcszFullPath, int iAccessMode);
  
  // Gracefully close the file
  void PrivClose();

  void PrivFlush();

  // Ensure only one file is accessed at a time
  void Lock();
  void Unlock();

  // For internal purpose
  void SetNeedFlush(bool bNeed=true) { m_bNeedFlush = bNeed; }

  // Check group name
  // Look for forbidden char and replace them by '_'
  yat::String GetGroupName(const char *pcszName);
  
  void GetAccess();
  void SaveState();
  
public:
  // Constructor
  NexusFileImpl(const char *pcszFullPath=NULL);

  // Destructor
  ~NexusFileImpl();

  // Creates file
  void Create(const char *pcszFullPath, ENexusCreateMode eMode);

  // Opens an existing file for read operations
  void OpenRead(const char *pcszFullPath);

  // Opens an existing file for read/write operations
  void OpenReadWrite(const char *pcszFullPath);

  // Closes currently opened file
  void Close();

  // Flushes all data to the file
  void Flush();

  // Adds a new group
  void CreateGroup(const char *pcszName, const char *pcszClass, bool bOpen=true);

  // Opens a existing group
  bool OpenGroup(const char *pcszName, const char *pcszClass, bool bThrowException=true);

  // Open or create a group from a full path inside a NeXus file
  bool CreateOpenGroupPath(const char *pszPath, bool bCreate, bool bThrowException);

  // CurrentDataset
  std::string CurrentDataset();

  // CurrentGroup
  std::string CurrentGroup();

  // CurrentGroupPath
  std::string CurrentGroupPath(std::vector<std::string> *pvecGroups=NULL);

  // Closes current group
  void CloseGroup();

  // Closes all opened groups
  void CloseAllGroups();

  // Creates data set
  void CreateDataSet(const char *pcszName, NexusDataType eDataType, 
                     int iRank, int *piDim, int bOpen);

  // Creates compressed data set
  void CreateCompressedDataSet(const char *pcszName, NexusDataType eDataType, 
                               int iRank, int *piDim, int *piChunkDim, int bOpen);

  // Closes currenly open dataset
  void CloseDataSet();

  // Quickly creates simple data set and writes the data
  void WriteData(const char *pcszName, void *pData, NexusDataType eDataType, int iRank,
                 int *piDim, bool bCreate=true);

  // Quickly creates simple data set and writes part of the data
  void WriteDataSubSet(const char *pcszName, void *pData, NexusDataType eDataType, 
                       int iRank, int *piStart, int *piDim, bool bCreate=true, bool bNoDim = false);

  // Opens a already existing data set
  bool OpenDataSet(const char *pcszName, bool bThrowException=true);

  // Puts data in an existing data set
  void PutData(void *pData, const char *pcszName, int bFlush);

  // Puts data subset in an existing data set
  void PutDataSubSet(void *pData, int *piStart, int *piSize, const char *pcszName);

  // Put Data set attribut
  void PutAttr(const char *pcszName, void *pValue, int iLen, int iDataType);

  // Read data values from a data set in currently open group
  void GetData(NexusDataSet *DataSet, const char *pcszDataSetName);

  // Reads data values from a data set in currently open group
  void GetDataSubSet(NexusDataSet *pDataSet, const char *pcszDataSetName);

  // Gets info about a data set
  void GetDataSetInfo(NexusDataSetInfo *pDataSetInfo, const char *pcszDataSetName);

  // Read attribute
  void GetAttribute(const char *pcszAttr, int *pBufLen, void *pData, int iDataType);
  void GetAttribute(const char *pcszAttr, int *pBufLen, void *pData, int *piDataType);

  // Get info about the first item (data set or group) in the current group
  int GetFirstItem(NexusItemInfo *pItemInfo);
  
  // Get info about the next item (data set or group) in the current group
  int GetNextItem(NexusItemInfo *pItemInfo);
  
  // Get info about the first attribute of the specified data set
  int GetFirstAttribute(NexusAttrInfo *pAttrInfo, const char *pcszDataSet=NULL);

  // Get info about the next attribute of the specified data set
  int GetNextAttribute(NexusAttrInfo *pAttrInfo);

  // Get a handle on the currently open data set in order to link it with a group
  void GetDataSetLink(NexusItemID *pnxl);

  // Get a handle on the currently open group set in order to link it with a group
  void GetGroupLink(NexusItemID *pnxl);

  // Link a item to the currently open group
  void LinkToCurrentGroup(const NexusItemID &nxl);

  // Get the number of items in the current group
  int ItemCount();

  // Get the number of items in the current group
  int AttrCount();
  
  // Get the group children lists
  void GetGroupChildren(std::vector<std::string> *pvecDatasets, std::vector<std::string> *pvecGroupNames, std::vector<std::string> *pvecGroupClasses);
  NexusItemInfoList GetGroupChildren();
};

} // namespace

#ifdef MULTIPLE_ACCESS
  #define LOCK_ON_FILE \
    yat::AutoMutex<> _lock(s_mtxMultiAccess); \
    GetAccess();

  #define LIGHT_LOCK yat::AutoMutex<> _lock(s_mtxMultiAccess);
  
#else
    #define LOCK_ON_FILE
    #define LIGHT_LOCK
#endif  

//=============================================================================
//
// NeXus File Class
//
//=============================================================================
yat::Semaphore NexusFileImpl::s_semGuardian;
yat::ThreadUID NexusFileImpl::s_uidThread = YAT_INVALID_THREAD_UID;

#ifdef MULTIPLE_ACCESS
  NexusFileImpl* NexusFileImpl::s_pCurrentImpl = NULL;
  yat::Mutex NexusFileImpl::s_mtxMultiAccess;
#endif

//---------------------------------------------------------------------------
// Constructor
//---------------------------------------------------------------------------
NexusFileImpl::NexusFileImpl(const char *pszFullPath)
{
  m_hFile = NULL;
  if( NULL != pszFullPath )
    m_strFullPath = pszFullPath;
  m_strOpenDataSet = g_strNoDataSet;
  m_vecOpenGroup.push_back(ROOT_LEVEL);
  m_bNeedFlush = false;
  m_iLastAccessMode = 0;
}

//---------------------------------------------------------------------------
// Destructor
//---------------------------------------------------------------------------
NexusFileImpl::~NexusFileImpl()
{
  Close();
#ifdef MULTIPLE_ACCESS
  yat::AutoMutex<> _lock(s_mtxMultiAccess);
  if( this == s_pCurrentImpl )
    s_pCurrentImpl = NULL;
#endif  
}

//=============================================================================
//
// NeXus global lock object
//
//=============================================================================
//=============================================================================
// NexusGlobalLock::NexusGlobalLock
//=============================================================================
NexusGlobalLock::NexusGlobalLock()
{
  NexusFileImpl::s_mtxMultiAccess.lock();
}

//=============================================================================
// NexusGlobalLock::~NexusGlobalLock
//=============================================================================
NexusGlobalLock::~NexusGlobalLock()
{
  NexusFileImpl::s_mtxMultiAccess.unlock();
}

//*****************************************************************************
// Private methods
//*****************************************************************************

//---------------------------------------------------------------------------
// NexusFileImpl::PrivOpenFile
//---------------------------------------------------------------------------
void NexusFileImpl::PrivOpenFile(const char *pcszFullPath, int iAccessMode)
{
  if( NULL == pcszFullPath && m_strFullPath.empty() )
    throw NexusException(MISSING_FILE_NAME, "NexusFileImpl::PrivOpenFile");
  else if( pcszFullPath && false == yat::String(m_strFullPath).is_equal_no_case(yat::String(pcszFullPath)) )
    m_strFullPath = pcszFullPath;

  Lock();

  yat::log_verbose("nex", "Opening file '%s' whith access mode %d", PSZ(m_strFullPath), iAccessMode);

  int iRc = NXopen(PCSZToPSZ(m_strFullPath.c_str()), (NXaccess)iAccessMode, &m_hFile);
  if( iRc != NX_OK )
  {
    // Failure
    Unlock();
    switch( iAccessMode )
    {
      case NXACC_CREATE4:
      case NXACC_CREATE5:
        throw NexusException(PSZ_FMT(CREATE_FILE_ERR, pcszFullPath), "NexusFileImpl::PrivOpenFile");
      case NXACC_READ:
        throw NexusException(PSZ_FMT(OPENING_FILE_READ_ERR, pcszFullPath), "NexusFileImpl::PrivOpenFile");
      case NXACC_RDWR:
        throw NexusException(PSZ_FMT(OPENING_FILE_RW_ERR, pcszFullPath), "NexusFileImpl::PrivOpenFile");
      default:
        throw NexusException(PSZ_FMT(BAD_ACCESS_MODE, pcszFullPath), "NexusFileImpl::PrivOpenFile");
    }
  }
}

//---------------------------------------------------------------------------
// NexusFileImpl::CheckGroupName
//---------------------------------------------------------------------------
yat::String NexusFileImpl::GetGroupName(const char *pcszName)
{
  if( !pcszName )
    throw NexusException(PSZ_FMT(BAD_GROUP_NAME_ERR), "NexusFileImpl::GetGroupName");

  yat::String strGroupName(pcszName);
  strGroupName.replace('/', '_');
  strGroupName.replace('\\', '_');
  strGroupName.replace(' ', '_');
  strGroupName.replace('*', '_');
  strGroupName.replace('"', '_');
  strGroupName.replace('\'', '_');
  strGroupName.replace(',', '_');
  strGroupName.replace(';', '_');
  strGroupName.replace(':', '_');
  strGroupName.replace('!', '_');
  strGroupName.replace('?', '_');

  if( !yat::String(pcszName).is_equal(strGroupName) )
    yat::log_warning("nex", "Changed group name from '%s' to '%s'", pcszName, PSZ(strGroupName));

  return strGroupName;
}

#ifdef MULTIPLE_ACCESS
//---------------------------------------------------------------------------
// NexusFileImpl::GetAccess
//---------------------------------------------------------------------------
void NexusFileImpl::GetAccess()
{
    static bool sbReEnter = false;
    if( sbReEnter )
        return;
    sbReEnter = true;

  if( s_pCurrentImpl != this )
  {
    if( NULL != s_pCurrentImpl )
    { 
      // Close the current file
      s_pCurrentImpl->SaveState();
      s_pCurrentImpl->PrivClose();
    }

    // Restore file object state
    if( m_iLastAccessMode )
    {
      try
      {
        PrivOpenFile(PSZ(m_strFullPath), m_iLastAccessMode);
        CreateOpenGroupPath(PSZ(CurrentGroupPath(&m_FileState.vecOpenGroup)), false, false);
        if( !yat::String(m_FileState.strOpenDataSet).is_equal(g_strNoDataSet) )
          OpenDataSet(PSZ(m_FileState.strOpenDataSet));
      }
      catch( NexusException &e)
      {
          sbReEnter = false;
          throw e;
      }
    }    
    s_pCurrentImpl = this;
  }
  sbReEnter = false;
}

//---------------------------------------------------------------------------
// NexusFileImpl::SaveState
//---------------------------------------------------------------------------
void NexusFileImpl::SaveState()
{
  m_FileState.strOpenDataSet = m_strOpenDataSet;
  m_FileState.vecOpenGroup = m_vecOpenGroup;
}
#endif

//---------------------------------------------------------------------------
// NexusFileImpl::Lock
//---------------------------------------------------------------------------
void NexusFileImpl::Lock()
{
#ifndef MULTIPLE_ACCESS
  yat::SemaphoreState semState = s_semGuardian.try_wait();
  if( yat::SEMAPHORE_NO_RSC == semState )
  {  // Resource unavailable
    if( yat::ThreadingUtilities::self() == s_uidThread )
      // Already owened by current thread => throw a exception
      throw NexusException(FILE_ALREADY_LOCKED, "NexusFileImpl::Lock");
    else
      // wait
      s_semGuardian.wait();
  }
  s_uidThread = yat::ThreadingUtilities::self();
#endif
}

//---------------------------------------------------------------------------
// NexusFileImpl::Unlock
//---------------------------------------------------------------------------
void NexusFileImpl::Unlock()
{
#ifndef MULTIPLE_ACCESS
  s_uidThread = YAT_INVALID_THREAD_UID;
  s_semGuardian.post();
#endif
}

//*****************************************************************************
// Publics methods
//*****************************************************************************

//---------------------------------------------------------------------------
// NexusFileImpl::PrivClose
//---------------------------------------------------------------------------
void NexusFileImpl::PrivClose()
{
  if( NULL != m_hFile )
  {
    // Gracefully close opened groups
    CloseAllGroups();
    // Flushing data
    Flush();

    yat::log_verbose("nex", "Close file '%s'", PSZ(m_strFullPath));

    // Then close the file
    NXclose(&m_hFile);
    m_hFile = NULL;
    
    Unlock();
  }
}

//---------------------------------------------------------------------------
// NexusFileImpl::Close
//---------------------------------------------------------------------------
void NexusFileImpl::Close()
{
  LOCK_ON_FILE
  
  PrivClose();
    m_iLastAccessMode = 0;
}

//---------------------------------------------------------------------------
// NexusFileImpl::PrivFlush
//---------------------------------------------------------------------------
void NexusFileImpl::PrivFlush()
{
  if( NULL != m_hFile && m_bNeedFlush)
  {
    if( NX_OK != NXflush(&m_hFile) )
    {
      // No needs to retry since it will fail again then call Close()
      m_bNeedFlush = false;
      throw NexusException(PSZ_FMT(FLUSH_ERR, PSZ(m_strFullPath)), "NexusFileImpl::Flush");
    }
  }

  yat::log_verbose("nex", "Flush data");

  // Flushing data closes the currently open data set
  m_strOpenDataSet = g_strNoDataSet;
}

//---------------------------------------------------------------------------
// NexusFileImpl::Flush
//---------------------------------------------------------------------------
void NexusFileImpl::Flush()
{
  LOCK_ON_FILE
    PrivFlush();
}

//---------------------------------------------------------------------------
// NexusFileImpl::Create
//---------------------------------------------------------------------------
void NexusFileImpl::Create(const char *pcszFullPath, ENexusCreateMode eMode)
{
  LOCK_ON_FILE
  
  int iMode = 0;
  switch( eMode )
  {
    case NX_HDF4:
      iMode = NXACC_CREATE4; break;
    case NX_HDF5:
    case NX_XML: // Not implemented yet
      iMode = NXACC_CREATE5; break;
  }
  PrivOpenFile(pcszFullPath, iMode);
  m_iLastAccessMode = NXACC_RDWR;
}

//---------------------------------------------------------------------------
// NexusFileImpl::OpenRead
//---------------------------------------------------------------------------
void NexusFileImpl::OpenRead(const char *pcszFullPath)
{
  LOCK_ON_FILE
  
  PrivOpenFile(pcszFullPath, NXACC_READ);
  m_iLastAccessMode = NXACC_READ;
}

//---------------------------------------------------------------------------
// NexusFileImpl::OpenReadWrite
//---------------------------------------------------------------------------
void NexusFileImpl::OpenReadWrite(const char *pcszFullPath)
{
  LOCK_ON_FILE
  
  PrivOpenFile(pcszFullPath, NXACC_RDWR);
  m_iLastAccessMode = NXACC_RDWR;
}

//---------------------------------------------------------------------------
// NexusFileImpl::CreateGroup
//---------------------------------------------------------------------------
void NexusFileImpl::CreateGroup(const char *pcszName, const char *pcszClass, bool bOpen)
{
  LOCK_ON_FILE
  
  CloseDataSet();

  if( NULL == m_hFile )
    throw NexusException(NO_HANDLE, "NexusFileImpl::CreateGroup");
  
  if( !pcszName )
    throw NexusException(PSZ_FMT(MAKE_GROUP_ERR, "<no_name>", PSZ(m_strFullPath)),
                         "NexusFileImpl::CreateGroup");

  yat::String strGroupName = GetGroupName(pcszName);

  OUTPUT_DISABLE;

  int iRc = NXmakegroup(m_hFile, PCSZToPSZ(PSZ(strGroupName)), PCSZToPSZ(pcszClass));

  OUTPUT_ENABLE;

  if( iRc != NX_OK )
    throw NexusException(PSZ_FMT(MAKE_GROUP_ERR, pcszName, PSZ(m_strFullPath)), "NexusFileImpl::CreateGroup");

  if( bOpen )
    OpenGroup(PSZ(strGroupName), pcszClass);
}

//---------------------------------------------------------------------------
// NexusFileImpl::OpenGroup
//---------------------------------------------------------------------------
bool NexusFileImpl::OpenGroup(const char *pcszName, const char *pcszClass, bool bThrowException)
{
  LOCK_ON_FILE
  
  if( NULL == m_hFile )
    throw NexusException(NO_HANDLE, "NexusFileImpl::OpenGroup");

  yat::String strGroup;
  strGroup.printf("%s(%s)", pcszName, pcszClass);
  yat::log_verbose("nex", "Opening group '%s'", PSZ(strGroup));

  OUTPUT_DISABLE;

  // Trying to open group without filtering the requested name
  yat::String strGroupName = pcszName;
  int iRc = NXopengroup(m_hFile, PCSZToPSZ(PSZ(strGroupName)), PCSZToPSZ(pcszClass));
  if( iRc != NX_OK )
  {
    // Trying 
    strGroupName = GetGroupName(pcszName);
    strGroup.printf("%s(%s)", PSZ(strGroupName), pcszClass);
    iRc = NXopengroup(m_hFile, PCSZToPSZ(PSZ(strGroupName)), PCSZToPSZ(pcszClass));
  }

  OUTPUT_ENABLE;

  if( iRc != NX_OK )
  {
    if( bThrowException )
      throw NexusException(PSZ_FMT(OPEN_GROUP_ERR, PSZ(strGroupName), PSZ(m_strFullPath)),
                           "NexusFileImpl::OpenGroup");
    else
      return false;
  }

  // Empiler le nom du groupe
  m_vecOpenGroup.push_back(strGroup);
  return true;
}

//---------------------------------------------------------------------------
// NexusFileImpl::CurrentDataset
//---------------------------------------------------------------------------
std::string NexusFileImpl::CurrentDataset()
{
  LOCK_ON_FILE
  
  return m_strOpenDataSet;
}

//---------------------------------------------------------------------------
// NexusFileImpl::CurrentGroup
//---------------------------------------------------------------------------
std::string NexusFileImpl::CurrentGroup()
{
  LOCK_ON_FILE
  
  return m_vecOpenGroup.back();
}

//---------------------------------------------------------------------------
// NexusFileImpl::CurrentGroupPath
//---------------------------------------------------------------------------
std::string NexusFileImpl::CurrentGroupPath(std::vector<std::string> *pvecGroups)
{
  LOCK_ON_FILE
  
  std::vector<std::string> *pvec = &m_vecOpenGroup;
  if( pvecGroups )
    pvec = pvecGroups;

  std::string strPath = "/";
  for( std::size_t ui = 1; ui < pvec->size(); ui++ )
  {
    strPath = (strPath + (*pvec)[ui]) + "/";
  }
  return strPath;
}

//---------------------------------------------------------------------------
// NexusFileImpl::CloseGroup
//---------------------------------------------------------------------------
void NexusFileImpl::CloseGroup()
{
  LOCK_ON_FILE
  
  CloseDataSet();

  if( m_vecOpenGroup.size() > 1 )
  {
    yat::log_verbose("nex", "Close group");

    if( NXclosegroup(m_hFile) != NX_OK )
      throw NexusException(PSZ_FMT(CLOSE_GROUP_ERR, PSZ(m_vecOpenGroup.back()),
                           PSZ(m_strFullPath)), "NexusFileImpl::CloseGroup");
    m_vecOpenGroup.pop_back();
  }
}

//---------------------------------------------------------------------------
// NexusFileImpl::CloseAllGroups
//---------------------------------------------------------------------------
void NexusFileImpl::CloseAllGroups()
{
  LOCK_ON_FILE
  
  while( m_vecOpenGroup.size() > 1 )
    CloseGroup();
}

//---------------------------------------------------------------------------
// NexusFileImpl::CloseDataSet
//---------------------------------------------------------------------------
bool NexusFileImpl::OpenDataSet(const char *pcszName, bool bThrowException)
{
  LOCK_ON_FILE
  
  if( m_strOpenDataSet != pcszName )
    CloseDataSet();

  yat::log_verbose("nex", "Open data set '%s'", pcszName);

  OUTPUT_DISABLE;

  int iRc = NXopendata(m_hFile, PCSZToPSZ(pcszName));

  OUTPUT_ENABLE;

  if( iRc != NX_OK )
  {
    if( bThrowException )
      throw NexusException(PSZ_FMT(OPEN_DATA_ERR, pcszName, PSZ(m_vecOpenGroup.back()), PSZ(m_strFullPath)),
                           "NexusFileImpl::OpenDataSet");
    else
      return false;
  }
  m_strOpenDataSet = pcszName;
  return true;
}

//---------------------------------------------------------------------------
// NexusFileImpl::CloseDataSet
//---------------------------------------------------------------------------
void NexusFileImpl::CloseDataSet()
{
  LOCK_ON_FILE
  
  if( m_strOpenDataSet != g_strNoDataSet )
  {
    yat::log_verbose("nex", "Close data set '%s'", PSZ(m_strOpenDataSet));

    if( NXclosedata(m_hFile) != NX_OK )
      throw NexusException(PSZ_FMT(CLOSE_DATA_ERR, PSZ(m_vecOpenGroup.back()), PSZ(m_strFullPath)),
                           "NexusFileImpl::CloseDataSet");

    m_strOpenDataSet = g_strNoDataSet;
  }
}

//---------------------------------------------------------------------------
// NexusFileImpl::CreateDataSet
//---------------------------------------------------------------------------
void NexusFileImpl::CreateCompressedDataSet(const char *pcszName, NexusDataType eDataType, 
                                            int iRank, int *piDim, int *piChunkDim, int bOpen)
{
  LOCK_ON_FILE
  
  if( m_strOpenDataSet != pcszName )
    CloseDataSet();

  int aiChunkSize[MAX_RANK];
  if( piChunkDim )
  {
    for( int i = 0; i < iRank; i++ )
      aiChunkSize[i] = piChunkDim[i];
  }
  else
  {
    for( int i = 0; i < iRank; i++ )
      aiChunkSize[i] = piDim[i];
  }

  yat::log_verbose("nex", "create compressed data set '%s'", pcszName);


  // Create dataset
  if( NXcompmakedata(m_hFile, PCSZToPSZ(pcszName), (int)eDataType, iRank, 
                     piDim, NX_COMP_LZW, aiChunkSize) != NX_OK )
  {
    throw NexusException(PSZ_FMT(CREATE_DATA_ERR, pcszName, PSZ(m_vecOpenGroup.back()), PSZ(m_strFullPath)),
                         "NexusFileImpl::CreateCompressedDataSet");
  }

  if( bOpen )
    OpenDataSet(pcszName);
}

//---------------------------------------------------------------------------
// NexusFileImpl::CreateDataSet
//---------------------------------------------------------------------------
void NexusFileImpl::CreateDataSet(const char *pcszName, NexusDataType eDataType, 
                                  int iRank, int *piDim, int bOpen)
{
  if( NX_NONE == eDataType )
  {
    throw NexusException(PSZ_FMT(CREATE_DATA_BAD_TYPE_ERR, pcszName, PSZ(m_vecOpenGroup.back()), PSZ(m_strFullPath)),
                         "NexusFileImpl::CreateDataSet");
  }

  LOCK_ON_FILE
  
  if( m_strOpenDataSet != pcszName )
    CloseDataSet();
  
  yat::log_verbose("nex", "Create data set '%s'", pcszName);

  OUTPUT_DISABLE;

  int iRc = 0;
  if( piDim )
    iRc = NXmakedata(m_hFile, PCSZToPSZ(pcszName), (int)eDataType, iRank, piDim);
  else
  {
    int iSize = NX_UNLIMITED;
    iRc = NXmakedata(m_hFile, PCSZToPSZ(pcszName), (int)eDataType, iRank, &iSize);
  }

  OUTPUT_ENABLE;

  // Create dataset
  if( iRc != NX_OK )
  {
    throw NexusException(PSZ_FMT(CREATE_DATA_ERR, pcszName, PSZ(m_vecOpenGroup.back()), PSZ(m_strFullPath)),
                         "NexusFileImpl::CreateDataSet");
  }

  if( bOpen )
    OpenDataSet(pcszName);
}

//---------------------------------------------------------------------------
// NexusFileImpl::PutData
//---------------------------------------------------------------------------
void NexusFileImpl::PutData(void *pData, const char *pcszName, int bFlush)
{
  LOCK_ON_FILE
  
  if( NULL != pcszName && m_strOpenDataSet != pcszName )
  {
    // The DataSet 'pcszName' isn't open
    CloseDataSet();
    // Try to openit
    OpenDataSet(pcszName);
  }

  if( NXputdata(m_hFile, pData) != NX_OK )
  {
    throw NexusException(PSZ_FMT(PUT_DATA_ERR, PSZ(m_strOpenDataSet), PSZ(m_vecOpenGroup.back()), PSZ(m_strFullPath)),
                         "NexusFileImpl::PutData");
  }

  if( bFlush )
    Flush();
}

//---------------------------------------------------------------------------
// NexusFileImpl::PutDataSubSet
//---------------------------------------------------------------------------
void NexusFileImpl::PutDataSubSet(void *pData, int *piStart, int *piSize,
                                const char *pcszName)
{
  LOCK_ON_FILE
  
  if( NULL != pcszName && m_strOpenDataSet != pcszName )
  {
    // The DataSet 'pcszName' isn't open
    CloseDataSet();
    // Try to openit
    OpenDataSet(pcszName);
  }

  if( NXputslab(m_hFile, pData, piStart, piSize) != NX_OK )
  {
    throw NexusException(PSZ_FMT(PUT_DATASUBSET_ERR, PSZ(m_strOpenDataSet), PSZ(m_vecOpenGroup.back()), PSZ(m_strFullPath)),
                         "NexusFileImpl::PutDataSubSet");
  }
}

//---------------------------------------------------------------------------
// NexusFileImpl::WriteData
//---------------------------------------------------------------------------
void NexusFileImpl::WriteData(const char *pcszName, void *pData, 
                              NexusDataType eDataType, int iRank, int *piDim,
                              bool bCreate)
{
  LOCK_ON_FILE
  
  if( m_strOpenDataSet != pcszName )
    CloseDataSet();
  
  if( bCreate )
  {
    // Create dataset
    CreateDataSet(pcszName, eDataType, iRank, piDim, true);
  }
  // Put data
  PutData(pData, pcszName, false);
}

//---------------------------------------------------------------------------
// NexusFileImpl::WriteDataSubSet
//---------------------------------------------------------------------------
void NexusFileImpl::WriteDataSubSet(const char *pcszName, void *pData, NexusDataType eDataType, 
                                    int iRank, int *piStart, int *piSize, bool bCreate, bool bNoDim)
{
  LOCK_ON_FILE
  
  if( m_strOpenDataSet != pcszName )
    CloseDataSet();
  
  if( bCreate )
  {
    // Create dataset
    if( !bNoDim )
      CreateDataSet(pcszName, eDataType, iRank, piSize, true);
    else
    {
      // Create unlimited size dataset
      int iSize = NX_UNLIMITED;
      CreateDataSet(pcszName, eDataType, iRank, &iSize, true);
    }
  }
  // Put data
  PutDataSubSet(pData, piStart, piSize, pcszName);
}

//---------------------------------------------------------------------------
// NexusFileImpl::PutAttr
//---------------------------------------------------------------------------
void NexusFileImpl::PutAttr(const char *pcszName, void *pValue, int iLen, int iDataType)
{
  LOCK_ON_FILE
  
  if( NXputattr(m_hFile, PCSZToPSZ(pcszName), pValue, iLen, iDataType) != NX_OK )
  {
    throw NexusException(PSZ_FMT(PUT_DATA_ERR, pcszName, PSZ(m_vecOpenGroup.back()), PSZ(m_strFullPath)),
                         "NexusFileImpl::PutAttr");
  }
}

//---------------------------------------------------------------------------
// NexusFileImpl::GetDataSetInfo
//---------------------------------------------------------------------------
void NexusFileImpl::GetDataSetInfo(NexusDataSetInfo *pDataSetInfo, 
                                   const char *pcszDataSet)
{
  LOCK_ON_FILE
  
  if( m_strOpenDataSet != pcszDataSet )
  {
    // The DataSet 'pcszName' isn't open
    CloseDataSet();
    // Try to open correct data set
    OpenDataSet(pcszDataSet);
    // Clear the NexusDataSetInfo object
    pDataSetInfo->Clear();
  }

  if( pDataSetInfo->IsEmpty() )
  {
    // Read Data set info
    int iRank, iDataType;
    if( NX_OK != NXgetinfo(m_hFile, &iRank, pDataSetInfo->DimArray(), &iDataType) )
    {
      throw NexusException(PSZ_FMT(GETINFO_ERR, pcszDataSet, PSZ(m_vecOpenGroup.back()), PSZ(m_strFullPath)),
                           "NexusFileImpl::GetDataSetInfo");
    }

    pDataSetInfo->SetInfo((NexusDataType)iDataType, iRank);
    pDataSetInfo->SetTotalDim(iRank, pDataSetInfo->DimArray());
  }
}

//---------------------------------------------------------------------------
// NexusFileImpl::GetData
//---------------------------------------------------------------------------
void NexusFileImpl::GetData(NexusDataSet *pDataSet, const char *pcszDataSet)
{
  LOCK_ON_FILE
  
  if( NULL != pcszDataSet )
    // First, get info about the data set
    GetDataSetInfo(pDataSet, pcszDataSet);

  // Allocate dataset
  pDataSet->Alloc();

  // Read data
  if( NX_OK != NXgetdata(m_hFile, pDataSet->Data()) )
  {
    throw NexusException(PSZ_FMT(GETDATA_ERR, pcszDataSet, PSZ(m_vecOpenGroup.back()), PSZ(m_strFullPath)),
                         "NexusFileImpl::GetData");
  }
}

//---------------------------------------------------------------------------
// NexusFileImpl::GetDataSubSet
//---------------------------------------------------------------------------
void NexusFileImpl::GetDataSubSet(NexusDataSet *pDataSet, const char *pcszDataSet)
{
  LOCK_ON_FILE
  
  // Allocate dataset
  pDataSet->Alloc();

  // Read data
  if( NX_OK != NXgetslab(m_hFile, pDataSet->Data(), pDataSet->StartArray(),
                         pDataSet->DimArray()) )
  {
    throw NexusException(PSZ_FMT(GETDATA_ERR, pcszDataSet, PSZ(m_vecOpenGroup.back()), PSZ(m_strFullPath)),
                         "NexusFileImpl::GetDataSubSet");
  }
}

//---------------------------------------------------------------------------
// NexusFileImpl::GetAttribute
//---------------------------------------------------------------------------
void NexusFileImpl::GetAttribute(const char *pcszAttr, int *piBufLen, 
                                 void *pData, int iDataType)
{
  LOCK_ON_FILE
  
  int _iDataType = iDataType;

  // Read attribute
  if( NXgetattr(m_hFile, PCSZToPSZ(pcszAttr), pData, piBufLen, &_iDataType) != NX_OK )
  {
    throw NexusException(PSZ_FMT(GET_ATTR_ERR, pcszAttr, PSZ(m_vecOpenGroup.back()), PSZ(m_strFullPath)),
                         "NexusFileImpl::GetAttribute");
  }

  // Check type, except if NX_NONE is the passed type
  if( NX_NONE != iDataType && _iDataType != iDataType )
  {
    throw NexusException(PSZ_FMT(GET_ATTR_ERR, pcszAttr, PSZ(m_vecOpenGroup.back()), PSZ(m_strFullPath)),
                         "NexusFileImpl::GetAttribute");

  }
}

//---------------------------------------------------------------------------
// NexusFileImpl::GetAttribute
//---------------------------------------------------------------------------
void NexusFileImpl::GetAttribute(const char *pcszAttr, int *piBufLen, 
                                 void *pData, int *piDataType)
{
  LOCK_ON_FILE
  
  // Read attribute
  if( NXgetattr(m_hFile, PCSZToPSZ(pcszAttr), pData, piBufLen, piDataType) != NX_OK )
  {
    throw NexusException(PSZ_FMT(GET_ATTR_ERR, pcszAttr, PSZ(m_vecOpenGroup.back()), PSZ(m_strFullPath)),
                         "NexusFileImpl::GetAttribute");
  }
}

//---------------------------------------------------------------------------
// NexusFileImpl::ItemCount
//---------------------------------------------------------------------------
int NexusFileImpl::ItemCount()
{
  LOCK_ON_FILE
  
  int iCount;
  NXname name, class_name;
  if( NXgetgroupinfo(m_hFile, &iCount, name, class_name) != NX_OK )
    throw NexusException(PSZ_FMT(GET_GROUPINFO_ERR, PSZ(m_vecOpenGroup.back()), PSZ(m_strFullPath)),
                         "NexusFileImpl::ItemCount");
  return iCount;
}  

//---------------------------------------------------------------------------
// NexusFile::AttrCount
//---------------------------------------------------------------------------
int NexusFileImpl::AttrCount()
{
  LOCK_ON_FILE
  
  int iCount;
  if( NXgetattrinfo(m_hFile, &iCount) != NX_OK )
    throw NexusException(PSZ_FMT(GET_ATTRINFO_ERR,  PSZ(m_vecOpenGroup.back()), PSZ(m_strFullPath)),
                         "NexusFileImpl::AttrCount");
  return iCount;
}

//---------------------------------------------------------------------------
// NexusFileImpl::GetFirstItem
//---------------------------------------------------------------------------
int NexusFileImpl::GetFirstItem(NexusItemInfo *pItemInfo)
{
  LOCK_ON_FILE
  
  int iStatus = NX_OK;

  // Initiate enumeration
  iStatus = NXinitgroupdir(m_hFile);
  if( NX_ERROR == iStatus )
  {
    throw NexusException(PSZ_FMT(GET_INIT_ENUM_ITEM, PSZ(m_strFullPath)),  
                         "NexusFileImpl::GetFirstItem");
  }

  // Get first item
  return GetNextItem(pItemInfo);
}
  
//---------------------------------------------------------------------------
// NexusFileImpl::GetNextItem
//---------------------------------------------------------------------------
int NexusFileImpl::GetNextItem(NexusItemInfo *pItemInfo)
{
  LOCK_ON_FILE
  
  int iStatus = NXgetnextentry(m_hFile, pItemInfo->m_pszItem, pItemInfo->m_pszClass, 
                              (int *)&(pItemInfo->m_eDataType));
  if( NX_ERROR == iStatus )
  {
    throw NexusException(PSZ_FMT(GET_RETREIVE_ITEM, PSZ(m_strFullPath)),
                         "NexusFileImpl::GetNextItem");
  }

  return iStatus;
}

//---------------------------------------------------------------------------
// NexusFileImpl::GetFirstAttribute
//---------------------------------------------------------------------------
int NexusFileImpl::GetFirstAttribute(NexusAttrInfo *pAttrInfo, const char *pcszDataSet)
{
  LOCK_ON_FILE
  
  int iStatus = NX_OK;

  if( pcszDataSet )
    OpenDataSet(pcszDataSet);

  // Initiate enumeration
  iStatus = NXinitattrdir(m_hFile);
  if( NX_ERROR == iStatus )
  {
    throw NexusException(PSZ_FMT(GET_INIT_ENUM_ATTR, PSZ(m_strFullPath)),
                         "NexusFileImpl::GetFirstAttribute");
  }

  // Get first attribute
  return GetNextAttribute(pAttrInfo);
}

//---------------------------------------------------------------------------
// NexusFileImpl::GetNextAttribute
//---------------------------------------------------------------------------
int NexusFileImpl::GetNextAttribute(NexusAttrInfo *pAttrInfo)
{
  LOCK_ON_FILE
  
  int iStatus = NXgetnextattr(m_hFile, pAttrInfo->m_pszName, &pAttrInfo->m_iLen,
                              (int *)&(pAttrInfo->m_eDataType));
  if( NX_ERROR == iStatus )
  {
    throw NexusException(PSZ_FMT(GET_RETREIVE_ATTR, PSZ(m_strFullPath)),
                         "NexusFileImpl::GetNextAttribute");
  }

  return iStatus;
}

//---------------------------------------------------------------------------
// NexusFileImpl::GetDataSetHandle
//---------------------------------------------------------------------------
void NexusFileImpl::GetDataSetLink(NexusItemID *pnxl)
{
  LOCK_ON_FILE
  
  NXgetdataID(m_hFile, (NXlink *)(pnxl->m_pLink));
}

//---------------------------------------------------------------------------
// NexusFileImpl::GetGroupHandle
//---------------------------------------------------------------------------
void NexusFileImpl::GetGroupLink(NexusItemID *pnxl)
{
  LOCK_ON_FILE
  
  NXgetgroupID(m_hFile, (NXlink *)(pnxl->m_pLink));
}

//---------------------------------------------------------------------------
// NexusFileImpl::LinkToCurrentGroup
//---------------------------------------------------------------------------
void NexusFileImpl::LinkToCurrentGroup(const NexusItemID &nxl)
{
  LOCK_ON_FILE
  
  if( NX_OK != NXmakelink(m_hFile, (NXlink *)(nxl.m_pLink)) )
  {
    throw NexusException(PSZ_FMT(LINK_ITEM_ERR, PSZ(m_strFullPath)),
                         "NexusFileImpl::LinkToCurrentGroup");
  }
}

//-----------------------------------------------------------------------------
// NexusFileImpl::CreateOpenGroupPath
//-----------------------------------------------------------------------------
bool NexusFileImpl::CreateOpenGroupPath(const char *pszPath, bool bCreate, bool bThrowException)
{
  LOCK_ON_FILE
  
  yat::String strPath = pszPath;

  if( strPath.is_equal(CurrentGroupPath()) )
  {
    // already the currently opend group
    //PrintfDebug("NexusFileImpl::CreateOpenGroupPath %s already opened (%s)", PSZ(strPath), PSZ(CurrentGroupPath()));
    return true;
  }

  if( strPath.start_with('/') )
  {
    // Sarting from root level => ensure that no group is already opened
    CloseAllGroups();
    strPath.erase(0, 1);
  }

  if( NULL == pszPath )
    // No path => nothing to do
    return false;

  while( strPath.size() > 0 )
  {
    yat::String strGroup;
    yat::String strClass;

    // we extract the name and the class's name of the Item.
    strPath.extract_token('/', &strGroup);
    strGroup.extract_token_right('<', '>', &strClass);
    if( strClass.empty() )
      // 2nd chance !
      strGroup.extract_token_right('(', ')', &strClass);
    if( strClass.empty() )
      // last chance !
      strGroup.extract_token_right('{', '}', &strClass);

    strClass.trim();
    strGroup.trim();

    if( strGroup.is_equal("..") )
    {
      // Up one level
      CloseGroup();
    }

    else if( strGroup.is_equal(".") )
    {
      // do nothing
    }

    // Provide groupe name and class
    else if( !strClass.empty() && !strGroup.empty() )
    {
      // Open the group
      bool bOpened = OpenGroup( PSZ(strGroup), PSZ(strClass), bThrowException);
      if( !bOpened )
      {
        if( !bCreate )
          return false;
        else
          // Create
          CreateGroup(PSZ(strGroup), PSZ(strClass));
      }
    }

    // Provide only class name
    else if( strGroup.empty() && !strClass.empty() )
    {     
      // Find first group of class {strClass}
      NexusItemInfo ItemInfo;
      int iRc = GetFirstItem(&ItemInfo);
      // We start our research
      while( iRc != NX_EOD )
      {
        if( ItemInfo.IsGroup() && yat::String(ItemInfo.ClassName()).is_equal(strClass) )
        {
          strGroup = ItemInfo.ItemName();
          break;
        }
        iRc = GetNextItem(&ItemInfo);
      }
      if( !strGroup.empty() )
        // we open the group found.
        OpenGroup(PSZ(strGroup), PSZ(strClass), bThrowException);
      else
        return false;
    }

    // Provide only group name
    else if( strClass.empty() && !strGroup.empty() )
    {
      // Find first group of class {strClass}
      NexusItemInfo ItemInfo;
      int iRc = GetFirstItem(&ItemInfo);
      // we start our research.
      while( iRc != NX_EOD )
      {
        if( ItemInfo.IsGroup() && yat::String(ItemInfo.ItemName()).is_equal(strGroup) )
        {
          strClass = ItemInfo.ClassName();
          break;
        }
        iRc = GetNextItem(&ItemInfo);
      }
      if( !strClass.empty() )
        // we open the group found.
        OpenGroup(PSZ(strGroup), PSZ(strClass), bThrowException);
      else
        return false;
    }
  }
  
  return true;
}

//-----------------------------------------------------------------------------
// NexusFileImpl::GetGroupChildren
//-----------------------------------------------------------------------------
void NexusFileImpl::GetGroupChildren(std::vector<std::string> *pvecDatasets, std::vector<std::string> *pvecGroupNames, std::vector<std::string> *pvecGroupClasses)
{
  LOCK_ON_FILE
  
  NexusItemInfo ItemInfo;
  
  int iRc = GetFirstItem(&ItemInfo);
  // we start to scan each descending nodes
  while( NX_OK == iRc )
  {
    if( ItemInfo.IsDataSet() )
      pvecDatasets->push_back(ItemInfo.ItemName());

    if( ItemInfo.IsGroup() )
    {
      pvecGroupNames->push_back(ItemInfo.ItemName());
      pvecGroupClasses->push_back(ItemInfo.ClassName());
    }
    iRc = GetNextItem(&ItemInfo);
  }
}

//-----------------------------------------------------------------------------
// NexusFileImpl::GetGroupChildren
//-----------------------------------------------------------------------------
NexusItemInfoList NexusFileImpl::GetGroupChildren()
{
  LOCK_ON_FILE
  
  NexusItemInfoList listItemInfo;
  NexusItemInfoPtr ptrItemInfo(new NexusItemInfo);
  
  int iRc = GetFirstItem(ptrItemInfo.get());
  // we start to scan each descending nodes
  while( NX_OK == iRc )
  {
    listItemInfo.push_back(ptrItemInfo);
    ptrItemInfo = new NexusItemInfo;
    iRc = GetNextItem(ptrItemInfo.get());
  }
  return listItemInfo;
}

//=============================================================================
// NeXus item info
//
//=============================================================================
NexusItemInfo::NexusItemInfo()
{
  m_pszItem = new char[MAX_NAME_LENGTH];
  m_pszClass = new char[MAX_NAME_LENGTH];
  m_eDataType = NX_NONE;
}

NexusItemInfo::~NexusItemInfo()
{
  delete [] m_pszItem;
  delete [] m_pszClass;
}

//=============================================================================
// NeXus attribute info
//
//=============================================================================
NexusAttrInfo::NexusAttrInfo()
{
  m_pszName = new char[MAX_NAME_LENGTH];
}

NexusAttrInfo::~NexusAttrInfo()
{
  delete [] m_pszName;
}

//=============================================================================
//
// NeXus File Class - wrapper methods
//
//=============================================================================

//---------------------------------------------------------------------------
// Constructor
//---------------------------------------------------------------------------
NexusFile::NexusFile(const char *pcszFullPath, OpenMode eMode, bool)
{
  m_pImpl = new NexusFileImpl(pcszFullPath);
  m_pUserPtr = NULL;

  switch( eMode )
  {
  case READ:
    m_pImpl->OpenRead(pcszFullPath);
    break;
  case WRITE:
    m_pImpl->OpenReadWrite(pcszFullPath);
    break;
  default:
    // NONE
    break;
  }
}

//---------------------------------------------------------------------------
// Destructor
//---------------------------------------------------------------------------
NexusFile::~NexusFile()
{
  Close();
  delete m_pImpl;
}

//---------------------------------------------------------------------------
// NexusFile::Close
//---------------------------------------------------------------------------
void NexusFile::Close()
{
  m_pImpl->Close();
}

//---------------------------------------------------------------------------
// NexusFile::Flush
//---------------------------------------------------------------------------
void NexusFile::Flush()
{
  m_pImpl->Flush();
}

//---------------------------------------------------------------------------
// NexusFile::Create
//---------------------------------------------------------------------------
void NexusFile::Create(const char *pcszFullPath, ENexusCreateMode eMode)
{
  m_pImpl->Create(pcszFullPath, eMode);
}

//---------------------------------------------------------------------------
// NexusFile::OpenRead
//---------------------------------------------------------------------------
void NexusFile::OpenRead(const char *pcszFullPath)
{
  m_pImpl->OpenRead(pcszFullPath);
}

//---------------------------------------------------------------------------
// NexusFile::OpenReadWrite
//---------------------------------------------------------------------------
void NexusFile::OpenReadWrite(const char *pcszFullPath)
{
  m_pImpl->OpenReadWrite(pcszFullPath);
}

//---------------------------------------------------------------------------
// NexusFile::CreateGroup
//---------------------------------------------------------------------------
void NexusFile::CreateGroup(const char *pcszName, const char *pcszClass, bool bOpen)
{
  m_pImpl->CreateGroup(pcszName, pcszClass, bOpen);
}

//---------------------------------------------------------------------------
// NexusFile::OpenGroup
//---------------------------------------------------------------------------
bool NexusFile::OpenGroup(const char *pcszName, const char *pcszClass, bool bThrowException)
{
  return m_pImpl->OpenGroup(pcszName, pcszClass, bThrowException);
}

//---------------------------------------------------------------------------
// NexusFile::CloseGroup
//---------------------------------------------------------------------------
void NexusFile::CloseGroup()
{
  m_pImpl->CloseGroup();
}

//---------------------------------------------------------------------------
// NexusFile::CloseAllGroups
//---------------------------------------------------------------------------
void NexusFile::CloseAllGroups()
{
  m_pImpl->CloseAllGroups();
}

//---------------------------------------------------------------------------
// NexusFile::CloseDataSet
//---------------------------------------------------------------------------
bool NexusFile::OpenDataSet(const char *pcszName, bool bThrowException)
{
  return m_pImpl->OpenDataSet(pcszName, bThrowException);
}

//---------------------------------------------------------------------------
// NexusFile::CloseDataSet
//---------------------------------------------------------------------------
void NexusFile::CloseDataSet()
{
  m_pImpl->CloseDataSet();
}

//---------------------------------------------------------------------------
// NexusFile::CreateCompressedDataSet
//---------------------------------------------------------------------------
void NexusFile::CreateCompressedDataSet(const char *pcszName, NexusDataType eDataType, 
                                        int iRank, int *piDim, int *piChunkDim, int bOpen)
{
  m_pImpl->CreateCompressedDataSet(pcszName, eDataType, iRank, piDim, piChunkDim, bOpen);
}

//---------------------------------------------------------------------------
// NexusFile::CreateDataSet
//---------------------------------------------------------------------------
void NexusFile::CreateDataSet(const char *pcszName, NexusDataType eDataType, 
                              int iRank, int *piDim, int bOpen)
{
  m_pImpl->CreateDataSet(pcszName, eDataType, iRank, piDim, bOpen);
}

//---------------------------------------------------------------------------
// NexusFile::PutData
//---------------------------------------------------------------------------
void NexusFile::PutData(void *pData, const char *pcszName, int bFlush)
{
  m_pImpl->PutData(pData, pcszName, bFlush);
}

//---------------------------------------------------------------------------
// NexusFile::PutDataSubSet
//---------------------------------------------------------------------------
void NexusFile::PutDataSubSet(void *pData, int *piStart, int *piSize,
                              const char *pcszName)
{
  m_pImpl->PutDataSubSet(pData, piStart, piSize, pcszName);
}

//---------------------------------------------------------------------------
// NexusFile::WriteData
//---------------------------------------------------------------------------
void NexusFile::WriteData(const char *pcszName, void *pData, 
                          NexusDataType eDataType, int iRank, int *piDim,
                          bool bCreate)
{
  m_pImpl->WriteData(pcszName, pData, eDataType, iRank, piDim, bCreate);
}

//---------------------------------------------------------------------------
// NexusFile::WriteDataSubSet
//---------------------------------------------------------------------------
void NexusFile::WriteDataSubSet(const char *pcszName, void *pData, NexusDataType eDataType, 
                       int iRank, int *piStart, int *piSize, bool bCreate, bool bNoDim)
{
  m_pImpl->WriteDataSubSet(pcszName, pData, eDataType, iRank, piStart, piSize, bCreate, bNoDim);
}

//---------------------------------------------------------------------------
// NexusFile::WriteData | float version
//---------------------------------------------------------------------------
void NexusFile::WriteData(const char *pcszName, float fValue, bool bCreate)
{
  int iLen = 1;
  m_pImpl->WriteData(pcszName, &fValue, NX_FLOAT32, 1, &iLen, bCreate);
}

//---------------------------------------------------------------------------
// NexusFile::WriteData | double version
//---------------------------------------------------------------------------
void NexusFile::WriteData(const char *pcszName, double dValue, bool bCreate)
{
  int iLen = 1;
  m_pImpl->WriteData(pcszName, &dValue, NX_FLOAT64, 1, &iLen, bCreate);
}

//---------------------------------------------------------------------------
// NexusFile::WriteData | long version
//---------------------------------------------------------------------------
void NexusFile::WriteData(const char *pcszName, long lValue, bool bCreate)
{
  int iLen = 1;
  m_pImpl->WriteData(pcszName, &lValue, NX_INT32, 1, &iLen, bCreate);
}

//---------------------------------------------------------------------------
// NexusFile::WriteData | std::string version
//---------------------------------------------------------------------------
void NexusFile::WriteData(const char *pcszName, const char *pcszValue, bool bCreate)
{
  int iLen = strlen(pcszValue);
  m_pImpl->WriteData(pcszName, PCSZToPSZ(pcszValue), NX_CHAR, 1, &iLen, bCreate);
}

//---------------------------------------------------------------------------
// NexusFile::WriteData | binary version
//---------------------------------------------------------------------------
void NexusFile::WriteData(const char *pcszName, void *pData, int _iLen, bool bCreate)
{
  int iLen = _iLen;
  m_pImpl->WriteData(pcszName, pData, NX_BINARY, 1, &iLen, bCreate);
}

//---------------------------------------------------------------------------
// NexusFile::PutAttr
//---------------------------------------------------------------------------
void NexusFile::PutAttr(const char *pcszName, void *pValue, int iLen, 
                        NexusDataType eDataType)
{
  m_pImpl->PutAttr(pcszName, pValue, iLen, (int)eDataType);
}

//---------------------------------------------------------------------------
// NexusFile::PutAttr | 'long' version
//---------------------------------------------------------------------------
void NexusFile::PutAttr(const char *pcszName, long lValue)
{
  m_pImpl->PutAttr(pcszName, &lValue, 1, NX_INT32);
}

//---------------------------------------------------------------------------
// NexusFile::PutAttr | 'pcsz' version
//---------------------------------------------------------------------------
void NexusFile::PutAttr(const char *pcszName, const char *pcszValue)
{
  m_pImpl->PutAttr(pcszName, (void *)pcszValue, strlen(pcszValue), NX_CHAR);
}

//---------------------------------------------------------------------------
// NexusFile::PutAttr | 'double' version
//---------------------------------------------------------------------------
void NexusFile::PutAttr(const char *pcszName, double dValue)
{
  m_pImpl->PutAttr(pcszName, &dValue, 1, NX_FLOAT64);
}

//---------------------------------------------------------------------------
// NexusFile::PutAttr | 'float' version
//---------------------------------------------------------------------------
void NexusFile::PutAttr(const char *pcszName, float fValue)
{
  m_pImpl->PutAttr(pcszName, &fValue, 1, NX_FLOAT32);
}

//---------------------------------------------------------------------------
// NexusFile::GetData
//---------------------------------------------------------------------------
void NexusFile::GetData(NexusDataSet *pDataSet, const char *pcszDataSet)
{
  m_pImpl->GetData(pDataSet, pcszDataSet);
}

//---------------------------------------------------------------------------
// NexusFile::GetDataSubSet
//---------------------------------------------------------------------------
void NexusFile::GetDataSubSet(NexusDataSet *pDataSet, const char *pcszDataSet)
{
  m_pImpl->GetDataSubSet(pDataSet, pcszDataSet);
}

//---------------------------------------------------------------------------
// NexusFile::GetDataSetInfo
//---------------------------------------------------------------------------
void NexusFile::GetDataSetInfo(NexusDataSetInfo *pDataSetInfo, const char *pcszDataSet)
{
  m_pImpl->GetDataSetInfo(pDataSetInfo, pcszDataSet);
}

//---------------------------------------------------------------------------
// NexusFile::GetAttribute
//---------------------------------------------------------------------------
void NexusFile::GetAttribute(const char *pcszAttr, int *piBufLen, 
                             void *pData, NexusDataType eDataType)
{
  m_pImpl->GetAttribute(pcszAttr, piBufLen, pData, (int)eDataType);
}

//---------------------------------------------------------------------------
// NexusFile::GetAttribute 'long' version
//---------------------------------------------------------------------------
void NexusFile::GetAttribute(const char *pcszAttr, long *plValue)
{
  int iLen = 1;
  m_pImpl->GetAttribute(pcszAttr, &iLen, plValue, NX_INT32);
}

//---------------------------------------------------------------------------
// NexusFile::GetAttribute 'double' version
//---------------------------------------------------------------------------
void NexusFile::GetAttribute(const char *pcszAttr, double *pdValue)
{
  int iLen = 1;
  m_pImpl->GetAttribute(pcszAttr, &iLen, pdValue, NX_FLOAT64);
}

//---------------------------------------------------------------------------
// NexusFile::GetAttribute 'float' version
//---------------------------------------------------------------------------
void NexusFile::GetAttribute(const char *pcszAttr, float *pfValue)
{
  int iLen = 1;
  m_pImpl->GetAttribute(pcszAttr, &iLen, pfValue, NX_FLOAT32);
}

//---------------------------------------------------------------------------
// NexusFile::GetAttribute 'std::string' version
//---------------------------------------------------------------------------
void NexusFile::GetAttribute(const char *pcszAttr, std::string *pstrValue)
{
  char szBuf[1024];
  int iLen = 1024;
  m_pImpl->GetAttribute(pcszAttr, &iLen, szBuf, NX_CHAR);
  (*pstrValue) = szBuf;
}
void NexusFile::GetAttribute(const char *pcszAttr, char *pszValue, int iBufLen)
{
  int iLen = iBufLen;
  m_pImpl->GetAttribute(pcszAttr, &iLen, pszValue, NX_CHAR);
}

//---------------------------------------------------------------------------
// NexusFile::GetFirstItem
//---------------------------------------------------------------------------
int NexusFile::GetFirstItem(NexusItemInfo *pItemInfo)
{
  return m_pImpl->GetFirstItem(pItemInfo);
}
  
//---------------------------------------------------------------------------
// NexusFile::GetNextItem
//---------------------------------------------------------------------------
int NexusFile::GetNextItem(NexusItemInfo *pItemInfo)
{
  return m_pImpl->GetNextItem(pItemInfo);
}

//---------------------------------------------------------------------------
// NexusFile::GetFirstAttribute
//---------------------------------------------------------------------------
int NexusFile::GetFirstAttribute(NexusAttrInfo *pAttrInfo, const char *pcszDataSet)
{
  return m_pImpl->GetFirstAttribute(pAttrInfo, pcszDataSet);
}

//---------------------------------------------------------------------------
// NexusFile::GetNextAttribute
//---------------------------------------------------------------------------
int NexusFile::GetNextAttribute(NexusAttrInfo *pAttrInfo)
{
  return m_pImpl->GetNextAttribute(pAttrInfo);
}

//---------------------------------------------------------------------------
// NexusFile::GetDataSetLink
//---------------------------------------------------------------------------
void NexusFile::GetDataSetLink(NexusItemID *pnxl)
{
  m_pImpl->GetDataSetLink(pnxl);
}

//---------------------------------------------------------------------------
// NexusFile::GetGroupHandle
//---------------------------------------------------------------------------
void NexusFile::GetGroupLink(NexusItemID *pnxl)
{
  m_pImpl->GetGroupLink(pnxl);
}

//---------------------------------------------------------------------------
// NexusFile::LinkToCurrentGroup
//---------------------------------------------------------------------------
void NexusFile::LinkToCurrentGroup(const NexusItemID &nxl)
{
  m_pImpl->LinkToCurrentGroup(nxl);
}

//---------------------------------------------------------------------------
// NexusFile::ItemCount
//---------------------------------------------------------------------------
int NexusFile::ItemCount()
{
  return m_pImpl->ItemCount();
}

//---------------------------------------------------------------------------
// NexusFile::AttrCount
//---------------------------------------------------------------------------
int NexusFile::AttrCount()
{
  return m_pImpl->AttrCount();
}

//-----------------------------------------------------------------------------
// NexusFile::OpenGroupPath
//-----------------------------------------------------------------------------
bool NexusFile::OpenGroupPath(const char *pszPath, bool bThrowException)
{
  yat::log_verbose("nex", "Open group from path '%s'", pszPath);
  return m_pImpl->CreateOpenGroupPath(pszPath, false, bThrowException);
}

//-----------------------------------------------------------------------------
// NexusFile::CreateGroupPath
//-----------------------------------------------------------------------------
bool NexusFile::CreateGroupPath(const char *pszPath)
{
  yat::log_verbose("nex", "Create group from path '%s'", pszPath);
  return m_pImpl->CreateOpenGroupPath(pszPath, true, false);
}

//-----------------------------------------------------------------------------
// NexusFile::CurrentGroupName
//-----------------------------------------------------------------------------
std::string NexusFile::CurrentGroupName()
{
  std::string strGroup = m_pImpl->CurrentGroup();
  return strGroup.substr(0, strGroup.find('('));
}

//-----------------------------------------------------------------------------
// NexusFile::CurrentGroupClass
//-----------------------------------------------------------------------------
std::string NexusFile::CurrentGroupClass()
{
  std::string strGroup = m_pImpl->CurrentGroup();  
  return strGroup.substr(strGroup.find('(') + 1, strGroup.find(')') - strGroup.find('(') - 1);
}

//-----------------------------------------------------------------------------
// NexusFile::CurrentGroupPath
//-----------------------------------------------------------------------------
std::string NexusFile::CurrentGroupPath()
{
  return m_pImpl->CurrentGroupPath();
}

//-----------------------------------------------------------------------------
// NexusFile::CurrentDataset
//-----------------------------------------------------------------------------
std::string NexusFile::CurrentDataset()
{
  return m_pImpl->CurrentDataset();
}

//-----------------------------------------------------------------------------
// NexusFile::GetGroupChildren
//-----------------------------------------------------------------------------
void NexusFile::GetGroupChildren(std::vector<std::string> *pvecDatasets, std::vector<std::string> *pvecGroupNames, std::vector<std::string> *pvecGroupClasses)
{
  m_pImpl->GetGroupChildren(pvecDatasets, pvecGroupNames, pvecGroupClasses);
}

//-----------------------------------------------------------------------------
// NexusFile::GetGroupChildren
//-----------------------------------------------------------------------------
NexusItemInfoList NexusFile::GetGroupChildren()
{
  return m_pImpl->GetGroupChildren();
}


