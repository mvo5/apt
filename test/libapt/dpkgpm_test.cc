#include <apt-pkg/cachefile.h>
#include <apt-pkg/init.h>
#include <apt-pkg/dpkgpm.h>

#include "assert.h"

class TestDPkgPM : public pkgDPkgPM
{
public:
   TestDPkgPM(pkgDepCache *Cache)  : pkgDPkgPM(Cache)
   {
   }
   void ProcessDpkgStatusLine(int OutStatusFd, char *line)
   {
      pkgDPkgPM::ProcessDpkgStatusLine(OutStatusFd, line);
   }
};

int main(int argc,char *argv[])
{
   pkgInitConfig(*_config);
   pkgInitSystem(*_config, _system);

   OpProgress progress;
   pkgCacheFile cache;
   cache.Open(progress, false);
   pkgDepCache *depcache = cache.GetDepCache();

   TestDPkgPM pm(depcache);


   char *ok = "status: 2vcard: half-configured";
   pm.ProcessDpkgStatusLine(STDOUT_FILENO, ok);
   
#if 0
   char *err = "status: /var/cache/apt/archives/krecipes_0.8.1-0ubuntu1_i386.deb : error : trying to overwrite `/usr/share/doc/kde/HTML/en/krecipes/krectip.png', which is also in package krecipes-data ";
   pm.ProcessDpkgStatusLine(STDOUT_FILENO, err);
#endif
   
   std::cout << "PASS" << std::endl;

   return 0;
}
