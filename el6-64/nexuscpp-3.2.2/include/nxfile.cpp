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

#include <algorithm>

#include <yat/utils/Logging.h>
#include <yat/memory/MemBuf.h>
#include <yat/utils/String.h>
#include <yat/Version.h>

#include "nexuscpp/nxfile.h"

/**
* \TODO
* * let iterators be random access
* * erros
* * * locate & wrap remaining HDF5 exceptions to Nexus
* * * put error descriptions as variables
* * * format error descriptions with input params
* * protect methods against multithreaded access
* * TESTS !!!!!!!!!!!!!!!!

* HOW-TO compile LZ4 plugin to be loaded dynamically:
* * ensure that HDF5 is built as a dynamic lib
* * download source code https://github.com/michaelrissi/HDF5Plugin
* * setup HDF5_PLUGIN_PATH env to the path containing the future plugin (.so/.dll)
* * On Linux:
* * * make
* * On Windows:
* * * Using gcc:
* * * * g++ -shared -o h5zlz4.dll -I <path-to-hdf5-include-dir> *.cpp -lws2_32
* * * Using MSVC :
* * * * set source to .cpp to avoid C89 standard (no need to put "extern "C"")
* * * * cl /LD /I <path-to-hdf5-include-dir> ws2_32.lib *.cpp
* * put the .so/.dll in your plugin path
*/

/// Use enum instead of #define for NeXus DataTypes
/// \NOTE useless if no <napi.h>
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

/// macro that transforms HDF5 exceptions into NexusException

#define NEXUS_CATCH(desc, origin) catch (const H5::Exception& err) { throw castException(err, desc, origin); }


/// macro that let methods be thread safe

#ifndef NXFILE_NO_LOCK
    #define NXFILE_LOCK NexusGlobalLock __lock
#else
    #define NXFILE_LOCK do {} while (0)
#endif

#ifdef WIN32
    #undef min
#endif

namespace nxcpp
{

const char* NO_SIGNAL_ATTR_ERR = "Cannot find 'signal' attribute";

//=============================================================================
// NeXus item identifier
//
// This class holds a nexus links
//=============================================================================
NexusItemID::NexusItemID()
{
    m_pLink = new std::string;
}

NexusItemID::~NexusItemID()
{
    delete (std::string*)m_pLink;
}

//=============================================================================
// NeXus item info
//
//=============================================================================

bool NexusItemInfo::IsDataSet() const
{
    return !strncmp(m_pszClass, DATASET_CLASS, 3);
}

bool NexusItemInfo::IsGroup() const
{
    return !strncmp(m_pszClass, "NX", 2);
}

//=============================================================================
//
// NeXus Attribute Class
//
//=============================================================================
//---------------------------------------------------------------------------
// Constructor
//---------------------------------------------------------------------------
NexusAttr::NexusAttr(const std::string &strName) : NexusAttrInfo()
{
    strncpy(this->m_pszName, strName.c_str(), MAX_NAME_LENGTH);
    this->m_iLen = 0;
    this->m_eDataType = NX_NONE;
    this->m_pAttrValue = NULL;

}

NexusAttr::NexusAttr(const NexusAttr &aAttr)
{
    strncpy(this->m_pszName, aAttr.m_pszName, MAX_NAME_LENGTH);
    this->m_iLen = aAttr.m_iLen;
    this->m_eDataType = aAttr.m_eDataType;
    this->CopyValue(aAttr);
}

//---------------------------------------------------------------------------
// Destructor
//---------------------------------------------------------------------------
NexusAttr::~NexusAttr()
{
    if( m_pAttrValue != NULL )
    {
        switch( m_eDataType )
        {
        case NX_INT8:
        case NX_UINT8:
            delete (char *)m_pAttrValue;
            break;
        case NX_INT16:
        case NX_UINT16:
            delete (short *)m_pAttrValue;
            break;
        case NX_INT32:
        case NX_UINT32:
            delete (long *)m_pAttrValue;
            break;
        case NX_INT64:
        case NX_UINT64:
            delete (yat::int64 *)m_pAttrValue;
            break;
        case NX_FLOAT32:
            delete (float *)m_pAttrValue;
            break;
        case NX_FLOAT64:
            delete (double *)m_pAttrValue;
            break;
        default:
            break;
        }
    }
}

//---------------------------------------------------------------------------
// NexusAttr::SetLong
//---------------------------------------------------------------------------
void NexusAttr::SetLong(long lValue)
{
    m_pAttrValue = new long(lValue);
    m_iLen = 1;
    m_eDataType = NX_INT32;
}

//---------------------------------------------------------------------------
// NexusAttr::SetCString
//---------------------------------------------------------------------------
void NexusAttr::SetCString(const char *pcszValue)
{
    m_iLen = strlen(pcszValue);
    //  m_pAttrValue = PCSZToPSZ(pcszValue);
    m_pAttrValue = new char[m_iLen];
    strncpy( (char*) m_pAttrValue, pcszValue, m_iLen);
    m_eDataType = NX_CHAR;
}

//---------------------------------------------------------------------------
// NexusAttr::SetString
//---------------------------------------------------------------------------
void NexusAttr::SetString(const std::string &strValue)
{
    m_iLen = strlen(strValue.c_str());
    m_pAttrValue = new char[m_iLen];
    strncpy( (char*) m_pAttrValue, strValue.c_str(), m_iLen);
    m_eDataType = NX_CHAR;
}

//---------------------------------------------------------------------------
// NexusAttr::SetDouble
//---------------------------------------------------------------------------
void NexusAttr::SetDouble(double dValue)
{
    m_pAttrValue = new double(dValue);
    m_iLen = 1;
    m_eDataType = NX_FLOAT64;
}

//---------------------------------------------------------------------------
// NexusAttr::SetFloat
//---------------------------------------------------------------------------
void NexusAttr::SetFloat(float fValue)
{
    m_pAttrValue = new float(fValue);
    m_iLen = 1;
    m_eDataType = NX_FLOAT32;
}

//---------------------------------------------------------------------------
// NexusAttr::GetLong
//---------------------------------------------------------------------------
long NexusAttr::GetLong() const
{
    long value = 0;
    if( m_pAttrValue != NULL )
    {
        switch( m_eDataType )
        {
        case NX_INT8:
        case NX_UINT8:
        case NX_INT16:
        case NX_UINT16:
        case NX_INT32:
        case NX_UINT32:
        case NX_INT64:
        case NX_UINT64:
            value = *(long*) m_pAttrValue;
            break;
        default:
            break;
        }
    }

    return value;
}

//---------------------------------------------------------------------------
// NexusAttr::GetDouble
//---------------------------------------------------------------------------
double NexusAttr::GetDouble() const
{
    double value = 0.;
    if( m_pAttrValue != NULL )
    {
        switch( m_eDataType )
        {
        case NX_INT8:
        case NX_UINT8:
        case NX_INT16:
        case NX_UINT16:
        case NX_INT32:
        case NX_UINT32:
        case NX_INT64:
        case NX_UINT64:
        case NX_FLOAT32:
        case NX_FLOAT64:
            value = *(double*) m_pAttrValue;
            break;
        default:
            break;
        }
    }

    return value;
}

//---------------------------------------------------------------------------
// NexusAttr::GetFloat
//---------------------------------------------------------------------------
float NexusAttr::GetFloat() const
{
    float value = 0.;
    if( m_pAttrValue != NULL )
    {
        switch( m_eDataType )
        {
        case NX_INT8:
        case NX_UINT8:
        case NX_INT16:
        case NX_UINT16:
        case NX_INT32:
        case NX_UINT32:
        case NX_INT64:
        case NX_UINT64:
        case NX_FLOAT32:
        case NX_FLOAT64:
            value = *(float*) m_pAttrValue;
            break;
        default:
            break;
        }
    }

    return value;
}

//---------------------------------------------------------------------------
// NexusAttr::Getyat::String
//---------------------------------------------------------------------------
std::string NexusAttr::GetString() const
{
    std::string value;
    if( m_pAttrValue != NULL )
    {
        switch( m_eDataType )
        {
        case NX_INT8:
        case NX_UINT8:
        case NX_INT16:
        case NX_UINT16:
        case NX_INT32:
        case NX_UINT32:
        case NX_INT64:
        case NX_UINT64:
        case NX_FLOAT32:
        case NX_FLOAT64:
            yat::StringUtil::printf(&value, "%d",GetDouble());
            break;
        case NX_CHAR:
            value.append((char*) m_pAttrValue, m_iLen);
            break;
        default:
            break;
        }
    }

    return value;
}

//---------------------------------------------------------------------------
// NexusAttr::RawValue
//---------------------------------------------------------------------------
void * NexusAttr::RawValue() const
{
    return m_pAttrValue;
}

//---------------------------------------------------------------------------
// NexusAttr::CopyValue
//---------------------------------------------------------------------------
void NexusAttr::CopyValue(const NexusAttr &aAttr)
{
    if( aAttr.m_pAttrValue != NULL )
    {
        m_eDataType = aAttr.m_eDataType;
        m_iLen = aAttr.m_iLen;
        switch( aAttr.m_eDataType )
        {
        case NX_INT8:
        case NX_UINT8:
            m_pAttrValue = new char( *((char*) aAttr.m_pAttrValue) );
            break;
        case NX_INT16:
        case NX_UINT16:
            m_pAttrValue = new short( *((short*) aAttr.m_pAttrValue) );
            break;
        case NX_INT32:
        case NX_UINT32:
            m_pAttrValue = new int( *((int*) aAttr.m_pAttrValue) );
            break;
        case NX_INT64:
        case NX_UINT64:
            m_pAttrValue = new yat::int64( *((yat::int64*) aAttr.m_pAttrValue) );
            break;
        case NX_FLOAT32:
            m_pAttrValue = new float( *((float*) aAttr.m_pAttrValue) );
            break;
        case NX_FLOAT64:
            m_pAttrValue = new double( *((double*) aAttr.m_pAttrValue) );
            break;
        case NX_CHAR:
            m_pAttrValue = new char[aAttr.m_iLen];
            strncpy( (char*) m_pAttrValue, (char*) aAttr.m_pAttrValue, aAttr.m_iLen );
            break;
        default:
            break;
        }
    }
}



//=============================================================================
// NeXus Data set info
//
//=============================================================================

//---------------------------------------------------------------------------
// NexusDataSetInfo::DataTypeSize
//---------------------------------------------------------------------------
int NexusDataSetInfo::DataTypeSize(NexusDataType eDataType)
{
    switch( eDataType )
    {
    case NX_INT16:
    case NX_UINT16:
        return sizeof(short);
    case NX_INT32:
    case NX_UINT32:
        return sizeof(long);
    case NX_INT64:
    case NX_UINT64:
        return sizeof(yat::int64);
    case NX_FLOAT32:
        return sizeof(float);
    case NX_FLOAT64:
        return sizeof(double);
    default:
        return 1;
    }
}

//---------------------------------------------------------------------------
// Constructors
//---------------------------------------------------------------------------
NexusDataSetInfo::NexusDataSetInfo()
{
    m_eDataType = NX_NONE;
    m_iRank = 0;
    m_piDim = new int[MAX_RANK];
    m_iTotalRank = 0;
    m_piTotalDim = new int[MAX_RANK];
    m_piStart = NULL;
}

//---------------------------------------------------------------------------
// Destructors
//---------------------------------------------------------------------------
NexusDataSetInfo::~NexusDataSetInfo()
{
    Clear();
    delete [] m_piDim;
    delete [] m_piTotalDim;
    if( m_piStart )
        delete [] m_piStart;
}

//---------------------------------------------------------------------------
// NexusDataSetInfo::Clear
//---------------------------------------------------------------------------
void NexusDataSetInfo::Clear()
{
    m_eDataType = NX_NONE;
    m_iRank = 0;
}

//---------------------------------------------------------------------------
// NexusDataSetInfo::IsEmpty
//---------------------------------------------------------------------------
bool NexusDataSetInfo::IsEmpty() const
{
    if( NX_NONE == m_eDataType && 0 == m_iRank )
        return true;
    return false;
}

//---------------------------------------------------------------------------
// NexusDataSetInfo::SetInfo
//---------------------------------------------------------------------------
void NexusDataSetInfo::SetInfo(NexusDataType eDataType, int iRank)
{
    m_eDataType = eDataType;
    m_iRank = iRank;
}


//---------------------------------------------------------------------------
// NexusDataSetInfo::SetTotalDim
//---------------------------------------------------------------------------
void NexusDataSetInfo::SetTotalDim(int iTotalRank, int *piTotalDim)
{
    m_iTotalRank = iTotalRank;
    for( int iRank = 0; iRank < m_iTotalRank; iRank++ )
    {
        m_piTotalDim[iRank] = piTotalDim[iRank];
    }
}

//---------------------------------------------------------------------------
// NexusDataSetInfo::Size
//---------------------------------------------------------------------------
int NexusDataSetInfo::Size() const
{
    int iSize = 1;
    for( int i = 0; i < m_iRank; i++ )
        iSize *= m_piDim[i];
    return iSize;
}

//---------------------------------------------------------------------------
// NexusDataSetInfo::DatumSize
//---------------------------------------------------------------------------
unsigned int NexusDataSetInfo::DatumSize() const
{
    switch( m_eDataType )
    {
    case NX_INT16:
    case NX_UINT16:
        return sizeof(short);
    case NX_INT32:
    case NX_UINT32:
        return sizeof(long);
    case NX_INT64:
    case NX_UINT64:
        return sizeof(yat::int64);
    case NX_FLOAT32:
        return sizeof(float);
    case NX_FLOAT64:
        return sizeof(double);
    default: // CHAR, NX_INT8, NX_UINT8
        return 1;
    }
}

//---------------------------------------------------------------------------
// NexusDataSetInfo::StartArray
//---------------------------------------------------------------------------
int *NexusDataSetInfo::StartArray()
{
    if( !m_piStart )
    {
        m_piStart = new int[MAX_RANK];
        memset(m_piStart, 0, MAX_RANK * sizeof(int));
    }
    return m_piStart;
}

//=============================================================================
// NeXus Data set
//
//=============================================================================

//---------------------------------------------------------------------------
// Constructors
//---------------------------------------------------------------------------
NexusDataSet::NexusDataSet()
{
    m_pData = NULL;
    m_owner = true;
}

NexusDataSet::NexusDataSet(const NexusDataSet &dataset)
{
    m_owner = true;
    m_eDataType = dataset.m_eDataType;
    m_iRank = dataset.m_iRank;
    m_iTotalRank = dataset.m_iTotalRank;

    for( int i = 0; i < m_iRank; i++ )
    {
        m_piDim[i] = dataset.m_piDim[i];
    }

    for( int i = 0; i < m_iTotalRank; i++ )
    {
        m_piTotalDim[i] = dataset.m_piTotalDim[i];
    }

    if( dataset.m_piStart )
    {
        m_piStart = new int[MAX_RANK];
        memset(m_piStart, 0, MAX_RANK * sizeof(int));
        for( int i = 0; i < m_iRank; i++ )
            m_piStart[i] = dataset.m_piStart[i];
    }

    unsigned int size = BufferSize();
    yat::MemBuf buf(size);
    buf.put_bloc(dataset.m_pData, size);
    buf.set_owner(false);
    m_pData = (void*) buf.buf();
}

NexusDataSet::NexusDataSet(NexusDataType eDataType, void *pData, int iRank, int *piDim, int *piStart)
{
    m_owner = true;
    m_eDataType = eDataType;
    m_pData = pData;
    m_iRank = iRank;
    m_iTotalRank = iRank;

    for( int i = 0; i < iRank; i++ )
    {
        m_piDim[i] = piDim[i];
        m_piTotalDim[i] = piDim[i];
    }
    if( piStart )
    {
        m_piStart = new int[MAX_RANK];
        memset(m_piStart, 0, MAX_RANK * sizeof(int));
        for( int i = 0; i < iRank; i++ )
            m_piStart[i] = piStart[i];
    }
}

//---------------------------------------------------------------------------
// Destructors
//---------------------------------------------------------------------------
NexusDataSet::~NexusDataSet()
{
    Clear();
}

//---------------------------------------------------------------------------
// NexusDataSet::FreeData
//---------------------------------------------------------------------------
void NexusDataSet::FreeData()
{
    if( m_pData && m_owner )
    {
        switch( m_eDataType )
        {
        case NX_INT16:
        case NX_UINT16:
            delete [] (short *)m_pData;
            break;
        case NX_INT32:
        case NX_UINT32:
        case NX_FLOAT32:
            delete [] (long *)m_pData;
            break;
        case NX_INT64:
        case NX_UINT64:
            delete [] (yat::int64 *)m_pData;
            break;
        case NX_FLOAT64:
            delete [] (double *)m_pData;
            break;
        default:  // CHAR, NX_INT8, NX_UINT8
            delete [] (char *)m_pData;
            break;
        }
    }

    m_pData = NULL;
}

//---------------------------------------------------------------------------
// NexusDataSet::Clear
//---------------------------------------------------------------------------
void NexusDataSet::Clear()
{
    FreeData();
    NexusDataSetInfo::Clear();
}

//---------------------------------------------------------------------------
// NexusDataSet::SetOwner
//---------------------------------------------------------------------------
void NexusDataSet::SetOwner(bool this_has_ownership)
{
    m_owner = false;
}

//---------------------------------------------------------------------------
// NexusDataSet::Size
//---------------------------------------------------------------------------
unsigned int NexusDataSet::Size() const
{
    unsigned int uiSize=1;
    int iRank;
    for( iRank = 0; iRank < m_iRank; iRank++ )
        uiSize *= m_piDim[iRank];
    return uiSize;
}

//---------------------------------------------------------------------------
// NexusDataSet::MemSize
//---------------------------------------------------------------------------
unsigned int NexusDataSet::MemSize() const
{
    return Size() * DatumSize();
}

//---------------------------------------------------------------------------
// NexusDataSet::Alloc
//---------------------------------------------------------------------------
void NexusDataSet::Alloc()
{
    try {
        switch( m_eDataType )
        {
        case NX_INT16:
        case NX_UINT16:
            m_pData = new short[Size()];
            break;
        case NX_INT32:
        case NX_UINT32:
        case NX_FLOAT32:
            m_pData = new yat::int32[Size()];
            break;
        case NX_INT64:
        case NX_UINT64:
            m_pData = new yat::int64[Size()];
            break;
        case NX_FLOAT64:
            m_pData = new double[Size()];
            break;
        default:  // CHAR, NX_INT8, NX_UINT8
            m_pData = new char[Size()];
            break;
        }
    }
    catch (std::bad_alloc&) {
        throw NexusException("Can't allocate dataset buffer", "NexusDataSet::Alloc");
    }
}

//---------------------------------------------------------------------------
// NexusDataSet::SetDimension
//---------------------------------------------------------------------------
void NexusDataSet::SetDimension(int iDim, int iSize)
{
    if( iDim < MAX_RANK )
        m_piDim[iDim] = iSize;
}

//---------------------------------------------------------------------------
// NexusDataSet::SetData
//---------------------------------------------------------------------------
void NexusDataSet::SetData(const void *pData, NexusDataType eDataType, int iRank, int *piDimArray)
{
    // Clear data
    Clear();

    // Store properties
    m_iRank = iRank;
    m_eDataType = eDataType;
    for( iRank = 0; iRank < m_iRank; iRank++ )
        m_piDim[iRank] = piDimArray[iRank];

    // Allocate memory for data block
    Alloc();

    memcpy(m_pData, pData, MemSize());
}

//=============================================================================
//
// NeXus File Class - high level methods
//
//=============================================================================

//---------------------------------------------------------------------------
// NexusFile::WriteData
//---------------------------------------------------------------------------
void NexusFile::WriteData(const char *pcszName, void *pData,
                          NexusDataType eDataType, const std::vector<std::size_t>& shape,
                          bool bCreate)
{
    int iRank = shape.size();
    int dim[32];
    for( std::size_t i = 0; i < std::min(std::size_t(32), shape.size()); ++i )
        dim[i] = static_cast<int>(shape[i]);
    WriteData(pcszName, pData, eDataType, iRank, &dim[0], bCreate);
}

//-----------------------------------------------------------------------------
// NexusFile::SearchGroup
//-----------------------------------------------------------------------------
int NexusFile::SearchGroup(const char *pszGroupName, const char *pszClassName,
                           std::vector<std::string> *pvecPaths, const char *pszStartPath)
{
    static bool bReEnter = false;
    bool bFirstCall = !bReEnter;
    bReEnter = true;

    if( bFirstCall && pszStartPath )
        OpenGroupPath(pszStartPath);

    NexusItemInfo aItemInfo;
    int iRc = GetFirstItem(&aItemInfo);
    while( NX_OK == iRc )
    {
        if( aItemInfo.IsGroup() )
        {
            std::string strPath = pszStartPath;
            strPath += '/' + std::string(aItemInfo.ItemName()) + '<' + std::string(aItemInfo.ClassName()) + '>';
            if( stricmp(pszClassName, aItemInfo.ClassName()) == 0 &&
                    (!pszGroupName || stricmp(pszGroupName, aItemInfo.ItemName()) == 0) )
            {
                pvecPaths->push_back(strPath);
            }

            OpenGroup(aItemInfo.ItemName(), aItemInfo.ClassName());
            SearchGroup(pszGroupName, pszClassName, pvecPaths, PSZ(strPath));
            CloseGroup();
        }

        iRc = GetNextItem(&aItemInfo);
    }

    if( bFirstCall )
    {
        bReEnter = false;
        if( pvecPaths->size() == 0 )
            return NX_EOD;
    }
    return NX_OK;
}

//-----------------------------------------------------------------------------
// NexusFile::SearchDataSetFromAttr
//-----------------------------------------------------------------------------
int NexusFile::SearchDataSetFromAttr(const char *pszAttrName, std::vector<std::string> *pvecDataSets,
                                     const std::string &strAttrVal)
{
    return SearchDataSetFromAttrAndRank(pszAttrName, -1, pvecDataSets, strAttrVal);
}

//-----------------------------------------------------------------------------
// NexusFile::SearchFirstDataSetFromAttr
//-----------------------------------------------------------------------------
int NexusFile::SearchFirstDataSetFromAttr(const char *pszAttrName, std::string *pstrDataSet,
                                          const std::string &strAttrVal)
{
    std::vector<std::string> vecstrDataSet;
    int rc = SearchDataSetFromAttr(pszAttrName, &vecstrDataSet, strAttrVal);
    if( NX_OK == rc )
        *pstrDataSet = vecstrDataSet[0];
    else
        *pstrDataSet = "";
    return rc;
}

//-----------------------------------------------------------------------------
// NexusFile::SearchDataSetFromAttrAndRank
//-----------------------------------------------------------------------------
int NexusFile::SearchDataSetFromAttrAndRank(const char *pszAttrName, int iRank, std::vector<std::string> *pvecDataSets,
                                            const std::string &strAttrVal)
{
    std::string strName(pszAttrName);
    // browse through items into current group
    NexusItemInfo aItemInfo;
    int iRc = GetFirstItem(&aItemInfo);
    while( NX_OK == iRc )
    {
        if( aItemInfo.IsDataSet() )
        {
            bool bSelected = false;
            // Open data set and through its attributs
            NexusAttrInfo aAttrInfo;
            int iRc = GetFirstAttribute(&aAttrInfo, aItemInfo.ItemName());
            while( NX_OK == iRc )
            {
                if( yat::StringUtil::is_equal_no_case(strName, aAttrInfo.AttrName()) )
                {
                    if( !strAttrVal.empty() )
                    {
                        // Get attr value
                        std::string strValue = GetAttributeAsString(aAttrInfo);
                        if( strValue == strAttrVal )
                            // Ok add data set name
                            bSelected = true;
                    }
                    else
                        // Ok add data set name
                        bSelected = true;

                    break;
                }
                iRc = GetNextAttribute(&aAttrInfo);
            }

            if( bSelected )
            {
                // Check dataset rank
                if( iRank != -1 )
                {
                    bSelected = false;
                    bool bOpened = OpenDataSet(aItemInfo.ItemName(), false);
                    if( bOpened )
                    {
                        NexusDataSetInfo info;
                        GetDataSetInfo(&info, aItemInfo.ItemName());

                        if( iRank == info.Rank() )
                            bSelected = true;
                    }
                }
            }

            if( bSelected )
                pvecDataSets->push_back(aItemInfo.ItemName());
        }
        iRc = GetNextItem(&aItemInfo);
    }

    if( pvecDataSets->size() )
        return NX_OK;

    return NX_EOD;
}

//-----------------------------------------------------------------------------
// NexusFile::SearchFirstDataSetFromAttrAndRank
//-----------------------------------------------------------------------------
int NexusFile::SearchFirstDataSetFromAttrAndRank(const char *pszAttrName, int iRank, std::string *pstrDataSet,
                                                 const std::string &strAttrVal)
{
    std::vector<std::string> vecstrDataSet;
    int rc = SearchDataSetFromAttrAndRank(pszAttrName, iRank, &vecstrDataSet, strAttrVal);
    if( NX_OK == rc )
        *pstrDataSet = vecstrDataSet[0];
    else
        *pstrDataSet = "";
    return rc;
}

//-----------------------------------------------------------------------------
// NexusFile::SearchDataSetFromAttr
//-----------------------------------------------------------------------------
std::string NexusFile::GetAttributeAsString(const NexusAttrInfo &aAttrInfo)
{
    // Get attribute
    int iBufLen = 1024;
    char szBuf[1024];
    NexusDataType iDataType = aAttrInfo.DataType();
    GetAttribute(aAttrInfo.AttrName(), &iBufLen, szBuf, iDataType);

    std::ostringstream ossVal;
    // Setting floating point precision
    ossVal.precision(10);

    switch( iDataType )
    {
    case NX_INT8:
    {
        int iVal = *((char *)szBuf);
        ossVal << iVal;
    }
        break;
    case NX_UINT8:
    {
        int iVal = *((unsigned char *)szBuf);
        ossVal << iVal;
    }
        break;
    case NX_INT16:
        ossVal << *((short *)szBuf);
        break;
    case NX_UINT16:
        ossVal << *((unsigned short *)szBuf);
        break;
    case NX_INT32:
        ossVal << *((long *)szBuf);
        break;
    case NX_UINT32:
        ossVal << *((unsigned long *)szBuf);
        break;
    case NX_FLOAT32:
        ossVal << *((float *)szBuf);
        break;
    case NX_FLOAT64:
        ossVal << *((double *)szBuf);
        break;
    case NX_CHAR:
        ossVal.write(szBuf, iBufLen);
        break;
    default:
        //## Error...
        break;
    }
    return ossVal.str();
}

//-----------------------------------------------------------------------------
// NexusFile::HasAttribute
//-----------------------------------------------------------------------------
bool NexusFile::HasAttribute(const char *pszAttrName, const char *pcszDataSet, const std::string &strAttrVal)
{
    NexusAttrInfo attrInfo;
    int iStatus = GetFirstAttribute(&attrInfo, pcszDataSet);
    while( iStatus != NX_EOD )
    {
        if( strAttrVal.empty() && stricmp(attrInfo.AttrName(), pszAttrName) == 0 )
            return true;
        else if( stricmp(attrInfo.AttrName(), pszAttrName) == 0 )
        {
            std::string strValue = GetAttributeAsString(attrInfo);
            if( strValue == strAttrVal )
                return true;
        }
        iStatus = GetNextAttribute(&attrInfo);
    }
    return false;
}

//-----------------------------------------------------------------------------
// NexusFile::GetAttributeAsString
//-----------------------------------------------------------------------------
bool NexusFile::GetAttributeAsString(const char *pszAttrName, const char *pcszDataSet, std::string *pstrValue)
{
    NexusAttrInfo attrInfo;
    int iStatus = GetFirstAttribute(&attrInfo, pcszDataSet);
    while( iStatus != NX_EOD )
    {
        if( stricmp(attrInfo.AttrName(), pszAttrName) == 0 )
        {
            *pstrValue = GetAttributeAsString(attrInfo);
            return true;
        }
        iStatus = GetNextAttribute(&attrInfo);
    }
    return false;
}

//-----------------------------------------------------------------------------
// NexusFile::BuildAxisDict
//-----------------------------------------------------------------------------
bool NexusFile::BuildAxisDict(std::map<std::string, std::string> *pdict, const char *pszGroupPath, const char *pcszDataSet)
{
    // Open group and dataset if needed
    if( pszGroupPath )
        OpenGroupPath(pszGroupPath);
    if( pcszDataSet )
        OpenDataSet(pcszDataSet);

    // Check 'signal' attribute
    if( !HasAttribute("signal") )
    {
        throw NexusException(NO_SIGNAL_ATTR_ERR, "NexusFile::GetAxisList");
    }

    std::string strAxes;
    // Check for 'axes' attribute
    if( HasAttribute("axes") )
    {
        // The 'axes' attribute immediately give the axis std::list
        GetAttribute("axes", &strAxes);
    }

    if( !strAxes.empty() && !(yat::StringUtil::start_with(strAxes, "[") || yat::StringUtil::end_with(strAxes, '?') || yat::StringUtil::end_with(strAxes, ']')) )
    {
        // Retreive separator
        unsigned int iSepPos =  strAxes.find_first_of(",:");
        char cSep = ':';
        if( iSepPos != std::string::npos )
            cSep = strAxes[iSepPos];

        int iDim=1;
        std::string strAxeName;
        while( !strAxes.empty() )
        {
            std::ostringstream oss;
            oss << "axis_" << iDim << "_1";
            yat::StringUtil::extract_token_right(&strAxes, cSep, &strAxeName);
            (*pdict)[oss.str()] = strAxeName;
        }
    }
    else
    {
        // Search for datasets with 'axis' attribute
        NexusItemInfo aItemInfo;
        int iRc = GetFirstItem(&aItemInfo);
        while( NX_EOD != iRc )
        {
            if( aItemInfo.IsDataSet() )
            {
                OpenDataSet(aItemInfo.ItemName());
                if( HasAttribute("axis") )
                {
                    // We found a axis
                    // Get axis (dimension) and order (primary)
                    long lAxis, lPrimary=1;
                    GetAttribute("axis", &lAxis);
                    // Look for 'primary' attribute
                    if( HasAttribute("primary") )
                        GetAttribute("primary", &lPrimary);
                    // Fill dictionnary
                    std::ostringstream oss;
                    oss << "axis_" << lAxis << "_" << lPrimary;
                    (*pdict)[oss.str()] = aItemInfo.ItemName();
                }
                CloseDataSet();
            }
            iRc = GetNextItem(&aItemInfo);
        }
    }
    return true;
}

//-----------------------------------------------------------------------------
// NexusFile::BuildScanAxisDict
//-----------------------------------------------------------------------------
bool NexusFile::BuildScanAxisDict(std::map<std::string, std::string> *pdict, const char *pszGroupPath, const char *pcszDataSet)
{
    // Open group and dataset if needed
    if( pszGroupPath )
        OpenGroupPath(pszGroupPath);
    if( pcszDataSet )
        OpenDataSet(pcszDataSet);

    // Check 'signal' attribute
    if( !HasAttribute("signal") )
    {
        throw NexusException(NO_SIGNAL_ATTR_ERR, "NexusFile::GetAxisList");
    }

    std::string strAxes;
    // Check for 'axes' attribute
    if( HasAttribute("axes") )
    {
        // The 'axes' attribute immediately give the axis std::list
        GetAttribute("axes", &strAxes);
    }

    // Search for datasets with 'axis' attribute
    NexusItemInfo aItemInfo;
    int iRc = GetFirstItem(&aItemInfo);
    while( NX_EOD != iRc )
    {
        if( aItemInfo.IsDataSet() )
        {
            OpenDataSet(aItemInfo.ItemName());
            if( HasAttribute("axis") )
            {
                // We found a axis
                // Get axis (dimension) and order (primary)
                long lAxis, lPrimary=1;
                GetAttribute("axis", &lAxis);
                // Look for 'primary' attribute
                if( HasAttribute("primary") )
                    GetAttribute("primary", &lPrimary);

                // Look for trajectory pointer
                if( HasAttribute("trajectory") )
                {
                    std::string strTrajectory;
                    GetAttribute("trajectory", &strTrajectory);

                    // Check trajectory dataset but don't throw exception if the dataset is not present
                    if( OpenDataSet(PSZ(strTrajectory), false) )
                    {
                        CloseDataSet();
                        // Fill dictionnary with readed axis values (e.g. actuator at Soleil)
                        std::ostringstream oss1;
                        oss1 << "axis-readed_" << lAxis << "_" << lPrimary;
                        (*pdict)[oss1.str()] = aItemInfo.ItemName();

                        // Fill dictionnary with setted values  (e.g. trajectory at Soleil)
                        std::ostringstream oss2;
                        oss2 << "axis-setted_" << lAxis << "_" << lPrimary;
                        (*pdict)[oss2.str()] = strTrajectory;

                    }
                    else
                    {
                        // don't exists
                        yat::log_error("nex", "Trajectory dataset '%s' doesn't exists", PSZ(strTrajectory));
                        // Fill dictionnary
                        std::ostringstream oss;
                        oss << "axis_" << lAxis << "_" << lPrimary;
                        (*pdict)[oss.str()] = aItemInfo.ItemName();
                    }
                }
                else
                {
                    // Fill dictionnary
                    std::ostringstream oss;
                    oss << "axis_" << lAxis << "_" << lPrimary;
                    (*pdict)[oss.str()] = aItemInfo.ItemName();
                }
            }
            CloseDataSet();
        }
        iRc = GetNextItem(&aItemInfo);
    }
    return true;
}

//-----------------------------------------------------------------------------
// NexusFile::GetScanDim
//-----------------------------------------------------------------------------
int NexusFile::GetScanDim(const char *pszGroupPath)
{
    // Open group and dataset if needed
    if( pszGroupPath )
        OpenGroupPath(pszGroupPath);

    int iMaxAxis = -1;
    // Search for datasets with 'axis' attribute
    NexusItemInfo aItemInfo;
    int iRc = GetFirstItem(&aItemInfo);
    while( NX_EOD != iRc )
    {
        if( aItemInfo.IsDataSet() )
        {
            OpenDataSet(aItemInfo.ItemName());
            if( HasAttribute("axis") )
            {
                // We found a axis
                // Get axis (dimension) and order (primary)
                long lAxis=0;
                GetAttribute("axis", &lAxis);
                if( lAxis > iMaxAxis )
                    iMaxAxis = (int) lAxis;
            }
            CloseDataSet();
        }
        iRc = GetNextItem(&aItemInfo);
    }

    if( 0 == iMaxAxis )
        // Time scan
        return 1;

    if( -1 == iMaxAxis )
        // No axis dataset found => not a scan
        return 0;
    return iMaxAxis;
}

//=============================================================================
// NeXus exceptions
//
//=============================================================================

//---------------------------------------------------------------------------
// NexusException::PrintMessage
//---------------------------------------------------------------------------
void NexusException::PrintMessage()
{
    dump();
}

//---------------------------------------------------------------------------
// NexusException::GetMessage
//---------------------------------------------------------------------------
void NexusException::GetMsg(char *pBuf, int iLen)
{
    std::string strMsg;
    for (size_t i = 0; i < errors.size(); i++) \
        strMsg += errors[i].reason + ". Desc.: " + errors[i].desc + ". Origin: " + errors[i].origin;
    strncpy(pBuf, strMsg.c_str(), iLen - 1);
}

//-----------------------------------------------------------------
// get_version
//-----------------------------------------------------------------
const char* get_version()
{
    static bool s_reenter = false;
    if( !s_reenter )
        yat::Version::add_dependency( YAT_XSTR(HDF_PROJECT_NAME), YAT_XSTR(HDF_PROJECT_VERSION) );
    s_reenter = true;
    
    return YAT_XSTR(PROJECT_VERSION);
}
//-----------------------------------------------------------------
// get_name
//-----------------------------------------------------------------
const char* get_name()
{
    return YAT_XSTR(PROJECT_NAME);
}

}
