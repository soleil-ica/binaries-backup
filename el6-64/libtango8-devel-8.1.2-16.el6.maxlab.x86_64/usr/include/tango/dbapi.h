//
// dbapi.h -	include file for TANGO database api
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


#ifndef _DBAPI_H
#define _DBAPI_H

#include <vector>
#include <errno.h>
#include <devapi.h>


using namespace std;

namespace Tango {

//
// forward declarations

class DbDatum;
class DbDevInfo;
class DbDevImportInfo;
class DbDevExportInfo;
class DbServerInfo;
class DbDevFullInfo;
class DbHistory;

class FileDatabase;
class DbServerCache;
class Util;
class AccessProxy;

typedef vector<DbDevInfo> DbDevInfos;
typedef vector<DbDevExportInfo> DbDevExportInfos;
typedef vector<DbDevImportInfo> DbDevImportInfos;
typedef vector<DbDatum> DbData;

#define		POGO_DESC	"Description"
#define		POGO_TITLE	"ProjectTitle"

//
// Database - database object for implementing generic high-level
//                 interface for TANGO database api
//

/****************************************************************************************
 * 																						*
 * 					The Database class													*
 * 					------------------													*
 * 																						*
 ***************************************************************************************/

class Database : public Tango::Connection
{
private :
	virtual string get_corba_name(bool);
	virtual string build_corba_name() {return string("nada");}
	virtual int get_lock_ctr() {return 0;}
	virtual void set_lock_ctr(int) {}

    class DatabaseExt
    {
    public:
        DatabaseExt():db_tg(NULL) {};

        Tango::Util 	*db_tg;
        omni_mutex		map_mutex;
    };

#ifdef HAS_UNIQUE_PTR
    unique_ptr<DatabaseExt>     ext;
#else
	DatabaseExt			        *ext;
#endif

	bool				db_multi_svc;
	vector<string>		multi_db_port;
	vector<string>		multi_db_host;
	FileDatabase 		*filedb;
	string 				file_name;
	int					serv_version;

	AccessProxy			*access_proxy;
	bool				access_checked;
	DevErrorList		access_except_errors;

	map<string,string>	dev_class_cache;
	string				db_device_name;

	bool				access_service_defined;

	DbDatum         make_string_array(string, CORBA::Any_var &);
	vector<DbHistory> make_history_array(bool, CORBA::Any_var &);

	void check_access();
	inline string dev_name();
	void set_server_release();
	void check_access_and_get();

public :
	Database(CORBA::ORB *orb=NULL);
	Database(string &host, int port, CORBA::ORB *orb=NULL);
	Database(string &file);

	Database(const Database &);
	Database & operator=(const Database &);

	void write_filedatabase();
	void reread_filedatabase();
	void write_event_channel_ior_filedatabase(string &);
	void build_connection ();
	void post_reconnection();
	~Database();
	inline Device_var &get_dbase() { return device;}
	void check_tango_host(const char *);
	AccessControlType check_access_control(string &);
	bool is_control_access_checked() {return access_checked;}
	void set_access_checked(bool val) {access_checked = val;}

	void set_tango_utils(Tango::Util *ptr) {ext->db_tg=ptr;}
	int get_server_release() {return serv_version;}

	DevErrorList &get_access_except_errors() {return access_except_errors;}
	void clear_access_except_errors() {access_except_errors.length(0);}
	bool is_command_allowed(string &,string &);

	bool is_multi_tango_host() {return db_multi_svc;}
	vector<string> &get_multi_host() {return multi_db_host;}
	vector<string> &get_multi_port() {return multi_db_port;}

	const string &get_file_name();

#ifdef _TG_WINDOWS_
	Database(CORBA::ORB *orb,string &,string &);
	long get_tango_host_from_reg(char **,string &,string &);
#endif

//
// general methods
//

	string get_info();
	DbDatum get_host_list();
	DbDatum get_host_list(string &);
	DbDatum get_services(string &,string &);
	DbDatum get_device_service_list(string &);
	void register_service(string &,string &,string &);
	void unregister_service(string &,string &);
	CORBA::Any *fill_server_cache(string &,string &);

//
// device methods
//

	void add_device(DbDevInfo&);
	void delete_device(string);
	DbDevImportInfo import_device(string &);
	void export_device(DbDevExportInfo &);
	void unexport_device(string);
    DbDevFullInfo get_device_info(string &);

	DbDatum get_device_name(string &, string &,DbServerCache *dsc);
	DbDatum get_device_name(string &, string &);
	DbDatum get_device_exported(string &);
	DbDatum get_device_domain(string &);
	DbDatum get_device_family(string &);
	DbDatum get_device_member(string &);
	void get_device_alias(string,string &);
	void get_alias(string,string &);
	DbDatum get_device_alias_list(string &);
	string get_class_for_device(string &);
	DbDatum get_class_inheritance_for_device(string &);
	DbDatum get_device_exported_for_class(string &);
	void put_device_alias(string &,string &);
	void delete_device_alias(string &);

//
// server methods
//
	void add_server(string &, DbDevInfos&);
	void delete_server(string &);
	void export_server(DbDevExportInfos &);
	void unexport_server(string &);
	void rename_server(const string &,const string &);

	DbServerInfo get_server_info(string &);
	void put_server_info(DbServerInfo &);
	void delete_server_info(string &);
	DbDatum get_server_class_list(string &);
	DbDatum get_server_name_list();
	DbDatum get_instance_name_list(string &);
	DbDatum get_server_list();
	DbDatum get_server_list(string &);
	DbDatum get_host_server_list(string &);
	DbDatum get_device_class_list(string &);

//
// property methods
//

	void get_property(string, DbData &,DbServerCache *dsc);
	void get_property(string st, DbData &db) {get_property(st,db,NULL);}
	void get_property_forced(string, DbData &,DbServerCache *dsc = NULL);
	void put_property(string, DbData &);
	void delete_property(string, DbData &);
	vector<DbHistory> get_property_history(string &,string &);
	DbDatum get_object_list(string &);
	DbDatum get_object_property_list(string &,string &);

	void get_device_property(string, DbData &, DbServerCache *dsc);
	void get_device_property(string st, DbData &db) {get_device_property(st,db,NULL);}
	void put_device_property(string, DbData &);
	void delete_device_property(string, DbData &);
	vector<DbHistory> get_device_property_history(string &,string &);
	DbDatum get_device_property_list(string &,string &);
	void get_device_property_list(string &,const string &,vector<string> &,DbServerCache *dsc = NULL);

	void get_device_attribute_property(string, DbData &, DbServerCache *dsc);
	void get_device_attribute_property(string st, DbData &db) {get_device_attribute_property(st,db,NULL);}
	void put_device_attribute_property(string, DbData &);
	void delete_device_attribute_property(string, DbData &);
	void delete_all_device_attribute_property(string, DbData &);
	vector<DbHistory> get_device_attribute_property_history(string &,string &,string &);
	void get_device_attribute_list(string &,vector<string> &);

	void get_class_property(string, DbData &, DbServerCache *dsc);
	void get_class_property(string st,DbData &db) {get_class_property(st,db,NULL);}
	void put_class_property(string, DbData &);
	void delete_class_property(string, DbData &);
	vector<DbHistory> get_class_property_history(string &,string &);
	DbDatum get_class_list(string &);
	DbDatum get_class_property_list(string &);

	void get_class_attribute_property(string, DbData &, DbServerCache *dsc);
	void get_class_attribute_property(string st,DbData &db) {get_class_attribute_property(st,db,NULL);}
	void put_class_attribute_property(string, DbData &);
	void delete_class_attribute_property(string, DbData &);
	vector<DbHistory> get_class_attribute_property_history(string &,string &,string &);
	DbDatum get_class_attribute_list(string &,string &);


// attribute methods

	void get_attribute_alias(string, string&);
	DbDatum get_attribute_alias_list(string &);
	void put_attribute_alias(string &,string &);
	void delete_attribute_alias(string &);

// event methods

	void export_event(DevVarStringArray *);
	void unexport_event(string &);
	CORBA::Any *import_event(string &);

// alias methods

	void get_device_from_alias(string, string &);
	void get_alias_from_device(string, string &);
	void get_attribute_from_alias(string, string &);
	void get_alias_from_attribute(string, string &);
};

//
// Some Database class inline methods
//


inline string Database::dev_name()
{
	if (db_device_name.empty() == true)
	{
		CORBA::String_var n = device->name();
		db_device_name = n;
	}
	return db_device_name;
}


//
// Some macros to call the Db server
// These macros will do some retries in case of
// timeout while calling the DB device
// This is necessary in case of massive DS
// startup (after a power cut for instance)
// when the Db is over-loaded
//

//					cerr << "Database timeout while executing command " << NAME << ", still " << db_retries << " retries" << endl;

#define MANAGE_EXCEPT(NAME) \
	catch (Tango::CommunicationFailed &e) \
	{ \
		if (e.errors.length() >= 2) \
		{ \
			if (::strcmp(e.errors[1].reason.in(),"API_DeviceTimedOut") == 0) \
			{ \
				if (db_retries != 0) \
				{ \
					db_retries--; \
					if (db_retries == 0) \
						throw; \
				} \
				else \
					throw; \
			} \
			else \
				throw; \
		} \
		else \
			throw; \
	}

#define CALL_DB_SERVER_NO_RET(NAME,SEND) \
	{ \
		bool retry_mac = true; \
		long db_retries = 0; \
		if (ext->db_tg != NULL) \
		{ \
			if (ext->db_tg->is_svr_starting() == true) \
				db_retries = DB_START_PHASE_RETRIES; \
		} \
		while (retry_mac == true) \
		{ \
			try \
			{ \
				command_inout(NAME,SEND); \
				retry_mac = false; \
			} \
			MANAGE_EXCEPT(NAME) \
		} \
	}

#define CALL_DB_SERVER(NAME,SEND,RET) \
	{ \
		bool retry_mac = true; \
		long db_retries = 0; \
		if (ext->db_tg != NULL) \
		{ \
			if (ext->db_tg->is_svr_starting() == true) \
				db_retries = DB_START_PHASE_RETRIES; \
		} \
		while (retry_mac == true) \
		{ \
			try \
			{ \
				RET = command_inout(NAME,SEND); \
				retry_mac = false; \
			} \
			MANAGE_EXCEPT(NAME) \
		} \
	}

//
// DbProperty - a database object for accessing general properties which
//                  are stored in the database
//
class DbProperty
{
public :
	DbProperty(string);
	~DbProperty();
//
// methods
//
	void get(DbData&);
	void put(DbData&);
	void delete_(DbData&);
};

//
// DbDevice - a database object for accessing device related information
//                 in the database
//

class DbDevice
{
private :
	string 		name;
	Database 	*dbase;
	int 		db_ind;
	bool 		ext_dbase;

    class DbDeviceExt
    {
    public:
        DbDeviceExt() {};
    };

#ifdef HAS_UNIQUE_PTR
    unique_ptr<DbDeviceExt>     ext;
#else
	DbDeviceExt	                *ext;
#endif

public :
	DbDevice(string &);
	DbDevice(string &, Database *);
	DbDevice(string &,string &,string &);
	~DbDevice();
	void set_name(string &new_name) {name = new_name;}
	Database *get_dbase();
	void set_dbase(Database *db) {dbase = db;}
//
// methods
//
	DbDevImportInfo import_device();
	void export_device(DbDevExportInfo&);
	void add_device(DbData&);
	void delete_device();
	void get_property(DbData&);
	void put_property(DbData&);
	void delete_property(DbData&);
	void get_attribute_property(DbData&);
	void put_attribute_property(DbData&);
	void delete_attribute_property(DbData&);
	AccessControlType check_access_control();
	void clear_access_except_errors();
	void get_property_list(const string &,vector<string> &);
};

//
// DbAttribute - a database object for accessing Attribute related information
//                 in the database
//

class DbAttribute
{
private :
	string name;
	string device_name;
	Database *dbase;
	int db_ind;
	bool ext_dbase;

public :
	DbAttribute(string &, string &);
	DbAttribute(string &, string &, Database *);
	DbAttribute(string &,string &, string &,string &);
	~DbAttribute();
//
// methods
//
	void get_property(DbData&);
	void put_property(DbData&);
	void delete_property(DbData&);
};
//
// DbServer - a database object for accessing server related information
//            in the database
//
class DbServer
{
private :
	string 		name;
	Database 	*dbase;
	int 		db_ind;
	bool 		ext_dbase;

    class DbServerExt
    {
    public:
        DbServerExt() {};
    };

#ifdef HAS_UNIQUE_PTR
    unique_ptr<DbServerExt> ext;
#else
	DbServerExt	            *ext;
#endif

public :
	DbServer(string);
	DbServer(string, Database*);
	~DbServer();
//
// methods
//
	void add_server(DbDevInfos&);
	void delete_server();
	void export_server(DbDevExportInfos&);
	void unexport_server();
	DbServerInfo get_server_info();
};
//
// DbClass - a database object for accessing class related information
//                in the database
//
class DbClass
{
private :
	string 		name;
	Database 	*dbase;
	int 		db_ind;
	bool 		ext_dbase;

    class DbClassExt
    {
    public:
        DbClassExt() {};
    };

#ifdef HAS_UNIQUE_PTR
    unique_ptr<DbClassExt>  ext;
#else
	DbClassExt	            *ext;
#endif

public :
	DbClass(string, Database*);
	DbClass(string);
	~DbClass();
//
// methods
//
	void get_property(DbData&);
	void put_property(DbData&);
	void delete_property(DbData&);
	void get_attribute_property(DbData&);
	void put_attribute_property(DbData&);
	void delete_attribute_property(DbData&);
};
//
// DbDatum - data object for sending and receiving data from the
//               TANGO database api
//
class DbDatum
{
public :
	enum except_flags
	{
		isempty_flag,
		wrongtype_flag,
		numFlags
	};

	string name;
	vector<string> value_string;
//
// constructor methods
//
	DbDatum();
	DbDatum (string);
	DbDatum (const char *);
	~DbDatum();
	DbDatum(const DbDatum &);
	DbDatum &operator=(const DbDatum &);

	size_t size() {return value_string.size();}
	bool is_empty();

	void exceptions(bitset<numFlags> fl) { exceptions_flags = fl;}
	bitset<numFlags> exceptions() {return exceptions_flags;}
	void reset_exceptions(except_flags fl) {exceptions_flags.reset((size_t)fl);}
	void set_exceptions(except_flags fl) {exceptions_flags.set((size_t)fl);}

//
// insert methods
//

	void operator << (bool);
	void operator << (short);
	void operator << (unsigned char);
	void operator << (unsigned short);
	void operator << (DevLong);
	void operator << (DevULong);
	void operator << (DevLong64);
	void operator << (DevULong64);
	void operator << (float);
	void operator << (double);
	void operator << (char *);
//	void operator << (char *&);
	void operator << (const char *);
//	void operator << (const char *&);
	void operator << (string&);

	void operator << (vector<string>&);
	void operator << (vector<short>&);
	void operator << (vector<unsigned short>&);
	void operator << (vector<DevLong>&);
	void operator << (vector<DevULong>&);
	void operator << (vector<DevLong64>&);
	void operator << (vector<DevULong64>&);
	void operator << (vector<float>&);
	void operator << (vector<double>&);

//
// extract methods
//

	bool operator >> (bool&);
	bool operator >> (short&);
	bool operator >> (unsigned char&);
	bool operator >> (unsigned short&);
	bool operator >> (DevLong&);
	bool operator >> (DevULong&);
	bool operator >> (DevLong64&);
	bool operator >> (DevULong64&);
	bool operator >> (float&);
	bool operator >> (double&);
	bool operator >> (const char*&);
	bool operator >> (string&);

	bool operator >> (vector<string>&);
	bool operator >> (vector<short>&);
	bool operator >> (vector<unsigned short>&);
	bool operator >> (vector<DevLong>&);
	bool operator >> (vector<DevULong>&);
	bool operator >> (vector<DevLong64>&);
	bool operator >> (vector<DevULong64>&);
	bool operator >> (vector<float>&);
	bool operator >> (vector<double>&);

private :

	int 				value_type;
	int 				value_size;
	bitset<numFlags> 	exceptions_flags;

    class DbDatumExt
    {
    public:
        DbDatumExt() {};
    };

#ifdef HAS_UNIQUE_PTR
    unique_ptr<DbDatumExt>  ext;
#else
	DbDatumExt			    *ext;
#endif
};
//
// DbHistory data object for receiving data history from the
//           TANGO database api
//
class DbHistory
{
public:

//
// constructor methods
//

  DbHistory(string ,string ,vector<string> &);
  DbHistory(string ,string ,string ,vector<string> &);

//
// getter methods
//

  string get_name();
  string get_attribute_name();
  string get_date();
  DbDatum get_value();
  bool is_deleted();

private:

  string  propname;   // Property name
  string  attname;    // Attribute name (Not used for device properties)
  DbDatum value;      // Property value
  string  date;       // Update date
  bool    deleted;    // Deleted flag

  string format_mysql_date(string );
  void make_db_datum(vector<string> &);
};
//
// DbDevInfo
//
class DbDevInfo
{
public :
	string name;
	string _class;
	string server;
};
//
// DbDevImportInfo
//
class DbDevImportInfo
{
public :
	string name;
	long exported;
	string ior;
	string version;
};

/****************************************************************
 *                                                              *
 *                  DbDevFullInfo                               *
 *                                                              *
 ****************************************************************/


class DbDevFullInfo: public DbDevImportInfo
{
public :
    string  class_name;
	string  ds_full_name;
	string  host;
	string  started_date;
	string  stopped_date;
	long    pid;
};

//
// DbDevExportInfo
//

class DbDevExportInfo
{
public:
	string name;
	string ior;
	string host;
	string version;
	int pid;

};

//
// DbServerInfo
//

class DbServerInfo
{
public :
	string name;
	string host;
	int mode;
	int level;
};


/****************************************************************************************
 * 																						*
 * 					The DbServerCache class												*
 * 					------------------													*
 * 																						*
 ***************************************************************************************/

//
// DbServerCache data object to implement a DB cache
// used during the DS startup sequence
//

class DbServerCache
{
public:
	typedef struct
	{
		int 			first_idx;
		int 			last_idx;
	}EltIdx;

	typedef struct
	{
		int				first_idx;
		int				last_idx;
		int				prop_nb;
		int 			*props_idx;
	}PropEltIdx;

	typedef struct
	{
		int 			first_idx;
		int				last_idx;
		int				att_nb;
		int				*atts_idx;
	}AttPropEltIdx;

	typedef struct
	{
		PropEltIdx		dev_prop;
		AttPropEltIdx 	dev_att_prop;
	}DevEltIdx;

	typedef struct
	{
		PropEltIdx 		class_prop;
		AttPropEltIdx 	class_att_prop;
		EltIdx 			dev_list;
		int 			dev_nb;
		DevEltIdx		*devs_idx;
	}ClassEltIdx;

	DbServerCache(Database *,string &,string &);
	~DbServerCache();

	const DevVarLongStringArray *import_adm_dev();
	const DevVarLongStringArray *import_notifd_event();
	const DevVarLongStringArray *import_adm_event();
	const DevVarStringArray *get_class_property(DevVarStringArray *);
	const DevVarStringArray *get_dev_property(DevVarStringArray *);
	const DevVarStringArray *get_dev_list(DevVarStringArray *);
	const DevVarStringArray *get_class_att_property(DevVarStringArray *);
	const DevVarStringArray *get_dev_att_property(DevVarStringArray *);
	const DevVarStringArray *get_obj_property(DevVarStringArray *);
	const DevVarStringArray *get_device_property_list(DevVarStringArray *);
	const DevVarLongStringArray *import_tac_dev(string &);

	const EltIdx &get_imp_dat() {return imp_adm;}
	const EltIdx &get_imp_notifd_event() {return imp_notifd_event;}
	const EltIdx &get_imp_adm_event() {return imp_adm_event;}
	const PropEltIdx &get_DServer_class_prop() {return DServer_class_prop;}
	const PropEltIdx &get_Default_prop() {return Default_prop;}
	const PropEltIdx &get_adm_dev_prop() {return adm_dev_prop;}
	const PropEltIdx &get_ctrl_serv_prop() {return ctrl_serv_prop;}
	int get_class_nb() {return class_nb;}
	const ClassEltIdx *get_classes_elt() {return classes_idx;}
	int get_data_nb() {return n_data;}

private:
	void prop_indexes(int &,int &,PropEltIdx &,const DevVarStringArray *);
	void prop_att_indexes(int &,int &,AttPropEltIdx &,const DevVarStringArray *);
	void get_obj_prop(DevVarStringArray *,PropEltIdx &,bool dev_prop=false);
	int find_class(DevString );
	int find_dev_att(DevString,int &,int &);
	int find_obj(DevString obj_name,int &);
	void get_obj_prop_list(DevVarStringArray *,PropEltIdx &);

	CORBA::Any_var			received;
	const DevVarStringArray *data_list;
	int 					n_data;

	EltIdx					imp_adm;
	EltIdx					imp_notifd_event;
	EltIdx					imp_adm_event;
	EltIdx                  imp_tac;
	PropEltIdx				ctrl_serv_prop;
	PropEltIdx				DServer_class_prop;
	PropEltIdx				Default_prop;
	PropEltIdx				adm_dev_prop;
	int 					class_nb;
	ClassEltIdx				*classes_idx;

	DevVarLongStringArray	imp_adm_data;
	DevVarLongStringArray	imp_notifd_event_data;
	DevVarLongStringArray	imp_adm_event_data;
	DevVarLongStringArray	imp_tac_data;
	DevVarStringArray		ret_obj_prop;
	DevVarStringArray		ret_dev_list;
	DevVarStringArray		ret_obj_att_prop;
	DevVarStringArray		ret_prop_list;
};

/****************************************************************************************
 * 																						*
 * 					The DbServerData class												*
 * 					----------------													*
 * 																						*
 ***************************************************************************************/

//
// DbServerData object to implement the features required to move a complete device server proces
// configuration from one database to another one
//

class DbServerData
{
private:
    struct TangoProperty
    {
        string   		name;
        vector<string> 	values;

        TangoProperty(string &na, vector<string> &val):name(na),values(val) {}
	};

    struct TangoAttribute: vector<TangoProperty>
    {
        string   				name;

        TangoAttribute(string na):name(na) {}
    };

	struct TangoDevice: DeviceProxy
	{
        string 	name;
        vector<TangoProperty>   properties;
        vector<TangoAttribute>  attributes;

        TangoDevice(string &);

		string get_name() {return name;}
        vector<TangoProperty> &get_properties() {return properties;}
        vector<TangoAttribute> &get_attributes() {return attributes;}

		void put_properties(Database *);
        void put_attribute_properties(Database *);
	};

	struct TangoClass: vector<TangoDevice>
	{
        string  name;
        vector<TangoProperty>   	properties;
        vector<TangoAttribute>   	attributes;

		TangoClass(const string &,const string &,Database *);

		string get_name() {return name;}
        vector<TangoProperty> &get_properties() {return properties;}
        vector<TangoAttribute> &get_attributes() {return attributes;}

		void put_properties(Database *);
        void put_attribute_properties(Database *);
        void remove_properties(Database *);
	};

	void create_server(Database *);
	void put_properties(Database *);

	string   			full_server_name;
    vector<TangoClass>  classes;

public:
	DbServerData(const string &,const string &);
	~DbServerData() {}

	const string &get_name() {return full_server_name;}
	vector<TangoClass> &get_classes() {return classes;}

	void put_in_database(const string &);
	bool already_exist(const string &);
	void remove();
	void remove(const string &);
};

} // End of Tango namespace

#endif /* _DBAPI_H */
