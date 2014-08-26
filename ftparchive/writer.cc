// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: writer.cc,v 1.14 2004/03/24 01:40:43 mdz Exp $
/* ######################################################################

   Writer 
   
   The file writer classes. These write various types of output, sources,
   packages and contents.
   
   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#include <config.h>

#include <apt-pkg/configuration.h>
#include <apt-pkg/deblistparser.h>
#include <apt-pkg/error.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/gpgv.h>
#include <apt-pkg/hashes.h>
#include <apt-pkg/md5.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/debfile.h>
#include <apt-pkg/pkgcache.h>
#include <apt-pkg/sha1.h>
#include <apt-pkg/sha2.h>
#include <apt-pkg/tagfile.h>

#include <ctype.h>
#include <fnmatch.h>
#include <ftw.h>
#include <locale.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctime>
#include <iostream>
#include <sstream>
#include <memory>
#include <utility>

#include "apt-ftparchive.h"
#include "writer.h"
#include "cachedb.h"
#include "multicompress.h"

#include <apti18n.h>
									/*}}}*/
using namespace std;
FTWScanner *FTWScanner::Owner;

// SetTFRewriteData - Helper for setting rewrite lists			/*{{{*/
// ---------------------------------------------------------------------
/* */
static inline TFRewriteData SetTFRewriteData(const char *tag,
			     const char *rewrite,
			     const char *newtag = 0)
{
   TFRewriteData tfrd;
   tfrd.Tag = tag;
   tfrd.Rewrite = rewrite;
   tfrd.NewTag = newtag;
   return tfrd;
}
									/*}}}*/
// ConfigToDoHashes - which hashes to generate				/*{{{*/
static void SingleConfigToDoHashes(unsigned int &DoHashes, std::string const &Conf, unsigned int const Flag)
{
   if (_config->FindB(Conf, true) == true)
      DoHashes |= Flag;
   else
      DoHashes &= ~Flag;
}
static void ConfigToDoHashes(unsigned int &DoHashes, std::string const &Conf)
{
   SingleConfigToDoHashes(DoHashes, Conf + "::MD5", Hashes::MD5SUM);
   SingleConfigToDoHashes(DoHashes, Conf + "::SHA1", Hashes::SHA1SUM);
   SingleConfigToDoHashes(DoHashes, Conf + "::SHA256", Hashes::SHA256SUM);
   SingleConfigToDoHashes(DoHashes, Conf + "::SHA512", Hashes::SHA512SUM);
}
									/*}}}*/

// FTWScanner::FTWScanner - Constructor					/*{{{*/
// ---------------------------------------------------------------------
/* */
FTWScanner::FTWScanner(string const &Arch): Arch(Arch), DoHashes(~0)
{
   ErrorPrinted = false;
   NoLinkAct = !_config->FindB("APT::FTPArchive::DeLinkAct",true);
   ConfigToDoHashes(DoHashes, "APT::FTPArchive");
}
									/*}}}*/
// FTWScanner::Scanner - FTW Scanner					/*{{{*/
// ---------------------------------------------------------------------
/* This is the FTW scanner, it processes each directory element in the
   directory tree. */
int FTWScanner::ScannerFTW(const char *File,const struct stat * /*sb*/,int Flag)
{
   if (Flag == FTW_DNR)
   {
      Owner->NewLine(1);
      ioprintf(c1out, _("W: Unable to read directory %s\n"), File);
   }   
   if (Flag == FTW_NS)
   {
      Owner->NewLine(1);
      ioprintf(c1out, _("W: Unable to stat %s\n"), File);
   }   
   if (Flag != FTW_F)
      return 0;

   return ScannerFile(File, true);
}
									/*}}}*/
// FTWScanner::ScannerFile - File Scanner				/*{{{*/
// ---------------------------------------------------------------------
/* */
int FTWScanner::ScannerFile(const char *File, bool const &ReadLink)
{
   const char *LastComponent = strrchr(File, '/');
   char *RealPath = NULL;

   if (LastComponent == NULL)
      LastComponent = File;
   else
      LastComponent++;

   vector<string>::const_iterator I;
   for(I = Owner->Patterns.begin(); I != Owner->Patterns.end(); ++I)
   {
      if (fnmatch((*I).c_str(), LastComponent, 0) == 0)
         break;
   }
   if (I == Owner->Patterns.end())
      return 0;

   /* Process it. If the file is a link then resolve it into an absolute
      name.. This works best if the directory components the scanner are
      given are not links themselves. */
   char Jnk[2];
   Owner->OriginalPath = File;
   if (ReadLink &&
       readlink(File,Jnk,sizeof(Jnk)) != -1 &&
       (RealPath = realpath(File,NULL)) != 0)
   {
      Owner->DoPackage(RealPath);
      free(RealPath);
   }
   else
      Owner->DoPackage(File);
   
   if (_error->empty() == false)
   {
      // Print any errors or warnings found
      string Err;
      bool SeenPath = false;
      while (_error->empty() == false)
      {
	 Owner->NewLine(1);
	 
	 bool const Type = _error->PopMessage(Err);
	 if (Type == true)
	    cerr << _("E: ") << Err << endl;
	 else
	    cerr << _("W: ") << Err << endl;
	 
	 if (Err.find(File) != string::npos)
	    SeenPath = true;
      }      
      
      if (SeenPath == false)
	 cerr << _("E: Errors apply to file ") << "'" << File << "'" << endl;
      return 0;
   }
   
   return 0;
}
									/*}}}*/
// FTWScanner::RecursiveScan - Just scan a directory tree		/*{{{*/
// ---------------------------------------------------------------------
/* */
bool FTWScanner::RecursiveScan(string const &Dir)
{
   char *RealPath = NULL;
   /* If noprefix is set then jam the scan root in, so we don't generate
      link followed paths out of control */
   if (InternalPrefix.empty() == true)
   {
      if ((RealPath = realpath(Dir.c_str(),NULL)) == 0)
	 return _error->Errno("realpath",_("Failed to resolve %s"),Dir.c_str());
      InternalPrefix = RealPath;
      free(RealPath);
   }
   
   // Do recursive directory searching
   Owner = this;
   int const Res = ftw(Dir.c_str(),ScannerFTW,30);
   
   // Error treewalking?
   if (Res != 0)
   {
      if (_error->PendingError() == false)
	 _error->Errno("ftw",_("Tree walking failed"));
      return false;
   }
   
   return true;
}
									/*}}}*/
// FTWScanner::LoadFileList - Load the file list from a file		/*{{{*/
// ---------------------------------------------------------------------
/* This is an alternative to using FTW to locate files, it reads the list
   of files from another file. */
bool FTWScanner::LoadFileList(string const &Dir, string const &File)
{
   char *RealPath = NULL;
   /* If noprefix is set then jam the scan root in, so we don't generate
      link followed paths out of control */
   if (InternalPrefix.empty() == true)
   {
      if ((RealPath = realpath(Dir.c_str(),NULL)) == 0)
	 return _error->Errno("realpath",_("Failed to resolve %s"),Dir.c_str());
      InternalPrefix = RealPath;      
      free(RealPath);
   }
   
   Owner = this;
   FILE *List = fopen(File.c_str(),"r");
   if (List == 0)
      return _error->Errno("fopen",_("Failed to open %s"),File.c_str());
   
   /* We are a tad tricky here.. We prefix the buffer with the directory
      name, that way if we need a full path with just use line.. Sneaky and
      fully evil. */
   char Line[1000];
   char *FileStart;
   if (Dir.empty() == true || Dir.end()[-1] != '/')
      FileStart = Line + snprintf(Line,sizeof(Line),"%s/",Dir.c_str());
   else
      FileStart = Line + snprintf(Line,sizeof(Line),"%s",Dir.c_str());   
   while (fgets(FileStart,sizeof(Line) - (FileStart - Line),List) != 0)
   {
      char *FileName = _strstrip(FileStart);
      if (FileName[0] == 0)
	 continue;
	 
      if (FileName[0] != '/')
      {
	 if (FileName != FileStart)
	    memmove(FileStart,FileName,strlen(FileStart));
	 FileName = Line;
      }
      
#if 0
      struct stat St;
      int Flag = FTW_F;
      if (stat(FileName,&St) != 0)
	 Flag = FTW_NS;
#endif

      if (ScannerFile(FileName, false) != 0)
	 break;
   }
  
   fclose(List);
   return true;
}
									/*}}}*/
// FTWScanner::Delink - Delink symlinks					/*{{{*/
// ---------------------------------------------------------------------
/* */
bool FTWScanner::Delink(string &FileName,const char *OriginalPath,
			unsigned long long &DeLinkBytes,
			unsigned long long const &FileSize)
{
   // See if this isn't an internaly prefix'd file name.
   if (InternalPrefix.empty() == false &&
       InternalPrefix.length() < FileName.length() && 
       stringcmp(FileName.begin(),FileName.begin() + InternalPrefix.length(),
		 InternalPrefix.begin(),InternalPrefix.end()) != 0)
   {
      if (DeLinkLimit != 0 && DeLinkBytes/1024 < DeLinkLimit)
      {
	 // Tidy up the display
	 if (DeLinkBytes == 0)
	    cout << endl;
	 
	 NewLine(1);
	 ioprintf(c1out, _(" DeLink %s [%s]\n"), (OriginalPath + InternalPrefix.length()),
		    SizeToStr(FileSize).c_str());
	 c1out << flush;
	 
	 if (NoLinkAct == false)
	 {
	    char OldLink[400];
	    if (readlink(OriginalPath,OldLink,sizeof(OldLink)) == -1)
	       _error->Errno("readlink",_("Failed to readlink %s"),OriginalPath);
	    else
	    {
	       if (unlink(OriginalPath) != 0)
		  _error->Errno("unlink",_("Failed to unlink %s"),OriginalPath);
	       else
	       {
		  if (link(FileName.c_str(),OriginalPath) != 0)
		  {
		     // Panic! Restore the symlink
		     if (symlink(OldLink,OriginalPath) != 0)
                        _error->Errno("symlink", "failed to restore symlink");
		     return _error->Errno("link",_("*** Failed to link %s to %s"),
					  FileName.c_str(),
					  OriginalPath);
		  }	       
	       }
	    }	    
	 }
	 
	 DeLinkBytes += FileSize;
	 if (DeLinkBytes/1024 >= DeLinkLimit)
	    ioprintf(c1out, _(" DeLink limit of %sB hit.\n"), SizeToStr(DeLinkBytes).c_str());      
      }
      
      FileName = OriginalPath;
   }
   
   return true;
}
									/*}}}*/

// PackagesWriter::PackagesWriter - Constructor				/*{{{*/
// ---------------------------------------------------------------------
/* */
PackagesWriter::PackagesWriter(string const &DB,string const &Overrides,string const &ExtOverrides,
			       string const &Arch) :
   FTWScanner(Arch), Db(DB), Stats(Db.Stats), TransWriter(NULL)
{
   Output = stdout;
   SetExts(".deb .udeb");
   DeLinkLimit = 0;

   // Process the command line options
   ConfigToDoHashes(DoHashes, "APT::FTPArchive::Packages");
   DoAlwaysStat = _config->FindB("APT::FTPArchive::AlwaysStat", false);
   DoContents = _config->FindB("APT::FTPArchive::Contents",true);
   NoOverride = _config->FindB("APT::FTPArchive::NoOverrideMsg",false);
   LongDescription = _config->FindB("APT::FTPArchive::LongDescription",true);

   if (Db.Loaded() == false)
      DoContents = false;

   // Read the override file
   if (Overrides.empty() == false && Over.ReadOverride(Overrides) == false)
      return;
   else
      NoOverride = true;

   if (ExtOverrides.empty() == false)
      Over.ReadExtraOverride(ExtOverrides);

   _error->DumpErrors();
}
                                                                        /*}}}*/
// FTWScanner::SetExts - Set extensions to support                      /*{{{*/
// ---------------------------------------------------------------------
/* */
bool FTWScanner::SetExts(string const &Vals)
{
   ClearPatterns();
   string::size_type Start = 0;
   while (Start <= Vals.length()-1)
   {
      string::size_type const Space = Vals.find(' ',Start);
      string::size_type const Length = ((Space == string::npos) ? Vals.length() : Space) - Start;
      if ( Arch.empty() == false )
      {
	 AddPattern(string("*_") + Arch + Vals.substr(Start, Length));
	 AddPattern(string("*_all") + Vals.substr(Start, Length));
      }
      else
	 AddPattern(string("*") + Vals.substr(Start, Length));

      Start += Length + 1;
   }

   return true;
}

									/*}}}*/
// PackagesWriter::DoPackage - Process a single package			/*{{{*/
// ---------------------------------------------------------------------
/* This method takes a package and gets its control information and 
   MD5, SHA1 and SHA256 then writes out a control record with the proper fields 
   rewritten and the path/size/hash appended. */
bool PackagesWriter::DoPackage(string FileName)
{      
   // Pull all the data we need form the DB
   if (Db.GetFileInfo(FileName,
	    true, /* DoControl */
	    DoContents,
	    true, /* GenContentsOnly */
	    false, /* DoSource */
	    DoHashes, DoAlwaysStat) == false)
   {
     return false;
   }

   unsigned long long FileSize = Db.GetFileSize();
   if (Delink(FileName,OriginalPath,Stats.DeLinkBytes,FileSize) == false)
      return false;
   
   // Lookup the overide information
   pkgTagSection &Tags = Db.Control.Section;
   string Package = Tags.FindS("Package");
   string Architecture;
   // if we generate a Packages file for a given arch, we use it to
   // look for overrides. if we run in "simple" mode without the 
   // "Architecures" variable in the config we use the architecure value
   // from the deb file
   if(Arch != "")
      Architecture = Arch;
   else
      Architecture = Tags.FindS("Architecture");
   auto_ptr<Override::Item> OverItem(Over.GetItem(Package,Architecture));
   
   if (Package.empty() == true)
      return _error->Error(_("Archive had no package field"));

   // If we need to do any rewriting of the header do it now..
   if (OverItem.get() == 0)
   {
      if (NoOverride == false)
      {
	 NewLine(1);
	 ioprintf(c1out, _("  %s has no override entry\n"), Package.c_str());
      }
      
      OverItem = auto_ptr<Override::Item>(new Override::Item);
      OverItem->FieldOverride["Section"] = Tags.FindS("Section");
      OverItem->Priority = Tags.FindS("Priority");
   }

   char Size[40];
   sprintf(Size,"%llu", (unsigned long long) FileSize);
   
   // Strip the DirStrip prefix from the FileName and add the PathPrefix
   string NewFileName;
   if (DirStrip.empty() == false &&
       FileName.length() > DirStrip.length() &&
       stringcmp(FileName.begin(),FileName.begin() + DirStrip.length(),
		 DirStrip.begin(),DirStrip.end()) == 0)
      NewFileName = string(FileName.begin() + DirStrip.length(),FileName.end());
   else 
      NewFileName = FileName;
   if (PathPrefix.empty() == false)
      NewFileName = flCombine(PathPrefix,NewFileName);

   /* Configuration says we don't want to include the long Description
      in the package file - instead we want to ship a separated file */
   string desc;
   if (LongDescription == false) {
      desc = Tags.FindS("Description").append("\n");
      OverItem->FieldOverride["Description"] = desc.substr(0, desc.find('\n')).c_str();
   }

   // This lists all the changes to the fields we are going to make.
   std::vector<TFRewriteData> Changes;

   Changes.push_back(SetTFRewriteData("Size", Size));
   for (HashStringList::const_iterator hs = Db.HashesList.begin(); hs != Db.HashesList.end(); ++hs)
   {
      if (hs->HashType() == "MD5Sum")
	 Changes.push_back(SetTFRewriteData("MD5sum", hs->HashValue().c_str()));
      else
	 Changes.push_back(SetTFRewriteData(hs->HashType().c_str(), hs->HashValue().c_str()));
   }
   Changes.push_back(SetTFRewriteData("Filename", NewFileName.c_str()));
   Changes.push_back(SetTFRewriteData("Priority", OverItem->Priority.c_str()));
   Changes.push_back(SetTFRewriteData("Status", 0));
   Changes.push_back(SetTFRewriteData("Optional", 0));

   string DescriptionMd5;
   if (LongDescription == false) {
      MD5Summation descmd5;
      descmd5.Add(desc.c_str());
      DescriptionMd5 = descmd5.Result().Value();
      Changes.push_back(SetTFRewriteData("Description-md5", DescriptionMd5.c_str()));
      if (TransWriter != NULL)
	 TransWriter->DoPackage(Package, desc, DescriptionMd5);
   }

   // Rewrite the maintainer field if necessary
   bool MaintFailed;
   string NewMaint = OverItem->SwapMaint(Tags.FindS("Maintainer"),MaintFailed);
   if (MaintFailed == true)
   {
      if (NoOverride == false)
      {
	 NewLine(1);
	 ioprintf(c1out, _("  %s maintainer is %s not %s\n"),
	       Package.c_str(), Tags.FindS("Maintainer").c_str(), OverItem->OldMaint.c_str());
      }
   }

   if (NewMaint.empty() == false)
      Changes.push_back(SetTFRewriteData("Maintainer", NewMaint.c_str()));

   /* Get rid of the Optional tag. This is an ugly, ugly, ugly hack that
      dpkg-scanpackages does. Well sort of. dpkg-scanpackages just does renaming
      but dpkg does this append bit. So we do the append bit, at least that way the
      status file and package file will remain similar. There are other transforms
      but optional is the only legacy one still in use for some lazy reason. */
   string OptionalStr = Tags.FindS("Optional");
   if (OptionalStr.empty() == false)
   {
      if (Tags.FindS("Suggests").empty() == false)
	 OptionalStr = Tags.FindS("Suggests") + ", " + OptionalStr;
      Changes.push_back(SetTFRewriteData("Suggests", OptionalStr.c_str()));
   }

   for (map<string,string>::const_iterator I = OverItem->FieldOverride.begin();
        I != OverItem->FieldOverride.end(); ++I)
      Changes.push_back(SetTFRewriteData(I->first.c_str(),I->second.c_str()));

   Changes.push_back(SetTFRewriteData( 0, 0));

   // Rewrite and store the fields.
   if (TFRewrite(Output,Tags,TFRewritePackageOrder,Changes.data()) == false)
      return false;
   fprintf(Output,"\n");

   return Db.Finish();
}
									/*}}}*/

// TranslationWriter::TranslationWriter - Constructor			/*{{{*/
// ---------------------------------------------------------------------
/* Create a Translation-Master file for this Packages file */
TranslationWriter::TranslationWriter(string const &File, string const &TransCompress,
					mode_t const &Permissions) : Output(NULL),
							RefCounter(0)
{
   if (File.empty() == true)
      return;

   Comp = new MultiCompress(File, TransCompress, Permissions);
   Output = Comp->Input;
}
									/*}}}*/
// TranslationWriter::DoPackage - Process a single package		/*{{{*/
// ---------------------------------------------------------------------
/* Create a Translation-Master file for this Packages file */
bool TranslationWriter::DoPackage(string const &Pkg, string const &Desc,
				  string const &MD5)
{
   if (Output == NULL)
      return true;

   // Different archs can include different versions and therefore
   // different descriptions - so we need to check for both name and md5.
   string const Record = Pkg + ":" + MD5;

   if (Included.find(Record) != Included.end())
      return true;

   fprintf(Output, "Package: %s\nDescription-md5: %s\nDescription-en: %s\n",
	   Pkg.c_str(), MD5.c_str(), Desc.c_str());

   Included.insert(Record);
   return true;
}
									/*}}}*/
// TranslationWriter::~TranslationWriter - Destructor			/*{{{*/
// ---------------------------------------------------------------------
/* */
TranslationWriter::~TranslationWriter()
{
   if (Comp == NULL)
      return;

   delete Comp;
}
									/*}}}*/

// SourcesWriter::SourcesWriter - Constructor				/*{{{*/
// ---------------------------------------------------------------------
/* */
SourcesWriter::SourcesWriter(string const &DB, string const &BOverrides,string const &SOverrides,
			     string const &ExtOverrides) :
   Db(DB), Stats(Db.Stats)
{
   Output = stdout;
   AddPattern("*.dsc");
   DeLinkLimit = 0;
   Buffer = 0;
   BufSize = 0;
   
   // Process the command line options
   ConfigToDoHashes(DoHashes, "APT::FTPArchive::Sources");
   NoOverride = _config->FindB("APT::FTPArchive::NoOverrideMsg",false);
   DoAlwaysStat = _config->FindB("APT::FTPArchive::AlwaysStat", false);

   // Read the override file
   if (BOverrides.empty() == false && BOver.ReadOverride(BOverrides) == false)
      return;
   else
      NoOverride = true;

   // WTF?? The logic above: if we can't read binary overrides, don't even try
   // reading source overrides. if we can read binary overrides, then say there
   // are no overrides. THIS MAKES NO SENSE! -- ajt@d.o, 2006/02/28

   if (ExtOverrides.empty() == false)
      SOver.ReadExtraOverride(ExtOverrides);
   
   if (SOverrides.empty() == false && FileExists(SOverrides) == true)
      SOver.ReadOverride(SOverrides,true);
}
									/*}}}*/
// SourcesWriter::DoPackage - Process a single package			/*{{{*/
static std::ostream& addDscHash(std::ostream &out, unsigned int const DoHashes,
      Hashes::SupportedHashes const DoIt, pkgTagSection &Tags, char const * const FieldName,
      HashString const * const Hash, unsigned long long Size, std::string FileName)
{
   if ((DoHashes & DoIt) != DoIt || Tags.Exists(FieldName) == false || Hash == NULL)
      return out;
   out << "\n " << Hash->HashValue() << " " << Size << " " << FileName
      << "\n " << Tags.FindS(FieldName);
   return out;
}
bool SourcesWriter::DoPackage(string FileName)
{
   // Pull all the data we need form the DB
   if (Db.GetFileInfo(FileName,
	    false, /* DoControl */
	    false, /* DoContents */
	    false, /* GenContentsOnly */
	    true, /* DoSource */
	    DoHashes, DoAlwaysStat) == false)
   {
      return false;
   }

   // we need to perform a "write" here (this is what finish is doing)
   // because the call to Db.GetFileInfo() in the loop will change
   // the "db cursor"
   Db.Finish();

   // read stuff
   char *Start = Db.Dsc.Data;
   char *BlkEnd = Db.Dsc.Data + Db.Dsc.Length;

   // Add extra \n to the end, just in case (as in clearsigned they are missing)
   *BlkEnd++ = '\n';
   *BlkEnd++ = '\n';

   pkgTagSection Tags;
   if (Tags.Scan(Start,BlkEnd - Start) == false)
      return _error->Error("Could not find a record in the DSC '%s'",FileName.c_str());
   
   if (Tags.Exists("Source") == false)
      return _error->Error("Could not find a Source entry in the DSC '%s'",FileName.c_str());
   Tags.Trim();

   // Lookup the overide information, finding first the best priority.
   string BestPrio;
   string Bins = Tags.FindS("Binary");
   char Buffer[Bins.length() + 1];
   auto_ptr<Override::Item> OverItem(0);
   if (Bins.empty() == false)
   {
      strcpy(Buffer,Bins.c_str());
      
      // Ignore too-long errors.
      char *BinList[400];
      TokSplitString(',',Buffer,BinList,sizeof(BinList)/sizeof(BinList[0]));
      
      // Look at all the binaries
      unsigned char BestPrioV = pkgCache::State::Extra;
      for (unsigned I = 0; BinList[I] != 0; I++)
      {
	 auto_ptr<Override::Item> Itm(BOver.GetItem(BinList[I]));
	 if (Itm.get() == 0)
	    continue;

	 unsigned char NewPrioV = debListParser::GetPrio(Itm->Priority);
	 if (NewPrioV < BestPrioV || BestPrio.empty() == true)
	 {
	    BestPrioV = NewPrioV;
	    BestPrio = Itm->Priority;
	 }	 

	 if (OverItem.get() == 0)
	    OverItem = Itm;
      }
   }
   
   // If we need to do any rewriting of the header do it now..
   if (OverItem.get() == 0)
   {
      if (NoOverride == false)
      {
	 NewLine(1);	 
	 ioprintf(c1out, _("  %s has no override entry\n"), Tags.FindS("Source").c_str());
      }
      
      OverItem = auto_ptr<Override::Item>(new Override::Item);
   }
   
   struct stat St;
   if (stat(FileName.c_str(), &St) != 0)
      return _error->Errno("fstat","Failed to stat %s",FileName.c_str());

   auto_ptr<Override::Item> SOverItem(SOver.GetItem(Tags.FindS("Source")));
   // const auto_ptr<Override::Item> autoSOverItem(SOverItem);
   if (SOverItem.get() == 0)
   {
      ioprintf(c1out, _("  %s has no source override entry\n"), Tags.FindS("Source").c_str());
      SOverItem = auto_ptr<Override::Item>(BOver.GetItem(Tags.FindS("Source")));
      if (SOverItem.get() == 0)
      {
        ioprintf(c1out, _("  %s has no binary override entry either\n"), Tags.FindS("Source").c_str());
	 SOverItem = auto_ptr<Override::Item>(new Override::Item);
	 *SOverItem = *OverItem;
      }
   }

   // Add the dsc to the files hash list
   string const strippedName = flNotDir(FileName);
   std::ostringstream ostreamFiles;
   addDscHash(ostreamFiles, DoHashes, Hashes::MD5SUM, Tags, "Files", Db.HashesList.find("MD5Sum"), St.st_size, strippedName);
   string const Files = ostreamFiles.str();

   std::ostringstream ostreamSha1;
   addDscHash(ostreamSha1, DoHashes, Hashes::SHA1SUM, Tags, "Checksums-Sha1", Db.HashesList.find("SHA1"), St.st_size, strippedName);
   std::ostringstream ostreamSha256;
   addDscHash(ostreamSha256, DoHashes, Hashes::SHA256SUM, Tags, "Checksums-Sha256", Db.HashesList.find("SHA256"), St.st_size, strippedName);
   std::ostringstream ostreamSha512;
   addDscHash(ostreamSha512, DoHashes, Hashes::SHA512SUM, Tags, "Checksums-Sha512", Db.HashesList.find("SHA512"), St.st_size, strippedName);

   // Strip the DirStrip prefix from the FileName and add the PathPrefix
   string NewFileName;
   if (DirStrip.empty() == false &&
       FileName.length() > DirStrip.length() &&
       stringcmp(DirStrip,OriginalPath,OriginalPath + DirStrip.length()) == 0)
      NewFileName = string(OriginalPath + DirStrip.length());
   else 
      NewFileName = OriginalPath;
   if (PathPrefix.empty() == false)
      NewFileName = flCombine(PathPrefix,NewFileName);

   string Directory = flNotFile(OriginalPath);
   string Package = Tags.FindS("Source");

   // Perform operation over all of the files
   string ParseJnk;
   const char *C = Files.c_str();
   char *RealPath = NULL;
   for (;isspace(*C); C++);
   while (*C != 0)
   {   
      // Parse each of the elements
      if (ParseQuoteWord(C,ParseJnk) == false ||
	  ParseQuoteWord(C,ParseJnk) == false ||
	  ParseQuoteWord(C,ParseJnk) == false)
	 return _error->Error("Error parsing file record");

      string OriginalPath = Directory + ParseJnk;

      // Add missing hashes to source files
      if (((DoHashes & Hashes::SHA1SUM) == Hashes::SHA1SUM && !Tags.Exists("Checksums-Sha1")) ||
          ((DoHashes & Hashes::SHA256SUM) == Hashes::SHA256SUM && !Tags.Exists("Checksums-Sha256")) ||
          ((DoHashes & Hashes::SHA512SUM) == Hashes::SHA512SUM && !Tags.Exists("Checksums-Sha512")))
      {
         if (Db.GetFileInfo(OriginalPath,
                            false, /* DoControl */
                            false, /* DoContents */
                            false, /* GenContentsOnly */
                            false, /* DoSource */
                            DoHashes,
                            DoAlwaysStat) == false)
         {
            return _error->Error("Error getting file info");
         }

         for (HashStringList::const_iterator hs = Db.HashesList.begin(); hs != Db.HashesList.end(); ++hs)
	 {
	    if (hs->HashType() == "MD5Sum")
	       continue;
	    char const * fieldname;
	    std::ostream * out;
	    if (hs->HashType() == "SHA1")
	    {
	       fieldname = "Checksums-Sha1";
	       out = &ostreamSha1;
	    }
	    else if (hs->HashType() == "SHA256")
	    {
	       fieldname = "Checksums-Sha256";
	       out = &ostreamSha256;
	    }
	    else if (hs->HashType() == "SHA512")
	    {
	       fieldname = "Checksums-Sha512";
	       out = &ostreamSha512;
	    }
	    else
	    {
	       _error->Warning("Ignoring unknown Checksumtype %s in SourcesWriter::DoPackages", hs->HashType().c_str());
	       continue;
	    }
	    if (Tags.Exists(fieldname) == true)
	       continue;
	    (*out) << "\n " << hs->HashValue() << " " << Db.GetFileSize() << " " << ParseJnk;
	 }

	 // write back the GetFileInfo() stats data 
	 Db.Finish();
      }

      // Perform the delinking operation
      char Jnk[2];

      if (readlink(OriginalPath.c_str(),Jnk,sizeof(Jnk)) != -1 &&
	  (RealPath = realpath(OriginalPath.c_str(),NULL)) != 0)
      {
	 string RP = RealPath;
	 free(RealPath);
	 if (Delink(RP,OriginalPath.c_str(),Stats.DeLinkBytes,St.st_size) == false)
	    return false;
      }
   }

   Directory = flNotFile(NewFileName);
   if (Directory.length() > 2)
      Directory.erase(Directory.end()-1);

   string const ChecksumsSha1 = ostreamSha1.str();
   string const ChecksumsSha256 = ostreamSha256.str();
   string const ChecksumsSha512 = ostreamSha512.str();

   // This lists all the changes to the fields we are going to make.
   // (5 hardcoded + checksums + maintainer + end marker)
   std::vector<TFRewriteData> Changes;

   Changes.push_back(SetTFRewriteData("Source",Package.c_str(),"Package"));
   if (Files.empty() == false)
      Changes.push_back(SetTFRewriteData("Files",Files.c_str()));
   if (ChecksumsSha1.empty() == false)
      Changes.push_back(SetTFRewriteData("Checksums-Sha1",ChecksumsSha1.c_str()));
   if (ChecksumsSha256.empty() == false)
      Changes.push_back(SetTFRewriteData("Checksums-Sha256",ChecksumsSha256.c_str()));
   if (ChecksumsSha512.empty() == false)
      Changes.push_back(SetTFRewriteData("Checksums-Sha512",ChecksumsSha512.c_str()));
   if (Directory != "./")
      Changes.push_back(SetTFRewriteData("Directory",Directory.c_str()));
   Changes.push_back(SetTFRewriteData("Priority",BestPrio.c_str()));
   Changes.push_back(SetTFRewriteData("Status",0));

   // Rewrite the maintainer field if necessary
   bool MaintFailed;
   string NewMaint = OverItem->SwapMaint(Tags.FindS("Maintainer"),MaintFailed);
   if (MaintFailed == true)
   {
      if (NoOverride == false)
      {
	 NewLine(1);	 
	 ioprintf(c1out, _("  %s maintainer is %s not %s\n"), Package.c_str(),
	       Tags.FindS("Maintainer").c_str(), OverItem->OldMaint.c_str());
      }      
   }
   if (NewMaint.empty() == false)
      Changes.push_back(SetTFRewriteData("Maintainer", NewMaint.c_str()));
   
   for (map<string,string>::const_iterator I = SOverItem->FieldOverride.begin(); 
        I != SOverItem->FieldOverride.end(); ++I)
      Changes.push_back(SetTFRewriteData(I->first.c_str(),I->second.c_str()));

   Changes.push_back(SetTFRewriteData(0, 0));
      
   // Rewrite and store the fields.
   if (TFRewrite(Output,Tags,TFRewriteSourceOrder,Changes.data()) == false)
      return false;
   fprintf(Output,"\n");

   Stats.Packages++;
   
   return true;
}
									/*}}}*/

// ContentsWriter::ContentsWriter - Constructor				/*{{{*/
// ---------------------------------------------------------------------
/* */
ContentsWriter::ContentsWriter(string const &DB, string const &Arch) :
		    FTWScanner(Arch), Db(DB), Stats(Db.Stats)

{
   SetExts(".deb");
   Output = stdout;
}
									/*}}}*/
// ContentsWriter::DoPackage - Process a single package			/*{{{*/
// ---------------------------------------------------------------------
/* If Package is the empty string the control record will be parsed to
   determine what the package name is. */
bool ContentsWriter::DoPackage(string FileName, string Package)
{
   if (!Db.GetFileInfo(FileName,
	    Package.empty(), /* DoControl */
	    true, /* DoContents */
	    false, /* GenContentsOnly */
	    false, /* DoSource */
	    0, /* DoHashes */
	    false /* checkMtime */))
   {
      return false;
   }

   // Parse the package name
   if (Package.empty() == true)
   {
      Package = Db.Control.Section.FindS("Package");
   }

   Db.Contents.Add(Gen,Package);
   
   return Db.Finish();
}
									/*}}}*/
// ContentsWriter::ReadFromPkgs - Read from a packages file		/*{{{*/
// ---------------------------------------------------------------------
/* */
bool ContentsWriter::ReadFromPkgs(string const &PkgFile,string const &PkgCompress)
{
   MultiCompress Pkgs(PkgFile,PkgCompress,0,false);
   if (_error->PendingError() == true)
      return false;

   // Open the package file
   FileFd Fd;
   if (Pkgs.OpenOld(Fd) == false)
      return false;

   pkgTagFile Tags(&Fd);
   if (_error->PendingError() == true)
      return false;

   // Parse.
   pkgTagSection Section;
   while (Tags.Step(Section) == true)
   {
      string File = flCombine(Prefix,Section.FindS("FileName"));
      string Package = Section.FindS("Section");
      if (Package.empty() == false && Package.end()[-1] != '/')
      {
	 Package += '/';
	 Package += Section.FindS("Package");
      }
      else
	 Package += Section.FindS("Package");
	 
      DoPackage(File,Package);
      if (_error->empty() == false)
      {
	 _error->Error("Errors apply to file '%s'",File.c_str());
	 _error->DumpErrors();
      }
   }

   // Tidy the compressor
   Fd.Close();

   return true;
}

									/*}}}*/

// ReleaseWriter::ReleaseWriter - Constructor				/*{{{*/
// ---------------------------------------------------------------------
/* */
ReleaseWriter::ReleaseWriter(string const &/*DB*/)
{
   if (_config->FindB("APT::FTPArchive::Release::Default-Patterns", true) == true)
   {
      AddPattern("Packages");
      AddPattern("Packages.gz");
      AddPattern("Packages.bz2");
      AddPattern("Packages.lzma");
      AddPattern("Packages.xz");
      AddPattern("Translation-*");
      AddPattern("Sources");
      AddPattern("Sources.gz");
      AddPattern("Sources.bz2");
      AddPattern("Sources.lzma");
      AddPattern("Sources.xz");
      AddPattern("Release");
      AddPattern("Contents-*");
      AddPattern("Index");
      AddPattern("md5sum.txt");
   }
   AddPatterns(_config->FindVector("APT::FTPArchive::Release::Patterns"));

   Output = stdout;
   time_t const now = time(NULL);

   setlocale(LC_TIME, "C");

   char datestr[128];
   if (strftime(datestr, sizeof(datestr), "%a, %d %b %Y %H:%M:%S UTC",
                gmtime(&now)) == 0)
   {
      datestr[0] = '\0';
   }

   time_t const validuntil = now + _config->FindI("APT::FTPArchive::Release::ValidTime", 0);
   char validstr[128];
   if (now == validuntil ||
       strftime(validstr, sizeof(validstr), "%a, %d %b %Y %H:%M:%S UTC",
                gmtime(&validuntil)) == 0)
   {
      validstr[0] = '\0';
   }

   setlocale(LC_TIME, "");

   map<string,string> Fields;
   Fields["Origin"] = "";
   Fields["Label"] = "";
   Fields["Suite"] = "";
   Fields["Version"] = "";
   Fields["Codename"] = "";
   Fields["Date"] = datestr;
   Fields["Valid-Until"] = validstr;
   Fields["Architectures"] = "";
   Fields["Components"] = "";
   Fields["Description"] = "";

   for(map<string,string>::const_iterator I = Fields.begin();
       I != Fields.end();
       ++I)
   {
      string Config = string("APT::FTPArchive::Release::") + (*I).first;
      string Value = _config->Find(Config, (*I).second.c_str());
      if (Value == "")
         continue;

      fprintf(Output, "%s: %s\n", (*I).first.c_str(), Value.c_str());
   }

   ConfigToDoHashes(DoHashes, "APT::FTPArchive::Release");
}
									/*}}}*/
// ReleaseWriter::DoPackage - Process a single package			/*{{{*/
// ---------------------------------------------------------------------
bool ReleaseWriter::DoPackage(string FileName)
{
   // Strip the DirStrip prefix from the FileName and add the PathPrefix
   string NewFileName;
   if (DirStrip.empty() == false &&
       FileName.length() > DirStrip.length() &&
       stringcmp(FileName.begin(),FileName.begin() + DirStrip.length(),
		 DirStrip.begin(),DirStrip.end()) == 0)
   {
      NewFileName = string(FileName.begin() + DirStrip.length(),FileName.end());
      while (NewFileName[0] == '/')
         NewFileName = string(NewFileName.begin() + 1,NewFileName.end());
   }
   else 
      NewFileName = FileName;

   if (PathPrefix.empty() == false)
      NewFileName = flCombine(PathPrefix,NewFileName);

   FileFd fd(FileName, FileFd::ReadOnly);

   if (!fd.IsOpen())
   {
      return false;
   }

   CheckSums[NewFileName].size = fd.Size();

   Hashes hs;
   hs.AddFD(fd, 0, DoHashes);
   CheckSums[NewFileName].Hashes = hs.GetHashStringList();
   fd.Close();

   return true;
}

									/*}}}*/
// ReleaseWriter::Finish - Output the checksums				/*{{{*/
// ---------------------------------------------------------------------
static void printChecksumTypeRecord(FILE * const Output, char const * const Type, map<string, ReleaseWriter::CheckSum> const &CheckSums)
{
      fprintf(Output, "%s:\n", Type);
      for(map<string,ReleaseWriter::CheckSum>::const_iterator I = CheckSums.begin();
	  I != CheckSums.end(); ++I)
      {
	 HashString const * const hs = I->second.Hashes.find(Type);
	 if (hs == NULL)
	    continue;
	 fprintf(Output, " %s %16llu %s\n",
		 hs->HashValue().c_str(),
		 (*I).second.size,
		 (*I).first.c_str());
      }
}
void ReleaseWriter::Finish()
{
   if ((DoHashes & Hashes::MD5SUM) == Hashes::MD5SUM)
      printChecksumTypeRecord(Output, "MD5Sum", CheckSums);
   if ((DoHashes & Hashes::SHA1SUM) == Hashes::SHA1SUM)
      printChecksumTypeRecord(Output, "SHA1", CheckSums);
   if ((DoHashes & Hashes::SHA256SUM) == Hashes::SHA256SUM)
      printChecksumTypeRecord(Output, "SHA256", CheckSums);
   if ((DoHashes & Hashes::SHA512SUM) == Hashes::SHA512SUM)
      printChecksumTypeRecord(Output, "SHA512", CheckSums);
}
