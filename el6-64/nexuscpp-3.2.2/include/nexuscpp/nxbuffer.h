//*****************************************************************************
/// Synchrotron SOLEIL
///
/// Nexus API for Tango servers
///
/// Creation : 2011/01/12
/// Authors  : Stephane Poirier, Clement Rodriguez
///
/// This program is free software; you can redistribute it and/or modify it under
/// the terms of the GNU General Public License as published by the Free Software
/// Foundation; version 2 of the License.
/// 
/// This program is distributed in the hope that it will be useful, but WITHOUT 
/// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
/// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
///
//*****************************************************************************

#ifndef __NX_BUFFER_H__
#define __NX_BUFFER_H__

// including standard files
#ifndef __STRING_INCLUDED__
  #include <string>
  #define __STRING_INCLUDED__
#endif

#ifndef __VECTOR_INCLUDED__
  #include <vector>
  #define __VECTOR_INCLUDED__
#endif

// YAT
#include <yat/utils/String.h>
#include <yat/time/Time.h>
#include <yat/threading/Task.h>
#include <yat/threading/Mutex.h>
#include <yat/utils/Callback.h>

// NexusCPP
#include <nexuscpp/nxwriter.h>

namespace nxcpp
{

typedef std::vector<std::size_t> shape_t;
/*
#define SHAPE_1D \
  shape_t
  
// shape datatype
class Shape
{
public:
  /// Generic c-tor
  Shape(const std::vector<std::size_t>& dims) : dims_(dims) {}

  /// Copy c-tor
  Shape(const Shape& other) : dims_(other.dims_) {}
    
  /// Default c-tor (e.g. 0-D shape)
  Shape() {}

  /// 1-D shape
  Shape(std::size_t dim);

  /// 2-D shape
  Shape(std::size_t dimX, std::size_t dimY);

  /// 3-D shape
  Shape(std::size_t dimX, std::size_t dimY, std::size_t dimZ);

  /// Add a dimension and its size
  Shape& append(std::size_t dim_size);

  /// append the shape dimension to 
  void append(const Shape& shape);
    
  /// Dimensions array accessor
  const std::vector<std::size_t>& dimensions() const { return dims_; }

  /// read only accessor
  std::size_Shape& operator[](std::size_t dim) const;
  
  /// read/Write accessor
  Shape& operator[](std::size_t dim);
  
  Shape& operator+(std::size_t dim_size);
  
private:
  std::vector<std::size_t>  dims_;
};
*/
//==============================================================================
/// DataItemCategory
///==============================================================================
enum DataItemCategory
{
  NX_GENERIC     = 1,  /// GenericDataSet
  NX_AXIS        = 2,  /// AxisDataSet
  NX_DATA_SIGNAL = 3,  /// DataSet (signal)
  NX_DATA        = 4   /// DataSet (for any use cases but signal)
};

#define NX_ATTR_NODE_NAME  "buf_name"
#define NX_ATTR_DATA_SIZE  "buf_size"
#define NX_ATTR_SLAB_START "buf_start"
#define NX_ATTR_SLAB_END   "buf_end"
#define NX_ATTR_SLAB_STOP  "buf_stop"
#define NX_ATTR_ACQ_BATCH  "buf_batch"

/// type of buffered data
/// '0' for one shoot data (scalar, spectrum or image), '1' for data subset in a 1D acquisition, ...
#define NX_ATTR_DATA_DIM   "buf_dim"

//==============================================================================
/// Write notification 
//==============================================================================
struct WriteNotification
{
  /// data item name
  std::string item_name;
  
  /// The number of written items
  yat::uint32 write_count;
  
};

// Write notification callback
YAT_DEFINE_CALLBACK(WriteNotificationCallback, WriteNotification);

//==============================================================================
/// DataStreamer
///
/// This class aims to ease the writing process of local NeXus files.
///
///@notice BufferedData is no longer instantiable because this class name 
///        is now deprecated
//==============================================================================
class NEXUSCPP_DECL DataStreamer: public DatasetWriter::IFlushNotification, public NexusFileWriter::INotify, public IExceptionHandler
{
public:

  typedef enum NEXUSCPP_DECL MemoryMode
  {
    COPY = 0,  // Default mode for all data items
    NO_COPY    // Applicable only for 2D data items (images)
  } MemoryMode;

  typedef struct NEXUSCPP_DECL Statistics
  {
    Statistics();
    yat::uint64 ui64WrittenBytes;
    yat::uint64 ui64PendingBytes;
    yat::uint64 ui64MaxPendingBytes;
    yat::uint64 ui64TotalBytes;
    yat::uint16 ui16ActiveWriters;
    yat::uint16 ui16MaxSimultaneousWriters;
    float       fInstantMbPerSec;
    float       fPeakMbPerSec;
    float       fAverageMbPerSec;
  } Statistics;

  /// Constructors
  /// @param sBufferName buffer file names suffix
  /// @param iAcquisitionSize total number of data (may be either scalars, spectrums or images)
  /// @param iBufferSize number of data to be stored into one buffer file
  /// @note iAcquisitionSize must be greater or equal to iBufferSize
  DataStreamer(const std::string &sBufferName, std::size_t nAcquisitionSize, std::size_t nBufferSize);
  DataStreamer(const std::string &sBufferName, std::size_t nBufferSize);

  /// @deprecated
  DataStreamer(const std::string &sBufferName, int iAcquisitionSize, int iBufferSize);
  DataStreamer(const std::string &sBufferName, int iBufferSize);

  /// Destructors
  virtual ~DataStreamer();
  
  /// Initialization
  /// @param sTargetPath Path of the output directory
  /// @param sDataPrefix this string will be used to insert a prefix on the file name in order to
  ///        distinguish data produced by several datastreamer of same type
  void Initialize(const std::string &sTargetPath, const std::string &sDataPrefix="");

  /// Set the write notification callback
  void SetWriteNotificationCallback(WriteNotificationCallback& cb) { m_write_notif_cb = cb; }
  
  /// Tell the streamer we start a new acquisition sequence, therefore the first file index of every stream
  /// will be reset to '1'
  /// This method whould not be called for all subsequent DataStreamer objects
  /// 
  static void ResetBufferIndex();
  
  /// Set the write notification callback
  /// Each tile a 
  ///
  void SetWriteNotificationCallback(const WriteNotificationCallback& cb);
  
  //@{  Data items declarations
  
    /// Generic data item declaration
    ///
    void AddDataItem(const std::string &sItemName, const std::vector<int>& viDimSize,
                     bool bDataSignal=true) ;
    
    /// scalar data item
    ///
    void AddDataItem0D(const std::string &sItemName, bool bDataSignal=true) ;
    
    /// 1-d (spectrum) dataitem
    ///
    void AddDataItem1D(const std::string &sItemName, int iSizeDim, bool bDataSignal=true)
                      ;
    
    /// 2-d (image) dataitem
    ///
    void AddDataItem2D(const std::string &sItemName, int iSizeDim1, int iSizeDim2, 
                       bool bDataSignal=true) ;
    
    /// axis data item (equals to 1-d data item but with additionnal attributes)
    ///
    void AddDataAxis(const std::string &sItemName, int iDimension, int iOrder) ;
    
    /// Set dataitem memory management mode
    void SetDataItemMemoryMode(const std::string &sItemName, MemoryMode mode);
  
  //@}
    
  //@{  Writing datas and attributes
    /// PushData
    ///
    /// @param sItemName sensor's name to which data will be added
    /// @param tData Added data
    /// @param vtDetectPos sensor's data position in the acquisition multidimensional space
    /// @param nCount canonical data count
    ///
    template <class TYPE> void PushData(const std::string &sItemName, const TYPE *tData,
                                        unsigned int nCount=1);
    
    /// PushAxisData
    ///
    /// @param sName Axis name on which datas will be added
    /// @param viVecPos Axis value's position in the acquisition multidimensional space
    /// @param pValue Measured position on the axis
    ///
    template <class TYPE> void PushAxisData(const std::string &sName, TYPE TValue);
    
    /// PushIntegerAttribute
    /// Set an integer-type attribute belonging to the date item named sItemName.
    /// 
    /// @param sItemName name of the dataitem or axis which the attribute will belong to
    /// @param sName name of the attribute
    /// @param lValue value of the attribute
    ///
    void PushIntegerAttribute(const std::string &sItemName, const std::string &sName, long lValue);

    /// PushFloatAttribute
    /// Set an integer-type attribute belonging to the date item named sItemName.
    /// 
    /// @param sItemName name of the dataitem or axis which the attribute will belong to
    /// @param sName name of the attribute
    /// @param lValue value of the attribute
    ///
    void PushFloatAttribute(const std::string &sItemName, const std::string &sName, double dValue);

    /// PushStringAttribute
    /// Set an std::string-type attribute belonging to the date item named sItemName.
    /// 
    /// @param sItemName name of the dataitem or axis which the attribute will belong to
    /// @param sName name of the attribute
    /// @param strValue value of the attribute
    ///
    void PushStringAttribute(const std::string &sItemName, const std::string &sName,
                             const std::string &strValue) ;

  //@}

  /// Set exception handler
  ///
  void SetExceptionHandler(IExceptionHandler *pHandler);

  /// Sets the name of the tango device
  void SetDeviceName(const std::string &strDevice) { m_sDevice = strDevice; }
  
  ///  Reset buffer in order to start a new acquisition
  void Reset();
  
  /// Terminate the buffering process  
  void Finalize();
  
  /// Abort the bufferization process
  ///
  /// @param bSynchronize if true wait for last pushed data to be recorder
  ///
  void Abort(bool bSynchronize=false);
  
  /// Close the buffer and wait for synchronization
  void Stop();
  
  /// Ask if all data have been flushed
  bool IsDone();
  
  /// Wait the recording of all pushed data
  void Synchronize();

  //@{ Setters
    void SetWriteMode(const NexusFileWriter::WriteMode &mode) { m_wmWritingMode = mode; }
    void SetWorkingFolder(const std::string &sPath) { m_sWorkingDir = sPath; }
    void SetTargetFolder(const std::string &sPath) { m_sTargetDir = sPath; }
  //@}

  //@{ Getters
    NexusFileWriter::WriteMode WriteMode() const { return m_wmWritingMode; }
    const std::string &GetWorkingFolder() const { return m_sWorkingDir; }
    const std::string &GetTargetFolder() const { return m_sTargetDir; }
    int  GetNbPushInFile() const { return m_nBufferSize; }
  //@}

  /// Clean
  /// Clean all buffers from destination folder
  /// @warning Don't use this method if you are in a multiple acquisition system
  ///          (i.e: 1 acquisition for the whole system == n acquisition at device level)
  ///
  void Clean();

  /// IFlushNotification
  ///
  void OnFlushData(DatasetWriter* pWriter);
  
  /// Check if a data item has been declared
  ///
  bool IsExistingItem(const std::string &sItemName);

  //@{ Statistics

    /// Reset all statistics
    void ResetStatistics();

    /// Gets a copy of the statistics
    Statistics GetStatistics() const;

  //@}  

  //@{ Static methods

    /// GenerateBufferName
    /// Generate a standard buffer file's name according to the base 
    /// name and given indexes
    ///
    /// @param strPrefix prefix used to ensure uniqueness of file name
    /// @param sBaseName buffer file base name
    /// @param iIndex buffer file index
    ///
    static std::string GenerateBufferName(const std::string &sBaseName, long lIndex,
                                          const std::string &strPrefix="");

  //@}
  
  //@{ Deprecated methods

    void SetDataItemNodeName(const std::string &, const std::string &) ;
    void SetPath(const std::string &, const std::string &) ;

  //@}

private:

  struct DataItemInfo
  {
    std::string             strDatasetName;
    MemoryMode              eMemoryMode;
    DatasetWriterPtr        ptrDatasetWriter;
    std::vector<int>        viNextStart;
    std::vector<int>        viCurrentStart;
    std::vector<int>        viTotalSize;
    std::vector<int>        viCurrentSize;
    unsigned int            uiPushCount;        // Pushed data since begining
    int                     iBatchIndex;
    yat::MemBuf             mbPendingData;      // Data to be flushed later into the next buffer file
    bool                    bAttributesWritten; // flag to ensure buffer attributes to be written once
    std::size_t             nPendingData;       // Pending data count
    std::size_t             nRank;              // Canonical data rank (0 to 2)
  };
  typedef std::map<std::string, DataItemInfo> DataItemMap;
  typedef std::map<std::string, long> IndexMap;
  typedef std::map<std::string, yat::uint64> StartMap;
  class StreamBufferTask*     m_pStreamBufferTask; // Task managing the file recording

  NexusFileWriter::WriteMode  m_wmWritingMode;     // NexusFileWriter writing mode: 
                                                   // SYNCHRONOUS or ASYNCHRONOUS
  std::string                 m_sWorkingDir;       // Path to local folder for quick write
  std::string                 m_sTargetDir;        // Path to target folder when write is done

  /// Members for files
  std::string                 m_sName;             // Name of the buffer
  std::string                 m_sDevice;           // Device name
  std::string                 m_strPrefix;         // Tango device name generating the data
  static IndexMap             s_mapFileIndex;      // Current buffer file's index
  static StartMap             s_mapStartIndex;     // Current data buffer's index
  static yat::Time            s_tmLastWriteAccess;

  std::size_t                 m_nAcquisitionSize;  // Total Data item count for the acquisition or batch in infinite acquisition mode
  std::size_t                 m_nBufferSize;       // Total data item count in each buffer file
  DataItemMap                 m_mapDataItem;       // Data items to be written
  unsigned int                m_uiStepCompleted;   // Last completed step
  bool                        m_bInProgress;       // true if data buffering is in progress
  std::size_t                 m_nBufferCount;      // Filled buffers since begining (in infinite mode: since begining of batch)
  std::size_t                 m_nTotalBufferCount; // Filled buffers since begining (m_nTotalBufferCount eq. m_nBufferCount in finite mode)
  mutable Statistics          m_Stats;
  static yat::Mutex           s_indexLock;
  mutable yat::Mutex          m_mtxLock;
  WriteNotificationCallback   m_write_notif_cb;    
  IExceptionHandler          *m_pHandler;          // Exception handler

  // PrivPushData
  //
  // @param sItemInfo sensor's information data
  // @param tData Added data
  // @param vtDetectPos sensor's data position in the acquisition multidimensional space
  // @param nCount canonical data count
  //
  void PrivPushDataItems(DataItemInfo &sItemInfo, const void *pData, std::size_t nCount, bool bNoCopy);

  // Push pending data
  //
  // @param sItemInfo sensor's information data
  //
  void PushPendingData(DataItemInfo& ItemInfo);

  // Check buffer state
  bool PrivIsBufferOpen();

  // Initializing default values
  void init(const std::string &sBufferName);
  
  void AddDataItem(const std::string &sItemName, const std::vector<int>& viDataDim,
                   DataItemCategory nxCat) ;

  // Buffer file control
  long GetNewBufferFirstIndex(const std::string &sBaseName, const std::string &strPrefix);
  bool CheckBufferDirectory(const std::string &sBaseName, const std::string &strPrefix);
  bool IsBufferPathAvailable(const std::string &sBaseName, long lIndex);
  void BufferingControl();
  DataItemInfo &GetDataItemInfo(const std::string &sItemName) ;
  DataItemInfo &GetDataItemInfoFromDatasetName(const std::string &strDataset) ;
  void PrivOpenNewbuffer();
  void PrivCloseBuffer(bool bStopMark=false);
  void EndRecording();

  // interface NexusFileWriter::INotify
  void OnWriteSubSet(NexusFileWriter* source_p, const std::string& dataset_path, int start_pos[MAX_RANK], int dim[MAX_RANK]);
  void OnWrite(NexusFileWriter* source_p, const std::string& dataset_path);
  void OnCloseFile(NexusFileWriter* source_p, const std::string& file_path);

  // interface IExceptionHandler
  void OnNexusException(const NexusException &e);
};

// Including template methods
#include <nexuscpp/impl/nxbuffer.hpp>

} // end of namespace

////////////////////////////////////////////////////////////////////////////////// 
// python ctypes wrapper (using ctype to avoid to use boost)
////////////////////////////////////////////////////////////////////////////////// 
// DO NOT CHANGE ANYTHING WITHOUT VALIDATING YOUR MODS WITH A FLYSCAN EXPERT
////////////////////////////////////////////////////////////////////////////////// 
extern "C" 
{
  //- ctor --------------------------------------------
  NEXUSCPP_DECL nxcpp::DataStreamer * nds_new (const char* dsn, 
                                               yat::uint32 nas, 
                                               yat::uint32 nbs);
 
  //- dtor --------------------------------------------
  NEXUSCPP_DECL void nds_delete (nxcpp::DataStreamer* ds);

  //- init --------------------------------------------
  NEXUSCPP_DECL int nds_init (nxcpp::DataStreamer* ds, 
                              const char* tp, 
                              const char* dp);

  //- stop -------------------------------------------
  NEXUSCPP_DECL int nds_stop (nxcpp::DataStreamer* ds);

  //- fini --------------------------------------------
  NEXUSCPP_DECL int nds_fini (nxcpp::DataStreamer* ds);

  //- 0D data item ------------------------------------
  NEXUSCPP_DECL int nds_add_data_item_0D (nxcpp::DataStreamer* ds, const char* in);

  //- 1D data item ------------------------------------
  NEXUSCPP_DECL int nds_add_data_item_1D (nxcpp::DataStreamer* ds,
                                          const char* in,
                                          yat::uint32 sd1);

  //- 2D data item ------------------------------------
  NEXUSCPP_DECL int nds_add_data_item_2D (nxcpp::DataStreamer* ds,
                                          const char* in,
                                          yat::uint32 sd1,
                                          yat::uint32 sd2);

  //- memory mode -------------------------------------
  NEXUSCPP_DECL int nds_set_data_item_memory_mode (nxcpp::DataStreamer* ds,
                                                   const char* in,
                                                   yat::uint32 mem_mode);
  
  //- push data into the stream -----------------------
  NEXUSCPP_DECL int nds_push_data (nxcpp::DataStreamer* ds,
                                   const char* data_item_name,
                                   yat::uint32 data_type,
                                   yat::uint32 num_items_in_data_buffer,
                                   void* data_buffer);
}

#endif
