// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
/* #####################################################################
   apt-sources - show and change sources.list data
   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#include <config.h>

#include <apt-pkg/cmndline.h>
#include <apt-pkg/error.h>
#include <apt-pkg/init.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/sourcelist.h>
#include <apt-pkg/metaindex.h>
#include <apt-pkg/indexfile.h>

#include <apt-private/private-output.h>
#include <apt-private/private-cmndline.h>

#include <apti18n.h>
									/*}}}*/
using namespace std;


// DoList - Show sources.list as lines					/*{{{*/
// ---------------------------------------------------------------------
/* */
bool DoList(CommandLine &CmdL)
{
   pkgSourceList SrcList;
   SrcList.ReadMainList();
   for(pkgSourceList::const_iterator I = SrcList.begin();
       I != SrcList.end(); 
       ++I)
   {
      std::cout << (*I)->GetSourceEntry() << std::endl;
   }

   return true;
}
									/*}}}*/

// ShowHelp - Show a help screen					/*{{{*/
// ---------------------------------------------------------------------
/* */
bool ShowHelp(CommandLine &CmdL)
{
   ioprintf(cout,_("%s %s for %s compiled on %s %s\n"),PACKAGE,PACKAGE_VERSION,
	    COMMON_ARCH,__DATE__,__TIME__);

   cout <<
    _("Usage: apt-show COMMAND [options]\n"
      "\n"
      "apt-sources is a simple command line interface for sources.list "
      "management\n"
      "\n"
      "Commands:\n"
      "   list - Show all available sources\n"
      "\n"
      "Options:\n"
      "  -h  This help text.\n"
      "  -c=? Read this configuration file\n"
      "  -o=? Set an arbitrary configuration option, eg -o dir::cache=/tmp\n"
      "See the apt-sources(8) and apt.conf(5) manual pages for more information.")
      << std::endl;
   return true;
}
									/*}}}*/
int main(int argc,const char *argv[])					/*{{{*/
{
   CommandLine::Dispatch Cmds[] = {{"help",&ShowHelp},
				   {"list",&DoList},
                                   {0,0}};

   std::vector<CommandLine::Args> Args = getCommandArgs("apt-sources", CommandLine::GetCommand(Cmds, argc, argv));

   // Set up gettext support
   setlocale(LC_ALL,"");
   textdomain(PACKAGE);

   // Parse the command line and initialize the package library
   CommandLine CmdL(Args.data(),_config);
   if (pkgInitConfig(*_config) == false ||
       CmdL.Parse(argc,argv) == false ||
       pkgInitSystem(*_config,_system) == false)
   {
      if (_config->FindB("version") == true)
	 ShowHelp(CmdL);
      _error->DumpErrors();
      return 100;
   }

   // See if the help should be shown
   if (_config->FindB("help") == true ||
       _config->FindB("version") == true ||
       CmdL.FileSize() == 0)
   {
      ShowHelp(CmdL);
      return 0;
   }

   // Deal with stdout not being a tty
   InitOutput();

   // Match the operation
   CmdL.DispatchArg(Cmds);

   // Print any errors or warnings found during parsing
   bool const Errors = _error->PendingError();
   if (_config->FindI("quiet",0) > 0)
      _error->DumpErrors();
   else
      _error->DumpErrors(GlobalError::DEBUG);
   return Errors == true ? 100 : 0;
}
									/*}}}*/
