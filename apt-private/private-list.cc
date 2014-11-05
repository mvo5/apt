// Include Files							/*{{{*/
#include <config.h>

#include <apt-pkg/cachefile.h>
#include <apt-pkg/cachefilter.h>
#include <apt-pkg/cacheset.h>
#include <apt-pkg/cmndline.h>
#include <apt-pkg/pkgrecords.h>
#include <apt-pkg/progress.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/macros.h>
#include <apt-pkg/pkgcache.h>
#include <apt-pkg/cacheiterators.h>

#include <apt-private/private-cacheset.h>
#include <apt-private/private-list.h>
#include <apt-private/private-output.h>

#include <iostream>
#include <sstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <apti18n.h>
									/*}}}*/
struct PackageSortAlphabetic						/*{{{*/
{
   bool operator () (const pkgCache::VerIterator &v_lhs,
                     const pkgCache::VerIterator &v_rhs)
    {
       const std::string &l_name = v_lhs.ParentPkg().FullName(true);
       const std::string &r_name = v_rhs.ParentPkg().FullName(true);
       return (l_name < r_name);
    }
};
									/*}}}*/
struct PackageSortDownloadSize						/*{{{*/
{
   bool operator () (const pkgCache::VerIterator &v_lhs, 
                     const pkgCache::VerIterator &v_rhs)
    {
       const unsigned long long &l_size = v_lhs->Size;
       const unsigned long long &r_size = v_rhs->Size;
       return (l_size < r_size);
    }
};
									/*}}}*/
struct PackageSortInstalledSize						/*{{{*/
{
   bool operator () (const pkgCache::VerIterator &v_lhs, 
                     const pkgCache::VerIterator &v_rhs)
    {
       pkgCache::VerFileIterator Vf;
       const unsigned long long l_size = v_lhs->InstalledSize;
       const unsigned long long r_size = v_rhs->InstalledSize;

       return (l_size < r_size);
    }
};
									/*}}}*/
class PackageNameMatcher : public Matcher
{
#ifdef PACKAGE_MATCHER_ABI_COMPAT
#define PackageMatcher PackageNameMatchesFnmatch
#endif
  public:
   PackageNameMatcher(const char **patterns)
   {
      for(int i=0; patterns[i] != NULL; ++i)
      {
         std::string pattern = patterns[i];
#ifdef PACKAGE_MATCHER_ABI_COMPAT
            APT::CacheFilter::PackageNameMatchesFnmatch *cachefilter = NULL;
            cachefilter = new APT::CacheFilter::PackageNameMatchesFnmatch(pattern);
#else
         APT::CacheFilter::PackageMatcher *cachefilter = NULL;
         if(_config->FindB("APT::Cmd::Use-Regexp", false) == true)
            cachefilter = new APT::CacheFilter::PackageNameMatchesRegEx(pattern);
         else
            cachefilter = new APT::CacheFilter::PackageNameMatchesFnmatch(pattern);
#endif
         filters.push_back(cachefilter);
      }
   }
   virtual ~PackageNameMatcher()
   {
      for(J=filters.begin(); J != filters.end(); ++J)
         delete *J;
   }
   virtual bool operator () (const pkgCache::PkgIterator &P) 
   {
      for(J=filters.begin(); J != filters.end(); ++J)
      {
         APT::CacheFilter::PackageMatcher *cachefilter = *J;
         if((*cachefilter)(P)) 
            return true;
      }
      return false;
   }

private:
   std::vector<APT::CacheFilter::PackageMatcher*> filters;   
   std::vector<APT::CacheFilter::PackageMatcher*>::const_iterator J;
   #undef PackageMatcher
};
									/*}}}*/
static void ListAllVersions(pkgCacheFile &CacheFile, pkgRecords &records,/*{{{*/
                     pkgCache::PkgIterator const &P, std::ostream &outs,
                     std::string const &format)
{
   for (pkgCache::VerIterator Ver = P.VersionList();
        Ver.end() == false; ++Ver)
   {
      ListSingleVersion(CacheFile, records, Ver, outs, format);
      outs << std::endl;
   }
}
									/*}}}*/
// Helper to output the list in a sorted fashion       			/*{{{*/
template<class T>
static bool OutputList(pkgCacheFile &CacheFile, pkgRecords &records, 
                       LocalitySortedVersionSet bag)
{
   bool const ShowAllVersions = _config->FindB("APT::Cmd::All-Versions", false);
   std::string format = "${color:highlight}${Package}${color:neutral}/${Origin} ${Version} ${Architecture}${ }${apt:Status}";
   if (_config->FindB("APT::Cmd::List-Include-Summary", false) == true)
      format += "\n  ${Description}\n";

   std::string SortMode = _config->Find("Apt::List::Sort-Mode", "alphabetic");
   if(SortMode == "download-size")
      format += " ${Download-Size-Nice}";
   else if(SortMode == "installed-size")
      format += " ${Installed-Size-Nice}";

   std::map<const pkgCache::VerIterator, std::string, T> output_map;
   for (LocalitySortedVersionSet::iterator V = bag.begin(); V != bag.end(); ++V)
   {
      std::stringstream outs;
      if(ShowAllVersions == true)
         ListAllVersions(CacheFile, records, V.ParentPkg(), outs, format);
      else
         ListSingleVersion(CacheFile, records, V, outs, format);
      output_map.insert(
         std::make_pair<const pkgCache::VerIterator, std::string>(
	       V, outs.str()));
   }

   std::map<const pkgCache::VerIterator, std::string>::const_iterator K;
   for (K = output_map.begin(); K != output_map.end(); ++K)
      std::cout << (*K).second << std::endl;

   // be nice and tell the user if there is more to see
   if (bag.size() == 1 && ShowAllVersions == false)
   {
      // start with -1 as we already displayed one version
      int versions = -1;
      pkgCache::VerIterator Ver = *bag.begin();
      for ( ; Ver.end() == false; ++Ver)
         ++versions;
      if (versions > 0)
         _error->Notice(P_("There is %i additional version. Please use the '-a' switch to see it", "There are %i additional versions. Please use the '-a' switch to see them.", versions), versions);
   }

   return true;
}
									/*}}}*/
// list - list package based on criteria        			/*{{{*/
// ---------------------------------------------------------------------
bool DoList(CommandLine &Cmd)
{
   pkgCacheFile CacheFile;
   pkgCache *Cache = CacheFile.GetPkgCache();
   if (unlikely(Cache == NULL))
      return false;
   pkgRecords records(CacheFile);

   const char **patterns;
   const char *all_pattern[] = { "*", NULL};

   if (strv_length(Cmd.FileList + 1) == 0)
   {
      patterns = all_pattern;
   } else {
      patterns = Cmd.FileList + 1;
   }

   PackageNameMatcher matcher(patterns);
   LocalitySortedVersionSet bag;
   OpTextProgress progress(*_config);
   progress.OverallProgress(0,
                            Cache->Head().PackageCount, 
                            Cache->Head().PackageCount,
                            _("Listing"));
   GetLocalitySortedVersionSet(CacheFile, &bag, matcher, &progress);
   std::string SortMode = _config->Find("Apt::List::Sort-Mode", "alphabetic");
   if(SortMode == "alphabetic")
      return OutputList<PackageSortAlphabetic>(CacheFile, records, bag);
   else if (SortMode == "download-size")
      return OutputList<PackageSortDownloadSize>(CacheFile, records, bag);
   else if (SortMode == "installed-size")
      return OutputList<PackageSortInstalledSize>(CacheFile, records, bag);
   else
      _error->Warning(_("Unknown Apt::List::SortMode mode %s"), SortMode.c_str());

   return false;
}
									/*}}}*/
