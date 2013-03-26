// Include Files							/*{{{*/
#include<config.h>

#include <apt-pkg/error.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/cachefilter.h>
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
#include <apt-private/private.h>

struct PackageSortAlphabetic
{
   bool operator () (const pkgCache::PkgIterator &p_lhs, 
                     const pkgCache::PkgIterator &p_rhs)
    {
       const std::string &l_name = p_lhs.FullName(true);
       const std::string &r_name = p_rhs.FullName(true);
       return (l_name < r_name);
    }
};
typedef APT::PackageContainer<std::set<pkgCache::PkgIterator, PackageSortAlphabetic> > SortedPackageSet;


std::string GetArchiveSuite(pkgCacheFile &CacheFile, pkgCache::PkgIterator P)
{
   pkgPolicy *policy = CacheFile.GetPolicy();
   pkgCache::VerIterator cand = policy->GetCandidateVer(P);

   pkgCache::VerIterator ver = cand;
   std::string suite = "";
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
   return suite;
}

std::string GetFlagsStr(pkgCacheFile &CacheFile, pkgCache::PkgIterator P)
{
   pkgDepCache *DepCache = CacheFile.GetDepCache();
   pkgDepCache::StateCache &state = (*DepCache)[P];

   std::string flags_str;
   if (state.NowBroken())
      flags_str = "B";
   if (P.CurrentVer() && state.Upgradable())
      flags_str = "u";
   else if (P.CurrentVer() != NULL)
      flags_str = "i";
   else
      flags_str = "-";
   return flags_str;
}

std::string GetCandidateVersion(pkgCacheFile &CacheFile, pkgCache::PkgIterator P)
{
   pkgPolicy *policy = CacheFile.GetPolicy();
   pkgCache::VerIterator cand = policy->GetCandidateVer(P);

   return cand ? cand.VerStr() : "(none)";
}

std::string GetInstalledVersion(pkgCacheFile &CacheFile, pkgCache::PkgIterator P)
{
   pkgCache::VerIterator inst = P.CurrentVer();

   return inst ? inst.VerStr() : "(none)";
}

std::string GetArchitecture(pkgCacheFile &CacheFile, pkgCache::PkgIterator P)
{
   pkgPolicy *policy = CacheFile.GetPolicy();
   pkgCache::VerIterator inst = P.CurrentVer();
   pkgCache::VerIterator cand = policy->GetCandidateVer(P);
   
   return inst ? inst.Arch() : cand.Arch();
}

std::string GetShortDescription(pkgCacheFile &CacheFile, pkgCache::PkgIterator P)
{
   pkgPolicy *policy = CacheFile.GetPolicy();
   pkgRecords records(CacheFile);

   pkgCache::VerIterator ver;
   if (P.CurrentVer())
      ver = P.CurrentVer();
   else
      ver = policy->GetCandidateVer(P);

   std::string ShortDescription = "(none)";
   if(ver)
   {
      pkgCache::DescIterator Desc = ver.TranslatedDescription();
      pkgRecords::Parser & parser = records.Lookup(Desc.FileList());

      ShortDescription = parser.ShortDesc();
   }
   return ShortDescription;
}

void ListSinglePackage(pkgCacheFile &CacheFile, pkgCache::PkgIterator P)
{
   std::string suite = GetArchiveSuite(CacheFile, P);
   std::string name_str = P.Name() + std::string("/") + suite;

   std::cout << std::setiosflags(std::ios::left)
             << std::setw(2) << GetFlagsStr(CacheFile, P)
             << std::setw(28) << name_str
             << " "
             << std::setw(20) << GetInstalledVersion(CacheFile, P)
             << " " 
             << std::setw(20) << GetCandidateVersion(CacheFile, P)
             << " "
             << std::setw(8) << GetArchitecture(CacheFile, P)
             << " "
             << std::setw(20) << GetShortDescription(CacheFile, P)
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

   std::string regexp;
   SortedPackageSet bag;
   SortedPackageSet::const_iterator I = bag.begin();
   // Cmd.FileList has at least "list" 
   const char **patterns;
   if (strv_length(Cmd.FileList + 1) == 0)
   {
      const char *meep[] = { ".*", NULL};
      patterns = meep;
   } else {
      patterns = Cmd.FileList + 1;
   }

   for(int i=0; patterns[i] != NULL; i++)
   {
      // FIXME: use fnmatch() here instead? that is what dpkg -l is using
      //        or make it a config option
      regexp = patterns[i];
      APT::CacheFilter::PackageNameMatchesRegEx regexfilter(regexp);

      for (pkgCache::PkgIterator P = Cache->PkgBegin(); P.end() == false; ++P)
      {
         // exclude virtual pkgs
         if (P.VersionList() == 0)
            continue;

         if (regexfilter(P) == false)
            continue;
         
         pkgDepCache::StateCache &state = (*DepCache)[P];
         
         if (_config->FindB("APT::Cmd::Installed") == true)
         {
            if (P.CurrentVer() != NULL)
            {
               bag.insert(P);
            }
         }
         else if (_config->FindB("APT::Cmd::Upgradable") == true)
         {
            if(P.CurrentVer() && state.Upgradable())
            {
               bag.insert(P);
            }
         }
         else 
         {
            bag.insert(P);
         }
      }
   }
   
   // output the (now sorted) PackageSet
   for (I = bag.begin(); I != bag.end(); ++I)
   {
      ListSinglePackage(CacheFile, (*I));
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
