// -*- mode: cpp; mode: fold -*-
// Include Files							/*{{{*/
#include<config.h>

#include <iostream>
#include <sstream>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include <apt-pkg/error.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/configuration.h>

#include <apti18n.h>
									/*}}}*/
static char * GenerateTemporaryFileTemplate(const char *basename)	/*{{{*/
{
   const char *tmpdir = getenv("TMPDIR");
#ifdef P_tmpdir
   if (!tmpdir)
      tmpdir = P_tmpdir;
#endif
   if (!tmpdir)
      tmpdir = "/tmp";

   std::string out;
   strprintf(out,  "%s/%s.XXXXXX", tmpdir, basename);
   return strdup(out.c_str());
}


using namespace std;

// RunGPGV - returns the command needed for verify			/*{{{*/
// ---------------------------------------------------------------------
/* Generating the commandline for calling gpgv is somehow complicated as
   we need to add multiple keyrings and user supplied options. */
bool ExecGPGV(std::string const &File, std::string const &FileGPG,
			int const &statusfd, int fd[2])
{
   if (File == FileGPG)
   {
      #define SIGMSG "-----BEGIN PGP SIGNED MESSAGE-----\n"
      char buffer[sizeof(SIGMSG)];
      FILE* gpg = fopen(File.c_str(), "r");
      if (gpg == NULL)
	 return _error->Errno("RunGPGV", _("Could not open file %s"), File.c_str());
      char const * const test = fgets(buffer, sizeof(buffer), gpg);
      fclose(gpg);
      if (test == NULL || strcmp(buffer, SIGMSG) != 0)
	 return _error->Error(_("File %s doesn't start with a clearsigned message"), File.c_str());
      #undef SIGMSG
   }


   string const gpgvpath = _config->Find("Dir::Bin::gpg", "/usr/bin/gpgv");
   // FIXME: remove support for deprecated APT::GPGV setting
   string const trustedFile = _config->Find("APT::GPGV::TrustedKeyring", _config->FindFile("Dir::Etc::Trusted"));
   string const trustedPath = _config->FindDir("Dir::Etc::TrustedParts");

   bool const Debug = _config->FindB("Debug::Acquire::gpgv", false);

   if (Debug == true)
   {
      std::clog << "gpgv path: " << gpgvpath << std::endl;
      std::clog << "Keyring file: " << trustedFile << std::endl;
      std::clog << "Keyring path: " << trustedPath << std::endl;
   }

   std::vector<string> keyrings;
   if (DirectoryExists(trustedPath))
     keyrings = GetListOfFilesInDir(trustedPath, "gpg", false, true);
   if (RealFileExists(trustedFile) == true)
     keyrings.push_back(trustedFile);

   std::vector<const char *> Args;
   Args.reserve(30);

   if (keyrings.empty() == true)
   {
      // TRANSLATOR: %s is the trusted keyring parts directory
      return _error->Error(_("No keyring installed in %s."),
			   _config->FindDir("Dir::Etc::TrustedParts").c_str());
   }

   Args.push_back(gpgvpath.c_str());
   Args.push_back("--ignore-time-conflict");

   if (statusfd != -1)
   {
      Args.push_back("--status-fd");
      char fd[10];
      snprintf(fd, sizeof(fd), "%i", statusfd);
      Args.push_back(fd);
   }

   for (vector<string>::const_iterator K = keyrings.begin();
	K != keyrings.end(); ++K)
   {
      Args.push_back("--keyring");
      Args.push_back(K->c_str());
   }

   Configuration::Item const *Opts;
   Opts = _config->Tree("Acquire::gpgv::Options");
   if (Opts != 0)
   {
      Opts = Opts->Child;
      for (; Opts != 0; Opts = Opts->Next)
      {
	 if (Opts->Value.empty() == true)
	    continue;
	 Args.push_back(Opts->Value.c_str());
      }
   }

   Args.push_back(FileGPG.c_str());
   if (FileGPG != File)
      Args.push_back(File.c_str());
   Args.push_back(NULL);

   if (Debug == true)
   {
      std::clog << "Preparing to exec: " << gpgvpath;
      for (std::vector<const char *>::const_iterator a = Args.begin(); *a != NULL; ++a)
	 std::clog << " " << *a;
      std::clog << std::endl;
   }

   if (statusfd != -1)
   {
      int const nullfd = open("/dev/null", O_RDONLY);
      close(fd[0]);
      // Redirect output to /dev/null; we read from the status fd
      dup2(nullfd, STDOUT_FILENO);
      dup2(nullfd, STDERR_FILENO);
      // Redirect the pipe to the status fd (3)
      dup2(fd[1], statusfd);

      putenv((char *)"LANG=");
      putenv((char *)"LC_ALL=");
      putenv((char *)"LC_MESSAGES=");
   }

   execvp(gpgvpath.c_str(), (char **) &Args[0]);
   return true;
}
									/*}}}*/
// SplitClearSignedFile - split message into data/signature		/*{{{*/
bool SplitClearSignedFile(std::string const &InFile, int const ContentFile,
      std::vector<std::string> * const ContentHeader, int const SignatureFile)
{
   FILE *in = fopen(InFile.c_str(), "r");
   if (in == NULL)
      return _error->Errno("fopen", "can not open %s", InFile.c_str());

   FILE *out_content = NULL;
   FILE *out_signature = NULL;
   if (ContentFile != -1)
   {
      out_content = fdopen(ContentFile, "w");
      if (out_content == NULL)
      {
	 fclose(in);
	 return _error->Errno("fdopen", "Failed to open file to write content to from %s", InFile.c_str());
      }
   }
   if (SignatureFile != -1)
   {
      out_signature = fdopen(SignatureFile, "w");
      if (out_signature == NULL)
      {
	 fclose(in);
	 if (out_content != NULL)
	    fclose(out_content);
	 return _error->Errno("fdopen", "Failed to open file to write signature to from %s", InFile.c_str());
      }
   }

   bool found_message_start = false;
   bool found_message_end = false;
   bool skip_until_empty_line = false;
   bool found_signature = false;
   bool first_line = true;

   char *buf = NULL;
   size_t buf_size = 0;
   while (getline(&buf, &buf_size, in) != -1)
   {
      _strrstrip(buf);
      if (found_message_start == false)
      {
	 if (strcmp(buf, "-----BEGIN PGP SIGNED MESSAGE-----") == 0)
	 {
	    found_message_start = true;
	    skip_until_empty_line = true;
	 }
      }
      else if (skip_until_empty_line == true)
      {
	 if (strlen(buf) == 0)
	    skip_until_empty_line = false;
	 // save "Hash" Armor Headers, others aren't allowed
	 else if (ContentHeader != NULL && strncmp(buf, "Hash: ", strlen("Hash: ")) == 0)
	    ContentHeader->push_back(buf);
      }
      else if (found_signature == false)
      {
	 if (strcmp(buf, "-----BEGIN PGP SIGNATURE-----") == 0)
	 {
	    found_signature = true;
	    found_message_end = true;
	    if (out_signature != NULL)
	       fprintf(out_signature, "%s\n", buf);
	 }
	 else if (found_message_end == false)
	 {
	    // we are in the message block
	    if(first_line == true) // first line does not need a newline
	    {
	       if (out_content != NULL)
		  fprintf(out_content, "%s", buf);
	       first_line = false;
	    }
	    else if (out_content != NULL)
	       fprintf(out_content, "\n%s", buf);
	 }
      }
      else if (found_signature == true)
      {
	 if (out_signature != NULL)
	    fprintf(out_signature, "%s\n", buf);
	 if (strcmp(buf, "-----END PGP SIGNATURE-----") == 0)
	    found_signature = false; // look for other signatures
      }
      // all the rest is whitespace, unsigned garbage or additional message blocks we ignore
   }
   if (out_content != NULL)
      fclose(out_content);
   if (out_signature != NULL)
      fclose(out_signature);
   fclose(in);

   if (found_signature == true)
      return _error->Error("Signature in file %s wasn't closed", InFile.c_str());

   // if we haven't found any of them, this an unsigned file,
   // so don't generate an error, but splitting was unsuccessful none-the-less
   if (found_message_start == false && found_message_end == false)
      return false;
   // otherwise one missing indicates a syntax error
   else if (found_message_start == false || found_message_end == false)
      return _error->Error("Splitting of file %s failed as it doesn't contain all expected parts", InFile.c_str());

   return true;
}
									/*}}}*/
bool OpenMaybeClearSignedFile(std::string const &ClearSignedFileName, FileFd &MessageFile) /*{{{*/
{
   char * const message = GenerateTemporaryFileTemplate("fileutl.message");
   int const messageFd = mkstemp(message);
   if (messageFd == -1)
   {
      free(message);
      return _error->Errno("mkstemp", "Couldn't create temporary file to work with %s", ClearSignedFileName.c_str());
   }
   // we have the fd, thats enough for us
   unlink(message);
   free(message);

   int const duppedMsg = dup(messageFd);
   if (duppedMsg == -1)
      return _error->Errno("dup", "Couldn't duplicate FD to work with %s", ClearSignedFileName.c_str());

   _error->PushToStack();
   bool const splitDone = SplitClearSignedFile(ClearSignedFileName.c_str(), messageFd, NULL, -1);
   bool const errorDone = _error->PendingError();
   _error->MergeWithStack();
   if (splitDone == false)
   {
      close(duppedMsg);

      if (errorDone == true)
	 return false;

      // we deal with an unsigned file
      MessageFile.Open(ClearSignedFileName, FileFd::ReadOnly);
   }
   else // clear-signed
   {
      if (lseek(duppedMsg, 0, SEEK_SET) < 0)
	 return _error->Errno("lseek", "Unable to seek back in message fd for file %s", ClearSignedFileName.c_str());
      MessageFile.OpenDescriptor(duppedMsg, FileFd::ReadOnly, true);
   }

   return MessageFile.Failed() == false;
}
									/*}}}*/

