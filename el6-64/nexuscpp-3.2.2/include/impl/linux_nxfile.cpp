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

#include <iostream>
#include <vector>
#include <deque>
#include <set>
#include <list>
#include <algorithm>

#include <H5Cpp.h>

#include <yat/utils/String.h>
#include <yat/utils/Logging.h>
#include <yat/memory/MemBuf.h>
#include <yat/Version.h>
#include <yat/file/FileName.h>
#include <yat/memory/UniquePtr.h>

#include "nexuscpp/h5zlz4.h"
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


/// helper class that prints debugging messages on standard output

class ScopePrinter {

    static int offset;

public:

    ScopePrinter(const std::string& where) : m_where(where) {
        dump(std::string(offset++, '.'));
    }

    ~ScopePrinter() {
        // dump("out");
        offset--;
    }

    void dump(const std::string& when) {
        std::cout << "[NexusCpp] " << when << " " << m_where << std::endl;
    }

private:

    std::string m_where;

};


int ScopePrinter::offset = 0;


#ifdef NXFILE_DEBUG_SCOPE
    #define NXFILE_COUT(data) ScopePrinter __scope_printer(data)
#else
    #define NXFILE_COUT(data) do {} while (0)
#endif

#define NXFILEIMPL_COUT(data) NXFILE_COUT("NexusFileImpl::" data)


namespace nxcpp
{

const char* UNSUPPORTED_NEXUS_DATATYPE = "Unsupported NeXus datatype";
const char* UNSUPPORTED_HDF5_DATATYPE = "Unsupported HDF5 datatype";
const char* NO_FG_OPEN = "No group nor file opened";
const char* NO_GROUP_OPEN = "No group opened";
const char* NO_DATASET_OPEN = "No dataset opened";

const H5Z_filter_t HDF5_FILTER_LZ4 = 32004;  /// (id assigned by the hdfgroup to the LZ4 compression filter)

const std::string classAttributeName = "NX_class"; // attribute name storing group class

//=======================================
// Error management
//
//=======================================

namespace // anonymous namespace managing errors
{
std::list<yat::Error> lastErrorStack;
yat::Mutex stackMutex;

/** transform an HDF5 exception to a nexus exception, all the stack previously filled
        * is forwarded.
        * \NOTE should we add getFuncName & getDetailMsg from the H5 exception?
        */

NexusException castException(const H5::Exception& /*err*/, const std::string& desc, const std::string& origin)
{
    yat::MutexLock lock(stackMutex);
    NexusException nxerr(PSZ(desc), PSZ(origin));
    nxerr.errors.insert(nxerr.errors.end(), lastErrorStack.begin(), lastErrorStack.end());
    return nxerr;
}

/** Callback running through the HDF5 error stack adding
        * converting HDF5 error data to yat Errors
        */

herr_t walkStack(unsigned int /*n*/, const H5E_error2_t *err_desc, void* /*client_data*/)
{
    lastErrorStack.push_back(yat::Error("HDF5_API_ERROR", err_desc->desc, err_desc->func_name));
    return 0;
}

/** Callback setting the error stack when a HDF5 error occurs. The whole stack is stored
        * until an exception is thrown. Note that we do not throw here in order to let the HDF5
        * api choose when an Exception is thrown.
        */

herr_t fillStack(hid_t estack, void* client_data)
{
    yat::MutexLock lock(stackMutex);
    lastErrorStack.clear();
    H5Ewalk2(estack, H5E_WALK_DOWNWARD, walkStack, client_data);
    return 0;
}

/** Initialize the callback "printing" HDF5 errors.
        * Actually, it fills an error stack.
        */

void initErrorManagement()
{
    H5Eset_auto2(H5E_DEFAULT, fillStack, NULL);
}
}


//=======================================
// Newly defined Structures
//
//=======================================

struct ChildInfo
{

    typedef unsigned Type;
    static const Type NO_TYPE = 0x00;
    static const Type UNKNOWN_TYPE = 0x01;
    static const Type GROUP_TYPE = 0x02;
    static const Type DATASET_TYPE = 0x04;
    static const Type LINK_TYPE = 0x08;
    static const Type ANY_TYPE = ~0x0;
    static const Type ANY_KNOWN_TYPE = ~UNKNOWN_TYPE;

    ChildInfo(const std::string& name = std::string(), Type type = NO_TYPE) : name(name), type(type) {}

    std::string name;
    Type type;

};

//=======================================
// Tool for conversion HDF5 / NeXus
//
//=======================================

ChildInfo::Type h5ChildType2nexusChildType(H5G_obj_t hdf5_type)
{
    switch(hdf5_type)
    {
    case H5G_DATASET: return ChildInfo::DATASET_TYPE;
    case H5G_GROUP: return ChildInfo::GROUP_TYPE;
    case H5G_LINK:
    case H5G_UDLINK: return ChildInfo::LINK_TYPE;
    default: return ChildInfo::UNKNOWN_TYPE;
    }
}

H5::DataType nexusData2h5Data(NexusDataType datatype)
{
    switch(datatype)
    {
    case NX_INT8: return H5::PredType::NATIVE_INT8;
    case NX_UINT8: return H5::PredType::NATIVE_UINT8;
    case NX_INT16: return H5::PredType::NATIVE_INT16;
    case NX_UINT16: return H5::PredType::NATIVE_UINT16;
    case NX_INT32: return H5::PredType::NATIVE_INT32;
    case NX_UINT32: return H5::PredType::NATIVE_UINT32;
    case NX_INT64: return H5::PredType::NATIVE_INT64;
    case NX_UINT64: return H5::PredType::NATIVE_UINT64;
    case NX_FLOAT32: return H5::PredType::NATIVE_FLOAT;
    case NX_FLOAT64: return H5::PredType::NATIVE_DOUBLE;
    case NX_CHAR: return H5::PredType::C_S1;
    default: throw NexusException(UNSUPPORTED_HDF5_DATATYPE, "nexusData2h5Data");
    }
}

/// binding between native types & nexus types (unused for now)
/// WARNING reverse operation (type -> nexus) is possible but int8_t and char correspond to the same type
template<NexusDataType TYPE> struct native_type;
template<> struct native_type<NX_INT8> { typedef yat::int8 type; };
template<> struct native_type<NX_UINT8> { typedef yat::uint8 type; };
template<> struct native_type<NX_INT16> { typedef yat::int16 type; };
template<> struct native_type<NX_UINT16> { typedef yat::uint16 type; };
template<> struct native_type<NX_INT32> { typedef yat::int32 type; };
template<> struct native_type<NX_UINT32> { typedef yat::uint32 type; };
template<> struct native_type<NX_INT64> { typedef yat::int64 type; };
template<> struct native_type<NX_UINT64> { typedef yat::uint64 type; };
template<> struct native_type<NX_FLOAT32> { typedef float type; };
template<> struct native_type<NX_FLOAT64> { typedef double type; };
template<> struct native_type<NX_CHAR> { typedef char type; };

NexusDataType h5Data2nexusData(const H5::DataType& datatype)
{
    H5T_class_t hclass = datatype.getClass();
    if (hclass == H5T_INTEGER)
    {
        /// check if the integer is signed or not
        H5T_sign_t sign = H5Tget_sign(datatype.getId());
        bool isSigned = sign == H5T_SGN_2;
        if (sign != H5T_SGN_2 && sign != H5T_SGN_NONE)
            throw NexusException("wrong sign for the given datatype", "h5Data2nexusData");
        /// don't care about order: H5T_order_t order = iDataType.getOrder();
        /// get the number of bytes required by the int
        switch( datatype.getSize() )
        {
          case 1: return isSigned ? NX_INT8 : NX_UINT8;
          case 2: return isSigned ? NX_INT16 : NX_UINT16;
          case 4: return isSigned ? NX_INT32 : NX_UINT32;
          case 8: return isSigned ? NX_INT64 : NX_UINT64;
        }
    }
    else if (hclass == H5T_FLOAT)
    {
        /// don't care about order nor sign
        /// get the number of bytes required by the float
        switch(datatype.getSize())
        {
          case 4: return NX_FLOAT32;
          case 8: return NX_FLOAT64;
        }
    }
    else if (hclass == H5T_STRING)
        return NX_CHAR;
    throw NexusException(UNSUPPORTED_HDF5_DATATYPE, "h5Data2nexusData");
}

//=======================================
// NeXus Naming Utils
//
//=======================================

namespace {

/// allows null str to be passed to methods requiring a std::string
inline std::string ToString(const char* str)
{
    return str == NULL ? std::string() : std::string(str);
}

/// replace forbbiden chars by underscores
std::string NormalizedName(const std::string& name)
{
    static const std::string fordidden_str("/\\ *\"',;:!?");
    static const std::set<char> forbidden(fordidden_str.begin(), fordidden_str.end());
    std::string result = name;
    for (std::string::iterator it = result.begin(), end = result.end() ; it != end ; ++it)
        if (forbidden.count(*it))
            *it = '_';
    return result;
}

/// return the last component of a path
std::string BaseName(const std::string& path)
{
    size_t start = path.find_last_of('/');
    return start != std::string::npos ? path.substr(start + 1) : path;
}

/// extract group & class names from a formatted string (className is left empty if not formatted)
std::pair<std::string, std::string> ExtractNXGroupName(const std::string& name)
{
    std::pair<std::string, std::string> out(name, "");
    /// test syntax Name:Class
    size_t pos = name.find(':');
    if (pos != std::string::npos)
    {
        out.first = name.substr(0, pos);
        out.second = name.substr(pos + 1);
    }
    else /// test syntaxes Name<Class>, Name(Class) or Name{Class}
    {
        static const std::string tokens("<>(){}");
        for(size_t i=0 ; i < 3 ; i++)
        {
            char left = tokens[2*i], right = tokens[2*i+1];
            yat::StringUtil::extract_token_right(&out.first, left, right, &out.second);

            if (!out.second.empty()) // extraction succeed
            {
                if( !yat::StringUtil::start_with(out.second, "NX") )
                    out.second = "NX" + out.second;
                break;
            }
        }
    }
    yat::StringUtil::trim(&out.first);
    yat::StringUtil::trim(&out.second);
    return out;
}

}

//=======================================
// HDF5 Utils
// (avoiding H5::Exception)
//=======================================

namespace {

std::vector<int> GetDimensions(const H5::DataSpace& space)
{
    hsize_t dims[MAX_DATASET_NDIMS]; /// prepare a buffer of hsize_t
    int ndims = space.getSimpleExtentDims(dims); /// fillup the buffer
    return std::vector<int>(dims, dims + ndims); /// downcast hsize_t to int (beware to crappy warnings)
}

std::string GetObjectName(const H5::H5Object* obj)
{
    try
    {
        return obj->getObjName();
    }
    NEXUS_CATCH("can't get object name", "GetObjectName");
}

// child utils

bool HasGroup(H5::CommonFG* fg, const std::string& name)
{
    try
    {
        return fg->childObjType(name) == H5O_TYPE_GROUP;
    }
    catch (const H5::Exception&)
    {
        return false;
    }
}

hsize_t CountChilds(const H5::CommonFG* fg)
{
    try
    {
        return fg->getNumObjs();
    }
    NEXUS_CATCH("can't get number of childs", "CountChilds");
}

std::string GetChildName(const H5::CommonFG* fg, hsize_t child)
{
    try
    {
        return fg->getObjnameByIdx(child);
    }
    NEXUS_CATCH("can't get child name", "GetChildName");
}

ChildInfo::Type GetChildType(const H5::CommonFG* fg, hsize_t child)
{
    try
    {
        return h5ChildType2nexusChildType(fg->getObjTypeByIdx(child));
    }
    NEXUS_CATCH("can't get child type", "GetChildType");
}

struct ChildIterator : public std::iterator<std::input_iterator_tag, ChildInfo>
{

    static ChildIterator begin(const H5::CommonFG* loc) { return ChildIterator(loc, 0); }
    static ChildIterator end(const H5::CommonFG* loc) { return ChildIterator(loc, -1); }

    ChildIterator() : m_loc(NULL), m_current(-1) {}
    ChildIterator(const ChildIterator& other) : m_loc(other.m_loc), m_current(other.m_current) {}
    ChildIterator(const H5::CommonFG* loc, hsize_t idx) : m_loc(loc), m_current(0) { setIndex(idx); }

    bool operator==(const ChildIterator& other) const {return m_current == other.m_current && m_loc == other.m_loc;}
    bool operator!=(const ChildIterator& other) const {return !(*this == other);}

    ChildIterator operator++(int) { ChildIterator tmp(*this); operator++(); return tmp; }
    ChildIterator& operator++() { setIndex(m_current + 1); return *this; }

    ChildInfo operator*() const { return ChildInfo(GetChildName(m_loc, m_current), GetChildType(m_loc, m_current)); }

    const H5::CommonFG* loc() const { return m_loc; }

private:

    void setIndex(hsize_t index)
    {
        hsize_t maxChilds = CountChilds(m_loc);
        m_current = (index > maxChilds) ? maxChilds : index;
    }

    const H5::CommonFG* m_loc;
    hsize_t m_current;

};

struct ChildFilterIterator : public std::iterator<std::input_iterator_tag, std::string>
{

    static ChildFilterIterator begin(const H5::CommonFG* loc, ChildInfo::Type mask) { return ChildFilterIterator(loc, 0, mask); }
    static ChildFilterIterator end(const H5::CommonFG* loc, ChildInfo::Type mask) { return ChildFilterIterator(loc, -1, mask); }

    ChildFilterIterator() : m_loc(NULL), m_current(-1), m_mask(ChildInfo::NO_TYPE) {}
    ChildFilterIterator(const ChildFilterIterator& other) : m_loc(other.m_loc), m_current(other.m_current), m_mask(other.m_mask) {}
    ChildFilterIterator(const H5::CommonFG* loc, hsize_t idx, ChildInfo::Type mask) : m_loc(loc), m_current(0), m_mask(mask) { setIndex(idx); }

    bool operator==(const ChildFilterIterator& other) const {return m_current == other.m_current && m_loc == other.m_loc && m_mask == other.m_mask;}
    bool operator!=(const ChildFilterIterator& other) const {return !(*this == other);}

    ChildFilterIterator operator++(int) { ChildFilterIterator tmp(*this); operator++(); return tmp; }
    ChildFilterIterator& operator++() { setIndex(m_current + 1); return *this;}

    std::string operator*() const { return GetChildName(m_loc, m_current); }

    const H5::CommonFG* loc() const { return m_loc; }

private:

    void setIndex(hsize_t index)
    {
        hsize_t maxChilds = CountChilds(m_loc);
        m_current = (index > maxChilds) ? maxChilds : index;
        while (m_current != maxChilds && !(GetChildType(m_loc, m_current) & m_mask))
            m_current++;
    }

    const H5::CommonFG* m_loc;
    hsize_t m_current;
    ChildInfo::Type m_mask;

};

// attributes utils

int CountAttributes(const H5::H5Location* loc)
{
    try
    {
        return loc->getNumAttrs();
    }
    NEXUS_CATCH("can't get number of attributes", "CountAttributes");
}

struct AttributeIterator : public std::iterator<std::input_iterator_tag, H5::Attribute>
{

    static AttributeIterator begin(const H5::H5Location* loc) { return AttributeIterator(loc, 0); }
    static AttributeIterator end(const H5::H5Location* loc) { return AttributeIterator(loc, -1); }

    AttributeIterator() : m_loc(NULL), m_current(0) {}
    AttributeIterator(const AttributeIterator& other) : m_loc(other.m_loc), m_current(other.m_current) {}
    AttributeIterator(const H5::H5Location* loc, unsigned int idx) : m_loc(loc), m_current(idx) { setIndex(idx); }

    bool operator==(const AttributeIterator& other) const {return m_current == other.m_current && m_loc == other.m_loc;}
    bool operator!=(const AttributeIterator& other) const {return !(*this == other);}

    AttributeIterator operator++(int) { AttributeIterator tmp(*this); operator++(); return tmp; }
    AttributeIterator& operator++() { setIndex(m_current + 1); return *this;}

    H5::Attribute operator*() const { return m_loc->openAttribute(m_current); } // assuming the function can't throw H5::Exception

    const H5::H5Location* loc() const { return m_loc; }

private:

    void setIndex(unsigned index)
    {
        unsigned maxChilds = (unsigned)CountAttributes(m_loc);
        m_current = (index > maxChilds) ? maxChilds : index;
    }

    const H5::H5Location* m_loc;
    unsigned m_current;

};

H5::Attribute OpenAttribute(H5::H5Location* loc, const std::string& name, bool caseSensitive)
{
    std::string msg = "attribute not found: " + name;
    try
    {
        /// case sensitive localisation
        NXFILE_COUT("OpeningAttribute " + name);
        return loc->openAttribute(name);
    }
    catch (const H5::Exception& err) /// attribute not found
    {
        if (caseSensitive)
            throw castException(err, msg, "OpenAttribute");
        try
        {
        /// case insensitive localisation
        const char* c_name = name.c_str();
        for(AttributeIterator it = AttributeIterator::begin(loc), end = AttributeIterator::end(loc); it != end ; ++it) {
            H5::Attribute attr(*it);
            std::string attrName = attr.getName();
            if (stricmp(c_name, attrName.c_str()) == 0)
                return attr;
        }
        } catch (const H5::Exception& err2)
        {
            throw castException(err2, "Error Iterating on Attributes", "OpenAttribute[ShouldNeverHappen!]");
        }
    }
    throw NexusException(msg.c_str(), "OpenAttribute");
}

int attributeMaxLength(const H5::Attribute& attribute) {

    H5::DataType memDataType = attribute.getDataType();
    if (memDataType.getClass() == H5T_STRING) {
        H5::StrType strDataType = attribute.getStrType();
        return (int)strDataType.getSize();
    } else {
        H5::DataSpace dataSpace = attribute.getSpace();
        std::vector<int> dataDims = GetDimensions(dataSpace);
        return dataDims.empty() ? 0 : dataDims[0]; /// update attribute length (0 for scalar)
    }
}

void ReadAttribute(H5::H5Location* loc, const std::string& name,
                   void* data, int* length, NexusDataType* dataType, bool caseSensitive)
{
    try
    {
        H5::Attribute attr = OpenAttribute(loc, name, caseSensitive);
        H5::DataType memDataType = attr.getDataType();
        *dataType = h5Data2nexusData(memDataType); /// update nexus data type
        if (memDataType.getClass() == H5T_STRING) {
            std::string buf;
            attr.read(memDataType, buf);
            int new_length = (int)::strnlen(buf.data(), buf.size());
            if (*length < new_length)
                throw NexusException("Attribute too long", "ReadAttribute");
            *length = new_length;
            memcpy(data, buf.data(), new_length);
        } else {
            H5::DataSpace dataSpace = attr.getSpace();
            std::vector<int> dataDims = GetDimensions(dataSpace);
            *length = dataDims.empty() ? 0 : dataDims[0]; /// update attribute length (0 for scalar)
            attr.read(memDataType, data); /// fill data
        }
    }
    NEXUS_CATCH("can't read attribute " + name, "ReadAttribute");
}

void ReadAttribute(H5::H5Location* loc, const std::string& name,
                   void* data, int* length, NexusDataType dataType, bool caseSensitive)
{
    try
    {
        H5::Attribute attr = OpenAttribute(loc, name, caseSensitive);
        H5::DataType memDataType = attr.getDataType();
        if (h5Data2nexusData(memDataType) != dataType)
            throw NexusException("wrong datatype", "ReadAttribute");
        if (memDataType.getClass() == H5T_STRING) {
            std::string buf;
            attr.read(memDataType, buf);
            int new_length = (int)::strnlen(buf.data(), buf.size());
            if (*length < new_length)
                throw NexusException("Attribute too long", "ReadAttribute");
            *length = new_length;
            memcpy(data, buf.data(), new_length);
        } else {
            H5::DataSpace dataSpace = attr.getSpace();
            std::vector<int> dataDims = GetDimensions(dataSpace);
            *length = dataDims.empty() ? 0 : dataDims[0]; /// update attribute length (0 for scalar)
            attr.read(memDataType, data); /// fill data
        }
    }
    NEXUS_CATCH("can't read attribute " + name, "ReadAttribute");
}

void CreateAttribute(H5::H5Location* loc, const std::string& name, void* data, int length, NexusDataType dataType) {
    try
    {
        H5::DataSpace dataSpace;
        H5::DataType memDataType;
        if (dataType == NX_CHAR) {
            memDataType = H5::StrType(0, length == 0 ? 1 : length);
        } else {
            memDataType = nexusData2h5Data(dataType);
            if (length > 1) {
                hsize_t dataDims[] = {length};
                dataSpace = H5::DataSpace(1, dataDims);
            }
        }
        H5::Attribute attr = loc->createAttribute(name, memDataType, dataSpace);
        attr.write(memDataType, data);
    }
    NEXUS_CATCH("can't create attribute " + name, "CreateAttribute");
}

void SetAttribute(H5::H5Location* loc, const std::string& name, void* data, int length, NexusDataType dataType)
{
    try {
        H5::Attribute attr = OpenAttribute(loc, name, true);
        loc->removeAttr(attr.getName()); // NOTE: getName is safer using insensitive opening
    }
    catch(const NexusException&) { // attribute does not exist
        // no-op
    }
    NEXUS_CATCH("can't remove attribute " + name, "SetAttribute");
    CreateAttribute(loc, name, data, length, dataType);
}

std::string ReadGroupClass(H5::H5Location* loc)
{
    std::string result;
    try
    {
        //*
        char buf[1024];
        int len = 1024;
        ReadAttribute(loc, classAttributeName, buf, &len, NX_CHAR, false);
        result.assign(buf, len);
        /*/
            try
            {
                H5::Attribute attr = OpenAttribute(loc, classAttributeName, true);
                H5::DataType memDataType = attr.getDataType();
                if (memDataType.getClass() == H5T_STRING)
                    attr.read(memDataType, result);
            }
            NEXUS_CATCH("can't read attribute", "ReadAttribute");
            //*/
    }
    catch(const NexusException&)
    {
        // if attribute does not exists returns an empty string
    }
    return result;
}

void SetGroupClass(H5::H5Location* loc, const std::string& className) {
    /// don't put attribute if no class provided
    if (className.empty())
        return;
    SetAttribute(loc, classAttributeName, (void*)className.data(), className.size(), NX_CHAR);
}

}

//=======================================
// NeXus Implementation
//
//=======================================

class NexusFileImpl
{
public:

    ////////////////////////////////////////////////////////
    /// static helpers                                   ///
    /// (defined here to take adavanteg of friend specs) ///
    ////////////////////////////////////////////////////////

    static bool FillInfo(const ChildIterator& it, NexusItemInfo* info)
    {
        ChildInfo child = *it;
        const H5::CommonFG* loc = it.loc();
        if (child.type == ChildInfo::DATASET_TYPE) {
            H5::DataSet dataset = loc->openDataSet(child.name);
            std::strcpy(info->m_pszItem, child.name.c_str());
            std::strcpy(info->m_pszClass, DATASET_CLASS);
            info->m_eDataType = h5Data2nexusData(dataset.getDataType());
            return true;
        } else if (child.type == ChildInfo::GROUP_TYPE) {
            H5::Group group = loc->openGroup(child.name);
            std::string className = ReadGroupClass(&group);
            std::strcpy(info->m_pszItem, child.name.c_str());
            std::strcpy(info->m_pszClass, className.c_str());
            info->m_eDataType = NX_NONE;
            return true;
        }
        return false;
    }

    static bool FillInfo(const AttributeIterator& it, NexusAttrInfo* info)
    {
        H5::Attribute attribute = *it;
        attribute.getName(info->m_pszName, MAX_NAME_LENGTH);
        info->m_eDataType = h5Data2nexusData(attribute.getDataType());
        info->m_iLen = attributeMaxLength(attribute);
        return true;
    }

    ////////////////////////////////
    /// constructors & descrutor ///
    ////////////////////////////////

    NexusFileImpl( const std::string& path, NexusFile::OpenMode mode, bool use_lock )
    {
        NXFILE_LOCK;
        m_file_p = 0;
        NXFILEIMPL_COUT("NexusFileImpl");

        if( use_lock )
        {
            if( mode == NexusFile::READ )
                m_lock_ptr.reset( new yat::LockFile( yat::FileName(path), yat::LockFile::READ) );
            else if( NexusFile::WRITE )
                m_lock_ptr.reset( new yat::LockFile( yat::FileName(path), yat::LockFile::WRITE) );
        }

        if( yat::FileName(path).file_exist() && mode != NexusFile::NONE )
            Open(path, mode);
    }

    ~NexusFileImpl()
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("~NexusFileImpl");
        try
        {
            Close();
        }
        catch(...)
        {
            /// we don't want to throw in the destructor
        }
        delete m_file_p;
    }

    ////////////////////////////////
    /// file related methods ... ///
    ////////////////////////////////

    void Create(const std::string& path)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("Create");
        PrivOpenFile(path, H5F_ACC_TRUNC);
        PrivCloseFile();
    }

    bool Open(const std::string& path, NexusFile::OpenMode mode)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("Open");

        // Lock the file
        if( m_lock_ptr )
            m_lock_ptr->lock();

        try
        {
            // open the file if a mode is specified
            // note that a file can't be created this way
            switch(mode)
            {
            case NexusFile::NONE: return false;
            case NexusFile::READ: return PrivOpenFile(path, H5F_ACC_RDONLY);
            case NexusFile::WRITE: return PrivOpenFile(path, H5F_ACC_RDWR);
            default: throw NexusException("unknown open mode", "NexusFileImpl::Open");
            }
        }
        catch(const NexusException& e)
        {
            // Unlock the file
            if( m_lock_ptr )
                m_lock_ptr->unlock();
            throw e;            
        }
        catch(...)
        {
            // Unlock the file
            if( m_lock_ptr )
                m_lock_ptr->unlock();
            throw NexusException("unknown exception caught", "NexusFileImpl::Open");            
        }
    }

    void Close()
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("Close");
        try
        {
            CloseAllGroups(); // pop & delete all groups (and the top dataset)
            PrivCloseFile(); // pop & close the file
        }
        catch( const NexusException& e )
        {
            // Unlock the file
            if( m_lock_ptr )
                m_lock_ptr->unlock();
            throw e;            
        }
        catch(...)
        {
            // Unlock the file
            if( m_lock_ptr )
                m_lock_ptr->unlock();
            throw NexusException("unknown exception caught", "NexusFileImpl::Close");            
        }
        // Unlock the file
        if( m_lock_ptr )
            m_lock_ptr->unlock();
    }

    void Flush()
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("Flush");
        try
        {
            m_file_p->flush(H5F_SCOPE_GLOBAL);
        }
        NEXUS_CATCH("flush operation failed", "NexusFileImpl::Flush");
    }

    /////////////////////////////////
    /// group related methods ... ///
    /////////////////////////////////

    int ItemCount()
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("ItemCount");
        H5::CommonFG* fg = GetSkip<H5::CommonFG>();
        return fg == NULL ? 0 : (int)CountChilds(fg);
    }

    std::string CurrentGroupPath()
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("CurrentGroupPath");
        if (m_open.size() == 1) // handle special case
            return "/";
        std::string result;
        for(std::deque<H5::H5Location*>::const_iterator it=m_open.begin(), end=m_open.end() ; it != end ; ++it) {
            H5::Group* group = dynamic_cast<H5::Group*>(*it);
            if (!group)
                continue;
            result += "/"; // add separator (and root as well)
            result += BaseName(GetObjectName(group)); // add group name
            std::string groupClass = ReadGroupClass(group);
            if (!groupClass.empty())
                result += "(" + groupClass + ")";
        }
        return result;
    }

    std::string CurrentGroupName()
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("CurrentGroupName");
        H5::Group* group = GetSkip<H5::Group>();
        if (!group)
            throw NexusException("no group/file opened", "NexusFileImpl::CurrentGroupName");
        return BaseName(GetObjectName(group));
    }

    std::string CurrentGroupClass()
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("CurrentGroupClass");
        H5::Group* group = GetSkip<H5::Group>();
        if (!group)
            throw NexusException("no group/file opened", "NexusFileImpl::CurrentGroupClass");
        return ReadGroupClass(group);
    }

    bool CreateOpenGroupPath(const std::string& path, bool create, bool throwException)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("CreateOpenGroupPath");
        // quick check the current path
        if (path == CurrentGroupPath())
            return true;

        CloseDataSet(); // close the current dataset if any

        std::vector<std::string> groups; // path components storage
        yat::StringUtil::split(path, '/', &groups, false); // extract path components
        std::vector<std::string>::const_iterator group_it = groups.begin(), group_end = groups.end();

        // close all opened groups if path starts with file root ("/")
        if( !groups.empty() && group_it->empty() )
        {
            CloseAllGroups();
            ++group_it;
        }

        // iterate over path components
        for(; group_it != group_end ; ++group_it)
        {
            std::pair<std::string, std::string> tmp = ExtractNXGroupName(*group_it);
            std::string groupName = tmp.first;
            std::string className = tmp.second;

            if (groupName == "..") // parent group
            {
                CloseGroup();
            }
            else if (groupName == ".") // self group
            {
                // noop
            }
            else if(!groupName.empty()) // provide at least group name
            {
                // Open the group
                bool opened = OpenGroup(groupName, className, throwException);
                if(!opened)
                {
                    if(!create)
                        return false;
                    else // Create
                        CreateGroup(groupName, className, true);
                }
            }
            else if(!className.empty())  // provide only class name
            {
                if (!PrivOpenGroupByClass(className))
                    return false;
            }
        }
        return true;
    }

    bool CloseGroup()
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("CloseGroup");
        CloseDataSet();
        return CloseTop<H5::Group>();
    }

    void CloseAllGroups()
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("CloseAllGroups");
        CloseDataSet();
        while(CloseTop<H5::Group>());
        // assert (m_open.size() == 1);
        // assert (m_file.getObjCount() == 1);
    }

    void CreateGroup(const std::string& name, const std::string& className, bool open)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("CreateGroup");
        try
        {
            /// get current opened group or file
            CloseDataSet();
            H5::CommonFG* node = GetTop<H5::CommonFG>();
            if (node == NULL)
                throw NexusException("can't create a group without a parent group or file", "NexusFileImpl::CreateGroup");
            /// create group
            H5::Group group = node->createGroup(name);
            /// set group's class
            SetGroupClass(&group, className);
            /// push it
            if (open)
                PushTop(new H5::Group(group));
        }
        NEXUS_CATCH("can't create group " + name + " - in file: " + file_path_, "NexusFileImpl::CreateGroup");
    }

    bool OpenGroup(const std::string& groupName, const std::string& className, bool throwException = true)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("OpenGroup");
        try {
            PrivOpenGroup(groupName, className);
            return true;
        } catch (const NexusException&) {
            if (throwException)
                throw;
            return false;
        }
    }

    void GetGroupChildren(std::vector<std::string>* datasets, std::vector<std::string>* groups, std::vector<std::string>* classes)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("GetGroupChildren");
        H5::CommonFG* fg = GetSkip<H5::CommonFG>();
        if (fg == NULL)
            return;
        ChildIterator it = ChildIterator::begin(fg), end = ChildIterator::end(fg);
        for(; it != end ; ++it) {
            ChildInfo info = *it;
            if (info.type == ChildInfo::DATASET_TYPE) {
                datasets->push_back(info.name);
            } else if (info.type == ChildInfo::GROUP_TYPE) {
                H5::Group group = fg->openGroup(info.name);
                groups->push_back(info.name);
                classes->push_back(ReadGroupClass(&group));
            }
        }
    }

    NexusItemInfoList GetGroupChildren()
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("GetGroupChildren");
        NexusItemInfoList result;
        H5::CommonFG* fg = GetSkip<H5::CommonFG>();
        if (fg) {
            ChildIterator it = ChildIterator::begin(fg), end = ChildIterator::end(fg);
            for(; it != end ; ++it) {
                NexusItemInfo* nxinfo = new NexusItemInfo;
                result.push_back(NexusItemInfoPtr(nxinfo));
                FillInfo(it, nxinfo);
            }
        }
        return result;
    }

    ///////////////////////////////////
    /// dataset related methods ... ///
    ///////////////////////////////////

    std::string CurrentDataSet()
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("CurrentDataSet");
        H5::DataSet* dataset = GetTop<H5::DataSet>();
        return dataset ? BaseName(dataset->getObjName()) : g_strNoDataSet;
    }

    bool CloseDataSet()
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("CloseDataSet");
        return CloseTop<H5::DataSet>();
    }

    void CreateDataSet(const std::string& name, NexusDataType dataType, int rank, int* dim, bool open)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("CreateDataSet");
        PrivCreateDataSet(name, dataType, rank, dim, H5::DSetCreatPropList::DEFAULT, open);
    }

    void CreateCompressedDataSet(const std::string& name, NexusDataType dataType, int rank, int* dim, int* chunk, int open)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("CreateCompressedDataSet");
        H5::DSetCreatPropList props;
        try
        {
            hsize_t chunkDims[MAX_DATASET_NDIMS];
            std::copy(chunk, chunk + rank, chunkDims);
            props.setChunk(rank, chunkDims);
            //unsigned int cd_values[] = {(unsigned)chunkSize}; // [block_size, nthreads] (http://www.hdfgroup.org/services/filters/HDF5_LZ4.pdf)
            props.setFilter(HDF5_FILTER_LZ4, H5Z_FLAG_MANDATORY);
        }
        NEXUS_CATCH("unable to use LZ4 filter for dataset " + name + " - in file: " + file_path_, "NexusFileImpl::CreateCompressedDataSet");
        PrivCreateDataSet(name, dataType, rank, dim, props, open);
    }

    bool OpenDataSet(const std::string& name, bool throwException = true)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("OpenDataSet");
        try
        {
            PrivOpenDataSet(name);
            return true;
        }
        catch (const NexusException&)
        {
            if (throwException)
                throw;
            return false;
        }
    }

    void GetDataSetInfo(const std::string& name, NexusDataSetInfo* dataSetInfo)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("GetDataSetInfo");
        H5::DataSet* dataSet = PrivOpenDataSet(name);
        // extract dataset info from HDF5
        H5::DataSpace dataSpace = dataSet->getSpace();
        H5::DataType memDataType = dataSet->getDataType();
        std::vector<int> dataDims = GetDimensions(dataSpace);
        int rank = dataDims.size();
        
        if (memDataType.getClass() == H5T_STRING)
        {
            std::string buf;
            dataSet->read(buf, memDataType, dataSpace);
            if( 0 == rank )
            {
                dataDims.push_back( (int)buf.size() );
                rank = 1;
            }
            else
                dataDims.front() = (int)buf.size(); // update true size
        }
        // update nexus dataset info
        dataSetInfo->Clear();
        if(!dataSetInfo->IsEmpty())
            return;
        std::copy(dataDims.begin(), dataDims.end(), dataSetInfo->DimArray());
        dataSetInfo->SetInfo(h5Data2nexusData(memDataType), rank);
        dataSetInfo->SetTotalDim(rank, dataSetInfo->DimArray());
    }

    ////////////////////////////////
    /// data related methods ... ///
    ////////////////////////////////

    void GetData(const std::string& name, NexusDataSet *dataSet)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("GetData");
        if (!name.empty())
            GetDataSetInfo(name, dataSet);
        dataSet->Alloc();
        GetData(dataSet->Data());
    }

    void GetData(void* data)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("GetData");
        /// get the dataset corresponding to name
        H5::DataSet* dataset = GetTop<H5::DataSet>();
        if (!dataset)
            throw NexusException("no dataset opened to read data", "NexusFileImpl::GetData");
        /// fill data with file one (assuming memory layout is the same as in the file)
        try
        {
            dataset->read(data, dataset->getDataType(), dataset->getSpace() );
        }
        NEXUS_CATCH("failed getting data in file: " + file_path_, "NexusFileImpl::GetData");
    }

    void WriteData(const std::string& name, void* data, NexusDataType dataType, int rank, int* dim, bool create)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("WriteData");
        if (create)
            CreateDataSet(name, dataType, rank, dim, true);
        PutData(name, data, false);
    }

    void PutData(const std::string& name, void* data, bool flush)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("PutData");
        /// get the dataset corresponding to name (current one if empty)
        H5::DataSet* dataset = PrivOpenDataSet(name);
        /// write data (assuming data provided has the same type as the set itself)
        try
        {
            dataset->write(data, dataset->getDataType());
        }
        NEXUS_CATCH("failed writing dataset " + name + " - in file: " + file_path_, "NexusFileImpl::PutData");
        if (flush)
            Flush();
    }

    ////////////////////////////////////////
    /// partial data related methods ... ///
    ////////////////////////////////////////

    void GetDataSubSet(const std::string& name, NexusDataSet *dataSet)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("GetDataSubSet");
        H5::DataSet* dataset = PrivOpenDataSet(name);
        dataSet->Alloc();
        try
        {
            /// memspace (NexusDataSet)
            int memrank = dataSet->Rank();
            int* start = dataSet->StartArray();
            int* size = dataSet->DimArray();

            hsize_t memdims[MAX_DATASET_NDIMS];
            hsize_t startdims[MAX_DATASET_NDIMS];
            std::copy(size, size + memrank, memdims);
            std::copy(start, start + memrank, startdims);

            H5::DataSpace memspace(memrank, memdims);
            /// filespace (subset selected)
            H5::DataSpace filespace = dataset->getSpace();
            if (memrank != filespace.getSimpleExtentNdims())
                throw NexusException("Wrong rank", "NexusFileImpl::GetDataSubSet");
            filespace.selectHyperslab(H5S_SELECT_SET, memdims, startdims);
            /// read data
            dataset->read(dataSet->Data(), dataset->getDataType(), memspace, filespace);
        }
        NEXUS_CATCH("failed getting data subset " + name + " - in file: " + file_path_, "NexusFileImpl::GetDataSubSet");
    }

    void PutDataSubSet(const std::string& name, void* data, int* start, int* size)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("PutDataSubSet");
        H5::DataSet* dataset = PrivOpenDataSet(name);
        try
        {
            H5::DataSpace subspace = dataset->getSpace();
            int rank = subspace.getSimpleExtentNdims();
            hsize_t ssize[MAX_DATASET_NDIMS];
            hsize_t sstart[MAX_DATASET_NDIMS];
            std::copy(size, size + rank, ssize);
            std::copy(start, start + rank, sstart);
            H5::DataSpace dataspace(rank, ssize); // given data must be the same shape as file subspace
            subspace.selectHyperslab(H5S_SELECT_SET, ssize, sstart);
            dataset->write(data, dataset->getDataType(), dataspace, subspace);
        }
        NEXUS_CATCH("failed writing data subset " + name + " - in file: " + file_path_, "NexusFileImpl::PutDataSubSet");
    }

    void WriteDataSubSet(const std::string& name, void* data, NexusDataType dataType,
                         int rank, int* start, int* size, bool create, bool noDim)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("WriteDataSubSet");
        if (noDim)
            throw NexusException("dimensions must be defined", "NexusFileImpl::WriteDataSubSet");
        if (create)
            CreateDataSet(name, dataType, rank, size, true);
        PutDataSubSet(std::string(), data, start, size);
    }

    ///////////////////////////////////////////////
    /// attribute related methods ...           ///
    /// - act on the top group or top dataset ? ///
    ///////////////////////////////////////////////

    int AttrCount()
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("AttrCount");
        H5::H5Location* loc = GetSkip<H5::H5Location>();
        if (!loc)
            throw NexusException(NO_FG_OPEN, "NexusFileImpl::AttrCount");
        return CountAttributes(loc);
    }

    void PutAttr(const std::string &name, void* data, int length, NexusDataType dataType)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("PutAttr");
        H5::H5Location* loc = GetTop<H5::H5Location>();
        if (!loc)
            throw NexusException("No group nor dataset open", "NexusFileImpl::PutAttr");
        SetAttribute(loc, name, data, length, dataType);
    }

    void GetAttr(const std::string &name, void* data, int* length, NexusDataType* dataType)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("GetAttr");
        H5::H5Location* loc = GetTop<H5::H5Location>();
        if (!loc)
            throw NexusException("No group nor dataset open", "NexusFileImpl::GetAttr");
        ReadAttribute(loc, name, data, length, dataType, false);
    }

    void GetAttr(const std::string &name, void* data, int* length, NexusDataType dataType)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("GetAttr");
        H5::H5Location* loc = GetTop<H5::H5Location>();
        if (!loc)
            throw NexusException("No group nor dataset open", "NexusFileImpl::GetAttr");
        ReadAttribute(loc, name, data, length, dataType, false);
    }

    /////////////
    /// links ///
    /////////////

    void GetDataSetLink(NexusItemID *pnxl) {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("GetDataSetLink");
        H5::DataSet* dataset = PrivOpenDataSet(); // get current dataset
        *((std::string*)pnxl->m_pLink) = GetObjectName(dataset);
    }

    void GetGroupLink(NexusItemID *pnxl) {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("GetGroupLink");
        H5::Group* group = GetTop<H5::Group>();
        if (!group)
            throw NexusException("no group opened, can't prepare link", "NexusFileImpl::GetGroupLink");
        *((std::string*)pnxl->m_pLink) = GetObjectName(group);
    }

    void LinkToCurrentGroup(const NexusItemID &nxl) {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("LinkToCurrentGroup");
        H5::Group* group = GetTop<H5::Group>();
        if (!group)
            throw NexusException("no group opened, can't link", "NexusFileImpl::LinkToCurrentGroup");
        const std::string& path = *((std::string*)nxl.m_pLink);
        if (path.empty())
            throw NexusException("no link selected", "NexusFileImpl::LinkToCurrentGroup");
        try
        {
            group->link(H5L_TYPE_HARD, path, BaseName(path));
        }
        NEXUS_CATCH("link fails", "NexusFileImpl::LinkToCurrentGroup");
    }

    /////////////////
    /// iterators ///
    /////////////////

    int GetFirstItem(NexusItemInfo *pItemInfo)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("GetFirstItem");
        /// get current opened group or file
        CloseDataSet();
        H5::CommonFG* loc = GetTop<H5::CommonFG>();
        if (!loc)
            throw NexusException("no group/file opened", "NexusFileImpl::GetFirstItem"); // return NX_ITEM_NOT_FOUND;
        /// get iterator, initialize it and fill info
        ChildIterator& it = *((ChildIterator*)pItemInfo->m_pContext);
        it = ChildIterator::begin(loc);
        if (it == ChildIterator::end(loc))
            return NX_EOD;
        FillInfo(it, pItemInfo);
        return NX_OK;

    }

    int GetNextItem(NexusItemInfo *pItemInfo)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("GetNextItem");
        ChildIterator& it = *((ChildIterator*)pItemInfo->m_pContext);
        const H5::CommonFG* loc = it.loc();
        if (!loc)
            throw NexusException("Item info not initialized by GetFirstItem", "NexusFileImpl::GetNextItem"); // NX_ITEM_NOT_FOUND;
        ++it;
        if (it == ChildIterator::end(loc))
            return NX_EOD;
        FillInfo(it, pItemInfo);
        return NX_OK;
    }

    int GetFirstAttribute(const std::string& name, NexusAttrInfo *pAttrInfo)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("GetFirstAttribute");
        /// get current opened group or file
        H5::DataSet* dataset = PrivOpenDataSet(name);
        AttributeIterator& it = *((AttributeIterator*)pAttrInfo->m_pContext);
        it = AttributeIterator::begin(dataset);
        if (it == AttributeIterator::end(dataset))
            return NX_EOD;
        FillInfo(it, pAttrInfo);
        return NX_OK;
    }

    int GetNextAttribute(NexusAttrInfo *pAttrInfo)
    {
        NXFILE_LOCK;
        NXFILEIMPL_COUT("GetNextAttribute");
        AttributeIterator& it = *((AttributeIterator*)pAttrInfo->m_pContext);
        const H5::H5Location* loc = it.loc();
        if (!loc)
            throw NexusException("Attribute info not initialized by GetFirstAttribute", "NexusFileImpl::GetNextAttribute"); // return NX_ITEM_NOT_FOUND;
        ++it;
        if (it == AttributeIterator::end(loc))
            return NX_EOD;
        FillInfo(it, pAttrInfo);
        return NX_OK;
    }

private:

    /////////////////////////////
    /// tools (thread unsafe) ///
    /////////////////////////////

    bool PrivOpenFile(const std::string& path, unsigned int flags)
    {
        NXFILEIMPL_COUT("PrivOpenFile");
        if (!m_open.empty())
            throw NexusException("can't open file " + path + ", previous one is still opened", "NexusFileImpl::PrivOpenFile");
        if (flags == H5F_ACC_DEFAULT)
            return false;
        try
        {
            m_file_p = new H5::H5File(path, flags); // .openFile can't create it !
        }
        NEXUS_CATCH("can't open file " + path, "NexusFileImpl::PrivOpenFile");
        file_path_ = path;
        PushTop(m_file_p);
        return true;
    }

    void PrivCloseFile()
    {
        NXFILEIMPL_COUT("PrivCloseFile");
        /// checking if stack is empty to avoid raising an exception for duplicate close
        if (m_open.empty())
            return;
        if (!PopTop<H5::H5File>())
            throw NexusException("can't close file, some groups are still opened", "NexusFileImpl::PrivCloseFile");
        try
        {
            delete m_file_p;
            m_file_p = 0;
        }
        NEXUS_CATCH("failed closing file " + file_path_, "NexusFileImpl::PrivCloseFile");
    }

    void PrivOpenGroup(const std::string& groupName, const std::string& className)
    {
        NXFILEIMPL_COUT("PrivOpenGroup");
        try
        {
            /// get current opened group or file
            CloseDataSet();
            H5::CommonFG* fg = GetTop<H5::CommonFG>();
            if (!fg)
                throw NexusException("can't open a group without a parent group or file", "NexusFileImpl::PrivOpenGroup");
            /// open the group
            H5::Group group = fg->openGroup(groupName);
            /// check group class
            if (!className.empty()) {
                std::string groupClass = ReadGroupClass(&group);
                if (groupClass != className)
                    throw NexusException("wrong class name", "NexusFileImpl::PrivOpenGroup");
            }
            /// push group
            PushTop(new H5::Group(group));
        }
        NEXUS_CATCH("can't open group " + groupName + " - in file: " + file_path_, "NexusFileImpl::PrivOpenGroup");
    }

    bool PrivOpenGroupByClass(const std::string& className) {
        NXFILEIMPL_COUT("PrivOpenGroupByClass");
        H5::CommonFG* loc = GetTop<H5::CommonFG>();
        if (!loc)
            return false; // mostly file is not opened
        ChildFilterIterator child_it = ChildFilterIterator::begin(loc, ChildInfo::GROUP_TYPE);
        ChildFilterIterator child_end = ChildFilterIterator::end(loc, ChildInfo::GROUP_TYPE);
        for(; child_it != child_end ; ++child_it) {
            H5::Group group = loc->openGroup(*child_it);
            if (ReadGroupClass(&group) == className) {
                PushTop(new H5::Group(group));
                return true;
            }
        }
        return false;
    }

    H5::DataSet* PrivOpenDataSet(const std::string& name = std::string())
    {
        NXFILEIMPL_COUT("PrivCreateDataSet");
        /// get the dataset corresponding to name (current one if empty)
        if (!name.empty())
        {
            try
            {
                /// get current opened group (no root)
                CloseDataSet();
                H5::Group* group = GetTop<H5::Group>();
                if (!group)
                    throw NexusException("can't open a dataset without a parent group", "NexusFileImpl::PrivOpenDataSet");
                /// open the dataset from the current group and push it
                PushTop(new H5::DataSet(group->openDataSet(name)));
            }
            NEXUS_CATCH("can't open dataset " + name + " - in file: " + file_path_, "NexusFileImpl::PrivOpenDataSet");
        }

        H5::DataSet* dataset = GetTop<H5::DataSet>();
        if (!dataset)
            throw NexusException(NO_DATASET_OPEN, "NexusFileImpl::PrivOpenDataSet");
        return dataset;
    }

    void PrivCreateDataSet(const std::string& name, NexusDataType dataType, int rank, int* dim, const H5::DSetCreatPropList& props, bool open)
    {
        NXFILEIMPL_COUT("PrivCreateDataSet");
        try
        {
            /// get current opened group (no root)
            CloseDataSet();
            H5::Group* group = GetTop<H5::Group>();
            if (!group)
                throw NexusException("can't create a dataset without a parent group", "NexusFileImpl::PrivCreateDataSet");
            /// create data set
            hsize_t dataDims[MAX_DATASET_NDIMS];
            std::copy(dim, dim + rank, dataDims);
            H5::DataSpace dataSpace;
            H5::DataType memDataType;
            if( NX_CHAR == dataType && 1 == rank )
            {
                // Characters string
                memDataType = H5::StrType(0, dim[0] == 0 ? 1 : dim[0]);
            }
            else
            {
                dataSpace = H5::DataSpace(rank, dataDims);
                memDataType = nexusData2h5Data(dataType);
            }
            H5::DataSet dataset = group->createDataSet(name, memDataType, dataSpace, props);
            /// push it
            if (open)
                PushTop(new H5::DataSet(dataset));
        }
        NEXUS_CATCH("can't create dataset " + name + " - in file: " + file_path_, "NexusFileImpl::PrivCreateDataSet");
    }

    ///////////////////////////////////
    /// stack tools (thread unsafe) ///
    ///////////////////////////////////

    template<class T>
    inline T* GetSkip()
    {
        H5::DataSet* dataset = PopTop<H5::DataSet>();
        T* top = GetTop<T>();
        PushTop(dataset);
        return top;
    }

    template<class T>
    inline bool CloseTop()
    {
        T* loc = GetTop<T>();
        if (loc)
        {
            m_open.pop_back();
            /// we don't call close so that we ensure
            /// the pointer deletion even if close fails
            /// note that close is done in the destructor
            /// NOTE the file is not closed this way
            if (dynamic_cast<H5::H5File*>(loc) != m_file_p)
                delete loc;
            return true;
        }
        return false;
    }

    template<class T>
    inline T* PopTop()
    {
        T* loc = GetTop<T>();
        if (loc)
            m_open.pop_back();
        return loc;
    }

    template<class T>
    inline T* GetTop()
    {
        if (m_open.empty())
            return NULL;
        return dynamic_cast<T*>(m_open.back());
    }

    inline void PushTop(H5::H5Location* loc)
    {
        if (loc)
            m_open.push_back(loc);
    }

    ///////////////
    /// members ///
    ///////////////

    H5::H5File* m_file_p;

    /// Location Open Stack: File Groups+ Dataset?
    std::deque<H5::H5Location*> m_open;

    static yat::Mutex s_global_mutex;  // exclusive access mutex
    
    std::string  file_path_;           // full nexus file name
    friend class NexusGlobalLock;

    yat::UniquePtr<yat::LockFile> m_lock_ptr;       // system-SWMR lock
};

// Static initialisation
yat::Mutex NexusFileImpl::s_global_mutex;

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
  NexusFileImpl::s_global_mutex.lock();
}

//=============================================================================
// NexusGlobalLock::~NexusGlobalLock
//=============================================================================
NexusGlobalLock::~NexusGlobalLock()
{
  NexusFileImpl::s_global_mutex.unlock();
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
    m_pContext = new ChildIterator;
}

NexusItemInfo::~NexusItemInfo()
{
    delete [] m_pszItem;
    delete [] m_pszClass;
    delete (ChildIterator*)m_pContext;
}

//=============================================================================
// NeXus attribute info
//
//=============================================================================
NexusAttrInfo::NexusAttrInfo()
{
    m_pszName = new char[MAX_NAME_LENGTH];
    m_pContext = new AttributeIterator;
}

NexusAttrInfo::~NexusAttrInfo()
{
    delete [] m_pszName;
    delete (AttributeIterator*)m_pContext;
}

//=============================================================================
//
// NeXus File Class - wrapper methods
//
//=============================================================================

//---------------------------------------------------------------------------
// Constructor
//---------------------------------------------------------------------------
void NexusFile::Initialize()
{

    NXFILE_LOCK;

    // watchdog against multiple calls
    static bool done = false;
    if (done)
        return;
    done = true;

    H5Zregister(H5Z_LZ4); // register LZ4 filter
    initErrorManagement(); // initialize error callback
}

//---------------------------------------------------------------------------
// Constructor
//---------------------------------------------------------------------------
NexusFile::NexusFile(const char *pcszFullPath, OpenMode eMode, bool use_lock)
{
    Initialize();
    m_pImpl = new NexusFileImpl( ToString(pcszFullPath), eMode, use_lock );
    m_pUserPtr = NULL;
}

//---------------------------------------------------------------------------
// Destructor
//---------------------------------------------------------------------------
NexusFile::~NexusFile()
{
    // automatic close
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
void NexusFile::Create(const char *pcszFullPath, ENexusCreateMode)
{
    m_pImpl->Create(pcszFullPath);
}

//---------------------------------------------------------------------------
// NexusFile::OpenRead
//---------------------------------------------------------------------------
void NexusFile::OpenRead(const char *pcszFullPath)
{
    m_pImpl->Open(pcszFullPath, READ);
}

//---------------------------------------------------------------------------
// NexusFile::OpenReadWrite
//---------------------------------------------------------------------------
void NexusFile::OpenReadWrite(const char *pcszFullPath)
{
    m_pImpl->Open(pcszFullPath, WRITE);
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
    m_pImpl->PutData(ToString(pcszName), pData, bFlush);
}

//---------------------------------------------------------------------------
// NexusFile::PutDataSubSet
//---------------------------------------------------------------------------
void NexusFile::PutDataSubSet(void *pData, int *piStart, int *piSize,
                              const char *pcszName)
{
    m_pImpl->PutDataSubSet(ToString(pcszName), pData, piStart, piSize);
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
    m_pImpl->WriteDataSubSet(ToString(pcszName), pData, eDataType, iRank, piStart, piSize, bCreate, bNoDim);
}

//---------------------------------------------------------------------------
// NexusFile::WriteData | float version
//---------------------------------------------------------------------------
void NexusFile::WriteData(const char *pcszName, float fValue, bool bCreate)
{
    int iLen = 1;
    m_pImpl->WriteData(ToString(pcszName), &fValue, NX_FLOAT32, 1, &iLen, bCreate);
}

//---------------------------------------------------------------------------
// NexusFile::WriteData | double version
//---------------------------------------------------------------------------
void NexusFile::WriteData(const char *pcszName, double dValue, bool bCreate)
{
    int iLen = 1;
    m_pImpl->WriteData(ToString(pcszName), &dValue, NX_FLOAT64, 1, &iLen, bCreate);
}

//---------------------------------------------------------------------------
// NexusFile::WriteData | long version
//---------------------------------------------------------------------------
void NexusFile::WriteData(const char *pcszName, long lValue, bool bCreate)
{
    int iLen = 1;
    m_pImpl->WriteData(ToString(pcszName), &lValue, NX_INT32, 1, &iLen, bCreate);
}

//---------------------------------------------------------------------------
// NexusFile::WriteData | std::string version
//---------------------------------------------------------------------------
void NexusFile::WriteData(const char *pcszName, const char *pcszValue, bool bCreate)
{
    int iLen = strlen(pcszValue);
    m_pImpl->WriteData(ToString(pcszName), (void*)pcszValue, NX_CHAR, 1, &iLen, bCreate);
}

//---------------------------------------------------------------------------
// NexusFile::WriteData | binary version
//---------------------------------------------------------------------------
void NexusFile::WriteData(const char *pcszName, void *pData, int _iLen, bool bCreate)
{
    int iLen = _iLen;
    m_pImpl->WriteData(ToString(pcszName), pData, NX_BINARY, 1, &iLen, bCreate);
}

//---------------------------------------------------------------------------
// NexusFile::PutAttr
//---------------------------------------------------------------------------
void NexusFile::PutAttr(const char *pcszName, void *pValue, int iLen,
                        NexusDataType eDataType)
{
    m_pImpl->PutAttr(ToString(pcszName), pValue, iLen, eDataType);
}

//---------------------------------------------------------------------------
// NexusFile::PutAttr | 'long' version
//---------------------------------------------------------------------------
void NexusFile::PutAttr(const char *pcszName, long lValue)
{
    m_pImpl->PutAttr(ToString(pcszName), &lValue, 1, NX_INT32);
}

//---------------------------------------------------------------------------
// NexusFile::PutAttr | 'pcsz' version
//---------------------------------------------------------------------------
void NexusFile::PutAttr(const char *pcszName, const char *pcszValue)
{
    m_pImpl->PutAttr(ToString(pcszName), (void *)pcszValue, strlen(pcszValue), NX_CHAR);
}

//---------------------------------------------------------------------------
// NexusFile::PutAttr | 'double' version
//---------------------------------------------------------------------------
void NexusFile::PutAttr(const char *pcszName, double dValue)
{
    m_pImpl->PutAttr(ToString(pcszName), &dValue, 1, NX_FLOAT64);
}

//---------------------------------------------------------------------------
// NexusFile::PutAttr | 'float' version
//---------------------------------------------------------------------------
void NexusFile::PutAttr(const char *pcszName, float fValue)
{
    m_pImpl->PutAttr(ToString(pcszName), &fValue, 1, NX_FLOAT32);
}

//---------------------------------------------------------------------------
// NexusFile::GetData
//---------------------------------------------------------------------------
void NexusFile::GetData(NexusDataSet *pDataSet, const char *pcszDataSet)
{
    m_pImpl->GetData(ToString(pcszDataSet), pDataSet);
}

//---------------------------------------------------------------------------
// NexusFile::GetDataSubSet
//---------------------------------------------------------------------------
void NexusFile::GetDataSubSet(NexusDataSet *pDataSet, const char *pcszDataSet)
{
    m_pImpl->GetDataSubSet(ToString(pcszDataSet), pDataSet);
}

//---------------------------------------------------------------------------
// NexusFile::GetDataSetInfo
//---------------------------------------------------------------------------
void NexusFile::GetDataSetInfo(NexusDataSetInfo *pDataSetInfo, const char *pcszDataSet)
{
    m_pImpl->GetDataSetInfo(ToString(pcszDataSet), pDataSetInfo);
}

//---------------------------------------------------------------------------
// NexusFile::GetAttribute
//---------------------------------------------------------------------------
void NexusFile::GetAttribute(const char *pcszAttr, int *piBufLen,
                             void *pData, NexusDataType eDataType)
{
    m_pImpl->GetAttr(ToString(pcszAttr), (void*)pData, piBufLen, eDataType);
}

//---------------------------------------------------------------------------
// NexusFile::GetAttribute 'long' version
//---------------------------------------------------------------------------
void NexusFile::GetAttribute(const char *pcszAttr, long *plValue)
{
    int iLen = 1;
    m_pImpl->GetAttr(ToString(pcszAttr), plValue, &iLen, NX_INT32);
}

//---------------------------------------------------------------------------
// NexusFile::GetAttribute 'double' version
//---------------------------------------------------------------------------
void NexusFile::GetAttribute(const char *pcszAttr, double *pdValue)
{
    int iLen = 1;
    m_pImpl->GetAttr(ToString(pcszAttr), pdValue, &iLen, NX_FLOAT64);
}

//---------------------------------------------------------------------------
// NexusFile::GetAttribute 'float' version
//---------------------------------------------------------------------------
void NexusFile::GetAttribute(const char *pcszAttr, float *pfValue)
{
    int iLen = 1;
    m_pImpl->GetAttr(ToString(pcszAttr), pfValue, &iLen, NX_FLOAT32);
}

//---------------------------------------------------------------------------
// NexusFile::GetAttribute 'std::string' version
//---------------------------------------------------------------------------
void NexusFile::GetAttribute(const char *pcszAttr, std::string *pstrValue)
{
    char szBuf[1024];
    int iLen = 1024;
    m_pImpl->GetAttr(ToString(pcszAttr), szBuf, &iLen, NX_CHAR);
    pstrValue->assign(szBuf, iLen);
}

void NexusFile::GetAttribute(const char *pcszAttr, char *pszValue, int iBufLen)
{
    int iLen = iBufLen;
    m_pImpl->GetAttr(ToString(pcszAttr), pszValue, &iLen, NX_CHAR);
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
    return m_pImpl->GetFirstAttribute(ToString(pcszDataSet), pAttrInfo);
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
    NXFILE_COUT( std::string("Open group from path ") + std::string(pszPath) );
    return m_pImpl->CreateOpenGroupPath(pszPath, false, bThrowException);
}

//-----------------------------------------------------------------------------
// NexusFile::CreateGroupPath
//-----------------------------------------------------------------------------
bool NexusFile::CreateGroupPath(const char *pszPath)
{
    NXFILE_COUT( std::string("Create group from path ") + std::string(pszPath) );
    return m_pImpl->CreateOpenGroupPath(pszPath, true, false);
}

//-----------------------------------------------------------------------------
// NexusFile::CurrentGroupName
//-----------------------------------------------------------------------------
std::string NexusFile::CurrentGroupName()
{
    return m_pImpl->CurrentGroupName();
}

//-----------------------------------------------------------------------------
// NexusFile::CurrentGroupClass
//-----------------------------------------------------------------------------
std::string NexusFile::CurrentGroupClass()
{
    return m_pImpl->CurrentGroupClass();
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
    return m_pImpl->CurrentDataSet();
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

}
