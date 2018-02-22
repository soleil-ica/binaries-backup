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

//==============================================================================
// DataStreamer
//==============================================================================
//------------------------------------------------------------------------------
// DataStreamer::PushData
//------------------------------------------------------------------------------
template <class TYPE> void DataStreamer::PushData(const std::string &sItemName, 
                                                  const TYPE *pData, unsigned int nCount)
{
  NX_SCOPE_DBG("BufferedData::PushData");

  if( 0 == nCount )
    throw yat::Exception("ERROR", "Can't write 0 item!!!", "DataStreamer::PushData");
  
  // Thread safety
  yat::AutoMutex<> _lock(m_mtxLock);

  DataItemInfo& ItemInfo = GetDataItemInfo(sItemName);
  NX_DBG("Item: " << sItemName << " : " << nCount);

  bool bContinue = true;
  TYPE *pToPush = (TYPE *)pData;
  std::size_t nToPush = nCount;

  while( bContinue )
  {
    if( !PrivIsBufferOpen() )
    {
      PrivOpenNewbuffer();
    }

    // Get data c-type at first push for this data item
    if( NX_NONE == ItemInfo.ptrDatasetWriter->DataType() )
      ItemInfo.ptrDatasetWriter->SetDataType<TYPE>();

    std::size_t nPush = 0;

    if( ItemInfo.mbPendingData.is_empty() && nToPush > 0 )
    {
      NX_DBG("No pending data");
      std::size_t uiCurrentBufferPushedData = ItemInfo.uiPushCount - (m_nBufferCount * m_nBufferSize);
      if( uiCurrentBufferPushedData + nToPush <= m_nBufferSize )
      {
        nPush = nToPush;
      }
      else
      {
        nPush = m_nBufferSize - uiCurrentBufferPushedData;
      }
      
      if( nCount > 1 )
      {
        NX_DBG("Push " << nPush << " / " << nToPush);
      }

      if( nPush == nCount && ItemInfo.ptrDatasetWriter->DataItemShape().size() >= 1 )
      { // Give the data's ownership to the Datasetwriter object, no copy is made for efficiency
        PrivPushDataItems(ItemInfo, pToPush, nPush, true);
      }
      else
      { // Push as much data as possible, data may be copied
        PrivPushDataItems(ItemInfo, pToPush, nPush, false);
      }
    }

    if( nPush < nToPush && PrivIsBufferOpen() )
    {
      // All data was not pushed and current buffer still open, store the rest
      std::size_t nPendingData = nToPush - nPush;
      NX_DBG("Add pending data: " << nPendingData << " items.");
      char *pToStore = (char *)pData + (nPush * ItemInfo.ptrDatasetWriter->DataItemSize());
      ItemInfo.mbPendingData.put_bloc(pToStore, nPendingData * ItemInfo.ptrDatasetWriter->DataItemSize());
      ItemInfo.nPendingData += nPendingData;
      // Data not pushed is now in the pending data buffer
      nToPush = 0;
    }
    else if( ItemInfo.mbPendingData.is_empty() )
    { // All data is pushed => exits the 'while' loop
      bContinue = false;
    }

    // Check if current buffer file is already completed, close it if it's completed (for all 
    // DataItem objects)
    BufferingControl();

    if( false == ItemInfo.mbPendingData.is_empty() && PrivIsBufferOpen() )
    { // There is pending data but buffer cannot be closed because other DataItem objects
      // have not already push all necessary data
      NX_DBG("There is pending data but buffer cannot be closed");
      bContinue = false;
    }
  }
}

//------------------------------------------------------------------------------
// DataStreamer::PushAxisData
//------------------------------------------------------------------------------
template <class TYPE> void DataStreamer::PushAxisData(const std::string &sName, TYPE TValue)
{
  DataStreamer::PushData(sName, &TValue);
}
