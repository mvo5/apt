// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
/* ######################################################################
   
   apt - CLI UI for apt
   
   Returns 100 on failure, 0 on success.
   
   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#include<config.h>

#include <cassert>
#include <locale.h>
#include <iostream>
#include <unistd.h>
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <iomanip>
#include <algorithm>


#include <apt-pkg/error.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/cacheset.h>
#include <apt-pkg/init.h>
#include <apt-pkg/progress.h>
#include <apt-pkg/sourcelist.h>
#include <apt-pkg/cmndline.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/pkgrecords.h>
#include <apt-pkg/srcrecords.h>
#include <apt-pkg/version.h>
#include <apt-pkg/policy.h>
#include <apt-pkg/tagfile.h>
#include <apt-pkg/algorithms.h>
#include <apt-pkg/sptr.h>
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/indexfile.h>
#include <apt-pkg/metaindex.h>

#include <apti18n.h>

#include <apt-private/private-list.h>
#include <apt-private/private-install.h>
#include <apt-private/private-output.h>
									/*}}}*/

int main(int argc,const char *argv[])					/*{{{*/
{
   CommandLine::Args Args[] = {
      {0,"installed","APT::Cmd::Installed",0},
      {0,"upgradable","APT::Cmd::Upgradable",0},
      {0,0,0,0}};
   CommandLine::Dispatch Cmds[] = {{"list",&List},
                                   {"install",&DoInstall},
                                   {"old-upgrade",&DoUpgrade},
                                   {"safe-upgrade",&DoSafeUpgrade},
                                   {0,0}};

   if(!isatty(1))
      std::cerr << std::endl
                << "WARNING WARNING "
                << argv[0]
                << " is *NOT* intended for scripts "
                << "use at your own peril^Wrisk"
                << std::endl
                << std::endl;

   InitOutput();

   // Set up gettext support
   setlocale(LC_ALL,"");
   textdomain(PACKAGE);

    if(pkgInitConfig(*_config) == false) 
    {
        _error->DumpErrors();
        return 100;
    }

   // FIXME: move into a new libprivate/private-install.cc:Install()
   _config->Set("DPkgPM::Progress", "1");

   // Parse the command line and initialize the package library
   CommandLine CmdL(Args, _config);
   if (CmdL.Parse(argc, argv) == false ||
       pkgInitSystem(*_config, _system) == false)
   {
      _error->DumpErrors();
      return 100;
   }

   CmdL.DispatchArg(Cmds);
}
									/*}}}*/
