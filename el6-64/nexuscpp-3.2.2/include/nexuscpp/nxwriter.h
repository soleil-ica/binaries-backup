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

#ifndef __NX_WRITER_H__
#define __NX_WRITER_H__

// standard library objets
#include <iostream>
#include <vector>
#include <set>

// yat library
#include <yat/CommonHeader.h>
#include <yat/memory/SharedPtr.h>
#include <yat/memory/MemBuf.h>
#include <yat/utils/String.h>
#include <yat/time/Timer.h>
#include <yat/threading/Mutex.h>

#include <nexuscpp/nxfile.h>

namespace nxcpp
{

// Forward declaration
class NexusFileWriterImpl;

typedef yat::SharedPtr<NexusDataSet, yat::Mutex> NexusDataSetPtr;
typedef yat::SharedPtr<NexusAttr, yat::Mutex> NexusAttrPtr;

//=============================================================================
/// Mix-in that add metadata capabilitie to objects
//=============================================================================
class MMetadata
{
public:
  enum Type
  {
    NONE = 0,
    STRING,
    INT,
    LONGINT,
    DOUBLE
  };
  
private:
  std::map<std::string, std::string> m_mapString; // "std::string" type metadata
  std::map<std::string, int> m_mapInteger;   // "Integer" type metadata
  std::map<std::string, long> m_mapLong;     // "Long Integer" type metadata
  std::map<std::string, double> m_mapDouble; // "Float" type metadata

public:
  /// Add std::string metadata
  void AddMetadata(const std::string &strKey, const std::string &strValue);
  /// Add C-ansi std::string metadata
  void AddMetadata(const std::string &strKey, const char *pszValue);
  /// Add integer matedata
  void AddMetadata(const std::string &strKey, int iValue);
  /// Add integer matedata
  void AddMetadata(const std::string &strKey, long lValue);
  /// Add float metadata
  void AddMetadata(const std::string &strKey, double dValue);

  /// Check metadata
  bool HasMetadata(const std::string &strKey) const;

  /// Get metadata as std::string
  /// Implicitely convert integer and float metadata as std::string value
  /// @param  bThrow if true throw a exception in case of no data
  /// @return true if metadata was found, otherwise false (if bThrow == false)
  bool GetMetadata(const std::string &strKey, std::string *pstrValue, bool bThrow=true) const;

  /// Get std::string metadata
  /// @param  bThrow if true throw a exception in case of no data
  /// @return true if metadata was found, otherwise false (if bThrow == false)
  bool GetStringMetadata(const std::string &strKey, std::string *pstrValue, bool bThrow=true) const;
  std::string StringMetadata(const std::string &strKey) const;

  /// Get integer metadata
  /// @param  bThrow if true throw a exception in case of no data
  /// @return true if metadata was found, otherwise false (if bThrow == false)
  bool GetIntegerMetadata(const std::string &strKey, int *piValue, bool bThrow=true) const;
  int IntegerMetadata(const std::string &strKey) const;

  /// Get long integer metadata
  /// @param  bThrow if true throw a exception in case of no data
  /// @return true if metadata was found, otherwise false (if bThrow == false)
  bool GetLongIntegerMetadata(const std::string &strKey, long *plValue, bool bThrow=true) const;
  long LongIntegerMetadata(const std::string &strKey) const;

  /// Get float metadata
  /// @param  bThrow if true throw a exception in case of no data
  /// @return true if metadata was found, otherwise false (if bThrow == false)
  bool GetDoubleMetadata(const std::string &strKey, double *pdValue, bool bThrow=true) const;
  double DoubleMetadata(const std::string &strKey) const;

  /// Get type of a metadata from its key name
  /// @param  bThrow if true throw a exception in case of no data
  /// @return MMetadata::Type value
  Type GetMetadataType(const std::string &strKey, bool bThrow=true) const;
  
  /// Returns the metadata keys std::list
  std::list<std::string> MetadataKeys() const;
  
  /// Clear all metadata
  void ClearMetadata();
};

//=============================================================================
/// Exception handling interface
//=============================================================================
class IExceptionHandler
{
public:
  virtual void OnNexusException(const NexusException &e)=0;
};

//=============================================================================
/// Synchronous/Asynchronous NeXus Writer File Class
///
/// This class allow asynchronous writing into a nexus file
/// @note This is wrapper class, the real job is make by a internal objet
//=============================================================================
class NEXUSCPP_DECL NexusFileWriter
{
public:
   enum WriteMode
  {
    SYNCHRONOUS = 0, /// Synchronous mode
    ASYNCHRONOUS,    /// Asynchronous mode

    ///@{ Depracated modes
    IMMEDIATE,       /// Asynchronous mode: write data as soon as possible
    DELAYED          /// Asynchronous mode: data are cached and written at specified time interval
    ///@}
  };

  typedef struct Statistics
  {
    Statistics();
    yat::uint64 ui64WrittenBytes;
    yat::uint64 ui64TotalBytes;
    float       fInstantMbPerSec; /// Instant rate in MBytes/s
    float       fAverageMbPerSec; /// Average rate in MBytes/s
  } Statistics;

  //=============================================================================
  /// Write notification interface
  //=============================================================================
  class INotify
  {
  public:
    virtual void OnWriteSubSet(NexusFileWriter* source_p, const std::string& dataset_path, int start_pos[MAX_RANK], int dim[MAX_RANK])=0;
    virtual void OnWrite(NexusFileWriter* source_p, const std::string& dataset_path)=0;
    virtual void OnCloseFile(NexusFileWriter* source_p, const std::string& nexus_file_path)=0;
  };

private:
  NexusFileWriterImpl *m_pImpl; // Implementation

  static std::size_t s_attempt_max_;        // max attempts for every nexus action
  static std::size_t s_attempt_delay_ms_;

public:
  /// Constructor
  ///
  /// @param strFilePath path + complete filename
  NexusFileWriter(const std::string &strFilePath, WriteMode eMode=ASYNCHRONOUS, unsigned int uiWritePeriod=2);

  /// Destructor
  ///
  ~NexusFileWriter();
  
  /// Set exception handler
  ///
  void SetExceptionHandler(IExceptionHandler *pHandler);

  /// Set the notification handler
  ///
  void SetNotificationHandler(INotify *pHandler);
  
  /// Set the notification handler
  ///
  void AddNotificationHandler(INotify* pHandler);

  /// Set cache size (in MB) for DELAYED mode
  ///
  void SetCacheSize(yat::uint16 usCacheSize);

  /// Create a dataset
  /// 
  /// @param strPath  Dataset path and name (ex: entry<NXentry>/sample<NXsample>/formula)
  /// @param aDataSet The dataset to write
  void CreateDataSet(const std::string &strPath, NexusDataType eDataType, int iRank, int *piDim);

  /// Push a dataset
  /// 
  /// @param strPath  Dataset path and name (ex: entry<NXentry>/sample<NXsample>/formula)
  /// @param aDataSet The dataset to write
  void PushDataSet(const std::string &strPath, NexusDataSetPtr ptrDataSet);

  /// Push a attribute
  /// Dataset must already exists, otherwise a error is repported and action is canceled
  /// 
  /// @param strPath Dataset path and name (ex: entry<NXentry>/sample<NXsample>/formula.attribute)
  /// @param aAttr   The attribute
  ///
  void PushDatasetAttr(const std::string &strPath, NexusAttrPtr ptrAttr);

  /// Abort: delete all remaining data if any, no more access to file
  /// When this method returns
  ///
  void Abort();
  
  /// True
  bool IsError();

  /// Returns true if there is no data to write
  bool IsEmpty();
  
  /// Returns true when all job is done
  bool Done();
  
  /// Wait until all job is done
  void Synchronize(bool bSendSyncMsg=true);

  /// Hold the data, force the NeXuisFileWriter to cache data and not writting it into the filename
  /// Until either Synchronize() or Hold(false) is calles
  /// Available only in asynchronous mode
  void Hold(bool b=true);
  
  /// Return true if it hold the data
  /// Make sense only in asynchronous mode
  bool IsHold() const;

  /// File name  
  const std::string &File() const;

  /// The file will not be closed after each write action
  void SetFileAutoClose(bool b);
  
  /// Close the nexus file
  void CloseFile();
  
  /// Engage the file lock mecanism
  void SetUseLock();

  //@{ Attempt values for retry mechanism

    /// Max attempts for write actions
    static std::size_t AttemptMax() { return s_attempt_max_; }

    /// delay (in ms) between to attempts
    static std::size_t AttemptDelay() { return s_attempt_delay_ms_; }
  
  //@}

  //@{ Statistics

    /// Reset all statistics
    void ResetStatistics();

    /// Gets a copy of the statistics
    Statistics GetStatistics() const;
  
  //@}
};

typedef yat::MemBuf DatasetBuf;
  
// Referenced pointer definition
typedef yat::SharedPtr<NexusFileWriter, yat::Mutex> NexusFileWriterPtr;

/// Data shape type
typedef std::vector<std::size_t> DataShape;

// Empty shape
extern const DataShape g_empty_shape;

//==============================================================================
/// DatasetWriter
///
/// class containing a NexusDataset with its attributes and its location
//==============================================================================
class NEXUSCPP_DECL DatasetWriter:public MMetadata
{
public:
  class IFlushNotification
  {
  public:
    virtual void OnFlushData(DatasetWriter* pWriter) = 0;
  };

private:
  std::string         m_strPath;       // NeXus group path
  std::string         m_strDataset;    // Dataset name
  DatasetBuf          m_mbData;
  DataShape           m_shapeMatrix;   // shape of the dataitem matrix
  yat::uint16         m_usMaxMB;        // Max cached mega-bytes before flush
  NexusFileWriterPtr  m_ptrNeXusFileWriter;
  NexusDataType       m_NexusDataType;
  std::size_t         m_nDataItemRank;
  yat::int32          m_aiDataItemDim[MAX_RANK];
  yat::uint32         m_ulDataItemSize;
  std::size_t         m_nTotalRank;
  int                 m_aiTotalDim[MAX_RANK];
  yat::uint64         m_ui64TotalSize;
  bool                m_bDatasetCreated;
  std::size_t         m_nDataItemCount;
  IFlushNotification* m_pFlushListener;
  bool                m_bUnlimited;         //#### true: Nexus dataset size is unlimited
  std::size_t         m_nWrittenCount;      // Count of written data items
  yat::Timeout        m_tmWriteTimeout;     // if expired, we should write the bufferized data

private:
  void CreateDataset();
  void Flush(bool call_on_flush);
  bool DirectFlush(const void *pData);
  void CheckWriter();
  void PreAllocBuffer();
  template <class TYPE>  void AdjustSizes();
  void init(yat::uint16 usMaxMB, yat::uint16 m_usWriteTimeout);

public:
  template <class TYPE> 
  void SetDataType();

  void Reset();
  
  void SetNeXusDataType(NexusDataType eDataType);
  
  void SetShapes(const DataShape &shapeDataItem, const DataShape &shapeMatrix);
  
  /// c-tor
  DatasetWriter(const DataShape &shapeDataItem, const DataShape &shapeMatrix=g_empty_shape, yat::uint16 usMaxMB=64, yat::uint16 m_usWriteTimeout=0);
  DatasetWriter(const DataShape &shapeDataItem, std::size_t one_dim_size, yat::uint16 usMaxMB=64, yat::uint16 m_usWriteTimeout=0);
  DatasetWriter(yat::uint16 usMaxMB=64, yat::uint16 m_usWriteTimeout=0);

  /// @deprecated
  DatasetWriter(const std::vector<int>& shapeDataItem, const std::vector<int>& shapeMatrix, yat::uint16 usMaxMB=64);
  DatasetWriter(const std::vector<int> &shapeDataItem, std::size_t one_dim_size, yat::uint16 usMaxMB=64);

  /// d-tor
  virtual ~DatasetWriter();

  /// Set Nexus file writer object
  /// Must be called before push any data, like after construction or a call to the Stop method
  ///
  /// @param ptrWriter referenced pointer  to the new writer object
  ///
  void SetNexusFileWriter(NexusFileWriterPtr ptrWriter);
  
  /// Set the flushing notification listener
  /// Must be called before push any data, like after construction or a call to the Stop method
  ///
  /// @param ptrWriter referenced pointer  to the new writer object
  ///
  void SetFlushListener(IFlushNotification* pListener) { m_pFlushListener = pListener; }
  
  /// Resizes matrix
  /// Must be called before push any data, like after construction or a call to the Stop method
  ///
  /// @param shapeMatrix new shape
  ///
  void SetMatrix(const DataShape &shapeMatrix=g_empty_shape);

  /// Change destination path
  void SetPath(const std::string &strPath, const std::string &strDataset);

  /// Change destination path
  void SetFullPath(const std::string &strFullPath);

  /// Set the dataset name
  void SetDatasetName(const std::string &strDatasetName);

  /// Sets the buffer size in Mega bytes
  void SetCacheSize(yat::uint16 usMaxMB) { m_usMaxMB = usMaxMB; }

  /// Adding integer-type attribute to the NeXus dataset
  void AddAttribute(const NexusAttrPtr &ptrAttr);

  /// Adding double-type attribute to the NeXus dataset
  void AddFloatAttribute(const std::string &strName, double dValue);

  /// Adding integer-type attribute to the NeXus dataset
  void AddIntegerAttribute(const std::string &strName, long lValue);

  /// Adding std::string-type attribute to the NeXus dataset
  void AddStringAttribute(const std::string &strName, const std::string &strValue);

  /// Push data
  void PushData(const void *pData, std::size_t nDataCount=1, bool bNoCopy=false);

  /// No more data to push, data is flushed to the NeXus file writer
  void Stop();

  /// Cancel => forget all
  void Abort();

  //@{ Accessors
  const DataShape &MatrixShape() const { return m_shapeMatrix; }
  DataShape DataItemShape() const;
  NexusDataType DataType() const { return m_NexusDataType; }
  std::string FullPath() const;
  const std::string &DatasetName() const { return m_strDataset; }
  std::size_t TotalRank() const { return m_nTotalRank; }
  std::size_t DataItemCount() const { return m_nDataItemCount; }
  yat::uint64 TotalSize() const { return m_ui64TotalSize; }
  yat::uint32 DataItemSize() const { return m_ulDataItemSize; }
  yat::uint32 MaxDataItemsCount() const { return (unsigned long)(m_ui64TotalSize / m_ulDataItemSize); }
  //@}
};

// Referenced pointer definition
typedef yat::SharedPtr<DatasetWriter, yat::Mutex> DatasetWriterPtr;

//==============================================================================
/// GenericDatasetWriter
///
/// class containing a NexusDataset with its attributes and its location
//==============================================================================
template <class TYPE> 
class GenericDatasetWriter: public DatasetWriter
{
public:
  /// c-tor
  GenericDatasetWriter(const DataShape &shapeDataItem, const DataShape &shapeMatrix=g_empty_shape, unsigned short usMaxMB=100);
  
  /// d-tor
  ~GenericDatasetWriter() {}

};

//==============================================================================
/// AxisDatasetWriter
///
/// class containing a NexusDataset with its attributes and its location
//==============================================================================
template <class TYPE> 
class AxisDatasetWriter: public DatasetWriter
{
public:
  /// c-tor
  AxisDatasetWriter(int iDim, int iSize, int iOrder=1);
  
  /// d-tor
  ~AxisDatasetWriter() {}
  
  void PushPosition(TYPE TValue);
};

//==============================================================================
/// SignalDatasetWriter
///
/// class containing a NexusDataset with its attributes and its location
//==============================================================================
template <class TYPE> 
class SignalDatasetWriter: public DatasetWriter
{
public:
  /// c-tor
  SignalDatasetWriter(const DataShape &shapeData, const DataShape &shapeMatrix=g_empty_shape, int iSignal=1);
  
  /// d-tor
  ~SignalDatasetWriter() {}
  
  void PushSignal(TYPE *pValue);
};

#include <nexuscpp/impl/nxwriter.hpp>

} // namespace nxcpp


#endif
