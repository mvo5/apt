#include <apt-pkg/cachefile.h>
#include <apt-pkg/init.h>
#include <apt-pkg/dpkgpm.h>

#include "assert.h"

// testclass so that we can access some protected members
class TestDPkgPM : public pkgDPkgPM
{
public:
   TestDPkgPM(pkgDepCache *Cache)  : pkgDPkgPM(Cache)
   {
      PackagesTotal = 1;
   }
   void ProcessDpkgStatusLine(int OutStatusFd, char *line)
   {
      pkgDPkgPM::ProcessDpkgStatusLine(OutStatusFd, line);
   }
};

// FIXME: move all into the TestDPkgPM class
TestDPkgPM* Setup()
{
   pkgInitConfig(*_config);
   pkgInitSystem(*_config, _system);

   _config->Set("Dpkg::ApportFailureReport", "false");

   OpProgress progress;
   pkgCacheFile cache;
   cache.Open(progress, false);
   pkgDepCache *depcache = cache.GetDepCache();

   TestDPkgPM *pm = new TestDPkgPM(depcache);
   return pm;
}

void TearDown(TestDPkgPM *pm)
{
   delete pm;
};

void test_process_dpkg_status_line_simple(TestDPkgPM *pm)
{
   // test
   char *ok = "status: 2vcard: half-configured";
   pm->ProcessDpkgStatusLine(STDOUT_FILENO, ok);
   
}

void test_process_dpkg_status_line_error(TestDPkgPM *pm)
{
   char tmpname[] = "dpkgpmXXXXXX";
   int fd = mkstemp(tmpname);

   char *err = "status: /var/cache/apt/archives/krecipes_0.8.1-0ubuntu1_i386.deb : error : trying to overwrite `/usr/share/doc/kde/HTML/en/krecipes/krectip.png', which is also in package krecipes-data ";
   pm->ProcessDpkgStatusLine(fd, err);

   char buf[200];
   lseek(fd, 0, SEEK_SET);
   read(fd, buf, sizeof(buf));

   equals(buf, "pmerror:/var/cache/apt/archives/krecipes_0.8.1-0ubuntu1_i386.deb :0:trying to overwrite `/usr/share/doc/kde/HTML/en/krecipes/krectip.png', which is also in package krecipes-data \n");

}

int main(int argc,char *argv[])
{
   TestDPkgPM *pm = Setup();

   test_process_dpkg_status_line_simple(pm);
   test_process_dpkg_status_line_error(pm);

   TearDown(pm);
}
