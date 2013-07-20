#include <apt-pkg/cachefile.h>
#include <apt-pkg/pkgcache.h>
#include <apt-pkg/depcache.h>
#include <apt-pkg/strutl.h>

#include "private-cacheset.h"

bool GetLocalitySortedVersionSet(pkgCacheFile &CacheFile, 
                                 LocalitySortedVersionSet &output_set)
{
    Matcher null_matcher = Matcher();
    return GetLocalitySortedVersionSet(CacheFile, output_set, null_matcher);
}

bool GetLocalitySortedVersionSet(pkgCacheFile &CacheFile, 
                                 LocalitySortedVersionSet &output_set,
                                 Matcher &matcher)
{
   pkgCache *Cache = CacheFile.GetPkgCache();
   pkgDepCache *DepCache = CacheFile.GetDepCache();

   for (pkgCache::PkgIterator P = Cache->PkgBegin(); P.end() == false; ++P)
   {
      if ((matcher)(P) == false)
         continue;

      // exclude virtual pkgs
      if (P.VersionList() == 0)
         continue;
      pkgDepCache::StateCache &state = (*DepCache)[P];
      if (_config->FindB("APT::Cmd::Installed") == true)
      {
         if (P.CurrentVer() != NULL)
         {
            output_set.insert(P.CurrentVer());
         }
      }
      else if (_config->FindB("APT::Cmd::Upgradable") == true)
      {
         if(P.CurrentVer() && state.Upgradable())
         {
            output_set.insert(P.CurrentVer());
         }
      }
      else 
      {
         pkgPolicy *policy = CacheFile.GetPolicy();
         output_set.insert(policy->GetCandidateVer(P));
      }
   }
   return true;
}
