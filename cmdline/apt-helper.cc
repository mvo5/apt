// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
/* #####################################################################
   apt-helper - cmdline helpers
   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#include <config.h>

#include <apt-pkg/configuration.h>
#include <apt-pkg/cmndline.h>
#include <apt-pkg/error.h>
#include <apt-pkg/init.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/acquire.h>
#include <apt-pkg/acquire-item.h>
#include <apt-pkg/proxy.h>

#include <apt-private/acqprogress.h>
#include <apt-private/private-output.h>
#include <apt-private/private-download.h>
#include <apt-private/private-cmndline.h>
#include <apt-private/private-main.h>
#include <apt-pkg/srvrec.h>

#include <iostream>
#include <string>
#include <vector>

#include <apti18n.h>
									/*}}}*/

static bool DoAutoDetectProxy(CommandLine &CmdL)			/*{{{*/
{
   if (CmdL.FileSize() != 2)
      return _error->Error(_("Need one URL as argument"));
   URI ServerURL(CmdL.FileList[1]);
   AutoDetectProxy(ServerURL);
   std::string SpecificProxy = _config->Find("Acquire::"+ServerURL.Access+"::Proxy::" + ServerURL.Host);
   ioprintf(std::cout, "Using proxy '%s' for URL '%s'\n",
            SpecificProxy.c_str(), std::string(ServerURL).c_str());

   return true;
}
									/*}}}*/
static bool DoDownloadFile(CommandLine &CmdL)				/*{{{*/
{
   if (CmdL.FileSize() <= 2)
      return _error->Error(_("Must specify at least one pair url/filename"));

   aptAcquireWithTextStatus Fetcher;
   size_t fileind = 0;
   std::vector<std::string> targetfiles;
   while (fileind + 2 <= CmdL.FileSize())
   {
      std::string download_uri = CmdL.FileList[fileind + 1];
      std::string targetfile = CmdL.FileList[fileind + 2];
      std::string hash;
      if (CmdL.FileSize() > fileind + 3)
	 hash = CmdL.FileList[fileind + 3];
      // we use download_uri as descr and targetfile as short-descr
      new pkgAcqFile(&Fetcher, download_uri, hash, 0, download_uri, targetfile,
	    "dest-dir-ignored", targetfile);
      targetfiles.push_back(targetfile);
      fileind += 3;
   }

   bool Failed = false;
   if (AcquireRun(Fetcher, 0, &Failed, NULL) == false || Failed == true)
      return _error->Error(_("Download Failed"));
   if (targetfiles.empty() == false)
      for (std::vector<std::string>::const_iterator f = targetfiles.begin(); f != targetfiles.end(); ++f)
	 if (FileExists(*f) == false)
	    return _error->Error(_("Download Failed"));

   return true;
}
									/*}}}*/
static bool DoSrvLookup(CommandLine &CmdL)				/*{{{*/
{
   if (CmdL.FileSize() <= 1)
      return _error->Error("Must specify at least one SRV record");

   for(size_t i = 1; CmdL.FileList[i] != NULL; ++i)
   {
      std::vector<SrvRec> srv_records;
      std::string const name = CmdL.FileList[i];
      c0out << "# Target\tPriority\tWeight\tPort # for " << name << std::endl;
      size_t const found = name.find(":");
      if (found != std::string::npos)
      {
	 std::string const host = name.substr(0, found);
	 size_t const port = atoi(name.c_str() + found + 1);
	 if(GetSrvRecords(host, port, srv_records) == false)
	    _error->Error(_("GetSrvRec failed for %s"), name.c_str());
      }
      else if(GetSrvRecords(name, srv_records) == false)
	 _error->Error(_("GetSrvRec failed for %s"), name.c_str());

      for (SrvRec const &I : srv_records)
	 c1out << I.target << "\t" << I.priority << "\t" << I.weight << "\t" << I.port << std::endl;
   }
   return true;
}
									/*}}}*/
bool ShowHelp(CommandLine &, CommandLine::DispatchWithHelp const  * Cmds)/*{{{*/
{
   ioprintf(std::cout, "%s %s (%s)\n", PACKAGE, PACKAGE_VERSION, COMMON_ARCH);

   if (_config->FindB("version") == true)
     return true;

   std::cout <<
    _("Usage: apt-helper [options] command\n"
      "       apt-helper [options] download-file uri target-path\n"
      "\n"
      "apt-helper is a internal helper for apt\n")
    << std::endl
    << _("Commands:") << std::endl;

   for (; Cmds->Handler != nullptr; ++Cmds)
   {
      if (Cmds->Help == nullptr)
	 continue;
      std::cout << "  " << Cmds->Match << " - " << Cmds->Help << std::endl;
   }

   std::cout << std::endl <<
      _("This APT helper has Super Meep Powers.") << std::endl;
   return true;
}
									/*}}}*/
std::vector<CommandLine::DispatchWithHelp> GetCommands()		/*{{{*/
{
   return {
      {"download-file", &DoDownloadFile, _("download the given uri to the target-path")},
      {"srv-lookup", &DoSrvLookup, _("lookup a SRV record (e.g. _http._tcp.ftp.debian.org)")},
      {"auto-detect-proxy", &DoAutoDetectProxy, _("detect proxy using apt.conf")},
      {nullptr, nullptr, nullptr}
   };
}
									/*}}}*/
int main(int argc,const char *argv[])					/*{{{*/
{
   InitLocale();

   CommandLine CmdL;
   auto const Cmds = ParseCommandLine(CmdL, APT_CMD::APT_HELPER, &_config, &_system, argc, argv);

   InitOutput();

   return DispatchCommandLine(CmdL, Cmds);
}
									/*}}}*/
