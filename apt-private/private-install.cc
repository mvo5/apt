// Include Files							/*{{{*/
#include <config.h>

#include <apt-pkg/acquire.h>
#include <apt-pkg/acquire-item.h>
#include <apt-pkg/algorithms.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/cacheset.h>
#include <apt-pkg/cmndline.h>
#include <apt-pkg/depcache.h>
#include <apt-pkg/error.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/pkgrecords.h>
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/sptr.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/cacheiterators.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/macros.h>
#include <apt-pkg/packagemanager.h>
#include <apt-pkg/pkgcache.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <algorithm>
#include <iostream>
#include <set>
#include <vector>

#include <apt-private/acqprogress.h>
#include <apt-private/private-install.h>
#include <apt-private/private-cachefile.h>
#include <apt-private/private-cacheset.h>
#include <apt-private/private-download.h>
#include <apt-private/private-output.h>

#include <apti18n.h>
									/*}}}*/
class pkgSourceList;

// InstallPackages - Actually download and install the packages		/*{{{*/
// ---------------------------------------------------------------------
/* This displays the informative messages describing what is going to 
   happen and then calls the download routines */
bool InstallPackages(CacheFile &Cache,bool ShwKept,bool Ask, bool Safety)
{
   if (_config->FindB("APT::Get::Purge",false) == true)
   {
      pkgCache::PkgIterator I = Cache->PkgBegin();
      for (; I.end() == false; ++I)
      {
	 if (I.Purge() == false && Cache[I].Mode == pkgDepCache::ModeDelete)
	    Cache->MarkDelete(I,true);
      }
   }
   
   bool Fail = false;
   bool Essential = false;
   
   // Show all the various warning indicators
   ShowDel(c1out,Cache);
   ShowNew(c1out,Cache);
   if (ShwKept == true)
      ShowKept(c1out,Cache);
   Fail |= !ShowHold(c1out,Cache);
   if (_config->FindB("APT::Get::Show-Upgraded",true) == true)
      ShowUpgraded(c1out,Cache);
   Fail |= !ShowDowngraded(c1out,Cache);
   if (_config->FindB("APT::Get::Download-Only",false) == false)
        Essential = !ShowEssential(c1out,Cache);
   Fail |= Essential;
   Stats(c1out,Cache);

   // Sanity check
   if (Cache->BrokenCount() != 0)
   {
      ShowBroken(c1out,Cache,false);
      return _error->Error(_("Internal error, InstallPackages was called with broken packages!"));
   }

   if (Cache->DelCount() == 0 && Cache->InstCount() == 0 &&
       Cache->BadCount() == 0)
      return true;

   // No remove flag
   if (Cache->DelCount() != 0 && _config->FindB("APT::Get::Remove",true) == false)
      return _error->Error(_("Packages need to be removed but remove is disabled."));
       
   // Run the simulator ..
   if (_config->FindB("APT::Get::Simulate") == true)
   {
      pkgSimulate PM(Cache);

#if (APT_PKG_MAJOR >= 4 && APT_PKG_MINOR >= 13)
      APT::Progress::PackageManager *progress = APT::Progress::PackageManagerProgressFactory();
      pkgPackageManager::OrderResult Res = PM.DoInstall(progress);
      delete progress;
#else
      int status_fd = _config->FindI("APT::Status-Fd",-1);
      pkgPackageManager::OrderResult Res = PM.DoInstall(status_fd);
#endif

      if (Res == pkgPackageManager::Failed)
	 return false;
      if (Res != pkgPackageManager::Completed)
	 return _error->Error(_("Internal error, Ordering didn't finish"));
      return true;
   }
   
   // Create the text record parser
   pkgRecords Recs(Cache);
   if (_error->PendingError() == true)
      return false;

   // Create the download object
   pkgAcquire Fetcher;
   AcqTextStatus Stat(ScreenWidth,_config->FindI("quiet",0));   
   if (_config->FindB("APT::Get::Print-URIs", false) == true)
   {
      // force a hashsum for compatibility reasons
      _config->CndSet("Acquire::ForceHash", "md5sum");
   }
   else if (Fetcher.Setup(&Stat, _config->FindDir("Dir::Cache::Archives")) == false)
      return false;

   // Read the source list
   if (Cache.BuildSourceList() == false)
      return false;
   pkgSourceList *List = Cache.GetSourceList();
   
   // Create the package manager and prepare to download
   SPtr<pkgPackageManager> PM= _system->CreatePM(Cache);
   if (PM->GetArchives(&Fetcher,List,&Recs) == false || 
       _error->PendingError() == true)
      return false;

   // Display statistics
   unsigned long long FetchBytes = Fetcher.FetchNeeded();
   unsigned long long FetchPBytes = Fetcher.PartialPresent();
   unsigned long long DebBytes = Fetcher.TotalNeeded();
   if (DebBytes != Cache->DebSize())
   {
      c0out << DebBytes << ',' << Cache->DebSize() << std::endl;
      c0out << _("How odd... The sizes didn't match, email apt@packages.debian.org") << std::endl;
   }
   
   // Number of bytes
   if (DebBytes != FetchBytes)
      //TRANSLATOR: The required space between number and unit is already included
      // in the replacement strings, so %sB will be correctly translate in e.g. 1,5 MB
      ioprintf(c1out,_("Need to get %sB/%sB of archives.\n"),
	       SizeToStr(FetchBytes).c_str(),SizeToStr(DebBytes).c_str());
   else if (DebBytes != 0)
      //TRANSLATOR: The required space between number and unit is already included
      // in the replacement string, so %sB will be correctly translate in e.g. 1,5 MB
      ioprintf(c1out,_("Need to get %sB of archives.\n"),
	       SizeToStr(DebBytes).c_str());

   // Size delta
   if (Cache->UsrSize() >= 0)
      //TRANSLATOR: The required space between number and unit is already included
      // in the replacement string, so %sB will be correctly translate in e.g. 1,5 MB
      ioprintf(c1out,_("After this operation, %sB of additional disk space will be used.\n"),
	       SizeToStr(Cache->UsrSize()).c_str());
   else
      //TRANSLATOR: The required space between number and unit is already included
      // in the replacement string, so %sB will be correctly translate in e.g. 1,5 MB
      ioprintf(c1out,_("After this operation, %sB disk space will be freed.\n"),
	       SizeToStr(-1*Cache->UsrSize()).c_str());

   if (_error->PendingError() == true)
      return false;

   /* Check for enough free space, but only if we are actually going to
      download */
   if (_config->FindB("APT::Get::Print-URIs") == false &&
       _config->FindB("APT::Get::Download",true) == true)
   {
      struct statvfs Buf;
      std::string OutputDir = _config->FindDir("Dir::Cache::Archives");
      if (statvfs(OutputDir.c_str(),&Buf) != 0) {
	 if (errno == EOVERFLOW)
	    return _error->WarningE("statvfs",_("Couldn't determine free space in %s"),
				 OutputDir.c_str());
	 else
	    return _error->Errno("statvfs",_("Couldn't determine free space in %s"),
				 OutputDir.c_str());
      } else if (unsigned(Buf.f_bfree) < (FetchBytes - FetchPBytes)/Buf.f_bsize)
      {
         struct statfs Stat;
         if (statfs(OutputDir.c_str(),&Stat) != 0
#if HAVE_STRUCT_STATFS_F_TYPE
             || unsigned(Stat.f_type) != RAMFS_MAGIC
#endif
             )
            return _error->Error(_("You don't have enough free space in %s."),
                OutputDir.c_str());
      }
   }
   
   // Fail safe check
   if (_config->FindI("quiet",0) >= 2 ||
       _config->FindB("APT::Get::Assume-Yes",false) == true)
   {
      if (Fail == true && _config->FindB("APT::Get::Force-Yes",false) == false)
	 return _error->Error(_("There are problems and -y was used without --force-yes"));
   }         

   if (Essential == true && Safety == true)
   {
      if (_config->FindB("APT::Get::Trivial-Only",false) == true)
	 return _error->Error(_("Trivial Only specified but this is not a trivial operation."));

      // TRANSLATOR: This string needs to be typed by the user as a confirmation, so be
      //             careful with hard to type or special characters (like non-breaking spaces)
      const char *Prompt = _("Yes, do as I say!");
      ioprintf(c2out,
	       _("You are about to do something potentially harmful.\n"
		 "To continue type in the phrase '%s'\n"
		 " ?] "),Prompt);
      c2out << std::flush;
      if (AnalPrompt(Prompt) == false)
      {
	 c2out << _("Abort.") << std::endl;
	 exit(1);
      }     
   }
   else
   {      
      // Prompt to continue
      if (Ask == true || Fail == true)
      {            
	 if (_config->FindB("APT::Get::Trivial-Only",false) == true)
	    return _error->Error(_("Trivial Only specified but this is not a trivial operation."));
	 
	 if (_config->FindI("quiet",0) < 2 &&
	     _config->FindB("APT::Get::Assume-Yes",false) == false)
	 {
            c2out << _("Do you want to continue?") << std::flush;
 	    if (YnPrompt() == false)
	    {
	       c2out << _("Abort.") << std::endl;
	       exit(1);
	    }     
	 }	 
      }      
   }
   
   // Just print out the uris an exit if the --print-uris flag was used
   if (_config->FindB("APT::Get::Print-URIs") == true)
   {
      pkgAcquire::UriIterator I = Fetcher.UriBegin();
      for (; I != Fetcher.UriEnd(); ++I)
	 std::cout << '\'' << I->URI << "' " << flNotDir(I->Owner->DestFile) << ' ' <<
	       I->Owner->FileSize << ' ' << I->Owner->HashSum() << std::endl;
      return true;
   }

   if (!CheckAuth(Fetcher, true))
      return false;

   /* Unlock the dpkg lock if we are not going to be doing an install
      after. */
   if (_config->FindB("APT::Get::Download-Only",false) == true)
      _system->UnLock();
   
   // Run it
   while (1)
   {
      bool Transient = false;
      if (_config->FindB("APT::Get::Download",true) == false)
      {
	 for (pkgAcquire::ItemIterator I = Fetcher.ItemsBegin(); I < Fetcher.ItemsEnd();)
	 {
	    if ((*I)->Local == true)
	    {
	       ++I;
	       continue;
	    }

	    // Close the item and check if it was found in cache
	    (*I)->Finished();
	    if ((*I)->Complete == false)
	       Transient = true;
	    
	    // Clear it out of the fetch list
	    delete *I;
	    I = Fetcher.ItemsBegin();
	 }	 
      }

      bool Failed = false;
      if (AcquireRun(Fetcher, 0, &Failed, &Transient) == false)
	 return false;

      /* If we are in no download mode and missing files and there were
         'failures' then the user must specify -m. Furthermore, there 
         is no such thing as a transient error in no-download mode! */
      if (Transient == true &&
	  _config->FindB("APT::Get::Download",true) == false)
      {
	 Transient = false;
	 Failed = true;
      }
      
      if (_config->FindB("APT::Get::Download-Only",false) == true)
      {
	 if (Failed == true && _config->FindB("APT::Get::Fix-Missing",false) == false)
	    return _error->Error(_("Some files failed to download"));
	 c1out << _("Download complete and in download only mode") << std::endl;
	 return true;
      }
      
      if (Failed == true && _config->FindB("APT::Get::Fix-Missing",false) == false)
      {
	 return _error->Error(_("Unable to fetch some archives, maybe run apt-get update or try with --fix-missing?"));
      }
      
      if (Transient == true && Failed == true)
	 return _error->Error(_("--fix-missing and media swapping is not currently supported"));
      
      // Try to deal with missing package files
      if (Failed == true && PM->FixMissing() == false)
      {
	 c2out << _("Unable to correct missing packages.") << std::endl;
	 return _error->Error(_("Aborting install."));
      }

      _system->UnLock();
      
#if (APT_PKG_MAJOR >= 4 && APT_PKG_MINOR >= 13)
      APT::Progress::PackageManager *progress = APT::Progress::PackageManagerProgressFactory();
      pkgPackageManager::OrderResult Res = PM->DoInstall(progress);
      delete progress;
#else
      int status_fd = _config->FindI("APT::Status-Fd", -1);
      pkgPackageManager::OrderResult Res = PM->DoInstall(status_fd);
#endif

      if (Res == pkgPackageManager::Failed || _error->PendingError() == true)
	 return false;
      if (Res == pkgPackageManager::Completed)
	 break;
      
      // Reload the fetcher object and loop again for media swapping
      Fetcher.Shutdown();
      if (PM->GetArchives(&Fetcher,List,&Recs) == false)
	 return false;
      
      _system->Lock();
   }

   std::set<std::string> const disappearedPkgs = PM->GetDisappearedPackages();
   if (disappearedPkgs.empty() == true)
      return true;

   std::string disappear;
   for (std::set<std::string>::const_iterator d = disappearedPkgs.begin();
	d != disappearedPkgs.end(); ++d)
      disappear.append(*d).append(" ");

   ShowList(c1out, P_("The following package disappeared from your system as\n"
	"all files have been overwritten by other packages:",
	"The following packages disappeared from your system as\n"
	"all files have been overwritten by other packages:", disappearedPkgs.size()), disappear, "");
   c0out << _("Note: This is done automatically and on purpose by dpkg.") << std::endl;

   return true;
}
									/*}}}*/
// DoAutomaticRemove - Remove all automatic unused packages		/*{{{*/
// ---------------------------------------------------------------------
/* Remove unused automatic packages */
static bool DoAutomaticRemove(CacheFile &Cache)
{
   bool Debug = _config->FindI("Debug::pkgAutoRemove",false);
   bool doAutoRemove = _config->FindB("APT::Get::AutomaticRemove", false);
   bool hideAutoRemove = _config->FindB("APT::Get::HideAutoRemove");

   pkgDepCache::ActionGroup group(*Cache);
   if(Debug)
      std::cout << "DoAutomaticRemove()" << std::endl;

   if (doAutoRemove == true &&
	_config->FindB("APT::Get::Remove",true) == false)
   {
      c1out << _("We are not supposed to delete stuff, can't start "
		 "AutoRemover") << std::endl;
      return false;
   }

   bool purgePkgs = _config->FindB("APT::Get::Purge", false);
   bool smallList = (hideAutoRemove == false &&
		strcasecmp(_config->Find("APT::Get::HideAutoRemove","").c_str(),"small") == 0);

   unsigned long autoRemoveCount = 0;
   APT::PackageSet tooMuch;
   APT::PackageList autoRemoveList;
   // look over the cache to see what can be removed
   for (unsigned J = 0; J < Cache->Head().PackageCount; ++J)
   {
      pkgCache::PkgIterator Pkg(Cache,Cache.List[J]);
      if (Cache[Pkg].Garbage)
      {
	 if(Pkg.CurrentVer() != 0 || Cache[Pkg].Install())
	    if(Debug)
	       std::cout << "We could delete %s" <<  Pkg.FullName(true).c_str() << std::endl;

	 if (doAutoRemove)
	 {
	    if(Pkg.CurrentVer() != 0 && 
	       Pkg->CurrentState != pkgCache::State::ConfigFiles)
	       Cache->MarkDelete(Pkg, purgePkgs, 0, false);
	    else
	       Cache->MarkKeep(Pkg, false, false);
	 }
	 else
	 {
	    if (hideAutoRemove == false && Cache[Pkg].Delete() == false)
	       autoRemoveList.insert(Pkg);
	    // if the package is a new install and already garbage we don't need to
	    // install it in the first place, so nuke it instead of show it
	    if (Cache[Pkg].Install() == true && Pkg.CurrentVer() == 0)
	    {
	       if (Pkg.CandVersion() != 0)
	          tooMuch.insert(Pkg);
	       Cache->MarkDelete(Pkg, false, 0, false);
	    }
	    // only show stuff in the list that is not yet marked for removal
	    else if(hideAutoRemove == false && Cache[Pkg].Delete() == false)
	       ++autoRemoveCount;
	 }
      }
   }

   // we could have removed a new dependency of a garbage package,
   // so check if a reverse depends is broken and if so install it again.
   if (tooMuch.empty() == false && (Cache->BrokenCount() != 0 || Cache->PolicyBrokenCount() != 0))
   {
      bool Changed;
      do {
	 Changed = false;
	 for (APT::PackageSet::const_iterator Pkg = tooMuch.begin();
	      Pkg != tooMuch.end(); ++Pkg)
	 {
	    APT::PackageSet too;
	    too.insert(*Pkg);
	    for (pkgCache::PrvIterator Prv = Cache[Pkg].CandidateVerIter(Cache).ProvidesList();
		 Prv.end() == false; ++Prv)
	       too.insert(Prv.ParentPkg());
	    for (APT::PackageSet::const_iterator P = too.begin(); P != too.end(); ++P)
	    {
	       for (pkgCache::DepIterator R = P.RevDependsList();
		    R.end() == false; ++R)
	       {
		  if (R.IsNegative() == true ||
		      Cache->IsImportantDep(R) == false)
		     continue;
		 pkgCache::PkgIterator N = R.ParentPkg();
		 if (N.end() == true || (N->CurrentVer == 0 && (*Cache)[N].Install() == false))
		    continue;
		 if (Debug == true)
		    std::clog << "Save " << Pkg << " as another installed garbage package depends on it" << std::endl;
		 Cache->MarkInstall(Pkg, false, 0, false);
		 if (hideAutoRemove == false)
		    ++autoRemoveCount;
		 tooMuch.erase(Pkg);
		 Changed = true;
		 break;
	       }
	       if (Changed == true)
		  break;
	    }
	    if (Changed == true)
	       break;
	 }
      } while (Changed == true);
   }

   std::string autoremovelist, autoremoveversions;
   if (smallList == false && autoRemoveCount != 0)
   {
      for (APT::PackageList::const_iterator Pkg = autoRemoveList.begin(); Pkg != autoRemoveList.end(); ++Pkg)
      {
	 if (Cache[Pkg].Garbage == false)
	    continue;
	 autoremovelist += Pkg.FullName(true) + " ";
	 autoremoveversions += std::string(Cache[Pkg].CandVersion) + "\n";
      }
   }

   // Now see if we had destroyed anything (if we had done anything)
   if (Cache->BrokenCount() != 0)
   {
      c1out << _("Hmm, seems like the AutoRemover destroyed something which really\n"
	         "shouldn't happen. Please file a bug report against apt.") << std::endl;
      c1out << std::endl;
      c1out << _("The following information may help to resolve the situation:") << std::endl;
      c1out << std::endl;
      ShowBroken(c1out,Cache,false);

      return _error->Error(_("Internal Error, AutoRemover broke stuff"));
   }

   // if we don't remove them, we should show them!
   if (doAutoRemove == false && (autoremovelist.empty() == false || autoRemoveCount != 0))
   {
      if (smallList == false)
	 ShowList(c1out, P_("The following package was automatically installed and is no longer required:",
	          "The following packages were automatically installed and are no longer required:",
	          autoRemoveCount), autoremovelist, autoremoveversions);
      else
	 ioprintf(c1out, P_("%lu package was automatically installed and is no longer required.\n",
	          "%lu packages were automatically installed and are no longer required.\n", autoRemoveCount), autoRemoveCount);
      c1out << P_("Use 'apt-get autoremove' to remove it.", "Use 'apt-get autoremove' to remove them.", autoRemoveCount) << std::endl;
   }
   return true;
}
									/*}}}*/
// DoCacheManipulationFromCommandLine					/*{{{*/
static const unsigned short MOD_REMOVE = 1;
static const unsigned short MOD_INSTALL = 2;

bool DoCacheManipulationFromCommandLine(CommandLine &CmdL, CacheFile &Cache)
{
   std::map<unsigned short, APT::VersionSet> verset;
   return DoCacheManipulationFromCommandLine(CmdL, Cache, verset);
}
bool DoCacheManipulationFromCommandLine(CommandLine &CmdL, CacheFile &Cache,
                                        std::map<unsigned short, APT::VersionSet> &verset)
{

   // Enter the special broken fixing mode if the user specified arguments
   bool BrokenFix = false;
   if (Cache->BrokenCount() != 0)
      BrokenFix = true;

   SPtr<pkgProblemResolver> Fix;
   if (_config->FindB("APT::Get::CallResolver", true) == true)
      Fix = new pkgProblemResolver(Cache);

   unsigned short fallback = MOD_INSTALL;
   if (strcasecmp(CmdL.FileList[0],"remove") == 0)
      fallback = MOD_REMOVE;
   else if (strcasecmp(CmdL.FileList[0], "purge") == 0)
   {
      _config->Set("APT::Get::Purge", true);
      fallback = MOD_REMOVE;
   }
   else if (strcasecmp(CmdL.FileList[0], "autoremove") == 0)
   {
      _config->Set("APT::Get::AutomaticRemove", "true");
      fallback = MOD_REMOVE;
   }

   std::list<APT::VersionSet::Modifier> mods;
   mods.push_back(APT::VersionSet::Modifier(MOD_INSTALL, "+",
		APT::VersionSet::Modifier::POSTFIX, APT::VersionSet::CANDIDATE));
   mods.push_back(APT::VersionSet::Modifier(MOD_REMOVE, "-",
		APT::VersionSet::Modifier::POSTFIX, APT::VersionSet::NEWEST));
   CacheSetHelperAPTGet helper(c0out);
   verset = APT::VersionSet::GroupedFromCommandLine(Cache,
		CmdL.FileList + 1, mods, fallback, helper);

   if (_error->PendingError() == true)
   {
      helper.showVirtualPackageErrors(Cache);
      return false;
   }


  TryToInstall InstallAction(Cache, Fix, BrokenFix);
  TryToRemove RemoveAction(Cache, Fix);

   // new scope for the ActionGroup
   {
      pkgDepCache::ActionGroup group(Cache);
      unsigned short const order[] = { MOD_REMOVE, MOD_INSTALL, 0 };

      for (unsigned short i = 0; order[i] != 0; ++i)
      {
	 if (order[i] == MOD_INSTALL)
	    InstallAction = std::for_each(verset[MOD_INSTALL].begin(), verset[MOD_INSTALL].end(), InstallAction);
	 else if (order[i] == MOD_REMOVE)
	    RemoveAction = std::for_each(verset[MOD_REMOVE].begin(), verset[MOD_REMOVE].end(), RemoveAction);
      }

      if (Fix != NULL && _config->FindB("APT::Get::AutoSolving", true) == true)
      {
         for (unsigned short i = 0; order[i] != 0; ++i)
         {
	    if (order[i] != MOD_INSTALL)
	       continue;
	    InstallAction.propergateReleaseCandiateSwitching(helper.selectedByRelease, c0out);
	    InstallAction.doAutoInstall();
	 }
      }

      if (_error->PendingError() == true)
      {
	 return false;
      }

      /* If we are in the Broken fixing mode we do not attempt to fix the
	 problems. This is if the user invoked install without -f and gave
	 packages */
      if (BrokenFix == true && Cache->BrokenCount() != 0)
      {
	 c1out << _("You might want to run 'apt-get -f install' to correct these:") << std::endl;
	 ShowBroken(c1out,Cache,false);
	 return _error->Error(_("Unmet dependencies. Try 'apt-get -f install' with no packages (or specify a solution)."));
      }

      if (Fix != NULL)
      {
	 // Call the scored problem resolver
	 Fix->Resolve(true);
      }

      // Now we check the state of the packages,
      if (Cache->BrokenCount() != 0)
      {
	 c1out << 
	    _("Some packages could not be installed. This may mean that you have\n" 
	      "requested an impossible situation or if you are using the unstable\n" 
	      "distribution that some required packages have not yet been created\n"
	      "or been moved out of Incoming.") << std::endl;
	 /*
	 if (Packages == 1)
	 {
	    c1out << std::endl;
	    c1out << 
	       _("Since you only requested a single operation it is extremely likely that\n"
		 "the package is simply not installable and a bug report against\n" 
		 "that package should be filed.") << std::endl;
	 }
	 */

	 c1out << _("The following information may help to resolve the situation:") << std::endl;
	 c1out << std::endl;
	 ShowBroken(c1out,Cache,false);
	 if (_error->PendingError() == true)
	    return false;
	 else
	    return _error->Error(_("Broken packages"));
      }
   }
   if (!DoAutomaticRemove(Cache)) 
      return false;

   // if nothing changed in the cache, but only the automark information
   // we write the StateFile here, otherwise it will be written in 
   // cache.commit()
   if (InstallAction.AutoMarkChanged > 0 &&
       Cache->DelCount() == 0 && Cache->InstCount() == 0 &&
       Cache->BadCount() == 0 &&
       _config->FindB("APT::Get::Simulate",false) == false)
      Cache->writeStateFile(NULL);

   return true;
}
									/*}}}*/
// DoInstall - Install packages from the command line			/*{{{*/
// ---------------------------------------------------------------------
/* Install named packages */
bool DoInstall(CommandLine &CmdL)
{
   CacheFile Cache;
   if (Cache.OpenForInstall() == false || 
       Cache.CheckDeps(CmdL.FileSize() != 1) == false)
      return false;

   std::map<unsigned short, APT::VersionSet> verset;

   if(!DoCacheManipulationFromCommandLine(CmdL, Cache, verset))
      return false;

   /* Print out a list of packages that are going to be installed extra
      to what the user asked */
   if (Cache->InstCount() != verset[MOD_INSTALL].size())
   {
      std::string List;
      std::string VersionsList;
      for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
      {
	 pkgCache::PkgIterator I(Cache,Cache.List[J]);
	 if ((*Cache)[I].Install() == false)
	    continue;
	 pkgCache::VerIterator Cand = Cache[I].CandidateVerIter(Cache);

	 if (verset[MOD_INSTALL].find(Cand) != verset[MOD_INSTALL].end())
	    continue;

	 List += I.FullName(true) + " ";
	 VersionsList += std::string(Cache[I].CandVersion) + "\n";
      }
      
      ShowList(c1out,_("The following extra packages will be installed:"),List,VersionsList);
   }

   /* Print out a list of suggested and recommended packages */
   {
      std::string SuggestsList, RecommendsList;
      std::string SuggestsVersions, RecommendsVersions;
      for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
      {
	 pkgCache::PkgIterator Pkg(Cache,Cache.List[J]);

	 /* Just look at the ones we want to install */
	 if ((*Cache)[Pkg].Install() == false)
	   continue;

	 // get the recommends/suggests for the candidate ver
	 pkgCache::VerIterator CV = (*Cache)[Pkg].CandidateVerIter(*Cache);
	 for (pkgCache::DepIterator D = CV.DependsList(); D.end() == false; )
	 {
	    pkgCache::DepIterator Start;
	    pkgCache::DepIterator End;
	    D.GlobOr(Start,End); // advances D

	    // FIXME: we really should display a or-group as a or-group to the user
	    //        the problem is that ShowList is incapable of doing this
            std::string RecommendsOrList,RecommendsOrVersions;
            std::string SuggestsOrList,SuggestsOrVersions;
	    bool foundInstalledInOrGroup = false;
	    for(;;)
	    {
	       /* Skip if package is  installed already, or is about to be */
               std::string target = Start.TargetPkg().FullName(true) + " ";
	       pkgCache::PkgIterator const TarPkg = Start.TargetPkg();
	       if (TarPkg->SelectedState == pkgCache::State::Install ||
		   TarPkg->SelectedState == pkgCache::State::Hold ||
		   Cache[Start.TargetPkg()].Install())
	       {
		  foundInstalledInOrGroup=true;
		  break;
	       }

	       /* Skip if we already saw it */
	       if (int(SuggestsList.find(target)) != -1 || int(RecommendsList.find(target)) != -1)
	       {
		  foundInstalledInOrGroup=true;
		  break; 
	       }

	       // this is a dep on a virtual pkg, check if any package that provides it
	       // should be installed
	       if(Start.TargetPkg().ProvidesList() != 0)
	       {
		  pkgCache::PrvIterator I = Start.TargetPkg().ProvidesList();
		  for (; I.end() == false; ++I)
		  {
		     pkgCache::PkgIterator Pkg = I.OwnerPkg();
		     if (Cache[Pkg].CandidateVerIter(Cache) == I.OwnerVer() && 
			 Pkg.CurrentVer() != 0)
			foundInstalledInOrGroup=true;
		  }
	       }

	       if (Start->Type == pkgCache::Dep::Suggests) 
	       {
		  SuggestsOrList += target;
		  SuggestsOrVersions += std::string(Cache[Start.TargetPkg()].CandVersion) + "\n";
	       }
	       
	       if (Start->Type == pkgCache::Dep::Recommends) 
	       {
		  RecommendsOrList += target;
		  RecommendsOrVersions += std::string(Cache[Start.TargetPkg()].CandVersion) + "\n";
	       }

	       if (Start >= End)
		  break;
	       ++Start;
	    }
	    
	    if(foundInstalledInOrGroup == false)
	    {
	       RecommendsList += RecommendsOrList;
	       RecommendsVersions += RecommendsOrVersions;
	       SuggestsList += SuggestsOrList;
	       SuggestsVersions += SuggestsOrVersions;
	    }
	       
	 }
      }

      ShowList(c1out,_("Suggested packages:"),SuggestsList,SuggestsVersions);
      ShowList(c1out,_("Recommended packages:"),RecommendsList,RecommendsVersions);

   }

   // See if we need to prompt
   // FIXME: check if really the packages in the set are going to be installed
   if (Cache->InstCount() == verset[MOD_INSTALL].size() && Cache->DelCount() == 0)
      return InstallPackages(Cache,false,false);

   return InstallPackages(Cache,false);   
}
									/*}}}*/
