//
// crash_report.h - include file for TANGO crash report service (NL@SOLEIL)
// 
// Copyright (C) : 2004,2005,2006,2007,2008,2009,2010
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

#if defined(ENABLE_CRASH_REPORT)

//-----------------------------------------------------------------------------
// DEPENDENCIES
//-----------------------------------------------------------------------------
#include <stdlib.h>
#include <ctime>
#include <fstream>
#include "tango_config.h"
#if defined(_TG_WINDOWS_)
# include <direct.h>
# include <winsock2.h>
#else
# include <sys/stat.h>
# include <netdb.h>   
# include <arpa/inet.h>
# include <netinet/in.h>
#endif
#include "breakpad/exception_handler.h"

//-----------------------------------------------------------------------------
// class: TangoCrashHandler (*** supposed to be used as a singleton ***)
//-----------------------------------------------------------------------------
class TangoCrashHandler 
{
public:
  //- ctor ----------------------
  explicit TangoCrashHandler (int argc, char *argv[])
    : m_dsn("unknown"), m_dsi("unknown"), m_dp("."), m_eh(0)
  {
    //- argv[0]: contains tango device-server binary name
    m_dsn = this->ds_binary_name(argv[0]);

    //- argv[1]: contains the tango device-server instance name
    if (argc > 1) m_dsi = argv[1];

    //- build minidump destination path
    std::stringstream dp;

    //- is the default minidump path overwritten by env. var?
    const char * tgcp = ::getenv("TANGO_CRASH_PATH");
    if (tgcp && ::strlen(tgcp))
    {
      //- TANGO_CRASH_PATH exists and isn't empty: use it
#if defined(_TG_WINDOWS_)
      dp << tgcp << "\\" << m_dsn;
#else
      dp << tgcp << "/" << m_dsn;
#endif
    }
    else
    {
      //- TANGO_CRASH_PATH is either missing or empty: use default path
#if defined(_TG_WINDOWS_)
      dp << "c:\\tango\\" << m_dsn;
#else
      dp << "/tmp/tango/" << m_dsn;
#endif
    }

    //- store the minidump path locally
    m_dp = dp.str();

    //- cleanup path for current platform
    this->cleanup_crash_path(m_dp);

    //- create the associated directory
    this->create_crash_directory(m_dp);

    //- tell the world what's the actual minidump path is...
    std::cout << "Crash report enabled [minidump will be generated in " 
              << m_dp 
              << "]" 
              << std::endl;

    //- instanciate the underlying google_breakpad::ExceptionHandler
#if defined(_TG_WINDOWS_)
    //- windows api offers a way to filter the minidump content
    //- according to http://www.debuginfo.com/articles/effminidumps2.html
    //- the following seems to be a good compromise between the minidump 
    //- file size and the "nature" of its debugging information 
    MINIDUMP_TYPE mdt = (MINIDUMP_TYPE)(
                                        MiniDumpWithDataSegs | 
                                        MiniDumpWithFullMemory | 
                                        MiniDumpWithHandleData |
                                        MiniDumpWithUnloadedModules |
		                                    MiniDumpWithProcessThreadData |
                                        MiniDumpWithPrivateReadWriteMemory
                                       ); 
    std::wstring wdp(m_dp.length(), L' ');
    std::copy(m_dp.begin(), m_dp.end(), wdp.begin());
    int ht = google_breakpad::ExceptionHandler::HANDLER_ALL;
    m_eh = new google_breakpad::ExceptionHandler(wdp, 
                                                 0, 
                                                 minidump_callback,
                                                 static_cast<void*>(this), 
                                                 ht, 
                                                 mdt, 
                                                 0, 
                                                 0);
#else
    m_eh = new google_breakpad::ExceptionHandler(m_dp, 
                                                 0, 
                                                 minidump_callback, 
                                                 static_cast<void*>(this), 
                                                 true);
#endif

    //- store singleton
    TangoCrashHandler::singleton = this;
  }

  //- dtor ---------------------- 
  ~TangoCrashHandler ()
  {
    delete m_eh;
  }

  //- dump_current_exec_state ---
  static inline void dump_current_exec_state ()
  {
    TangoCrashHandler * s = TangoCrashHandler::singleton;
    if (s && s->m_eh)
    {
#if defined(_TG_WINDOWS_)
      std::wstring wdp(s->m_dp.length(), L' ');
      std::copy(s->m_dp.begin(), s->m_dp.end(), wdp.begin());
      s->m_eh->WriteMinidump(wdp, 0, 0);
#else
      s->m_eh->WriteMinidump(s->m_dp, 0, 0);
#endif
    }
  }

private:
#if defined(_TG_WINDOWS_)
  //- callback function called after the minidump has been written: win32 impl.
  static bool minidump_callback (
                                  const wchar_t *dump_path,
                                  const wchar_t *minidump_id,
                                  void *context,
                                  EXCEPTION_POINTERS *exinfo,
                                  MDRawAssertionInfo *assertion,
                                  bool succeeded
                                )
#else
  //- callback function called after the minidump has been written: posix impl.
  static bool minidump_callback (
                                  const char *dump_path,
                                  const char *minidump_id,
                                  void *context,
                                  bool succeeded
                                 )
#endif
  {
    //- WARNING: HERE WE DO A LOT OF "THINGS" IN THE CONTEXT OF A CRASH!
    //- WARNING: WE MIGHT HAVE TO REDUCE THE TXT FILE CONTENT TO THE MINIMUM
    //- WARNING: MINIMUM REQUIRED IS THE DSERVER NAME. EXPERIENCE WILL TELL!

    //- get ref. to the TangoCrashHandler singleton
    TangoCrashHandler * tch = reinterpret_cast<TangoCrashHandler*>(context);
    if (! tch) return succeeded;

    //- generate tango specific crash info (device server indentification)
    try
    {
      //- get device server host name
      std::string host_name("unknown");
      std::string host_ipaddr("0.0.0.0");
      char host_name_cstr[128];
      if (! ::gethostname(host_name_cstr, 128))
      {
        host_name = host_name_cstr;
        struct hostent * host = ::gethostbyname(host_name_cstr);
        if (host)
          host_ipaddr = ::inet_ntoa(*((struct in_addr*)host->h_addr));
      }
      //- get TANGO_HOST env. var
      std::string tango_host_name("unknown");
      const char * tango_host_env_var = ::getenv("TANGO_HOST");
      if (tango_host_env_var && ::strlen(tango_host_env_var))
        tango_host_name = tango_host_env_var;
      //- don't even think about resolving the tango-db addr in this context!
#if defined(_TG_WINDOWS_)
      //- build 'wide' filename
      std::wstringstream wss;
      wss << dump_path << L"\\" << minidump_id;
      std::wstring w_file_name(wss.str());
      //- narrow 'wide' filename
      std::string n_file_name(w_file_name.begin(), w_file_name.end());
      n_file_name.assign(w_file_name.begin(), w_file_name.end());
#else
      //- build 'narrow' filename
      std::stringstream nss;
      nss << dump_path << "/" << minidump_id;
      std::string n_file_name(nss.str());
#endif
      //- create/open file
      std::string txt_file_name = n_file_name + ".txt";
      std::fstream file(txt_file_name.c_str(), std::fstream::out | std::fstream::trunc);
      if (file.fail()) return succeeded;
      //- get curent time
      time_t now = ::time(0);
      //- write file content
      file << "Tango Device Server Crash Report" << std::endl << std::endl;
      file << "Timestamp: " << ::ctime(&now);
      file << "Device Server Host: " << host_name << " (" << host_ipaddr << ")" << std::endl; 
      file << "Device Server Name: " << tch->m_dsn << "/" << tch->m_dsi << std::endl;
      file << "Tango Database: " << tango_host_name << std::endl; 
      file << "Minidump File: " << n_file_name << ".dmp" << std::endl;
      //- done! close the file
      file.close();
    }
    catch (...)
    {
      //- ignore any error
    }

    return succeeded;
  }

  //- cleanup_crash_path --------
  void cleanup_crash_path (std::string& full_crash_path)
  {
#if defined(_TG_WINDOWS_)
    const std::string right_sep("\\");
    const std::string wrong_sep("/");
#else
    const std::string right_sep("/");
    const std::string wrong_sep("\\");
#endif
    do
    {
      std::string::size_type pos = full_crash_path.rfind(wrong_sep);
      if (pos == std::string::npos)
        break;
      full_crash_path.replace(pos, 1, right_sep);
    } while (1);
  }

  //- create_crash_directory ----
  int create_crash_directory (const std::string& full_crash_path)
  {
#if defined(_TG_WINDOWS_)
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    std::string::size_type pos = full_crash_path.rfind(sep);
    if (pos != std::string::npos) 
    {
      std::string sub_crash_path;
      sub_crash_path.assign(full_crash_path.begin(), full_crash_path.begin() + pos);
      if (sub_crash_path.size())
        this->create_crash_directory(sub_crash_path);    
    }
#if defined(_TG_WINDOWS_)
    int res = ::_mkdir(full_crash_path.c_str());
#else
    int res = ::mkdir(full_crash_path.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);    
#endif
    return res;
  }

  //- ds_binary_name -----------
  std::string ds_binary_name (const std::string & argv0)
  {
#if defined(_TG_WINDOWS_)
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    std::string::size_type pos = argv0.rfind(sep);
    if (pos == std::string::npos) 
      return argv0;
    return argv0.substr(pos + 1);
  }
  
  //- device-server binary name
  std::string m_dsn;

  //- device-server instance name
  std::string m_dsi;

  //- mini dump file destination path
  std::string m_dp;

  //- google breakpad exception handler
  google_breakpad::ExceptionHandler * m_eh;

  //- TangoCrashHandler singleton
  static TangoCrashHandler * singleton;
};

#define DECLARE_CRASH_HANDLER \
  TangoCrashHandler * TangoCrashHandler::singleton = 0

#define INSTALL_CRASH_HANDLER \
  TangoCrashHandler tch(argc, argv)

#else //- ENABLE_CRASH_REPORT

# define DECLARE_CRASH_HANDLER
# define INSTALL_CRASH_HANDLER

#endif //- ENABLE_CRASH_REPORT
