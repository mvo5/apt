#include <string>
#include <iostream>
// for memcpy
#include <cstring>

#include <apt-pkg/gpgv.h>

#include "sources.h"

bool DscExtract::TakeDsc(const void *newData, unsigned long newSize)
{
   if(newSize > maxSize)
     return false;

   if (newSize == 0)
   {
      Length = 0;
      return true;
   }
   memcpy(Data, newData, newSize);
   Length = newSize;

   return true;
}

bool DscExtract::Read(std::string FileName)
{
   FileFd F;
   if (OpenMaybeClearSignedFile(FileName, F) == false)
      return false;
   
   unsigned long long const FSize = F.FileSize();
   if(FSize > maxSize)
     return false;

   if (F.Read(Data, FSize) == false)
      return false;
   Length = FSize;

   IsClearSigned = (FileName != F.Name());

   return true;
}


