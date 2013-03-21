// Include Files							/*{{{*/
#include<config.h>

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

#include <apt-private/private.h>

#include <cassert>
#include <locale.h>
#include <iostream>
#include <unistd.h>
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <iomanip>
#include <algorithm>

#include <apti18n.h>
									/*}}}*/
// Include Files							/*{{{*/
#include<config.h>

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

#include <apt-private/private.h>

#include <cassert>
#include <locale.h>
#include <iostream>
#include <unistd.h>
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <iomanip>
#include <algorithm>

#include <apti18n.h>
									/*}}}*/

void ListSinglePackage(pkgCacheFile &CacheFile, pkgCache::PkgIterator P)
{
   pkgPolicy *policy = CacheFile.GetPolicy();
   pkgCache::VerIterator inst = P.CurrentVer();
   pkgCache::VerIterator cand = policy->GetCandidateVer(P);

   std::string inst_ver_str = inst ? inst.VerStr() : "(none)";
   std::string cand_ver_str = cand ? cand.VerStr() : "(none)";
   std::string arch_str = inst ? inst.Arch() : cand.Arch();

   std::string suite = "";
   pkgCache::VerIterator ver = cand;
   if (ver && ver.FileList() && ver.FileList())
   {
      pkgCache::VerFileIterator VF = ver.FileList();
      for (; VF.end() == false ; ++VF)
      {
         // XXX: how to figure out the relevant suite? if its in multiple ones?
         //suite = suite + "," + VF.File().Archive();
         suite = VF.File().Archive();
      }
   }

   std::string name_str = P.Name() + std::string("/") + suite;
   std::cout << std::setw(28) << std::setiosflags(std::ios::left) << name_str
             << " " 
             << std::setw(20) << inst_ver_str
             << " " 
             << std::setw(20) << cand_ver_str
             << " "
             << std::setw(8) << arch_str
             << " "
             << std::endl;
}

// list - list package based on criteria        			/*{{{*/
// ---------------------------------------------------------------------
bool List(CommandLine &Cmd)
{
   pkgCacheFile CacheFile;
   pkgCache *Cache = CacheFile.GetPkgCache();
   pkgDepCache *DepCache = CacheFile.GetDepCache();
   if (unlikely(Cache == NULL))
      return false;

   for (pkgCache::PkgIterator P = Cache->PkgBegin(); P.end() == false; ++P)
   {
      pkgDepCache::StateCache &state = (*DepCache)[P];

      if (_config->FindB("APT::Cmd::Installed") == true)
      {
         if (P.CurrentVer() != NULL)
         {
            ListSinglePackage(CacheFile, P);
         }
      }
      else if (_config->FindB("APT::Cmd::Upgradable") == true)
      {
         if(P.CurrentVer() && state.Upgradable())
         {
            ListSinglePackage(CacheFile, P);
         }
      }
      else 
      {
         ListSinglePackage(CacheFile, P);
      }
   }
   return true;
}

// Dump - show everything						/*{{{*/
// ---------------------------------------------------------------------
/* This is worthless except fer debugging things */
bool Dump(CommandLine &Cmd)
{
   pkgCacheFile CacheFile;
   pkgCache *Cache = CacheFile.GetPkgCache();
   if (unlikely(Cache == NULL))
      return false;

   std::cout << "Using Versioning System: " << Cache->VS->Label << std::endl;
   
   for (pkgCache::PkgIterator P = Cache->PkgBegin(); P.end() == false; ++P)
   {
      std::cout << "Package: " << P.FullName(true) << std::endl;
      for (pkgCache::VerIterator V = P.VersionList(); V.end() == false; ++V)
      {
	 std::cout << " Version: " << V.VerStr() << std::endl;
	 std::cout << "     File: " << V.FileList().File().FileName() << std::endl;
	 for (pkgCache::DepIterator D = V.DependsList(); D.end() == false; ++D)
	    std::cout << "  Depends: " << D.TargetPkg().FullName(true) << ' ' << 
	                     DeNull(D.TargetVer()) << std::endl;
	 for (pkgCache::DescIterator D = V.DescriptionList(); D.end() == false; ++D)
	 {
	    std::cout << " Description Language: " << D.LanguageCode() << std::endl
		 << "                 File: " << D.FileList().File().FileName() << std::endl
		 << "                  MD5: " << D.md5() << std::endl;
	 } 
      }      
   }

   for (pkgCache::PkgFileIterator F = Cache->FileBegin(); F.end() == false; ++F)
   {
      std::cout << "File: " << F.FileName() << std::endl;
      std::cout << " Type: " << F.IndexType() << std::endl;
      std::cout << " Size: " << F->Size << std::endl;
      std::cout << " ID: " << F->ID << std::endl;
      std::cout << " Flags: " << F->Flags << std::endl;
      std::cout << " Time: " << TimeRFC1123(F->mtime) << std::endl;
      std::cout << " Archive: " << DeNull(F.Archive()) << std::endl;
      std::cout << " Component: " << DeNull(F.Component()) << std::endl;
      std::cout << " Version: " << DeNull(F.Version()) << std::endl;
      std::cout << " Origin: " << DeNull(F.Origin()) << std::endl;
      std::cout << " Site: " << DeNull(F.Site()) << std::endl;
      std::cout << " Label: " << DeNull(F.Label()) << std::endl;
      std::cout << " Architecture: " << DeNull(F.Architecture()) << std::endl;
   }

   return true;
}
									/*}}}*/
