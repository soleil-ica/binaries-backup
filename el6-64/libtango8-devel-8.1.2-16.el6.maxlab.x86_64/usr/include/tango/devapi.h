//////////////////////////////////////////////////////////////////
//
// devapi.h - include file for TANGO device api
//
//
// Copyright (C) :      2004,2005,2006,2007,2008,2009,2010,2011,2012,2013
//						European Synchrotron Radiation Facility
//                      BP 220, Grenoble 38043
//                      FRANCE
//
// This file is part of Tango.
//
// Tango is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Tango is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Tango.  If not, see <http://www.gnu.org/licenses/>.
//
// $Revision: 22213 $
//
///////////////////////////////////////////////////////////////

#ifndef _DEVAPI_H
#define _DEVAPI_H

#include <tango.h>
#include <tango_const.h>
#include <apiexcept.h>
#include <cbthread.h>
#include <lockthread.h>
#include <readers_writers_lock.h>

#include <bitset>

using namespace std;

namespace Tango {

//
// forward declarations
//

class DeviceData;
class DeviceAttribute;
class DbDevice;
class DbAttribute;
class DbDatum;
class DbDevImportInfo;
class Database;
class AsynReq;
class NotifdEventConsumer;
class ZmqEventConsumer;
class CallBack;
class AttributeProxy;
class TangoMonitor;

//
// Some typedef
//

typedef vector<DbDatum> DbData;

typedef union
{
	TangoSys_Pid	LockerPid;
	unsigned long	UUID[4];
}LockerId;

struct LockerInfo
{
	LockerLanguage	ll;
	LockerId		li;
	string			locker_host;
	string			locker_class;
};

struct LockingThread
{
	TangoMonitor	*mon;
	LockThCmd		*shared;
	LockThread		*l_thread;
};

struct _DevCommandInfo
{
	string 		cmd_name;
	long 		cmd_tag;
	long 		in_type;
	long 		out_type;
	string 		in_type_desc;
	string 		out_type_desc;

	bool operator==(const _DevCommandInfo &);
};

struct AttributeDimension
{
	long dim_x;
	long dim_y;
};

typedef struct _DevCommandInfo DevCommandInfo;

struct _CommandInfo : public DevCommandInfo
{
	Tango::DispLevel disp_level;

	bool operator==(const _CommandInfo &);
};

typedef _CommandInfo CommandInfo;
typedef vector<CommandInfo> CommandInfoList;

struct _DeviceInfo
{
	string dev_class;
	string server_id;
	string server_host;
	long server_version;
	string doc_url;
	string dev_type;
};

typedef _DeviceInfo DeviceInfo;

struct _DeviceAttributeConfig
{
	string 			name;
	AttrWriteType 	writable;
	AttrDataFormat 	data_format;
	int 			data_type;
	int 			max_dim_x;
	int 			max_dim_y;
	string 			description;
	string 			label;
	string 			unit;
	string 			standard_unit;
	string 			display_unit;
	string 			format;
	string 			min_value;
	string 			max_value;
	string 			min_alarm;
	string 			max_alarm;
	string 			writable_attr_name;
	vector<string> 	extensions;

	bool operator==(const _DeviceAttributeConfig &);
};

typedef struct _DeviceAttributeConfig DeviceAttributeConfig;

struct _AttributeInfo : public DeviceAttributeConfig
{
	Tango::DispLevel disp_level;

	friend ostream &operator<<(ostream &,_AttributeInfo &);
	bool operator==(const _AttributeInfo &);
};

typedef _AttributeInfo AttributeInfo;
typedef vector<AttributeInfo> AttributeInfoList;

struct _AttributeAlarmInfo
{
	string			min_alarm;
	string			max_alarm;
	string 			min_warning;
	string			max_warning;
	string			delta_t;
	string			delta_val;
	vector<string>	extensions;
};

struct _ChangeEventInfo
{
	string			rel_change;
	string			abs_change;
	vector<string>	extensions;
};

struct _PeriodicEventInfo
{
	string			period;
	vector<string>	extensions;
};

struct _ArchiveEventInfo
{
	string			archive_rel_change;
	string			archive_abs_change;
	string			archive_period;
	vector<string>	extensions;
};

typedef _ChangeEventInfo ChangeEventInfo;
typedef _PeriodicEventInfo PeriodicEventInfo;
typedef _ArchiveEventInfo ArchiveEventInfo;

struct _AttributeEventInfo
{
	ChangeEventInfo		ch_event;
	PeriodicEventInfo	per_event;
	ArchiveEventInfo	arch_event;
};

typedef _AttributeAlarmInfo AttributeAlarmInfo;
typedef _AttributeEventInfo AttributeEventInfo;

struct _AttributeInfoEx : public AttributeInfo
{
	AttributeAlarmInfo 	alarms;
	AttributeEventInfo	events;
	vector<string>		sys_extensions;

	_AttributeInfoEx & operator=(AttributeConfig_2 *);
	_AttributeInfoEx & operator=(AttributeConfig_3 *);

	friend ostream &operator<<(ostream &,_AttributeInfoEx &);
	bool operator==(const _AttributeInfoEx &);
};

typedef _AttributeInfoEx AttributeInfoEx;
typedef vector<AttributeInfoEx> AttributeInfoListEx;

//
// Can't use CALLBACK (without _) in the following enum because it's a
// pre-defined type on Windows....
//

enum asyn_req_type
{
	POLLING,
	CALL_BACK,
	ALL_ASYNCH
};

enum cb_sub_model
{
	PUSH_CALLBACK,
	PULL_CALLBACK
};

//
// Some define
//

#define 	CONNECTION_OK		1
#define 	CONNECTION_NOTOK	0

#define		PROT_SEP			"://"
#define		TACO_PROTOCOL		"taco"
#define		TANGO_PROTOCOL		"tango"

#define		MODIFIER			'#'
#define		DBASE_YES			"dbase=yes"
#define		DBASE_NO			"dbase=no"
#define		MODIFIER_DBASE_NO	"#dbase=no"

#define		HOST_SEP			':'
#define		PORT_SEP			'/'
#define		DEV_NAME_FIELD_SEP	'/'
#define		RES_SEP				"->"
#define		DEVICE_SEP			'/'

#define		FROM_IOR			"IOR"
#define		NOT_USED			"Unused"


/****************************************************************************************
 * 																						*
 * 					The ApiUtil class													*
 * 					-----------------													*
 * 																						*
 ***************************************************************************************/


class ApiUtil
{
public:
	TANGO_IMP_EXP static ApiUtil *instance();

	CORBA::ORB_ptr get_orb() {return CORBA::ORB::_duplicate(_orb);}
	void set_orb(CORBA::ORB_ptr orb_in) {_orb = orb_in;}
	void create_orb();
	int	get_db_ind();
	int	get_db_ind(string &host,int port);
	vector<Database *> &get_db_vect() {return db_vect;}
	bool in_server() {return in_serv;}
	void in_server(bool serv) {in_serv = serv;}

	TangoSys_Pid get_client_pid() {return ext->cl_pid;}
	void clean_locking_threads(bool clean=true);

	bool is_lock_exit_installed() {omni_mutex_lock guard(lock_th_map);return exit_lock_installed;}
	void set_lock_exit_installed(bool in) {omni_mutex_lock guard(lock_th_map);exit_lock_installed = in;}

	bool need_reset_already_flag() {return reset_already_executed_flag;}
	void need_reset_already_flag(bool in) {reset_already_executed_flag = in;}

	TANGO_IMP_EXP static inline void cleanup()
	{if (_instance != NULL){delete _instance;_instance=NULL;}}

	TANGO_IMP_EXP static inline bool _is_instance_null()
	{return _instance == NULL;}

//
// Utilities methods
//

	TANGO_IMP_EXP static int get_env_var(const char *,string &);
	int get_user_connect_timeout() {return ext->user_connect_timeout;}

	DevLong get_user_sub_hwm() {return ext->user_sub_hwm;}
	void set_event_buffer_hwm(DevLong val) {if (ext->user_sub_hwm == -1)ext->user_sub_hwm=val;}

	void get_ip_from_if(vector<string> &);
	void print_error_message(const char *);

//
// EventConsumer related methods
//

	void create_notifd_event_consumer();
	void create_zmq_event_consumer();

	bool is_notifd_event_consumer_created() {return ext->notifd_event_consumer != NULL;}
	NotifdEventConsumer *get_notifd_event_consumer();

	bool is_zmq_event_consumer_created() {return ext->zmq_event_consumer != NULL;}
	ZmqEventConsumer *get_zmq_event_consumer();

//
// Asynchronous methods
//

	void get_asynch_replies();
	void get_asynch_replies(long);
	AsynReq	*get_pasyn_table() {return asyn_p_table;}

	void set_asynch_cb_sub_model(cb_sub_model);
	cb_sub_model get_asynch_cb_sub_model() {return auto_cb;}

	size_t pending_asynch_call(asyn_req_type ty)
	{if (ty==POLLING)return asyn_p_table->get_request_nb();
	 else if (ty==CALL_BACK)return asyn_p_table->get_cb_request_nb();
	 else return (asyn_p_table->get_request_nb()+asyn_p_table->get_cb_request_nb());}

//
// Conv, between AttributeValuexxx and DeviceAttribute
//

	static void attr_to_device(const AttributeValue *,const AttributeValue_3 *,long,DeviceAttribute *);
	static void attr_to_device(const AttributeValue_4 *,long,DeviceAttribute *);

	static void device_to_attr(const DeviceAttribute &,AttributeValue_4 &);
	static void device_to_attr(const DeviceAttribute &,AttributeValue &,string &);

protected:
	ApiUtil();
	virtual ~ApiUtil();

	vector<Database *>			db_vect;
	omni_mutex					the_mutex;
	CORBA::ORB_ptr				_orb;
	bool						in_serv;

	cb_sub_model				auto_cb;
	CbThreadCmd					cb_thread_cmd;
	CallBackThread				*cb_thread_ptr;

	AsynReq						*asyn_p_table;

public:
	omni_mutex					lock_th_map;
	map<string,LockingThread>	lock_threads;

private:
    class ApiUtilExt
    {
    public:
        ApiUtilExt():notifd_event_consumer(NULL),cl_pid(0),user_connect_timeout(-1),
                     zmq_event_consumer(NULL),user_sub_hwm(-1) {};

        NotifdEventConsumer *notifd_event_consumer;
        TangoSys_Pid		cl_pid;
        int					user_connect_timeout;
        ZmqEventConsumer    *zmq_event_consumer;
        vector<string>      host_ip_adrs;
        DevLong             user_sub_hwm;
    };

	TANGO_IMP static ApiUtil 	*_instance;
	static omni_mutex			inst_mutex;
	bool						exit_lock_installed;
	bool						reset_already_executed_flag;

#ifdef HAS_UNIQUE_PTR
    unique_ptr<ApiUtilExt>      ext;
#else
	ApiUtilExt					*ext; 		// Class extension
#endif
};

/****************************************************************************************
 * 																						*
 * 					The DeviceData class												*
 * 					--------------------												*
 * 																						*
 ***************************************************************************************/


class DeviceData
{

public :
//
// constructor methods
//
	enum except_flags
	{
		isempty_flag,
		wrongtype_flag,
		numFlags
	};

	DeviceData();
	DeviceData(const DeviceData &);
	DeviceData & operator=(const DeviceData &);
#ifdef HAS_RVALUE
	DeviceData(DeviceData &&);
	DeviceData & operator=(DeviceData &&);
#endif
	~DeviceData();

	bool is_empty() {return any_is_null();}
	void exceptions(bitset<numFlags> fl) {exceptions_flags = fl;}
	bitset<numFlags> exceptions() {return exceptions_flags;}
	void reset_exceptions(except_flags fl) {exceptions_flags.reset((size_t)fl);}
	void set_exceptions(except_flags fl) {exceptions_flags.set((size_t)fl);}
	int get_type();

	CORBA::Any_var any;
//
// insert methods for native C++ types
//
	void operator << (bool datum) {any <<= CORBA::Any::from_boolean(datum);}
	void operator << (short datum) {any <<= datum;}
	void operator << (unsigned short datum) {any <<= datum;}
	void operator << (DevLong datum) {any <<= datum;}
	void operator << (DevULong datum) {any <<= datum;}
	void operator << (DevLong64 datum) {any <<= datum;}
	void operator << (DevULong64 datum) {any <<= datum;}
	void operator << (float datum) {any <<= datum;}
	void operator << (double datum) {any <<= datum;}
	void operator << (char *&datum) {any <<= datum;}
	void operator << (const char *&datum) {any <<= datum;}
	void operator << (string &datum) {any <<= datum.c_str();}
	void operator << (vector<unsigned char>&);
	void operator << (vector<string>&);
	void operator << (vector<short>&);
	void operator << (vector<unsigned short>&);
	void operator << (vector<DevLong> &);
	void operator << (vector<DevULong> &);
	void operator << (vector<DevLong64> &);
	void operator << (vector<DevULong64> &);
	void operator << (vector<float>&);
	void operator << (vector<double>&);
	void operator << (DevState datum) {(any.inout()) <<= datum;}
	void operator << (DevEncoded &datum) {(any.inout()) <<= datum;}

	void insert(vector<DevLong>&, vector<string>&);
	void insert(vector<double>&, vector<string>&);

	void insert(const string &,vector<unsigned char>&);
	void insert(const char *,DevVarCharArray *);
	void insert(const char *,unsigned char *,unsigned int);

//
// insert methods for TANGO CORBA sequence types
//

	inline void operator << (DevVarCharArray* datum) { any.inout() <<= datum;}
	inline void operator << (DevVarShortArray* datum) { any.inout() <<= datum;}
	inline void operator << (DevVarUShortArray* datum) { any.inout() <<= datum;}
	inline void operator << (DevVarLongArray* datum) { any.inout() <<= datum;}
	inline void operator << (DevVarLong64Array *datum) { any.inout() <<= datum;}
	inline void operator << (DevVarULongArray* datum) { any.inout() <<= datum;}
	inline void operator << (DevVarULong64Array* datum) { any.inout() <<= datum;}
	inline void operator << (DevVarFloatArray* datum) { any.inout() <<= datum;}
	inline void operator << (DevVarDoubleArray* datum) { any.inout() <<= datum;}
	inline void operator << (DevVarStringArray* datum) { any.inout() <<= datum;}
	inline void operator << (DevVarLongStringArray* datum) { any.inout() <<= datum;}
	inline void operator << (DevVarDoubleStringArray* datum) { any.inout() <<= datum;}

	inline void operator << (DevVarCharArray &datum) { any.inout() <<= datum;}
	inline void operator << (DevVarShortArray &datum) { any.inout() <<= datum;}
	inline void operator << (DevVarUShortArray datum) { any.inout() <<= datum;}
	inline void operator << (DevVarLongArray &datum) { any.inout() <<= datum;}
	inline void operator << (DevVarLong64Array &datum) { any.inout() <<= datum;}
	inline void operator << (DevVarULongArray &datum) { any.inout() <<= datum;}
	inline void operator << (DevVarULong64Array &datum) { any.inout() <<= datum;}
	inline void operator << (DevVarFloatArray &datum) { any.inout() <<= datum;}
	inline void operator << (DevVarDoubleArray &datum) { any.inout() <<= datum;}
	inline void operator << (DevVarStringArray &datum) { any.inout() <<= datum;}
	inline void operator << (DevVarLongStringArray &datum) { any.inout() <<= datum;}
	inline void operator << (DevVarDoubleStringArray &datum) { any.inout() <<= datum;}

//
// extract methods for native C++ types
//

	bool operator >> (bool&);
	bool operator >> (short&);
	bool operator >> (unsigned short&);
	bool operator >> (DevLong&);
	bool operator >> (DevULong&);
	bool operator >> (DevLong64&);
	bool operator >> (DevULong64&);
	bool operator >> (float&);
	bool operator >> (double&);
	bool operator >> (const char*&);
	bool operator >> (string&);

	bool operator >> (vector<unsigned char>&);
	bool operator >> (vector<string>&);
	bool operator >> (vector<short>&);
	bool operator >> (vector<unsigned short>&);
	bool operator >> (vector<DevLong>&);
	bool operator >> (vector<DevULong>&);
	bool operator >> (vector<DevLong64>&);
	bool operator >> (vector<DevULong64>&);
	bool operator >> (vector<float>&);
	bool operator >> (vector<double>&);
	bool operator >> (DevState&);
	bool extract(vector<DevLong>&, vector<string>&);
	bool extract(vector<double>&, vector<string>&);

    bool extract(const char *&,const unsigned char *&,unsigned int &);
    bool extract(string &,vector<unsigned char> &);

//
// extract methods for TANGO CORBA sequence types
//

	bool operator >> (const DevVarCharArray* &datum);
	bool operator >> (const DevVarShortArray* &datum);
	bool operator >> (const DevVarUShortArray* &datum);
	bool operator >> (const DevVarLongArray* &datum);
	bool operator >> (const DevVarLong64Array* &datum);
	bool operator >> (const DevVarULongArray* &datum);
	bool operator >> (const DevVarULong64Array* &datum);
	bool operator >> (const DevVarFloatArray* &datum);
	bool operator >> (const DevVarDoubleArray* &datum);
	bool operator >> (const DevVarStringArray* &datum);
	bool operator >> (const DevVarLongStringArray* &datum);
	bool operator >> (const DevVarDoubleStringArray* &datum);

	bool operator >> (const DevEncoded* &datum);
	bool operator >> (DevEncoded &datum);

	friend ostream &operator<<(ostream &,DeviceData &);

protected :
	bool any_is_null();

	bitset<numFlags> 	exceptions_flags;

private:
    class DeviceDataExt
    {
    public:
        DeviceDataExt() {};
    };

#ifdef HAS_UNIQUE_PTR
    unique_ptr<DeviceDataExt>   ext;
#else
	DeviceDataExt		        *ext;			// Class extension
#endif
};


/****************************************************************************************
 * 																						*
 * 					The DeviceAttribute class											*
 * 					-------------------------											*
 * 																						*
 ***************************************************************************************/

class DeviceAttribute
{

public :
//
// constructor methods
//
	enum except_flags
	{
		isempty_flag = 0,
		wrongtype_flag,
		failed_flag,
		unknown_format_flag,
		numFlags
	};

	DeviceAttribute();
	DeviceAttribute(const DeviceAttribute&);
	DeviceAttribute & operator=(const DeviceAttribute &);
#ifdef HAS_RVALUE
	DeviceAttribute(DeviceAttribute &&);
	DeviceAttribute & operator=(DeviceAttribute &&);
#endif

	void deep_copy(const DeviceAttribute &);

	DeviceAttribute(AttributeValue);

	DeviceAttribute(string&, short);
	DeviceAttribute(string&, DevLong);
	DeviceAttribute(string&, double);
	DeviceAttribute(string&, string&);
	DeviceAttribute(string&, const char *);
	DeviceAttribute(string&, float);
	DeviceAttribute(string&, bool);
	DeviceAttribute(string&, unsigned short);
	DeviceAttribute(string&, unsigned char);
	DeviceAttribute(string&, DevLong64);
	DeviceAttribute(string&, DevULong);
	DeviceAttribute(string&, DevULong64);
	DeviceAttribute(string&, DevState);
	DeviceAttribute(string&, DevEncoded &);

	DeviceAttribute(string&, vector<short> &);
	DeviceAttribute(string&, vector<DevLong> &);
	DeviceAttribute(string&, vector<double> &);
	DeviceAttribute(string&, vector<string> &);
	DeviceAttribute(string&, vector<float> &);
	DeviceAttribute(string&, vector<bool> &);
	DeviceAttribute(string&, vector<unsigned short> &);
	DeviceAttribute(string&, vector<unsigned char> &);
	DeviceAttribute(string&, vector<DevLong64> &);
	DeviceAttribute(string&, vector<DevULong> &);
	DeviceAttribute(string&, vector<DevULong64> &);
	DeviceAttribute(string&, vector<DevState> &);

	DeviceAttribute(string&, vector<short> &,int,int);
	DeviceAttribute(string&, vector<DevLong> &,int,int);
	DeviceAttribute(string&, vector<double> &,int,int);
	DeviceAttribute(string&, vector<string> &,int,int);
	DeviceAttribute(string&, vector<float> &,int,int);
	DeviceAttribute(string&, vector<bool> &,int,int);
	DeviceAttribute(string&, vector<unsigned short> &,int,int);
	DeviceAttribute(string&, vector<unsigned char> &,int,int);
	DeviceAttribute(string&, vector<DevLong64> &,int,int);
	DeviceAttribute(string&, vector<DevULong> &,int,int);
	DeviceAttribute(string&, vector<DevULong64> &,int,int);
	DeviceAttribute(string&, vector<DevState> &,int,int);

	DeviceAttribute(const char *, short);
	DeviceAttribute(const char *, DevLong);
	DeviceAttribute(const char *, double);
	DeviceAttribute(const char *, string&);
	DeviceAttribute(const char *, const char *);
	DeviceAttribute(const char *, float);
	DeviceAttribute(const char *, bool);
	DeviceAttribute(const char *, unsigned short);
	DeviceAttribute(const char *, unsigned char);
	DeviceAttribute(const char *, DevLong64);
	DeviceAttribute(const char *, DevULong);
	DeviceAttribute(const char *, DevULong64);
	DeviceAttribute(const char *, DevState);
	DeviceAttribute(const char *, DevEncoded &);

	DeviceAttribute(const char *, vector<short> &);
	DeviceAttribute(const char *, vector<DevLong> &);
	DeviceAttribute(const char *, vector<double> &);
	DeviceAttribute(const char *, vector<string> &);
	DeviceAttribute(const char *, vector<float> &);
	DeviceAttribute(const char *, vector<bool> &);
	DeviceAttribute(const char *, vector<unsigned short> &);
	DeviceAttribute(const char *, vector<unsigned char> &);
	DeviceAttribute(const char *, vector<DevLong64> &);
	DeviceAttribute(const char *, vector<DevULong> &);
	DeviceAttribute(const char *, vector<DevULong64> &);
	DeviceAttribute(const char *, vector<DevState> &);

	DeviceAttribute(const char *, vector<short> &,int,int);
	DeviceAttribute(const char *, vector<DevLong> &,int,int);
	DeviceAttribute(const char *, vector<double> &,int,int);
	DeviceAttribute(const char *, vector<string> &,int,int);
	DeviceAttribute(const char *, vector<float> &,int,int);
	DeviceAttribute(const char *, vector<bool> &,int,int);
	DeviceAttribute(const char *, vector<unsigned short> &,int,int);
	DeviceAttribute(const char *, vector<unsigned char> &,int,int);
	DeviceAttribute(const char *, vector<DevLong64> &,int,int);
	DeviceAttribute(const char *, vector<DevULong> &,int,int);
	DeviceAttribute(const char *, vector<DevULong64> &,int,int);
	DeviceAttribute(const char *, vector<DevState> &,int,int);

	~DeviceAttribute();

	AttrQuality 		quality;
	AttrDataFormat		data_format;
	string 				name;
	int 				dim_x;
	int 				dim_y;
	TimeVal 			time;

	string &get_name() {return name;}
	AttrQuality &get_quality() {return quality;}

	int get_dim_x() {return dim_x;}
	int get_dim_y() {return dim_y;}
	int get_written_dim_x() {return ext->w_dim_x;}
	int get_written_dim_y() {return ext->w_dim_y;}
	AttributeDimension get_r_dimension();
	AttributeDimension get_w_dimension();
	long get_nb_read();
	long get_nb_written();

	int get_type();
	AttrDataFormat get_data_format();
	TimeVal &get_date() {return time;}
	void set_name(string &na) {name =  na;}
	void set_name(const char *na) {string str(na);name = str;}
	bool is_empty();
	bool has_failed() {DevErrorList *tmp;if ((tmp=ext->err_list.operator->())==NULL)return false;
	                   else{if (tmp->length() != 0)return true;else return false;}}
	const DevErrorList &get_err_stack() {return ext->err_list.in();}

	DevVarEncodedArray_var &get_Encoded_data() const {return ext->EncodedSeq;}
	DevVarLong64Array_var &get_Long64_data() const {return ext->Long64Seq;}
	DevVarULongArray_var &get_ULong_data() const {return ext->ULongSeq;}
	DevVarULong64Array_var &get_ULong64_data() const {return ext->ULong64Seq;}
	DevVarStateArray_var &get_State_data() const {return ext->StateSeq;}
    DevErrorList_var &get_error_list() {return ext->err_list;}

    void set_w_dim_x(int val) {ext->w_dim_x = val;}
    void set_w_dim_y(int val) {ext->w_dim_y = val;}
    void set_err_list(DevErrorList *ptr) {ext->err_list = ptr;}
    void set_error_list(DevErrorList *ptr) {ext->err_list = ptr;}
    void set_Encoded_data(DevVarEncodedArray *ptr) {ext->EncodedSeq = ptr;}
    void set_Long64_data(DevVarLong64Array *ptr) {ext->Long64Seq = ptr;}
    void set_ULong_data(DevVarULongArray *ptr) {ext->ULongSeq = ptr;}
    void set_ULong64_data(DevVarULong64Array *ptr) {ext->ULong64Seq = ptr;}
    void set_State_data(DevVarStateArray *ptr) {ext->StateSeq = ptr;}

	void exceptions(bitset<numFlags> fl) {exceptions_flags = fl;}
	bitset<numFlags> exceptions() {return exceptions_flags;}
	void reset_exceptions(except_flags fl) {exceptions_flags.reset((size_t)fl);}
	void set_exceptions(except_flags fl) {exceptions_flags.set((size_t)fl);}


	DevVarLongArray_var 	LongSeq;
	DevVarShortArray_var 	ShortSeq;
	DevVarDoubleArray_var 	DoubleSeq;
	DevVarStringArray_var 	StringSeq;
	DevVarFloatArray_var	FloatSeq;
	DevVarBooleanArray_var	BooleanSeq;
	DevVarUShortArray_var	UShortSeq;
	DevVarCharArray_var		UCharSeq;

//
// For the state attribute
//

	DevState				d_state;
	bool					d_state_filled;

//
// Insert operators for  C++ types
//

	void operator << (short);
	void operator << (DevLong);
	void operator << (double);
	void operator << (string &);
	void operator << (float);
	void operator << (bool);
	void operator << (unsigned short);
	void operator << (unsigned char);
	void operator << (DevLong64);
	void operator << (DevULong);
	void operator << (DevULong64);
	void operator << (DevState);
	void operator << (DevEncoded &);
	void operator << (DevString);
	void operator << (const char *);

	void operator << (vector<short> &);
	void operator << (vector<DevLong> &);
	void operator << (vector<double> &);
	void operator << (vector<string> &);
	void operator << (vector<float> &);
	void operator << (vector<bool> &);
	void operator << (vector<unsigned short> &);
	void operator << (vector<unsigned char> &);
	void operator << (vector<DevLong64> &);
	void operator << (vector<DevULong> &);
	void operator << (vector<DevULong64> &);
	void operator << (vector<DevState> &);

	void operator << (const DevVarShortArray &datum);
	void operator << (const DevVarLongArray &datum);
	void operator << (const DevVarDoubleArray &datum);
	void operator << (const DevVarStringArray &datum);
	void operator << (const DevVarFloatArray &datum);
	void operator << (const DevVarBooleanArray &datum);
	void operator << (const DevVarUShortArray &datum);
	void operator << (const DevVarCharArray &datum);
	void operator << (const DevVarLong64Array &datum);
	void operator << (const DevVarULongArray &datum);
	void operator << (const DevVarULong64Array &datum);
	void operator << (const DevVarStateArray &datum);

	void operator << (DevVarShortArray *datum);
	void operator << (DevVarLongArray *datum);
	void operator << (DevVarDoubleArray *datum);
	void operator << (DevVarStringArray *datum);
	void operator << (DevVarFloatArray *datum);
	void operator << (DevVarBooleanArray *datum);
	void operator << (DevVarUShortArray *datum);
	void operator << (DevVarCharArray *datum);
	void operator << (DevVarLong64Array *datum);
	void operator << (DevVarULongArray *datum);
	void operator << (DevVarULong64Array *datum);
	void operator << (DevVarStateArray *datum);

//
// Insert methods
//

	void insert(vector<short> &,int,int);
	void insert(vector<DevLong> &,int,int);
	void insert(vector<double> &,int,int);
	void insert(vector<string> &,int,int);
	void insert(vector<float> &,int,int);
	void insert(vector<bool> &,int,int);
	void insert(vector<unsigned short> &,int,int);
	void insert(vector<unsigned char> &,int,int);
	void insert(vector<DevLong64> &,int,int);
	void insert(vector<DevULong> &,int,int);
	void insert(vector<DevULong64> &,int,int);
	void insert(vector<DevState> &,int,int);

	void insert(const DevVarShortArray &datum,int,int);
	void insert(const DevVarLongArray &datum,int,int);
	void insert(const DevVarDoubleArray &datum,int,int);
	void insert(const DevVarStringArray &datum,int,int);
	void insert(const DevVarFloatArray &datum,int,int);
	void insert(const DevVarBooleanArray &datum,int,int);
	void insert(const DevVarUShortArray &datum,int,int);
	void insert(const DevVarCharArray &datum,int,int);
	void insert(const DevVarLong64Array &datum,int,int);
	void insert(const DevVarULongArray &datum,int,int);
	void insert(const DevVarULong64Array &datum,int,int);
	void insert(const DevVarStateArray &datum,int,int);

	void insert(DevVarShortArray *datum,int,int);
	void insert(DevVarLongArray *datum,int,int);
	void insert(DevVarDoubleArray *datum,int,int);
	void insert(DevVarStringArray *datum,int,int);
	void insert(DevVarFloatArray *datum,int,int);
	void insert(DevVarBooleanArray *datum,int,int);
	void insert(DevVarUShortArray *datum,int,int);
	void insert(DevVarCharArray *datum,int,int);
	void insert(DevVarLong64Array *datum,int,int);
	void insert(DevVarULongArray *datum,int,int);
	void insert(DevVarULong64Array *datum,int,int);
	void insert(DevVarStateArray *datum,int,int);

	void insert(char *&,unsigned char *&,unsigned int);     // Deprecated. For compatibility purpose
	void insert(const char *,unsigned char *,unsigned int);
	void insert(const string &,vector<unsigned char> &);
	void insert(string &,vector<unsigned char> &);         // Deprecated. For compatibility purpose
	void insert(const char *,DevVarCharArray *);

//
// Extract operators for  C++ types
//

	bool operator >> (short &);
	bool operator >> (DevLong &);
	bool operator >> (double &);
	bool operator >> (string&);
	bool operator >> (float &);
	bool operator >> (bool &);
	bool operator >> (unsigned short &);
	bool operator >> (unsigned char &);
	bool operator >> (DevLong64 &);
	bool operator >> (DevULong &);
	bool operator >> (DevULong64 &);
	bool operator >> (DevState &);
	bool operator >> (DevEncoded &);

	bool operator >> (vector<string>&);
	bool operator >> (vector<short>&);
	bool operator >> (vector<DevLong>&);
	bool operator >> (vector<double>&);
	bool operator >> (vector<float>&);
	bool operator >> (vector<bool>&);
	bool operator >> (vector<unsigned short>&);
	bool operator >> (vector<unsigned char>&);
	bool operator >> (vector<DevLong64>&);
	bool operator >> (vector<DevULong>&);
	bool operator >> (vector<DevULong64>&);
	bool operator >> (vector<DevState>&);

	bool operator >> (DevVarShortArray* &datum);
	bool operator >> (DevVarLongArray* &datum);
	bool operator >> (DevVarDoubleArray* &datum);
	bool operator >> (DevVarStringArray* &datum);
	bool operator >> (DevVarFloatArray* &datum);
	bool operator >> (DevVarBooleanArray* &datum);
	bool operator >> (DevVarUShortArray* &datum);
	bool operator >> (DevVarCharArray* &datum);
	bool operator >> (DevVarLong64Array * &datum);
	bool operator >> (DevVarULongArray * &datum);
	bool operator >> (DevVarULong64Array * &datum);
	bool operator >> (DevVarStateArray * &datum);
	bool operator >> (DevVarEncodedArray *&datum);

//
// Extract_xxx methods
//

	bool extract_read (vector<string>&);
	bool extract_read (vector<short>&);
	bool extract_read (vector<DevLong>&);
	bool extract_read (vector<double>&);
	bool extract_read (vector<float>&);
	bool extract_read (vector<bool>&);
	bool extract_read (vector<unsigned short>&);
	bool extract_read (vector<unsigned char>&);
	bool extract_read (vector<DevLong64>&);
	bool extract_read (vector<DevULong>&);
	bool extract_read (vector<DevULong64>&);
	bool extract_read (vector<DevState>&);
	bool extract_read (string &,vector<unsigned char> &);

	bool extract_set  (vector<string>&);
	bool extract_set  (vector<short>&);
	bool extract_set  (vector<DevLong>&);
	bool extract_set  (vector<double>&);
	bool extract_set  (vector<float>&);
	bool extract_set  (vector<bool>&);
	bool extract_set  (vector<unsigned short>&);
	bool extract_set  (vector<unsigned char>&);
	bool extract_set  (vector<DevLong64>&);
	bool extract_set  (vector<DevULong>&);
	bool extract_set  (vector<DevULong64>&);
	bool extract_set  (vector<DevState>&);
	bool extract_set  (string &,vector<unsigned char> &);

	bool extract(char *&,unsigned char *&,unsigned int &);          // Deprecated, for compatibility purpose
	bool extract(const char *&,unsigned char *&,unsigned int &);
	bool extract(string &,vector<unsigned char> &);

	friend ostream &operator<<(ostream &,DeviceAttribute &);

protected :
	bitset<numFlags> 	exceptions_flags;
	void del_mem(int);
	bool check_for_data();
	bool check_wrong_type_exception();
	int  check_set_value_size(int seq_length);

protected:
    class DeviceAttributeExt
    {
    public:
        DeviceAttributeExt():w_dim_x(0),w_dim_y(0) {};
        DeviceAttributeExt & operator=(const DeviceAttributeExt &);

        void deep_copy(const DeviceAttributeExt &);

        DevErrorList_var		err_list;
        long 					w_dim_x;
        long					w_dim_y;

        DevVarLong64Array_var	Long64Seq;
        DevVarULongArray_var	ULongSeq;
        DevVarULong64Array_var	ULong64Seq;
        DevVarStateArray_var	StateSeq;
        DevVarEncodedArray_var	EncodedSeq;
    };

#ifdef HAS_UNIQUE_PTR
    unique_ptr<DeviceAttributeExt>  ext;
#else
	DeviceAttributeExt	            *ext;		// Class extension
#endif
};


/****************************************************************************************
 * 																						*
 * 					The xxxDataHistory classes											*
 * 					--------------------------											*
 * 																						*
 ***************************************************************************************/

class DeviceDataHistory: public DeviceData
{

public :
//
// constructor methods
//

	DeviceDataHistory();
	DeviceDataHistory(int, int *,DevCmdHistoryList *);
	DeviceDataHistory(const DeviceDataHistory &);
	DeviceDataHistory & operator=(const DeviceDataHistory &);
#ifdef HAS_RVALUE
	DeviceDataHistory(DeviceDataHistory &&);
	DeviceDataHistory &operator=(DeviceDataHistory &&);
#endif

	~DeviceDataHistory();

	bool has_failed() {return fail;}
	TimeVal &get_date() {return time;}
	const DevErrorList &get_err_stack() {return err.in();}
	friend ostream &operator<<(ostream &,DeviceDataHistory &);

// Three following methods for compatibility with older release

	bool failed() {return fail;}
	void failed(bool val) {fail = val;}
	void set_date(TimeVal &tv) {time = tv;}
	TimeVal &date() {return time;}
	const DevErrorList &errors() {return err.in();}
	void errors(DevErrorList_var &del) {err = del;}

protected:
	bool 				fail;
	TimeVal 			time;
	DevErrorList_var 	err;

	DevCmdHistoryList 	*seq_ptr;
	int 				*ref_ctr_ptr;

private:
    class DeviceDataHistoryExt
    {
    public:
        DeviceDataHistoryExt() {};
    };

#ifdef HAS_UNIQUE_PTR
    unique_ptr<DeviceDataHistoryExt>    ext_hist;
#else
	DeviceDataHistoryExt	            *ext_hist;		// Class extension
#endif
};

typedef vector<DeviceDataHistory> DeviceDataHistoryList;

class DeviceAttributeHistory: public DeviceAttribute
{

public :
//
// constructor methods
//

	DeviceAttributeHistory();
	DeviceAttributeHistory(int, DevAttrHistoryList_var &);
	DeviceAttributeHistory(int, DevAttrHistoryList_3_var &);
	DeviceAttributeHistory(const DeviceAttributeHistory &);
	DeviceAttributeHistory & operator=(const DeviceAttributeHistory &);
#ifdef HAS_RVALUE
	DeviceAttributeHistory(DeviceAttributeHistory &&);
	DeviceAttributeHistory &operator=(DeviceAttributeHistory &&);
#endif

	~DeviceAttributeHistory();

	bool has_failed() {return fail;}

// Three following methods for compatibility with older release

	bool failed() {return fail;}
	void failed(bool val) {fail = val;}
	TimeVal &date() {return time;}
//	const DevErrorList &errors() {return err;}

 	friend ostream &operator<<(ostream &,DeviceAttributeHistory &);

protected:
	bool 				fail;
	char 				compatibility_padding[16];

private:
    class DeviceAttributeHistoryExt
    {
    public:
        DeviceAttributeHistoryExt() {};
    };

#ifdef HAS_UNIQUE_PTR
    unique_ptr<DeviceAttributeHistoryExt>   ext_hist;
#else
	DeviceAttributeHistoryExt	            *ext_hist;	// Class extension
#endif
};


/****************************************************************************************
 * 																						*
 * 					The Connection class												*
 * 					--------------------												*
 * 																						*
 ***************************************************************************************/

class Connection
{
protected :
	bool 				dbase_used;			// Dev. with database
	bool 				from_env_var;		// DB from TANGO_HOST

	string 				host;				// DS host (if dbase_used=false)
	string 				port;				// DS port (if dbase_used=false)
	int 				port_num;			// DS port (as number)

	string 				db_host;			// DB host
	string 				db_port;			// DB port
	int 				db_port_num;		// DB port (as number)

	string 				ior;
	long 				pasyn_ctr;
	long				pasyn_cb_ctr;

	Tango::Device_var 	device;
	Tango::Device_2_var device_2;

	int 				timeout;

	int 				connection_state;
	int 				version;
	Tango::DevSource 	source;

	bool				check_acc;
	AccessControlType	access;

	virtual string get_corba_name(bool)=0;
	virtual string build_corba_name()=0;
	virtual int get_lock_ctr()=0;
	virtual void set_lock_ctr(int)=0;

	DeviceData redo_synch_cmd(TgRequest &);

	int get_env_var(const char *,string &);
	int get_env_var_from_file(string &,const char *,string &);

	void set_connection_state(int);
	void check_and_reconnect();
	void check_and_reconnect(Tango::DevSource &);
	void check_and_reconnect(Tango::AccessControlType &);
	void check_and_reconnect(Tango::DevSource &,Tango::AccessControlType &);

	long add_asyn_request(CORBA::Request_ptr,TgRequest::ReqType);
	void remove_asyn_request(long);

	void add_asyn_cb_request(CORBA::Request_ptr,CallBack *,Connection *,TgRequest::ReqType);
	void remove_asyn_cb_request(Connection *,CORBA::Request_ptr);
	long get_pasyn_cb_ctr();

    class ConnectionExt
    {
    public:
        ConnectionExt():tr_reco(true),prev_failed(false),prev_failed_t0(0.0),user_connect_timeout(-1),tango_host_localhost(false) {}
        ~ConnectionExt() {}
        ConnectionExt & operator=(const ConnectionExt &);

        bool				tr_reco;
        Tango::Device_3_var device_3;

        bool			  	prev_failed;
        double		  		prev_failed_t0;

        Tango::Device_4_var	device_4;
        omni_mutex			adm_dev_mutex;
        omni_mutex			asyn_mutex;
        ReadersWritersLock	con_to_mon;

        int					user_connect_timeout;
        bool				tango_host_localhost;
    };

#ifdef HAS_UNIQUE_PTR
    unique_ptr<ConnectionExt>   ext;
#else
	ConnectionExt		        *ext; 	// Class extension
#endif

public :
	virtual string dev_name()=0;

	Connection(CORBA::ORB *orb = NULL);
	Connection(bool dummy);
	virtual ~Connection();
	Connection(const Connection &);
	Connection & operator=(const Connection &);

	string &get_db_host() {return db_host;}
	string &get_db_port() {return db_port;}
	int get_db_port_num() {return db_port_num;}
	bool get_from_env_var() {return from_env_var;}
	static void get_fqdn(string &);

	bool is_dbase_used() {return dbase_used;}
	string &get_dev_host() {return host;}
	string &get_dev_port() {return port;}

	void connect(string &name);
	virtual void reconnect(bool);
	int get_idl_version() {return version;}
	Tango::Device_var &get_device() {return device;} 	// For CORBA expert !!

	virtual void set_timeout_millis(int timeout);
	virtual int get_timeout_millis();
	virtual Tango::DevSource get_source();
	virtual void set_source(Tango::DevSource sou);
	virtual void set_transparency_reconnection(bool val) {ext->tr_reco = val;}
	virtual bool get_transparency_reconnection() {return ext->tr_reco;}

	virtual DeviceData command_inout(string &);
	virtual DeviceData command_inout(const char *co) {string str(co);return command_inout(str);}
	virtual DeviceData command_inout(string &, DeviceData &);
	virtual DeviceData command_inout(const char *co,DeviceData &d) {string str(co);return command_inout(str,d);}
	virtual CORBA::Any_var command_inout(string &, CORBA::Any&);
	virtual CORBA::Any_var command_inout(const char *co, CORBA::Any &d) {string str(co);return command_inout(str,d);}

//
// Asynchronous methods
//

	void Cb_Cmd_Request(CORBA::Request_ptr,Tango::CallBack *);
	void Cb_ReadAttr_Request(CORBA::Request_ptr,Tango::CallBack *);
	void Cb_WriteAttr_Request(CORBA::Request_ptr req,Tango::CallBack *cb_ptr);
	void dec_asynch_counter(asyn_req_type ty);

	virtual long command_inout_asynch(const char *,DeviceData &argin,bool forget=false);
	virtual long command_inout_asynch(string &,DeviceData &argin,bool forget=false);
	virtual long command_inout_asynch(const char *,bool forget=false);
	virtual long command_inout_asynch(string &,bool forget=false);

	virtual DeviceData command_inout_reply(long);
	virtual DeviceData command_inout_reply(long,long);

	virtual void command_inout_asynch(const char *,DeviceData &argin,CallBack &cb);
	virtual void command_inout_asynch(string &,DeviceData &argin,CallBack &cb);
	virtual void command_inout_asynch(const char *,CallBack &cb);
	virtual void command_inout_asynch(string &,CallBack &cb);

	virtual void get_asynch_replies();
	virtual void get_asynch_replies(long);

	virtual void cancel_asynch_request(long);
	virtual void cancel_all_polling_asynch_request();

//
// Control access related methods
//

	AccessControlType get_access_control() {return access;}
	void set_access_control(AccessControlType acc) {access=acc;}
	AccessControlType get_access_right() {return get_access_control();}

};

/****************************************************************************************
 * 																						*
 * 					The DeviceProxy class												*
 * 					--------------------												*
 * 																						*
 ***************************************************************************************/

class DeviceProxy: public Tango::Connection
{
private :
	void real_constructor(string &,bool ch_acc=true);

	Tango::DbDevice 	*db_dev;
	string 				device_name;
	string 				alias_name;
	DeviceInfo 			_info;
	bool 				is_alias;
	DeviceProxy 		*adm_device;
	string 				adm_dev_name;
	omni_mutex 			netcalls_mutex;
	int					lock_ctr;
	int					lock_valid;

	void connect_to_adm_device();

	void retrieve_read_args(TgRequest &,vector<string> &);
	DeviceAttribute *redo_synch_read_call(TgRequest &);
	vector<DeviceAttribute> *redo_synch_reads_call(TgRequest &);
	void redo_synch_write_call(TgRequest &);
	void write_attribute(const AttributeValueList &);
	void write_attribute(const AttributeValueList_4 &);
	void create_locking_thread(ApiUtil *,DevLong);
	void local_import(string &);

	enum read_attr_type
	{
		SIMPLE,
		MULTIPLE
	};


	void read_attr_except(CORBA::Request_ptr,long,read_attr_type);
	void write_attr_except(CORBA::Request_ptr,long,TgRequest::ReqType);
	void check_connect_adm_device();

	friend class AttributeProxy;

protected :
	virtual string get_corba_name(bool);
	virtual string build_corba_name();
	virtual int get_lock_ctr() {return lock_ctr;}
	virtual void set_lock_ctr(int lo) {lock_ctr=lo;}

	enum polled_object
	{
		Cmd,
		Attr
	};

	bool is_polled(polled_object,string &, string &);
	virtual void reconnect(bool);
	void get_remaining_param(AttributeInfoListEx *);
	void from_hist4_2_AttHistory(DevAttrHistory_4_var &,vector<DeviceAttributeHistory> *);
	void from_hist4_2_DataHistory(DevCmdHistory_4_var &,vector<DeviceDataHistory> *);
	void ask_locking_status(vector<string> &,vector<DevLong> &);
	void get_locker_host(string &,string &);

	void same_att_name(vector<string> &,const char *);

private:
    class DeviceProxyExt
    {
    public:
        DeviceProxyExt() {};

        omni_mutex			lock_mutex;
    };

#ifdef HAS_UNIQUE_PTR
    unique_ptr<DeviceProxyExt>  ext_proxy;
#else
	DeviceProxyExt		        *ext_proxy;		// Class extension
#endif

public :
	DeviceProxy(string &name, CORBA::ORB *orb=NULL);
	DeviceProxy(string &name, bool ch_access, CORBA::ORB *orb=NULL);
	DeviceProxy(const char *, bool ch_access, CORBA::ORB *orb=NULL);
	DeviceProxy(const char *, CORBA::ORB *orb=NULL);

	DeviceProxy(const DeviceProxy &);
	DeviceProxy & operator=(const DeviceProxy &);
	virtual ~DeviceProxy();

	DeviceProxy():Connection((CORBA::ORB *)NULL),db_dev(NULL),adm_device(NULL),ext_proxy(Tango_NullPtr)
	{dbase_used = false;}

//
// general methods
//

	virtual DeviceInfo const &info();
	virtual inline string dev_name() { return device_name; }
	virtual void parse_name(string &);
	virtual Database *get_device_db();

	virtual string status();
	virtual DevState state();
	virtual string adm_name();
	virtual string description();
	virtual string name();
	virtual string alias();

	int get_tango_lib_version();

	virtual int ping();
	virtual vector<string> *black_box(int);
//
// device methods
//
	virtual CommandInfo command_query(string);
	virtual CommandInfoList *command_list_query();

	virtual DbDevImportInfo import_info();
//
// property methods
//
	virtual void get_property(string&, DbData&);
	virtual void get_property(vector<string>&, DbData&);
	virtual void get_property(DbData&);
	virtual void put_property(DbData&);
	virtual void delete_property(string&);
	virtual void delete_property(vector<string>&);
	virtual void delete_property(DbData&);
	virtual void get_property_list(const string &,vector<string> &);
//
// attribute methods
//
	virtual vector<string> *get_attribute_list();

	virtual AttributeInfoList *get_attribute_config(vector<string>&);
	virtual AttributeInfoListEx *get_attribute_config_ex(vector<string>&);
	virtual AttributeInfoEx get_attribute_config(const string &);

	virtual AttributeInfoEx attribute_query(string name) {return get_attribute_config(name);}
	virtual AttributeInfoList *attribute_list_query();
	virtual AttributeInfoListEx *attribute_list_query_ex();

	virtual void set_attribute_config(AttributeInfoList &);
	virtual void set_attribute_config(AttributeInfoListEx &);

	virtual DeviceAttribute read_attribute(string&);
	virtual DeviceAttribute read_attribute(const char *at) {string str(at);return read_attribute(str);}
	void read_attribute(const char *,DeviceAttribute &);
	void read_attribute(string &at,DeviceAttribute &da) {read_attribute(at.c_str(),da);}
	virtual vector<DeviceAttribute> *read_attributes(vector<string>&);

	virtual void write_attribute(DeviceAttribute&);
	virtual void write_attributes(vector<DeviceAttribute>&);

	virtual DeviceAttribute write_read_attribute(DeviceAttribute &);

//
// history methods
//
	virtual vector<DeviceDataHistory> *command_history(string &,int);
	virtual vector<DeviceDataHistory> *command_history(const char *na,int n)
			{string str(na);return command_history(str,n);}

	virtual vector<DeviceAttributeHistory> *attribute_history(string &,int);
	virtual vector<DeviceAttributeHistory> *attribute_history(const char *na,int n)
			{string str(na);return attribute_history(str,n);}
//
// Polling administration methods
//
	virtual vector<string> *polling_status();

	virtual void poll_command(string &, int);
	virtual void poll_command(const char *na, int per) {string tmp(na);poll_command(tmp,per);}
	virtual void poll_attribute(string &, int);
	virtual void poll_attribute(const char *na, int per) {string tmp(na);poll_attribute(tmp,per);}

	virtual int get_command_poll_period(string &);
	virtual int get_command_poll_period(const char *na)
			{string tmp(na);return get_command_poll_period(tmp);}
	virtual int get_attribute_poll_period(string &);
	virtual int get_attribute_poll_period(const char *na)
			{string tmp(na);return get_attribute_poll_period(tmp);}

	virtual bool is_command_polled(string &);
	virtual bool is_command_polled(const char *na) {string tmp(na);return is_command_polled(tmp);}
	virtual bool is_attribute_polled(string &);
	virtual bool is_attribute_polled(const char *na) {string tmp(na);return is_attribute_polled(tmp);}

	virtual void stop_poll_command(string &);
	virtual void stop_poll_command(const char *na) {string tmp(na);stop_poll_command(tmp);}
	virtual void stop_poll_attribute(string &);
	virtual void stop_poll_attribute(const char *na) {string tmp(na);stop_poll_attribute(tmp);}
//
// Asynchronous methods
//
	virtual long read_attribute_asynch(const char *na) {string tmp(na);return read_attribute_asynch(tmp);}
	virtual long read_attribute_asynch(string &att_name);
	virtual long read_attributes_asynch(vector <string> &);

	virtual vector<DeviceAttribute> *read_attributes_reply(long);
	virtual vector<DeviceAttribute> *read_attributes_reply(long,long);
	virtual DeviceAttribute *read_attribute_reply(long);
	virtual DeviceAttribute *read_attribute_reply(long,long);

	virtual long write_attribute_asynch(DeviceAttribute &);
	virtual long write_attributes_asynch(vector<DeviceAttribute> &);

	virtual void write_attributes_reply(long);
	virtual void write_attributes_reply(long,long);
	virtual void write_attribute_reply(long id) {write_attributes_reply(id);}
	virtual void write_attribute_reply(long to,long id) {write_attributes_reply(to,id);}

	virtual long pending_asynch_call(asyn_req_type req)
			{if (req == POLLING)return pasyn_ctr;
			else if (req==CALL_BACK) return pasyn_cb_ctr;
			else return (pasyn_ctr + pasyn_cb_ctr);}

	virtual void read_attributes_asynch(vector<string> &,CallBack &);
	virtual void read_attribute_asynch(const char *na,CallBack &cb) {string tmp(na);read_attribute_asynch(tmp,cb);}
	virtual void read_attribute_asynch(string &,CallBack &);

	virtual void write_attribute_asynch(DeviceAttribute &,CallBack &);
	virtual void write_attributes_asynch(vector<DeviceAttribute> &,CallBack &);
//
// Logging administration methods
//
#ifdef TANGO_HAS_LOG4TANGO
	virtual void add_logging_target(const string &target_type_name);
	virtual void add_logging_target(const char *target_type_name)
			{add_logging_target(string(target_type_name));}

	virtual void remove_logging_target(const string &target_type_name);
	virtual void remove_logging_target(const char *target_type_name)
			{remove_logging_target(string(target_type_name));}

	virtual vector<string> get_logging_target (void);
	virtual int get_logging_level (void);
	virtual void set_logging_level (int level);
#endif // TANGO_HAS_LOG4TANGO
//
// Event methods
//
	virtual int subscribe_event(const string &attr_name, EventType event, CallBack *,
	                   const vector<string> &filters);  // For compatibility with Tango < 8
	virtual int subscribe_event(const string &attr_name, EventType event, CallBack *,
	                   const vector<string> &filters, bool stateless); // For compatibility with Tango < 8
	virtual int subscribe_event(const string &attr_name, EventType event, int event_queue_size,
	                   const vector<string> &filters, bool stateless = false); // For compatibility with Tango < 8

	virtual int subscribe_event(const string &attr_name, EventType event, CallBack *);
	virtual int subscribe_event(const string &attr_name, EventType event, CallBack *,bool stateless);
	virtual int subscribe_event(const string &attr_name, EventType event, int event_queue_size,bool stateless = false);

	virtual void unsubscribe_event(int event_id);
//
// Methods to access data in event queues
//
	virtual void get_events (int event_id, EventDataList &event_list);
	virtual void get_events (int event_id, AttrConfEventDataList &event_list);
	virtual void get_events (int event_id, DataReadyEventDataList &event_list);
	virtual void get_events (int event_id, CallBack *cb);
	virtual int  event_queue_size(int event_id);
	virtual TimeVal get_last_event_date(int event_id);
	virtual bool is_event_queue_empty(int event_id);

//
// Locking methods
//
	virtual void lock(int lock_validity=DEFAULT_LOCK_VALIDITY);
	virtual void unlock(bool force=false);
	virtual string locking_status();
	virtual bool is_locked();
	virtual bool is_locked_by_me();
	virtual bool get_locker(LockerInfo &);
};

/****************************************************************************************
 * 																						*
 * 					The AttributeProxy class											*
 * 					--------------------												*
 * 																						*
 ***************************************************************************************/

class AttributeProxy
{
private :
	string 				attr_name;
	string 				device_name;
	Tango::DeviceProxy 	*dev_proxy;
	Tango::DbAttribute 	*db_attr;
	bool    			dbase_used;		// Dev. with database
	bool    			from_env_var;   // DB from TANGO_HOST

	string  			host;           // DS host (if dbase_used=false)
	string  			port;           // DS port (if dbase_used=false)
	int     			port_num;       // DS port (as number)

	string  			db_host;        // DB host
	string  			db_port;        // DB port
	int     			db_port_num;    // DB port (as number)

	void real_constructor(string &);
	void ctor_from_dp(const DeviceProxy *,string &);

    class AttributeProxyExt
    {
    public:
        AttributeProxyExt() {};
    };

#ifdef HAS_UNIQUE_PTR
    unique_ptr<AttributeProxyExt>   ext;
#else
	AttributeProxyExt	            *ext;     		// Class extension
#endif

public :
	AttributeProxy(string &name);
	AttributeProxy(const char *);
	AttributeProxy(const DeviceProxy *,string &);
	AttributeProxy(const DeviceProxy *,const char *);
	AttributeProxy(const AttributeProxy &);
	AttributeProxy & operator=(const AttributeProxy &);
	virtual ~AttributeProxy();

//
// general methods
//
	virtual inline string name() { return attr_name; }
	virtual inline DeviceProxy* get_device_proxy() { return dev_proxy; }
	virtual void parse_name(string &);

	virtual string status();
	virtual DevState state();
	virtual int ping();
	virtual void set_transparency_reconnection(bool);
	virtual bool get_transparency_reconnection();

//
// property methods
//

	virtual void get_property(string&, DbData&);
	virtual void get_property(vector<string>&, DbData&);
	virtual void get_property(DbData&);
	virtual void put_property(DbData&);
	virtual void delete_property(string&);
	virtual void delete_property(vector<string>&);
	virtual void delete_property(DbData&);

//
// attribute methods
//

	virtual AttributeInfoEx get_config();
	virtual void set_config(AttributeInfo &);
	virtual void set_config(AttributeInfoEx &);
	virtual DeviceAttribute read();
	virtual void write(DeviceAttribute&);
	virtual DeviceAttribute write_read(DeviceAttribute &);

//
// history methods
//

	virtual vector<DeviceAttributeHistory> *history(int);

//
// Polling administration methods
//

	virtual void poll(int);
	virtual int get_poll_period();
	virtual bool is_polled();
	virtual void stop_poll();

//
// Asynchronous methods
//

	virtual long read_asynch() {return dev_proxy->read_attribute_asynch(attr_name);}
	virtual DeviceAttribute *read_reply(long id) {return dev_proxy->read_attribute_reply(id);}
	virtual DeviceAttribute *read_reply(long id,long to) {return dev_proxy->read_attribute_reply(id,to);}

	virtual long write_asynch(DeviceAttribute &da) {return dev_proxy->write_attribute_asynch(da);}
	virtual void write_reply(long id) {dev_proxy->write_attribute_reply(id);}
	virtual void write_reply(long id,long to) {dev_proxy->write_attribute_reply(id,to);}

	virtual void read_asynch(CallBack &cb) {dev_proxy->read_attribute_asynch(attr_name,cb);}
	virtual void write_asynch(DeviceAttribute &da,CallBack &cb) {dev_proxy->write_attribute_asynch(da,cb);}

//
// Event methods
//

	virtual int subscribe_event (EventType event, CallBack *,const vector<string> &filters); // For compatibility
	virtual int subscribe_event (EventType event, CallBack *,const vector<string> &filters, bool stateless); // For compatibility
	virtual int subscribe_event (EventType event, int event_queue_size,const vector<string> &filters, bool stateless = false); // For compatibility

	virtual int subscribe_event (EventType event, CallBack *);
	virtual int subscribe_event (EventType event, CallBack *,bool stateless);
	virtual int subscribe_event (EventType event, int event_queue_size, bool stateless = false);

	virtual void unsubscribe_event (int ev_id) {dev_proxy->unsubscribe_event(ev_id);}

//
// Methods to access data in event queues
//

	virtual void get_events (int event_id, EventDataList &event_list)
	               {dev_proxy->get_events (event_id, event_list);}
	virtual void get_events (int event_id, AttrConfEventDataList &event_list)
	               {dev_proxy->get_events (event_id, event_list);}
	virtual void get_events (int event_id, CallBack *cb)
	               {dev_proxy->get_events (event_id, cb);}
	virtual int  event_queue_size(int event_id)
	               {return dev_proxy->event_queue_size(event_id);}
	virtual TimeVal get_last_event_date(int event_id)
	               {return dev_proxy->get_last_event_date(event_id);}
	virtual bool is_event_queue_empty(int event_id)
	               {return dev_proxy->is_event_queue_empty(event_id);}

};

/****************************************************************************************
 * 																						*
 * 					The DummyDeviceProxy class											*
 * 					--------------------												*
 * 																						*
 ***************************************************************************************/

class DummyDeviceProxy: public Tango::Connection
{
public:
	DummyDeviceProxy():Tango::Connection(true) {};

	virtual string get_corba_name(bool) {string str;return str;}
	virtual string build_corba_name() {string str;return str;}
	virtual int get_lock_ctr() {return 0;}
	virtual void set_lock_ctr(int) {};

	virtual string dev_name() {string str;return str;}

	int get_env_var(const char *cc,string &str_ref) {return Tango::Connection::get_env_var(cc,str_ref);}
};



///
///					Some inline methods
///					-------------------
///

inline ApiUtil *ApiUtil::instance()
{
	omni_mutex_lock lo(inst_mutex);

	if (_instance == NULL)
		_instance = new ApiUtil();
	return _instance;
}

inline long Connection::add_asyn_request(CORBA::Request_ptr req,TgRequest::ReqType req_type)
{
	omni_mutex_lock guard(ext->asyn_mutex);
	long id = ApiUtil::instance()->get_pasyn_table()->store_request(req,req_type);
	pasyn_ctr++;
	return id;
}

inline void Connection::remove_asyn_request(long id)
{
	omni_mutex_lock guard(ext->asyn_mutex);

	ApiUtil::instance()->get_pasyn_table()->remove_request(id);
	pasyn_ctr--;
}

inline void Connection::add_asyn_cb_request(CORBA::Request_ptr req,CallBack *cb,Connection *con,TgRequest::ReqType req_type)
{
	omni_mutex_lock guard(ext->asyn_mutex);
	ApiUtil::instance()->get_pasyn_table()->store_request(req,cb,con,req_type);
	pasyn_cb_ctr++;
}

inline void Connection::remove_asyn_cb_request(Connection *con,CORBA::Request_ptr req)
{
	omni_mutex_lock guard(ext->asyn_mutex);
	ApiUtil::instance()->get_pasyn_table()->remove_request(con,req);
	pasyn_cb_ctr--;
}

inline long Connection::get_pasyn_cb_ctr()
{
	long ret;
	ext->asyn_mutex.lock();
	ret = pasyn_cb_ctr;
	ext->asyn_mutex.unlock();
	return ret;
}

inline void Connection::dec_asynch_counter(asyn_req_type ty)
{
	omni_mutex_lock guard(ext->asyn_mutex);
	if (ty==POLLING)
		pasyn_ctr--;
	else if (ty==CALL_BACK)
		pasyn_cb_ctr--;
}

inline void DeviceProxy::check_connect_adm_device()
{
	omni_mutex_lock guard(ext->adm_dev_mutex);
	if (adm_device == NULL)
		connect_to_adm_device();
}

//
// For Tango 8 ZMQ event system
//

inline int DeviceProxy::subscribe_event (const string &attr_name, EventType event, CallBack *callback)
{
    vector<string> filt;
	return subscribe_event (attr_name,event,callback,filt,false);
}

inline int DeviceProxy::subscribe_event (const string &attr_name, EventType event,
                                 CallBack *callback,bool stateless)
{
    vector<string> filt;
    return subscribe_event(attr_name,event,callback,filt,stateless);
}

inline int DeviceProxy::subscribe_event (const string &attr_name, EventType event,
                                 int event_queue_size,bool stateless)
{
    vector<string> filt;
    return subscribe_event(attr_name,event,event_queue_size,filt,stateless);
}

///
///					Some macros
///					-----------
///

#define READ_ATT_EXCEPT(NAME_CHAR) \
		catch (Tango::ConnectionFailed &e) \
		{ \
			TangoSys_OMemStream desc; \
			desc << "Failed to read_attribute on device " << device_name; \
			desc << ", attribute " << NAME_CHAR << ends; \
			ApiConnExcept::re_throw_exception(e,(const char*)"API_AttributeFailed", \
                        	desc.str(), (const char*)"DeviceProxy::read_attribute()"); \
		} \
		catch (Tango::DevFailed &e) \
		{ \
			TangoSys_OMemStream desc; \
			desc << "Failed to read_attribute on device " << device_name; \
			desc << ", attribute " << NAME_CHAR << ends; \
			Except::re_throw_exception(e,(const char*)"API_AttributeFailed", \
                        	desc.str(), (const char*)"DeviceProxy::read_attribute()"); \
		} \
		catch (CORBA::TRANSIENT &trans) \
		{ \
			TRANSIENT_NOT_EXIST_EXCEPT(trans,"DeviceProxy","read_attribute"); \
		} \
		catch (CORBA::OBJECT_NOT_EXIST &one) \
		{ \
			if (one.minor() == omni::OBJECT_NOT_EXIST_NoMatch || one.minor() == 0) \
			{ \
				TRANSIENT_NOT_EXIST_EXCEPT(one,"DeviceProxy","read_attribute"); \
			} \
			else \
			{ \
				set_connection_state(CONNECTION_NOTOK); \
				TangoSys_OMemStream desc; \
				desc << "Failed to read_attribute on device " << device_name << ends; \
				ApiCommExcept::re_throw_exception(one, \
							      (const char*)"API_CommunicationFailed", \
                        				      desc.str(), \
							      (const char*)"DeviceProxy::read_attribute()"); \
			} \
		} \
		catch (CORBA::COMM_FAILURE &comm) \
		{ \
			if (comm.minor() == omni::COMM_FAILURE_WaitingForReply) \
			{ \
				TRANSIENT_NOT_EXIST_EXCEPT(comm,"DeviceProxy","read_attribute"); \
			} \
			else \
			{ \
				set_connection_state(CONNECTION_NOTOK); \
				TangoSys_OMemStream desc; \
				desc << "Failed to read_attribute on device " << device_name << ends; \
				ApiCommExcept::re_throw_exception(comm, \
							      (const char*)"API_CommunicationFailed", \
                        				      desc.str(), \
							      (const char*)"DeviceProxy::read_attribute()"); \
			} \
		} \
		catch (CORBA::SystemException &ce) \
        { \
			set_connection_state(CONNECTION_NOTOK); \
			TangoSys_OMemStream desc; \
			desc << "Failed to read_attribute on device " << device_name << ends; \
			ApiCommExcept::re_throw_exception(ce, \
						      (const char*)"API_CommunicationFailed", \
                        			      desc.str(), \
						      (const char*)"DeviceProxy::read_attribute()"); \
		}

///
/// 				Small utility classes
///					---------------------


class AutoConnectTimeout
{
public:
	AutoConnectTimeout(unsigned int to) {omniORB::setClientConnectTimeout((CORBA::ULong)to);}
	~AutoConnectTimeout() {omniORB::setClientConnectTimeout(0);}
};

} // End of Tango namespace

#endif /* _DEVAPI_H */
