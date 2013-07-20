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

#include <apti18n.h>

#include "private-output.h"
#include "private-cacheset.h"

// DisplayRecord - Displays the complete record for the package		/*{{{*/
// ---------------------------------------------------------------------
/* This displays the package record from the proper package index file. 
   It is not used by DumpAvail for performance reasons. */
bool DisplayRecord(pkgCacheFile &CacheFile, pkgCache::VerIterator V)
{
   pkgCache *Cache = CacheFile.GetPkgCache();
   if (unlikely(Cache == NULL))
      return false;

   // Find an appropriate file
   pkgCache::VerFileIterator Vf = V.FileList();
   for (; Vf.end() == false; ++Vf)
      if ((Vf.File()->Flags & pkgCache::Flag::NotSource) == 0)
	 break;
   if (Vf.end() == true)
      Vf = V.FileList();
      
   // Check and load the package list file
   pkgCache::PkgFileIterator I = Vf.File();
   if (I.IsOk() == false)
      return _error->Error(_("Package file %s is out of sync."),I.FileName());

   FileFd PkgF;
   if (PkgF.Open(I.FileName(), FileFd::ReadOnly, FileFd::Extension) == false)
      return false;

   // Read the record
   unsigned char *Buffer = new unsigned char[Cache->HeaderP->MaxVerFileSize+1];
   Buffer[V.FileList()->Size] = '\n';
   if (PkgF.Seek(V.FileList()->Offset) == false ||
       PkgF.Read(Buffer,V.FileList()->Size) == false)
   {
      delete [] Buffer;
      return false;
   }

   // Get a pointer to start of Description field
   const unsigned char *DescP = (unsigned char*)strstr((char*)Buffer, "\nDescription");
   if (DescP != NULL)
      ++DescP;
   else
      DescP = Buffer + V.FileList()->Size;

   // Write all but Description
   if (fwrite(Buffer,1,DescP - Buffer,stdout) < (size_t)(DescP - Buffer))
   {
      delete [] Buffer;
      return false;
   }

   // Show the right description
   pkgRecords Recs(*Cache);
   pkgCache::DescIterator Desc = V.TranslatedDescription();
   if (Desc.end() == false)
   {
      pkgRecords::Parser &P = Recs.Lookup(Desc.FileList());
      c1out << "Description" << ( (strcmp(Desc.LanguageCode(),"") != 0) ? "-" : "" ) << Desc.LanguageCode() << ": " << P.LongDesc();
      c1out << std::endl << "Description-md5: " << Desc.md5() << std::endl;

      // Find the first field after the description (if there is any)
      while ((DescP = (unsigned char*)strchr((char*)DescP, '\n')) != NULL)
      {
	 if (DescP[1] == ' ')
	    DescP += 2;
	 else if (strncmp((char*)DescP, "\nDescription", strlen("\nDescription")) == 0)
	    DescP += strlen("\nDescription");
	 else
	    break;
      }
      if (DescP != NULL)
	 ++DescP;
   }
   // if we have no translation, we found a lonely Description-md5, so don't skip it

   if (DescP != NULL)
   {
      // write the rest of the buffer
      const unsigned char *end=&Buffer[V.FileList()->Size];
      if (fwrite(DescP,1,end-DescP,stdout) < (size_t)(end-DescP))
      {
	 delete [] Buffer;
	 return false;
      }
   }

   // write a final newline (after the description)
   c1out << std::endl;
   delete [] Buffer;

   return true;
}
									/*}}}*/

// ShowPackage - Dump the package record to the screen			/*{{{*/
// ---------------------------------------------------------------------
/* */
bool ShowPackage(CommandLine &CmdL)
{
   pkgCacheFile CacheFile;
   CacheSetHelperVirtuals helper(true, GlobalError::NOTICE);
   APT::VersionList::Version const select = _config->FindB("APT::Cache::AllVersions", true) ?
			APT::VersionList::ALL : APT::VersionList::CANDIDATE;
   APT::VersionList const verset = APT::VersionList::FromCommandLine(CacheFile, CmdL.FileList + 1, select, helper);
   for (APT::VersionList::const_iterator Ver = verset.begin(); Ver != verset.end(); ++Ver)
      if (DisplayRecord(CacheFile, Ver) == false)
	 return false;

   if (verset.empty() == true)
   {
      if (helper.virtualPkgs.empty() == true)
        return _error->Error(_("No packages found"));
      else
        _error->Notice(_("No packages found"));
   }
   return true;
}
									/*}}}*/
