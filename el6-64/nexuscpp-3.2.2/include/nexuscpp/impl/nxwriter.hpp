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

#include <typeinfo>

//==============================================================================
// DatasetWriter
//==============================================================================

template <>
inline void DatasetWriter::SetDataType<int>()
{
  m_NexusDataType = NX_INT32;
  AdjustSizes<int>();
}

template <>
inline void DatasetWriter::SetDataType<unsigned int>()
{
  m_NexusDataType = NX_UINT32;
  AdjustSizes<unsigned int>();
}

template <>
inline void DatasetWriter::SetDataType<char>()
{
  m_NexusDataType = NX_INT8;
  AdjustSizes<char>();
}

template <>
inline void DatasetWriter::SetDataType<unsigned char>()
{
  m_NexusDataType = NX_UINT8;
  AdjustSizes<unsigned char>();
}

template <>
inline void DatasetWriter::SetDataType<short>()
{
  m_NexusDataType = NX_INT16;
  AdjustSizes<short>();
}

template <>
inline void DatasetWriter::SetDataType<unsigned short>()
{
  m_NexusDataType = NX_UINT16;
  AdjustSizes<unsigned short>();
}

template <>
inline void DatasetWriter::SetDataType<long>()
{
  if( sizeof(long) == 4 )
  {
    m_NexusDataType = NX_INT32;
  }
  else
  {
    m_NexusDataType = NX_INT64;
  }
  AdjustSizes<long>();
}

template <>
inline void DatasetWriter::SetDataType<unsigned long>()
{
  if( sizeof(unsigned long) == 4 )
  {
    m_NexusDataType = NX_UINT32;
  }
  else
  {
    m_NexusDataType = NX_UINT64;
  }
  AdjustSizes<unsigned long>();
}

#ifndef __x86_64
template <>
inline void DatasetWriter::SetDataType<yat::int64>()
{
  m_NexusDataType = NX_INT64;
  AdjustSizes<yat::int64>();
}

template <>
inline void DatasetWriter::SetDataType<yat::uint64>()
{
  m_NexusDataType = NX_UINT64;
  AdjustSizes<yat::uint64>();
}
#endif

template <>
inline void DatasetWriter::SetDataType<float>()
{
  m_NexusDataType = NX_FLOAT32;
  AdjustSizes<float>();
}

template <>
inline void DatasetWriter::SetDataType<double>()
{
  m_NexusDataType = NX_FLOAT64;
  AdjustSizes<double>();
}

template <class TYPE> 
void DatasetWriter::SetDataType()
{
  throw yat::Exception("UNRECOGNIZED TYPE", "Unable to map <TYPE> to Nexus type", "DatasetWriter::SetDataType");
}

//------------------------------------------------------------------------------
// DatasetWriter::AdjustSizes
//------------------------------------------------------------------------------
template <class TYPE>
void DatasetWriter::AdjustSizes()
{
  m_ulDataItemSize = sizeof(TYPE);
  
  for( unsigned int i = 0; i < m_nTotalRank; i++ )
  {
    m_ulDataItemSize *= m_aiDataItemDim[i];
  }
  yat::int64 totalSize = sizeof(TYPE);
  for( unsigned int i = 0; i < m_nTotalRank; i++ )
  {
    totalSize *= m_aiTotalDim[i];
  }
  if( totalSize < 0 )
  {
    // The result is negative in case of an unlimited dataset (first dimension fixed to '-1')
    m_ui64TotalSize = -totalSize;
  }
  else
  {
    m_ui64TotalSize = totalSize;
  }
}

//==============================================================================
// GenericDatasetWriter
//==============================================================================
//------------------------------------------------------------------------------
//  GenericDatasetWriter:: GenericDatasetWriter
//------------------------------------------------------------------------------
template <class TYPE> 
GenericDatasetWriter<TYPE>::GenericDatasetWriter(const DataShape &shapeDataItem, const DataShape &shapeMatrix, yat::uint16 usMaxMB)
: DatasetWriter(shapeDataItem, shapeMatrix, usMaxMB)
{
  SetDataType<TYPE>();
}

//==============================================================================
// AxisDatasetWriter
//==============================================================================
//------------------------------------------------------------------------------
// AxisDatasetWriter::AxisDatasetWriter
//------------------------------------------------------------------------------
template <class TYPE> 
AxisDatasetWriter<TYPE>::AxisDatasetWriter(int iDim, int iSize, int iOrder): DatasetWriter()
{
  // This is a 1D dataset
  DataShape shapeMatrix;
  shapeMatrix.push_back(iSize);
  
  // Scalar data item => Data Item shape is null
  SetShapes(g_empty_shape, shapeMatrix);
  SetDataType<TYPE>();
  
  AddIntegerAttribute("order", long(iDim));
  AddIntegerAttribute("primary", long(iOrder));
}

//------------------------------------------------------------------------------
// AxisDatasetWriter::PushPosition
//------------------------------------------------------------------------------
template <class TYPE> 
void AxisDatasetWriter<TYPE>::PushPosition(TYPE TValue)
{
  PushData((const void *)&TValue);
}

//==============================================================================
// SignalDatasetWriter
//==============================================================================
//------------------------------------------------------------------------------
// SignalDatasetWriter::SignalDatasetWriter
//------------------------------------------------------------------------------
template <class TYPE> 
SignalDatasetWriter<TYPE>::SignalDatasetWriter(const DataShape &shapeData, const DataShape &shapeMatrix, int iSignal): DatasetWriter(shapeData, shapeMatrix)
{
  SetShapes(shapeData, shapeMatrix);
  SetDataType<TYPE>();
}

//------------------------------------------------------------------------------
// SignalDatasetWriter::PushSignal
//------------------------------------------------------------------------------
template <class TYPE> 
void SignalDatasetWriter<TYPE>::PushSignal(TYPE *pValue)
{
  PushData((const void *)pValue);
}
