#ifndef SOURCES_H
#define SOURCES_H

#include <apt-pkg/tagfile.h>

class DscExtract 
{
 public:
   char *DscData;
   pkgTagSection Section;
   unsigned long Length;

   bool TakeDsc(const void *Data, unsigned long Size);
   bool Read(std::string FileName);
   
   DscExtract() : DscData(0), Length(0) {};
   ~DscExtract() { delete [] DscData; };
};


#endif
