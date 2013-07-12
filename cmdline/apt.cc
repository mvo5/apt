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
#include <apt-private/private-update.h>
#include <apt-private/private-update.h>
									/*}}}*/

bool ShowHelp(CommandLine &CmdL)
{
   ioprintf(c1out,_("%s %s for %s compiled on %s %s\n"),PACKAGE,PACKAGE_VERSION,
	    COMMON_ARCH,__DATE__,__TIME__);

   // FIXME: generate from CommandLine
   c1out << 
    _("Usage: apt [options] command\n"
      "\n"
      "CLI for apt.\n"
      "Commands: \n"
      " list - list packages\n"
      " update - update list of available packages\n"
      " install - install packages\n"
      " upgrade - upgrade the systems packages\n"
       );
   
   return true;
}

#include <sstream>
bool DoMoo(CommandLine &CmdL)
{
   signed short SuperCow = 1;
   if (CmdL.FileSize() != 0)
      for (const char **Moo = CmdL.FileList + 1; *Moo != 0; Moo++)
         if (strcasecmp(*Moo, "moo") == 0)
            SuperCow++;
   switch(SuperCow) {
     case 1:
      c1out <<
         "         (__) \n"
         "         (oo) \n"
         "   /------\\/ \n"
         "  / |    ||   \n"
         " *  /\\---/\\ \n"
         "    ~~   ~~   \n"
         "...\"Have you mooed today?\"...\n";
   break;
   case 2:
      // by Fernando Ribeiro in lp:56125
      c1out <<
         "         (__)  \n"
         " _______~(..)~ \n"
         "   ,----\\(oo) \n"
         "  /|____|,'    \n"
         " * /\"\\ /\\   \n"
         "   ~ ~ ~ ~     \n"
         "...\"Have you mooed today?\"...\n";
      break;
   case 3:
      c1out << 
         "        (__)\n"
         "        (oo)\n"
         "  /-----(__)\n"
         " / |   ||   \n"
         "* /\\---/\\   \n"
         "  ~~   ~~   \n"
        "\n";
      break;
   case 4:
      // by Paul TagLiamonte
      c1out << 
         "              _     _\n"
         "             (_\\___( \\,\n"
         "               )___   _  have you smashed some milk today?\n"
         "              /( (_)-(_)    /\n"
         "   ,---------'         \\_\n"
         " //(  ',__,'      \\  (' ')\n"
         "//  )              '----'\n"
         "'' ; \\     .--.  ,/\n"
         "   | )',_,'----( ;\n"
         "   ||| '''     '||\n";
      break;
   case 5:
      c1out << 
         "         [1;97m([0;33m__[1;97m)[0m\n"
         " [31m_______[33m~([1;34m..[0;33m)~[0m\n"
         "   [33m,----[31m\\[33m([1;4;35moo[0;33m)[0m\n"
         "  [33m/|____|,'[0m\n"
         " [1;5;97m*[0;33m /\\  /\\[0m\n"
         "[92;42mWwWwWwWwWwWwWwW[0m\n"
         "[92;42m  ... [1;36mmoo⁈[0;92;42m ... [0m\n"
         "\n";
      break;
   }

    return true;
}

int main(int argc,const char *argv[])					/*{{{*/
{
   CommandLine::Args Args[] = {
      {0,"installed","APT::Cmd::Installed",0},
      {0,"upgradable","APT::Cmd::Upgradable",0},
      {0,0,0,0}};
   CommandLine::Dispatch Cmds[] = {{"list",&List},
                                   // needs root
                                   {"install",&DoInstall},
                                   {"remove", &DoInstall},
                                   {"update",&DoUpdate},
                                   {"upgrade",&DoUpgradeWithAllowNewPackages},
                                   // helper
                                   {"moo",&DoMoo},
                                   {"help",&ShowHelp},
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
   _config->Set("Apt::Color", "1");

   // Parse the command line and initialize the package library
   CommandLine CmdL(Args, _config);
   if (CmdL.Parse(argc, argv) == false ||
       pkgInitSystem(*_config, _system) == false)
   {
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

   CmdL.DispatchArg(Cmds);
}
									/*}}}*/
