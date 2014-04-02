#include <string>

// for memcpy
#include <cstring>

#include <apt-pkg/gpgv.h>

#include "sources.h"

bool DscExtract::TakeDsc(const void *newData, unsigned long newSize)
{
   memcpy(DscData, newData, newSize);
   Length = newSize;

   if (newSize == 0)
   {
      Length = 0;
      return true;
   }

   return DscData[Length-1] == 0;
}

bool DscExtract::Read(std::string FileName)
{
   FileFd F;
   if (OpenMaybeClearSignedFile(FileName, F) == false)
      return false;
   
   unsigned long long const FSize = F.FileSize();
   DscData = new char[FSize];
   if (F.Read(DscData, FSize) == false)
      return false;
   return true;
}


