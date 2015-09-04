// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: deblistparser.h,v 1.9 2001/02/20 07:03:17 jgg Exp $
/* ######################################################################
   
   Debian Package List Parser - This implements the abstract parser 
   interface for Debian package files
   
   ##################################################################### */
									/*}}}*/
#ifndef PKGLIB_DEBLISTPARSER_H
#define PKGLIB_DEBLISTPARSER_H

#include <apt-pkg/pkgcachegen.h>
#include <apt-pkg/tagfile.h>
#include <apt-pkg/md5.h>
#include <apt-pkg/pkgcache.h>
#include <apt-pkg/macros.h>

#include <string>
#include <vector>

#ifndef APT_8_CLEANER_HEADERS
#include <apt-pkg/indexfile.h>
#endif

class FileFd;

class APT_HIDDEN debListParser : public pkgCacheListParser
{
   public:

   // Parser Helper
   struct WordList
   {
      const char *Str;
      unsigned char Val;
   };

   private:
   /** \brief dpointer placeholder (for later in case we need it) */
   void * const d;

   protected:
   pkgTagFile Tags;
   pkgTagSection Section;
   map_filesize_t iOffset;

   virtual bool ParseStatus(pkgCache::PkgIterator &Pkg,pkgCache::VerIterator &Ver);
   bool ParseDepends(pkgCache::VerIterator &Ver,const char *Tag,
		     unsigned int Type);
   bool ParseProvides(pkgCache::VerIterator &Ver);
   static bool GrabWord(std::string Word,WordList *List,unsigned char &Out);
   APT_HIDDEN unsigned char ParseMultiArch(bool const showErrors);

   public:

   APT_PUBLIC static unsigned char GetPrio(std::string Str);
      
   // These all operate against the current section
   virtual std::string Package() APT_OVERRIDE;
   virtual std::string Architecture() APT_OVERRIDE;
   virtual bool ArchitectureAll() APT_OVERRIDE;
   virtual std::string Version() APT_OVERRIDE;
   virtual bool NewVersion(pkgCache::VerIterator &Ver) APT_OVERRIDE;
   virtual std::string Description(std::string const &lang) APT_OVERRIDE;
   virtual std::vector<std::string> AvailableDescriptionLanguages() APT_OVERRIDE;
   virtual MD5SumValue Description_md5() APT_OVERRIDE;
   virtual unsigned short VersionHash() APT_OVERRIDE;
   virtual bool SameVersion(unsigned short const Hash, pkgCache::VerIterator const &Ver) APT_OVERRIDE;
   virtual bool UsePackage(pkgCache::PkgIterator &Pkg,
			   pkgCache::VerIterator &Ver) APT_OVERRIDE;
   virtual map_filesize_t Offset() APT_OVERRIDE {return iOffset;};
   virtual map_filesize_t Size() APT_OVERRIDE {return Section.size();};

   virtual bool Step() APT_OVERRIDE;

   bool LoadReleaseInfo(pkgCache::RlsFileIterator &FileI,FileFd &File,
			std::string const &section);

   APT_PUBLIC static const char *ParseDepends(const char *Start,const char *Stop,
	 std::string &Package,std::string &Ver,unsigned int &Op);
   APT_PUBLIC static const char *ParseDepends(const char *Start,const char *Stop,
	 std::string &Package,std::string &Ver,unsigned int &Op,
	 bool const &ParseArchFlags);
   APT_PUBLIC static const char *ParseDepends(const char *Start,const char *Stop,
	 std::string &Package,std::string &Ver,unsigned int &Op,
	 bool const &ParseArchFlags, bool const &StripMultiArch);
   APT_PUBLIC static const char *ParseDepends(const char *Start,const char *Stop,
	 std::string &Package,std::string &Ver,unsigned int &Op,
	 bool const &ParseArchFlags, bool const &StripMultiArch,
	 bool const &ParseRestrictionsList);

   APT_PUBLIC static const char *ConvertRelation(const char *I,unsigned int &Op);

   debListParser(FileFd *File);
   virtual ~debListParser();
};

class APT_HIDDEN debDebFileParser : public debListParser
{
 private:
   std::string DebFile;

 public:
   debDebFileParser(FileFd *File, std::string const &DebFile);
   virtual bool UsePackage(pkgCache::PkgIterator &Pkg,
			   pkgCache::VerIterator &Ver) APT_OVERRIDE;
};

class APT_HIDDEN debTranslationsParser : public debListParser
{
 public:
   // a translation can never be a real package
   virtual std::string Architecture() APT_OVERRIDE { return ""; }
   virtual std::string Version() APT_OVERRIDE { return ""; }

   debTranslationsParser(FileFd *File)
      : debListParser(File) {};
};

class APT_HIDDEN debStatusListParser : public debListParser
{
 public:
   virtual bool ParseStatus(pkgCache::PkgIterator &Pkg,pkgCache::VerIterator &Ver);
   debStatusListParser(FileFd *File)
      : debListParser(File) {};
};
#endif
