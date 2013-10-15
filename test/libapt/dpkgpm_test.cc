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
   }
   void ProcessDpkgStatusLine(int OutStatusFd, char *line)
   {
      pkgDPkgPM::ProcessDpkgStatusLine(OutStatusFd, line);
   }

   // helper
   bool Install(pkgCache::PkgIterator Pkg, std::string File) {
      return pkgDPkgPM::Install(Pkg, File);
   }
   bool Configure(pkgCache::PkgIterator Pkg) {
      return pkgDPkgPM::Configure(Pkg);
   }
   void BuildDpkgProgressMap() {
      pkgDPkgPM::BuildDpkgProgressMap();
   }
};

// FIXME: move all into the TestDPkgPM class
TestDPkgPM* Setup()
{
   // init
   pkgInitConfig(*_config);
   pkgInitSystem(*_config, _system);
   _config->Set("Dpkg::ApportFailureReport", "false");

   // get cache
   OpProgress progress;
   pkgCacheFile cache;
   cache.Open(progress, false);

   // build our test PM
   TestDPkgPM *pm = new TestDPkgPM(cache);
   
   // pretend we install stuff
   pkgCache::PkgIterator pkg = cache->FindPkg("apt");
   pm->Install(pkg, "/var/cache/apt/archives/apt_1.0_all.deb");
   pm->Configure(pkg);
   pm->BuildDpkgProgressMap();

   return pm;
}

void TearDown(TestDPkgPM *pm)
{
   delete pm;
};

int prepare_tmp_fd()
{
   char tmpname[] = "dpkgpmXXXXXX";
   int fd = mkstemp(tmpname);
   unlink(tmpname);
   return fd;
}

void assert_written_to_fd(int fd, string expected)
{
   char buf[200];
   lseek(fd, 0, SEEK_SET);
   int i = read(fd, buf, sizeof(buf));
   buf[i] = 0;

   equals(string(buf), expected);
}

void test_process_dpkg_status_line_simple(TestDPkgPM *pm)
{
   int fd = prepare_tmp_fd();

   char *ok = "status: apt: half-installed";
   pm->ProcessDpkgStatusLine(fd, ok);
   
   assert_written_to_fd(fd, "pmstatus:apt:amd64:20:Preparing apt:amd64\n");

   fd = prepare_tmp_fd();
   char *ok2 = "status: apt: unpacked";
   pm->ProcessDpkgStatusLine(fd, ok2);
   assert_written_to_fd(fd, "pmstatus:apt:amd64:40:Unpacking apt:amd64\n");

   fd = prepare_tmp_fd();
   char *ok3 = "status: apt: unpacked";
   pm->ProcessDpkgStatusLine(fd, ok3);
   assert_written_to_fd(fd, "pmstatus:apt:amd64:60:Preparing to configure apt:amd64\n");

   fd = prepare_tmp_fd();
   char *ok4 = "status: apt: half-configured";
   pm->ProcessDpkgStatusLine(fd, ok4);
   assert_written_to_fd(fd, "pmstatus:apt:amd64:80:Configuring apt:amd64\n");

   fd = prepare_tmp_fd();
   char *ok5 = "status: apt: installed";
   pm->ProcessDpkgStatusLine(fd, ok5);
   assert_written_to_fd(fd, "pmstatus:apt:amd64:100:Installed apt:amd64\n");

}

void test_process_dpkg_status_line_error(TestDPkgPM *pm)
{
   int fd = prepare_tmp_fd();

   char *err = "status: /var/cache/apt/archives/krecipes_0.8.1-0ubuntu1_i386.deb : error : trying to overwrite `/usr/share/doc/kde/HTML/en/krecipes/krectip.png', which is also in package krecipes-data ";

   pm->ProcessDpkgStatusLine(fd, err);

   assert_written_to_fd(fd, "pmerror:/var/cache/apt/archives/krecipes_0.8.1-0ubuntu1_i386.deb :100:trying to overwrite `/usr/share/doc/kde/HTML/en/krecipes/krectip.png', which is also in package krecipes-data \n");
}

int main(int argc,char *argv[])
{
   TestDPkgPM *pm = Setup();

   test_process_dpkg_status_line_simple(pm);
   test_process_dpkg_status_line_error(pm);

   TearDown(pm);
}
