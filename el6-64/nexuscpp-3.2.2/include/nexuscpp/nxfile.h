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

#ifndef __NX_FILE_H__
#define __NX_FILE_H__

// standard library objets
#include <iostream>
#include <vector>
#include <list>
#include <map>
#include <stack>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <cstring>

// yat
#include <yat/CommonHeader.h>
#include <yat/memory/SharedPtr.h>
#include "yat/threading/Utilities.h"
#include "yat/threading/Mutex.h"
#include <nexuscpp/impl/nxdebug.h>

#if defined(WIN32) && defined(NEXUSCPP_DLL)
#  if defined (NEXUSCPP_BUILD)
#   define NEXUSCPP_DECL __declspec(dllexport)
#  else
#   define NEXUSCPP_DECL __declspec(dllimport)
#  endif
#else
#   define NEXUSCPP_DECL
#endif

namespace nxcpp
{

const int MAX_NAME_LENGTH = 256;      // For class, attributes, group names
const int MAX_DATASET_NDIMS = 256; // Maximum number of dimensions in a dataset
const char DATASET_CLASS[] = "SDS";  // Class name for NeXus data set
const std::string g_strNoDataSet = "(no data set)";

//=============================================================================
/// NexusGlobalLock
///
/// Ensure exclusive access to the nexus file API during the scope of each instance
/// of this class
//=============================================================================
class NexusGlobalLock
{
public:
  NexusGlobalLock();
  ~NexusGlobalLock();
};

// Forward declaration
class NexusFileImpl;

/// NeXus data types (see napi.h)
enum NexusDataType
{
  NX_NONE      = 0,  /// Value not defined in the NeXus API
  NX_CHAR      = 4,
  NX_FLOAT32   = 5,
  NX_FLOAT64   = 6,
  NX_INT8      = 20,
  NX_UINT8     = 21,
  NX_INT16     = 22,
  NX_UINT16    = 23,
  NX_INT32     = 24,
  NX_UINT32    = 25,
  NX_INT64     = 26,
  NX_UINT64    = 27,
  NX_BINARY    = 21,
  /// Logical data types (not defined in NeXus API) readed in NeXus meta-DTD files
  ISO8601      = 100, /// (NX_CHAR)
  NX_INT       = 102, /// generic integer type
  NX_FLOAT     = 103  /// generic real type
};

/// NeXus return codes
enum NexusRC
{
  NX_OK = 1,
  NX_EOD = -1,
  NX_ITEM_NOT_FOUND = -2 /// Value not defined in the NeXus API
};

/// Create access types
enum ENexusCreateMode
{
  NX_HDF4 = 0,
  NX_HDF5,     // This is the default
  NX_XML       // Not implemented yet
};

const int MAX_RANK = 32;              // Max rank

//=============================================================================
/// NeXus exceptions
///
/// This class is designed to hold nexus exceptions
//=============================================================================
class NEXUSCPP_DECL NexusException: public yat::Exception
{
public:
  NexusException() {}
  
  NexusException(const char *pcszDesc, const char *pcszOrigin):
    yat::Exception("NEXUSCPP_ERROR", pcszDesc, pcszOrigin) {}
  
  NexusException(const std::string& desc, const char *pcszOrigin):
    yat::Exception("NEXUSCPP_ERROR", desc.c_str(), pcszOrigin) {}
  
  /// Prints error message on console
  void PrintMessage();

  /// Copies error message in a pre-allocated std::string buffer
  ///
  /// @param pBuf Buffer (allocated by caller) to copy the message in
  /// @param iLen Buffer length
  ///
  void GetMsg(char *pBuf, int iLen);
};

//=============================================================================
/// NeXus item identifier
///
/// This class holds a nexus item ids
//=============================================================================
class NEXUSCPP_DECL NexusItemID
{
friend class NexusFileImpl;

private:
  void *m_pLink;  // Anonymous pointer to Nexus link object
  
public:
  NexusItemID();
  ~NexusItemID();
};

//=============================================================================
/// NeXus Data set info
///
/// This class allow manipulation of NeXus Data sets and subsets
//=============================================================================
class NEXUSCPP_DECL NexusDataSetInfo
{
protected:
  NexusDataType  m_eDataType;      // data type
  int            m_iRank;          // Data storage rank
  int            m_iTotalRank;     // Whole dataset rank
  int           *m_piDim;          // size of each dimensions
  int           *m_piTotalDim;     // size of each dimensions of the whole dataset
  int           *m_piStart;        // indices of starting values in each dimensions

public:
  
  /// Returns size according to a data type
  static int DataTypeSize(NexusDataType eDataType);

  /// Constructor
  NexusDataSetInfo();

  /// Destructor
  ~NexusDataSetInfo();

  /// Sets datas
  ///
  /// @param eDataType Data type
  /// @param iRank Data rank
  ///
  void SetInfo(NexusDataType eDataType, int iRank);

  /// Clears instance
  void Clear();

  //-----------------------------------------------------------------
  /// @name Accessors
  //@{

  /// Returns the data type
  NexusDataType DataType() const { return m_eDataType; }

  /// Returns the data set rank
  int Rank() const { return m_iRank; }
  
  /// Returns the whole data set rank
  int TotalRank() const { return m_iTotalRank; }
  
  /// Returns size array of dimensions
  /// 
  int *DimArray() const { return m_piDim; }

  /// Returns size array of dimensions of the whole dataset
  /// @note can be bigger than the storage array of this NexusDataSetInfo
  ///
  int *TotalDimArray() const { return m_piTotalDim; }
  
  /// Set the whole dataset dimension's sizes
  ///
  /// @param iRankSpace Rank of the whole dataset
  /// @param piDimArray Dimension sizes of the whole dataset
  ///
  void SetTotalDim(int iTotalRank, int *piTotalDim);

  /// Returns total size
  ///
  int Size() const;

  /// Returns 'true' if no data set info is stored
  ///
  bool IsEmpty() const;

  /// Returns datum size
  ///
  unsigned int DatumSize() const;

  /// Returns buffer size
  unsigned int BufferSize() const { return Size() * DatumSize(); }

  /// Returns array of indices of starting values in Ith dimension
  /// 
  int *StartArray() const { return m_piStart; }
  int *StartArray();
	
  bool IsSubset() const { return m_piStart ? true:false;}

  //@} Accessors
};

//=============================================================================
/// NeXus Data set
///
/// This class allow manipulation of NeXus Data sets and subsets
//=============================================================================
class NEXUSCPP_DECL NexusDataSet : public NexusDataSetInfo
{
private:
  void        *m_pData;    // Data set
  bool         m_owner;    // ownsership

public:
  /// Constructors
  NexusDataSet();
  NexusDataSet(const NexusDataSet &dataset);
  NexusDataSet(NexusDataType eDataType, void *pData, int iRank, int *piDim, int *piStart=NULL);

  /// Destructor
  /// @note the data pointor is also deleted do not delete it another time
  ~NexusDataSet();

  /// Free data block
  ///
  void FreeData();

  /// Clears data set
  void Clear();

  /// Allocate the data set, according to the NexusDataSetInfo part
  ///
  void Alloc();

  /// Gives ownership or not
  ///
  /// By default the NexusDataset own the data pointer
  /// @param this_has_ownership if false the NexusDataset is no longer the owner
  ///
  void SetOwner(bool this_has_ownership);

  /// Have ownership on data buffer ?
  ///
  /// By default the NexusDataset own the data pointer
  /// @param this_has_ownership if false the NexusDataset is no longer the owner
  ///
  bool IsOwner() { return m_owner; }

  //-----------------------------------------------------------------
  /// @name Accessors
  //@{

  /// Returns a pointer to the data set
  void *Data() const { return m_pData; }

  /// Returns size of the data block (i.e. Size() * sizeof(<datatype>) )
  unsigned int MemSize() const;

  /// Returns the number of items
  unsigned int Size() const;

  /// Sets datas
  /// 
  /// @param pParam data pointer we take ownership
  ///
  void SetData(void *pData) { m_pData = pData; }

  /// Sets datas
  ///
  /// @param pData      Pointer to data to copy
  /// @param eDataType  Data type
  /// @param iRank      Number of dimension
  /// @param piDimArray Dimensions sizes
  ///
  void SetData(const void *pData, NexusDataType eDataType, int iRank, int *piDimArray);

  /// Sets one dimension size
  ///
  /// iDim  Dimension in range [0, 32[
  /// iSize Dimension size
  ///
  void SetDimension(int iDim, int iSize);

  //@} Accessors

  //=============================================================================
  /// Basic iterator to browse over NeXusDataSet values
  //=============================================================================
  template <class T> class Iterator
  {
  private:
    const NexusDataSet &m_DataSet;
    char *m_pDataItem;

  public:
    /// Constructor
    Iterator(const NexusDataSet &DataSet) : m_DataSet(DataSet)
    {
      m_pDataItem = (char *)DataSet.Data();
    }

    /// Return current value
    T Value() const
    {
      switch( m_DataSet.DataType() )
      {
        case NX_FLOAT32:
          return T(*((float *)m_pDataItem));
        case NX_FLOAT64:
          return T(*((double *)m_pDataItem));
        case NX_INT8:
          return T(*((char *)m_pDataItem));
        case NX_INT16:
          return T(*((short *)m_pDataItem));
        case NX_INT32:
          return T(*((long *)m_pDataItem));
        case NX_UINT8:
          return T(*((unsigned char *)m_pDataItem));
        case NX_UINT16:
          return T(*((unsigned short *)m_pDataItem));
        case NX_UINT32:
          return T(*((unsigned long *)m_pDataItem));
      }
      return T(0);
    }

    /// Dereferencing operator
    T operator *() { return Value(); }
  
    /// Post-incrementation operator
    Iterator operator++(int) 
    { 
      m_pDataItem += m_DataSet.DatumSize(); 
      return *this;
    }

    /// Pre-incrementation operator
    Iterator operator++() 
    { 
      m_pDataItem += m_DataSet.DatumSize(); 
      return *this;
    }

    /// Is end of buffer reached ?
    bool End() const 
    { 
      return m_pDataItem == (char *)m_DataSet.Data() + m_DataSet.MemSize(); 
    }
  };
};

//=============================================================================
/// NeXus item info
///
/// This class is used to store info about founded groups while
/// browsing a group level
//=============================================================================
class NEXUSCPP_DECL NexusItemInfo
{
friend class NexusFileImpl;

private:
  char          *m_pszItem;       // Name of NeXus data item (group or set)
  char          *m_pszClass;      // Class of NeXus group
  NexusDataType  m_eDataType;     // data type (NX_NONE for groups)
  void          *m_pContext;      // Extra Data storing current context

public:
  /// Constructor
  NexusItemInfo();

  /// Destructor
  ~NexusItemInfo();

  //-----------------------------------------------------------------
  /// Accessors
  //@{
  
  /// Returns name of NeXus data item (group or set)
  const char *ItemName() const { return m_pszItem; }

  /// Returns class name of NeXus group
  const char *ClassName() const { return m_pszClass; }

  /// Returns the data type
  NexusDataType DataType() const { return m_eDataType; }

  /// Returns 'true' id the item is a data set
  bool IsDataSet() const;

  /// Returns 'true' id the item is a group
  bool IsGroup() const;

  //@} Accessors
};

/// Smart pointer definition helper
typedef yat::SharedPtr<NexusItemInfo> NexusItemInfoPtr;

/// ItemInfo smart pointers list definition helper
typedef std::list<NexusItemInfoPtr> NexusItemInfoList;

//=============================================================================
/// NeXus Attribute info
///
/// This class is used to store info about founded attributes while browsing
/// a group level
//=============================================================================
class NEXUSCPP_DECL NexusAttrInfo
{
friend class NexusFileImpl;

protected:
  char          *m_pszName;   // Attribute name
  int            m_iLen;      // Attribute length
  NexusDataType  m_eDataType;     // Type of attribute data
  void          *m_pContext;      // Extra Data storing current context

public:
  /// Constructor
  NexusAttrInfo();

  /// Destructor
  ~NexusAttrInfo();

  //-----------------------------------------------------------------
  /// Accessors
  //@{
  
  /// Returns name of NeXus attribute
  const char *AttrName() const { return m_pszName; }

  /// Returns len of attribute
  int Len() const { return m_iLen; }

  /// Returns type attribute data
  NexusDataType DataType() const { return m_eDataType; }

  //@} Accessors
};

//=============================================================================
/// NeXus Attribute
///
/// This class is used to store info about founded attributes while browsing
/// a group level
//=============================================================================
class NEXUSCPP_DECL NexusAttr: public NexusAttrInfo
{
private:
  void    * m_pAttrValue;

public:
  NexusAttr(const std::string &strName);
  NexusAttr(const NexusAttr &aAttr);
  ~NexusAttr();

  /// Sets a value of type 'long'
  void SetLong(long lValue);

  /// Sets a value of type 'C characters std::string'
  void SetCString(const char *pcszValue);

  /// Sets a value of type 'STL std::string'
  void SetString(const std::string &strValue);

  /// Sets a value of type 'double'
  void SetDouble(double dValue);

  /// Sets a value of type 'float'
  void SetFloat(float fValue);

  /// Get 'long' type value
  long GetLong() const;
  
  /// Gets 'double' type value
  double GetDouble() const ;

  /// Gets 'float' type value
  float GetFloat() const;

  /// Gets 'std::string' type value
  std::string GetString() const;

  /// Returns the attr buffer
  void *RawValue() const;
  
private:
  void CopyValue(const NexusAttr &aAttr);
};

/// definition of a attribute collection 
typedef std::list<NexusAttr> NeXusAttrList;

//=============================================================================
/// NeXus File Class
///
/// This class allow manipulation of NeXus File with no need to known about
/// nexus file handle
/// @note This is wrapper class, the real job is make by a internal objet
//=============================================================================
class NEXUSCPP_DECL NexusFile
{
public:
  enum OpenMode
  {
    NONE,
    READ,
    WRITE
  };

private:
  NexusFileImpl *m_pImpl;    // Pointer to implementation
  void          *m_pUserPtr; // a free data pointer

public:

  static const bool USE_LOCK = true;

  /// Initialize the HDF5 context
  /// It is safe to call this function multiple times
  /// @note Any constructed NexusFile will initialize

  static void Initialize();

  /// @name Constructors and destructor
  //@{

  /// Constructor
  ///
  /// @param pcszFullPath path + complete filename
  NexusFile(const char *pcszFullPath=NULL, OpenMode eMode = NONE, bool use_lock = false);

  /// Destructor
  ///
  /// Perform all operations needed such as closing open groups
  ~NexusFile();

  //@}

  //-----------------------------------------------------------------
  /// @name File manipulation methods
  //@{

  /// Creating file
  ///
  /// Create Nexus file based on HDF4 library
  ///
  /// @param pcszFullPath path + complete filename
  /// @param eMode creating mode can be NX_HDF4, NX_HDF5 or NX_XML
  ///
  void Create(const char *pcszFullPath, ENexusCreateMode eMode = NX_HDF5);

  /// Opens an existing file for read operations
  ///
  /// @param pcszFullPath path + complete filename
  /// 
  void OpenRead(const char *pcszFullPath);

  /// Opens an existing file for read/write operations
  ///
  /// @param pcszFullPath path + complete filename
  /// 
  void OpenReadWrite(const char *pcszFullPath);

  /// Closes currently opened file
  /// Frees all allocated objects (groups & data sets)
  ///
  void Close();

  /// Flush
  /// Flushes all data to the NeXus file
  ///
  void Flush();

  //@}

  //-----------------------------------------------------------------
  /// @name groups manipulations
  //@{

  /// Adds a new group
  ///
  /// The new group is added under the currently opened group
  /// If no group is already open add the group at the top-level
  /// (hope this is a NXentry group)
  ///
  /// @param pcszName  Group name
  /// @param pcszClass NeXus class
  /// @param bOpen    If 'true' automatically open the group
  ///
  void CreateGroup(const char *pcszName, const char *pcszClass, bool bOpen=true);

  /// Opens a existing group
  ///
  /// @param pcszName  Group name
  /// @param pcszClass NeXus class
  /// @param bThrowException if true this method may throw a exception
  /// @return 
  ///
  bool OpenGroup(const char *pcszName, const char *pcszClass, bool bThrowException=true);

  /// Closes current group
  ///
  void CloseGroup();

  /// Closes all opened groups
  ///
  void CloseAllGroups();

  //@} groups manipulations

  //-----------------------------------------------------------------
  /// @name data sets manipulations
  //@{

  /// Creates data set
  ///
  /// @param pcszName Data set name
  /// @param eDataType Data type
  /// @param iRank Data set rank
  /// @param piDim Pointer to array of dimensions
  ///
  void CreateDataSet(const char *pcszName, NexusDataType eDataType, 
                     int iRank, int *piDim, int bOpen=true);

  /// Creates compressed data set
  ///
  /// @param pcszName Data set name
  /// @param eDataType Data type (see napi.h)
  /// @param iRank Data set rank
  /// @param piDim Pointer to array of dimensions
  /// @param piChunkDim Pointer to array of chunk dimensions
  ///
  void CreateCompressedDataSet(const char *pcszName, NexusDataType eDataType, 
                               int iRank, int *piDim, int *piChunkDim = NULL, int bOpen=true);

  /// Closes currenly open dataset
  ///
  void CloseDataSet();

  /// Quickly creates simple data set and writes the data
  ///
  /// @param pcszName Data set name
  /// @param pData pointer to value
  /// @param piDim Pointer to array of dimensions sizes
  /// @param eDataType NeXus Data type (see napi.h)
  /// @param iRank Dimensions count
  /// @param bCreate true for creating the dataset before write data otherwise
  ///        we suppose the dataset already created and opened
  ///
  void WriteData(const char *pcszName, void *pData, NexusDataType eDataType, 
                 int iRank, int *piDim, bool bCreate=true);

  void WriteData(const char *pcszName, void *pData, NexusDataType eDataType, 
                 const std::vector<std::size_t>& Shape, bool bCreate=true);

  void WriteDataSubSet(const char *pcszName, void *pData, NexusDataType eDataType, 
                       int iRank, int *piStart, int *piDim, bool bCreate=true, bool bNoDim = false);

  /// 'float' version of #WriteData
  void WriteData(const char *pcszName, float fValue, bool bCreate=true);

  /// 'double' version of #WriteData
  void WriteData(const char *pcszName, double dValue, bool bCreate=true);

  /// 'long' version of #WriteData
  void WriteData(const char *pcszName, long lValue, bool bCreate=true);

  /// 'std::string' version of #WriteData
  void WriteData(const char *pcszName, const char *pcszValue, bool bCreate=true);

  /// 'binary' version of #WriteData
  void WriteData(const char *pcszName, void *pData, int _iLen, bool bCreate=true);

  /// Opens a already existing data set
  ///
  /// @param pcszName Data set name
  bool OpenDataSet(const char *pcszName, bool bThrowException=true);

  /// Puts data in the currently open data set
  ///
  /// @param pData Pointer to the data bloc
  /// @param pcszName Data set name
  ///
  /// @note Use pcszName to specify another data set
  ///
  void PutData(void *pData, const char *pcszName=NULL, int bFlush=false);

  /// Puts data subset in the currently open data set
  ///
  /// @param pData Pointer to the data bloc
  /// @param piStart Indices of starting values in each dimension
  /// @param piSize Length of subset in each dimension
  /// @param pcszName Data set name
  ///
  /// @note Use pcszName to specify another data set
  ///
  void PutDataSubSet(void *pData, int *piStart, int *piSize, const char *pcszName=NULL);

  //-----------------------------------------------------------------
  /// @name Data set reading
  //@{
  
  /// Reads data values from a data set in currently open group
  ///
  /// @param pDataSet a pointer to a NexusDataSet instanciate by caller
  /// @param pcszDataSet Data set name
  /// @note if the data set name isn't provided, supposes that pDataSet isn't empty
  ///
  void GetData(NexusDataSet *pDataSet, const char *pcszDataSet=NULL);

  /// Reads data values from a data set in currently open group
  ///
  /// @param pDataSet a pointer to a NexusDataSet instanciate by caller
  /// @param pcszDataSet Data set name
  /// @note if the data set name isn't provided, supposes that pDataSet isn't empty
  ///
  void GetDataSubSet(NexusDataSet *pDataSet, const char *pcszDataSet=NULL);

  /// Gets info about a data set
  ///
  /// @param pDataSetInfo a pointer to a NexusDataSetInfo instanciate by caller
  /// @param pcszDataSet Data set name
  ///
  void GetDataSetInfo(NexusDataSetInfo *pDataSetInfo, const char *pcszDataSet);

  //@} data sets manipulations

  //-----------------------------------------------------------------
  /// @name Getting attributes
  //@{

  /// Gets a attribute
  ///
  /// @param pcszAttr Attribute name
  /// @param piBufLen Pointer to BufLen
  /// @param pData Pointer to attribute value buffer
  /// @param eDataType Attribute data value type
  ///
  /// @note The caller is responsible for allocating enough memory
  ///       for the attribute values
  void GetAttribute(const char *pcszAttr, int *piBufLen, void *pData, 
                    NexusDataType eDataType);
  
  /// Gets a 'long' attribute. Call 
  ///
  /// @param pcszAttr Attribute name
  /// @param plValue Pointer to attribute value buffer
  ///
  /// @note The caller is responsible for allocating enough memory
  ///       for the attribute values
  void GetAttribute(const char *pcszAttr, long *plValue);
  
  /// Gets a 'double' attribute
  ///
  /// @param pcszAttr Attribute name
  /// @param pdValue Pointer to attribute value buffer
  ///
  /// @note The caller is responsible for allocating enough memory
  ///       for the attribute values
  void GetAttribute(const char *pcszAttr, double *pdValue);

  /// Gets a 'float' attribute
  ///
  /// @param pcszAttr Attribute name
  /// @param pfValue Pointer to attribute value buffer
  ///
  /// @note The caller is responsible for allocating enough memory
  ///       for the attribute values
  void GetAttribute(const char *pcszAttr, float *pfValue);

  /// Gets a 'std::string' attribute
  ///
  /// @param pcszAttr Attribute name
  /// @param pstrValue Pointer to attribute value
  ///
  void GetAttribute(const char *pcszAttr, std::string *pstrValue);

  //@} Getting attributes

  //-----------------------------------------------------------------
  /// @name Putting attributes
  /// Writes an attribute of the currently open data set. 
  /// If no data set is open, a global attribute is generated. 
  /// The attribute has both a name and a value.
  /// @param pcszName Data set name
  /// @param pValue pointer to value
  /// @param iLen Value buffer length
  /// @param eDataType NeXus Data type (see napi.h)
  //@{

  /// Generic method for putting attribute
  void PutAttr(const char *pcszName, void *pValue, int iLen, NexusDataType eDataType);

  /// Puts a attribute of type 'long'
  void PutAttr(const char *pcszName, long lValue);

  /// Puts a attribute of type 'std::string'
  void PutAttr(const char *pcszName, const char *pcszValue);

  /// Puts a attribute of type 'double'
  void PutAttr(const char *pcszName, double dValue);

  /// Puts a attribute of type 'float'
  void PutAttr(const char *pcszName, float fValue);

  //@} // Putting attributes

  //-----------------------------------------------------------------
  /// @name Browsing methods
  //@{
 
  /// Get the number of items in the current group
  int ItemCount();

  /// Get the number of attributes in the current data set
  int AttrCount();

  /// Get info about the first item (data set or group) in the current group
  ///
  /// @param pItemInfo Pointer to NexusItemInfo used to store info
  ///
  /// @return NX_OK if data set found
  ///         NX_EOD if not
  ///
  int GetFirstItem(NexusItemInfo *pItemInfo);
  
  /// Get info about the next item (data set or group) in the current group
  ///
  /// @param pItemInfo Pointer to NexusItemInfo used to store info
  ///
  /// @return NX_OK if data set found
  ///         NX_EOD if not
  ///
  int GetNextItem(NexusItemInfo *pItemInfo);
  
  /// Get info about the first attribute of the specified data set
  ///
  /// @param pAttrInfo Pointer to a NexusAttrInfo object
  /// @param pcszDataSet Name of the data set to search in
  /// @return NX_OK if group found,
  ///         NX_EOD if not
  ///
  /// @note if pcszDataSet is NULL searching will be performed in currently open
  /// data set or in global attribute std::list if no data set is open
  /// @note You must call GetFirstAttribute in order to initialize the search process
  /// then call GetNextAttribute for retrieving information about the remaining
  /// attributes
  ///
  int GetFirstAttribute(NexusAttrInfo *pAttrInfo, const char *pcszDataSet=NULL);

  /// Get info about the next attribute of the specified data set
  ///
  /// @param pAttrInfo Pointer to a NexusAttrInfo object
  /// @return NX_OK if group found
  ///         NX_EOD if not
  ///
  /// @note You must call GetFirstAttribute in order to initialize the search process
  /// then call GetNextAttribute for retrieving information about the remaining
  /// attributes
  ///
  int GetNextAttribute(NexusAttrInfo *pAttrInfo);

  //@} // Browsing methods

  //-----------------------------------------------------------------
  /// @name Item linking methods
  //@{
 
  /// Get a handle on the currently open data set in order to link it with a group
  /// 
  /// @return a pointer to the handle, or NULL if no data set is open or no file is open
  ///
  void GetDataSetLink(NexusItemID *pnxl);

  /// Get a handle on the currently open group set in order to link it with a group
  /// 
  /// @return a pointer to the handle, or NULL if no file is open
  ///
  void GetGroupLink(NexusItemID *pnxl);

  /// Link a item to the currently open group
  ///
  /// @param pItemHandle Handle of the item to link
  ///
  void LinkToCurrentGroup(const NexusItemID &nxl);

  //@} // Item linking methods

  //-----------------------------------------------------------------
  /// @name User pointer accessors
  //@{

  /// Get user pointer
  void *UserPtr() const { return m_pUserPtr; } 

  /// Set user pointer
  void SetUserPtr(void *p) { m_pUserPtr = p; }

  //@} // User pointer accessors

  //-----------------------------------------------------------------
  /// @name high level methods
  //@{

  /// Opens a existing group from a given path
  ///
  /// @param pszPath   path to group in the form : /<NXroot>/{Group name}<{group class}>/...
  ///                  if no group name is specified then open the first group for the given class
  ///                  example : /<NXroot>/scan_1<NXentry>/<NXdata>
  /// @param bThrowException if true this method may throw a exception
  /// @return true if group has been succefully open
  ///
  bool OpenGroupPath(const char *pszPath, bool bThrowException=true);

  /// Create and open a group from a given path
  /// This method may throw a exception
  ///
  /// @param pszPath   path to group in the form : /<NXroot>/{Group name}<{group class}>/...
  ///                  if no group name is specified then open the first group for the given class
  ///                  example : /<NXroot>/scan_1<NXentry>/<NXdata>
  ///
  bool CreateGroupPath(const char *pszPath);

  /// Search for all occurences of a group (name and class) starting at a given level on the hierarchy
  ///
  /// @param pszGroupName name of searched group
  /// @param pszClassName class of searched group
  /// @param pvecPaths output std::vector of found groups
  /// @param pszStartPath path for starting research
  /// @return NX_OK if at least one group was found
  ///         NX_EOD if not
  ///
  int SearchGroup(const char *pszGroupName, const char *pszClassName,
                  std::vector<std::string> *pvecPaths, const char *pszStartPath=NULL);

  /// Search for all occurences of a data set (name) with a given attribute
  ///
  /// @param pszAttrName name of looked attribute 
  /// @param pvecPaths output std::vector of found data sets
  /// @param strAttrVal optionnal attribute value to match
  /// @return NX_OK if at least one data set was found
  ///         NX_EOD if not
  ///
  int SearchDataSetFromAttr(const char *pszAttrName, std::vector<std::string> *pvecDataSets, const std::string &strAttrVal="");

  /// Search for first occurence of a data set (name) with a given attribute
  ///
  /// @param pszAttrName name of looked attribute 
  /// @param pstrDataSet name of founded data set
  /// @param strAttrVal optionnal attribute value to match
  /// @return NX_OK if at least one data set was found
  ///         NX_EOD if not
  ///
  int SearchFirstDataSetFromAttr(const char *pszAttrName, std::string *pstrDataSet, const std::string &strAttrVal="");

  /// Search for all occurences of a data set (name) with a given attribute
  ///
  /// @param pszAttrName name of looked attribute 
  /// @param iRank dataset rank 
  /// @param pvecPaths output std::vector of found data sets
  /// @param strAttrVal optionnal attribute value to match
  /// @return NX_OK if at least one data set was found
  ///         NX_EOD if not
  ///
  int SearchDataSetFromAttrAndRank(const char *pszAttrName, int iRank, std::vector<std::string> *pvecDataSets, const std::string &strAttrVal="");

  /// Search for all occurences of a data set (name) with a given attribute
  ///
  /// @param pszAttrName name of looked attribute 
  /// @param iRank dataset rank 
  /// @param pvecPaths output std::vector of found data sets
  /// @param strAttrVal optionnal attribute value to match
  /// @return NX_OK if at least one data set was found
  ///         NX_EOD if not
  ///
  int SearchFirstDataSetFromAttrAndRank(const char *pszAttrName, int iRank, std::string *pstrDataSet, const std::string &strAttrVal="");

  /// Gets a attribute as a std::string regardless of its type
  ///
  /// @param aAttrInfo Attribute info object
  /// @return A STL std::string containing the value
  ///
  std::string GetAttributeAsString(const NexusAttrInfo &aAttrInfo);

  /// Check if dataset has attribute
  ///
  /// @param pszAttrName Name of looked attribute
  /// @param pcszDataSet (optionnal) Data set to open, if not specified look in currently opened dataset
  /// @param strAttrVal optionnal attribute value to match
  /// @return 'true' if sds has attribute, otherwise 'false'
  ///
  bool HasAttribute(const char *pszAttrName, const char *pcszDataSet=NULL, const std::string &strAttrVal="");

  /// Gets a attribute as a std::string regardless of its type
  ///
  /// @param pszAttrName Attribute name
  /// @param pcszDataSet Name of the data set containing the attributge
  /// @param pstrValue std::string object to put the value in
  /// @return true if attribute was found
  ///
  bool GetAttributeAsString(const char *pszAttrName, const char *pcszDataSet, std::string *pstrValue);

  /// Build a dictionnary that contains axis datasets for each dimension belong to a given dataset
  /// Entries in the dictionnary are in the form :
  /// 'axis_<axis>_<primary>' = '<dataset name>
  ///
  /// @param vecvecAxis Vector of axis vectors that will contains the axis datasets name
  /// @param pszGroup Group Path
  /// @param pszDataSet DataSet name
  ///
  /// @return 'true' if at least one axis has been found
  ///
  bool BuildAxisDict(std::map<std::string, std::string> *pmapAxis, const char *pszGroupPath=NULL, const char *pcszDataSet=NULL);

  /// Build a dictionnary that contains axis values (readed and setted) datasets for each dimension belong to a given dataset
  /// Entries in the dictionnary are in the form :
  /// 'axis-readed_<axis>_<primary>' = '<dataset name>
  /// 'axis-setted_<axis>_<primary>' = '<dataset name>
  ///
  /// @param vecvecAxis Vector of axis vectors that will contains the axis datasets name
  /// @param pszGroup Group Path
  /// @param pszDataSet DataSet name
  ///
  /// @return 'true' if at least one axis has been found
  ///
  bool BuildScanAxisDict(std::map<std::string, std::string> *pmapAxis, const char *pszGroupPath=NULL, const char *pcszDataSet=NULL);

  /// Get scam dimension
  ///
  /// Reads NXdata group and look for variables datasets (with attribute axis) and return the 
  /// biggest axis attribute value (note that in time scan case axis may be seeted to 0)
  ///
  /// @param pszGroup Group Path
  ///
  /// @return scan dimension (time scans are 1D scans), or 0 if no axis datasets was found
  ///
  int GetScanDim(const char *pszDataGroupPath);

  /// Current Group Name
  ///
  std::string CurrentGroupName();

  /// Current Group Class
  ///
  std::string CurrentGroupClass();

  /// Currently opened group path inside NeXus file
  ///
  std::string CurrentGroupPath();
  
  /// Currently opened dataset
  ///
  std::string CurrentDataset();
  
  /// Returns the children of the currently opened group
  ///
  /// @param pvecDatasets empty std::vector for storing dataset names
  /// @param pvecGroupNames empty std::vector for storing subgroups names
  /// @param pvecGroupClasses empty std::vector for storing subgroups classes
  ///
  void GetGroupChildren(std::vector<std::string> *pvecDatasets, std::vector<std::string> *pvecGroupNames, std::vector<std::string> *pvecGroupClasses);
  
  /// Returns the children of the currently opened group
  ///
  /// @return a list of NexusItemInfo object smart pointer (yat::SharedPtr)
  ///
  NexusItemInfoList GetGroupChildren();
  
  //@}

  //-----------------------------------------------------------------
  /// @name Deprecated methods
  //@{

  void GetAttribute(const char *pcszAttr, char *pszValue, int iBufLen);

  //@}

};

//-----------------------------------------------------------------
/// Free function: provide project version & name  
//-----------------------------------------------------------------
const char* get_version();
const char* get_name();

} // namespace nxcpp

#endif
