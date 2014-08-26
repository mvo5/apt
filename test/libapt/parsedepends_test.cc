#include <config.h>

#include <apt-pkg/deblistparser.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/pkgcache.h>

#include <string.h>
#include <string>

#include <gtest/gtest.h>

static void parseDependency(bool const StripMultiArch,  bool const ParseArchFlags, bool const ParseRestrictionsList)
{
   std::string Package;
   std::string Version;
   unsigned int Op = 5;
   unsigned int Null = 0;
   _config->Set("APT::Architecture","amd64");
   _config->Set("APT::Build-Profiles","stage1");

   const char* Depends =
      "debhelper:any (>= 5.0), "
      "libdb-dev:any, "
      "gettext:native (<= 0.12), "
      "libcurl4-gnutls-dev:native | libcurl3-gnutls-dev (>> 7.15.5), "
      "docbook-xml, "
      "apt (>= 0.7.25), "
      "not-for-me [ !amd64 ], "
      "only-for-me [ amd64 ], "
      "any-for-me [ any ], "
      "not-for-darwin [ !darwin-any ], "
      "cpu-for-me [ any-amd64 ], "
      "os-for-me [ linux-any ], "
      "cpu-not-for-me [ any-armel ], "
      "os-not-for-me [ kfreebsd-any ], "
      "not-in-stage1 <!profile.stage1>, "
      "not-in-stage1-or-nodoc <!profile.nodoc !profile.stage1>, "
      "only-in-stage1 <unknown.unknown profile.stage1>, "
      "overlord-dev:any (= 7.15.3~) | overlord-dev:native (>> 7.15.5), "
      ;

   // Stripping MultiArch is currently the default setting to not confuse
   // non-MultiArch capable users of the library with "strange" extensions.
   const char* Start = Depends;
   const char* End = Depends + strlen(Depends);

   Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
   if (StripMultiArch == true)
      EXPECT_EQ("debhelper", Package);
   else
      EXPECT_EQ("debhelper:any", Package);
   EXPECT_EQ("5.0", Version);
   EXPECT_EQ(Null | pkgCache::Dep::GreaterEq, Op);

   Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
   if (StripMultiArch == true)
      EXPECT_EQ("libdb-dev", Package);
   else
      EXPECT_EQ("libdb-dev:any", Package);
   EXPECT_EQ("", Version);
   EXPECT_EQ(Null | pkgCache::Dep::NoOp, Op);

   Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
   if (StripMultiArch == true)
      EXPECT_EQ("gettext", Package);
   else
      EXPECT_EQ("gettext:native", Package);
   EXPECT_EQ("0.12", Version);
   EXPECT_EQ(Null | pkgCache::Dep::LessEq, Op);

   Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
   if (StripMultiArch == true)
      EXPECT_EQ("libcurl4-gnutls-dev", Package);
   else
      EXPECT_EQ("libcurl4-gnutls-dev:native", Package);
   EXPECT_EQ("", Version);
   EXPECT_EQ(Null | pkgCache::Dep::Or, Op);

   Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
   EXPECT_EQ("libcurl3-gnutls-dev", Package);
   EXPECT_EQ("7.15.5", Version);
   EXPECT_EQ(Null | pkgCache::Dep::Greater, Op);

   Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
   EXPECT_EQ("docbook-xml", Package);
   EXPECT_EQ("", Version);
   EXPECT_EQ(Null | pkgCache::Dep::NoOp, Op);

   Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
   EXPECT_EQ("apt", Package);
   EXPECT_EQ("0.7.25", Version);
   EXPECT_EQ(Null | pkgCache::Dep::GreaterEq, Op);

   if (ParseArchFlags == true) {
      Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
      EXPECT_EQ("", Package); // not-for-me
   } else {
      EXPECT_EQ(true, 0 == debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList));
      Start = strstr(Start, ",");
      Start++;
   }

   if (ParseArchFlags == true) {
      Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
      EXPECT_EQ("only-for-me", Package);
      EXPECT_EQ("", Version);
      EXPECT_EQ(Null | pkgCache::Dep::NoOp, Op);
   } else {
      EXPECT_EQ(true, 0 == debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList));
      Start = strstr(Start, ",");
      Start++;
   }

   if (ParseArchFlags == true) {
      Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
      EXPECT_EQ("any-for-me", Package);
      EXPECT_EQ("", Version);
      EXPECT_EQ(Null | pkgCache::Dep::NoOp, Op);
   } else {
      EXPECT_EQ(true, 0 == debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList));
      Start = strstr(Start, ",");
      Start++;
   }

   if (ParseArchFlags == true) {
      Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
      EXPECT_EQ("not-for-darwin", Package);
      EXPECT_EQ("", Version);
      EXPECT_EQ(Null | pkgCache::Dep::NoOp, Op);
   } else {
      EXPECT_EQ(true, 0 == debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList));
      Start = strstr(Start, ",");
      Start++;
   }

   if (ParseArchFlags == true) {
      Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
      EXPECT_EQ("cpu-for-me", Package);
      EXPECT_EQ("", Version);
      EXPECT_EQ(Null | pkgCache::Dep::NoOp, Op);
   } else {
      EXPECT_EQ(true, 0 == debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList));
      Start = strstr(Start, ",");
      Start++;
   }

   if (ParseArchFlags == true) {
      Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
      EXPECT_EQ("os-for-me", Package);
      EXPECT_EQ("", Version);
      EXPECT_EQ(Null | pkgCache::Dep::NoOp, Op);
   } else {
      EXPECT_EQ(true, 0 == debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList));
      Start = strstr(Start, ",");
      Start++;
   }

   if (ParseArchFlags == true) {
      Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
      EXPECT_EQ("", Package); // cpu-not-for-me
   } else {
      EXPECT_EQ(true, 0 == debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList));
      Start = strstr(Start, ",");
      Start++;
   }

   if (ParseArchFlags == true) {
      Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
      EXPECT_EQ("", Package); // os-not-for-me
   } else {
      EXPECT_EQ(true, 0 == debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList));
      Start = strstr(Start, ",");
      Start++;
   }

   if (ParseRestrictionsList == true) {
      Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
      EXPECT_EQ("", Package); // not-in-stage1
   } else {
      EXPECT_EQ(true, 0 == debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList));
      Start = strstr(Start, ",");
      Start++;
   }

   if (ParseRestrictionsList == true) {
      Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
      EXPECT_EQ("", Package); // not-in-stage1-or-in-nodoc
   } else {
      EXPECT_EQ(true, 0 == debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList));
      Start = strstr(Start, ",");
      Start++;
   }

   if (ParseRestrictionsList == true) {
      Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
      EXPECT_EQ("only-in-stage1", Package);
   } else {
      EXPECT_EQ(true, 0 == debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList));
      Start = strstr(Start, ",");
      Start++;
   }

   Start = debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
   if (StripMultiArch == true)
      EXPECT_EQ("overlord-dev", Package);
   else
      EXPECT_EQ("overlord-dev:any", Package);
   EXPECT_EQ("7.15.3~", Version);
   EXPECT_EQ(Null | pkgCache::Dep::Equals | pkgCache::Dep::Or, Op);

   debListParser::ParseDepends(Start, End, Package, Version, Op, ParseArchFlags, StripMultiArch, ParseRestrictionsList);
   if (StripMultiArch == true)
      EXPECT_EQ("overlord-dev", Package);
   else
      EXPECT_EQ("overlord-dev:native", Package);
   EXPECT_EQ("7.15.5", Version);
   EXPECT_EQ(Null | pkgCache::Dep::Greater, Op);
}

// FIXME: This testcase is too big/complex
TEST(ParseDependsTest, Everything)
{
   bool StripMultiArch = true;
   bool ParseArchFlags = false;
   bool ParseRestrictionsList = false;
   unsigned short runner = 0;

test:
   {
      SCOPED_TRACE(std::string("StripMultiArch: ") + (StripMultiArch ? "true" : "false"));
      SCOPED_TRACE(std::string("ParseArchFlags: ") + (ParseArchFlags ? "true" : "false"));
      SCOPED_TRACE(std::string("ParseRestrictionsList: ") + (ParseRestrictionsList ? "true" : "false"));
      parseDependency(StripMultiArch, ParseArchFlags, ParseRestrictionsList);
   }
   if (StripMultiArch == false)
      if (ParseArchFlags == false)
	 ParseRestrictionsList = !ParseRestrictionsList;
   ParseArchFlags = !ParseArchFlags;
   StripMultiArch = !StripMultiArch;

   runner++;
   if (runner < 8)
      goto test; // this is the prove: tests are really evil ;)
}
