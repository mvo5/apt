// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
/* ######################################################################

   HTTP and HTTPS share a lot of common code and these classes are
   exactly the dumping ground for this common code

   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#include <config.h>

#include <apt-pkg/acquire-method.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/error.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/strutl.h>

#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "server.h"
#include "http_header.h"

#include <apti18n.h>
									/*}}}*/
using namespace std;

string ServerMethod::FailFile;
int ServerMethod::FailFd = -1;
time_t ServerMethod::FailTime = 0;

// ServerState::RunHeaders - Get the headers before the data		/*{{{*/
// ---------------------------------------------------------------------
/* Returns 0 if things are OK, 1 if an IO error occurred and 2 if a header
   parse error occurred */
ServerState::RunHeadersResult ServerState::RunHeaders(FileFd * const File)
{
   State = Header;
   
   Owner->Status(_("Waiting for headers"));

   Major = 0; 
   Minor = 0; 
   Result = 0; 
   Size = 0; 
   StartPos = 0;
   Encoding = Closes;
   HaveContent = false;
   time(&Date);

   do
   {
      string Data;
      if (ReadHeaderLines(Data) == false)
	 continue;

      if (Owner->Debug == true)
	 clog << Data;
      
      for (string::const_iterator I = Data.begin(); I < Data.end(); ++I)
      {
	 string::const_iterator J = I;
	 for (; J != Data.end() && *J != '\n' && *J != '\r'; ++J);
	 if (HeaderLine(string(I,J)) == false)
	    return RUN_HEADERS_PARSE_ERROR;
	 I = J;
      }

      // 100 Continue is a Nop...
      if (Result == 100)
	 continue;
      
      // Tidy up the connection persistence state.
      if (Encoding == Closes && HaveContent == true)
	 Persistent = false;
      
      return RUN_HEADERS_OK;
   }
   while (LoadNextResponse(false, File) == true);
   
   return RUN_HEADERS_IO_ERROR;
}
									/*}}}*/
// ServerState::HeaderLine - Process a header line			/*{{{*/
// ---------------------------------------------------------------------
/* */
bool ServerState::HeaderLine(string Line)
{
   if (Line.empty() == true)
      return true;

   if (stringcasecmp(Line.c_str(),Line.c_str()+4,"HTTP") == 0)
   {
      // Evil servers return no version
      if (Line[4] == '/')
      {
	 int const elements = sscanf(Line.c_str(),"HTTP/%3u.%3u %3u%359[^\n]",&Major,&Minor,&Result,Code);
	 if (elements == 3)
	 {
	    Code[0] = '\0';
	    if (Owner->Debug == true)
	       clog << "HTTP server doesn't give Reason-Phrase for " << Result << std::endl;
	 }
	 else if (elements != 4)
	    return _error->Error(_("The HTTP server sent an invalid reply header"));
      }
      else
      {
	 Major = 0;
	 Minor = 9;
	 if (sscanf(Line.c_str(),"HTTP %3u%359[^\n]",&Result,Code) != 2)
	    return _error->Error(_("The HTTP server sent an invalid reply header"));
      }

      /* Check the HTTP response header to get the default persistence
         state. */
      if (Major < 1)
	 Persistent = false;
      else
      {
	 if (Major == 1 && Minor == 0)
	    Persistent = false;
	 else
	    Persistent = true;
      }

      return true;
   }

   HttpHeader Header = HttpHeader(Line);
   if (Header.Empty())
      return _error->Error(_("Bad header line"));

   string Tag = Header.Name();
   string Val = Header.Value();

   if (stringcasecmp(Tag,"Content-Length") == 0)
   {
      if (Encoding == Closes)
	 Encoding = Stream;
      HaveContent = true;

      // The length is already set from the Content-Range header
      if (StartPos != 0)
	 return true;

      Size = strtoull(Val.c_str(), NULL, 10);
      if (Size >= std::numeric_limits<unsigned long long>::max())
	 return _error->Errno("HeaderLine", _("The HTTP server sent an invalid Content-Length header"));
      else if (Size == 0)
	 HaveContent = false;
      return true;
   }

   if (stringcasecmp(Tag,"Content-Type") == 0)
   {
      HaveContent = true;
      return true;
   }

   if (stringcasecmp(Tag,"Content-Range") == 0)
   {
      HaveContent = true;

      // §14.16 says 'byte-range-resp-spec' should be a '*' in case of 416
      if (Result == 416 && sscanf(Val.c_str(), "bytes */%llu",&Size) == 1)
      {
	 StartPos = 1; // ignore Content-Length, it would override Size
	 HaveContent = false;
      }
      else if (sscanf(Val.c_str(),"bytes %llu-%*u/%llu",&StartPos,&Size) != 2)
	 return _error->Error(_("The HTTP server sent an invalid Content-Range header"));
      if ((unsigned long long)StartPos > Size)
	 return _error->Error(_("This HTTP server has broken range support"));
      return true;
   }

   if (stringcasecmp(Tag,"Transfer-Encoding") == 0)
   {
      HaveContent = true;
      if (stringcasecmp(Val,"chunked") == 0)
	 Encoding = Chunked;
      return true;
   }

   if (stringcasecmp(Tag,"Connection") == 0)
   {
      if (stringcasecmp(Val,"close") == 0)
	 Persistent = false;
      if (stringcasecmp(Val,"keep-alive") == 0)
	 Persistent = true;
      return true;
   }

   if (stringcasecmp(Tag,"Last-Modified") == 0)
   {
      if (RFC1123StrToTime(Val.c_str(), Date) == false)
	 return _error->Error(_("Unknown date format"));
      return true;
   }

   if (stringcasecmp(Tag,"Location") == 0)
   {
      Location = Val;
      return true;
   }

   unsigned long MaxRewriters = _config->FindI("Acquire::http::MaxRewriters", 4);
   if (stringcasecmp(Tag,"Link") == 0 && MaxRewriters != 0)
   {
      // yay for this being the only header that APT cares about which
      // may be present more than once:
      vector<HttpHeader> LinkVec = Header.Split();
      unsigned long PreviousDepth = 0;
      for (size_t i = 0; i < LinkVec.size(); i++) 
      {
         HttpLink6249Header Link6249 = HttpLink6249Header(LinkVec[i]);

         // ignore errors and headers we don't care about
         if (Link6249.Empty())
            continue;

         // for now we are only interested in those with depth > 0
         // and only add new rewriters if the depth is greater than the
         // previous rewriter
         if (Link6249.Depth() == 0 || Link6249.Depth() < PreviousDepth)
            continue;

         HttpLinkHeader Link(LinkVec[i]);
         URI LinkUri = URI(Link.GetURI());
         // avoid same-host loops and protocol switches:
         if (LinkUri.Access != "http" || LinkUri.Host == ServerName.Host)
            continue;

         // Require a valid rewrite
         if (Link6249.DepthPath().length() == 0)
            continue;

         if (RFC6249Rewriters.size() > MaxRewriters)
            RFC6249Rewriters.pop_front();

         Link6249.SetOrigURI(ServerName);
         RFC6249Rewriters.push_back(Link6249);
         PreviousDepth = Link6249.Depth();
      }
      return true;
   }

   return true;
}
									/*}}}*/
// ServerState::ServerState - Constructor				/*{{{*/
ServerState::ServerState(URI Srv, ServerMethod *Owner) : ServerName(Srv), TimeOut(120), Owner(Owner)
{
   Reset();
}
									/*}}}*/
// ServerState::Reset - Reset the state				/*{{{*/
void ServerState::Reset()
{
   Major = 0;
   Minor = 0;
   Result = 0;
   Code[0] = '\0';
   Size = 0;
   StartPos = 0;
   Encoding = Closes;
   time(&Date);
   HaveContent = false;
   State = Header;
   Persistent = false;
   Pipeline = true;
   RFC6249Rewriters.clear();
};

bool ServerMethod::Configuration(string Message)			/*{{{*/
{
   return pkgAcqMethod::Configuration(Message);
}
									/*}}}*/

// ServerMethod::DealWithHeaders - Handle the retrieved header data	/*{{{*/
// ---------------------------------------------------------------------
/* We look at the header data we got back from the server and decide what
   to do. Returns DealWithHeadersResult (see http.h for details).
 */
ServerMethod::DealWithHeadersResult
ServerMethod::DealWithHeaders(FetchResult &Res)
{
   // Not Modified
   if (Server->Result == 304)
   {
      unlink(Queue->DestFile.c_str());
      Res.IMSHit = true;
      Res.LastModified = Queue->LastModified;
      return IMS_HIT;
   }
   
   /* Redirect
    *
    * Note that it is only OK for us to treat all redirection the same
    * because we *always* use GET, not other HTTP methods.  There are
    * three redirection codes for which it is not appropriate that we
    * redirect.  Pass on those codes so the error handling kicks in.
    */
   if (AllowRedirect
       && (Server->Result > 300 && Server->Result < 400)
       && (Server->Result != 300       // Multiple Choices
           && Server->Result != 304    // Not Modified
           && Server->Result != 306))  // (Not part of HTTP/1.1, reserved)
   {
      if (Server->Location.empty() == true);
      else if (Server->Location[0] == '/' && Queue->Uri.empty() == false)
      {
	 URI Uri = Queue->Uri;
	 if (Uri.Host.empty() == false)
            NextURI = URI::SiteOnly(Uri);
	 else
	    NextURI.clear();
	 NextURI.append(DeQuoteString(Server->Location));
	 return TRY_AGAIN_OR_REDIRECT;
      }
      else
      {
	 NextURI = DeQuoteString(Server->Location);
	 URI tmpURI = NextURI;
	 URI Uri = Queue->Uri;
	 // same protocol redirects are okay
	 if (tmpURI.Access == Uri.Access)
	    return TRY_AGAIN_OR_REDIRECT;
	 // as well as http to https
	 else if (Uri.Access == "http" && tmpURI.Access == "https")
	    return TRY_AGAIN_OR_REDIRECT;
      }
      /* else pass through for error message */
   }
   // retry after an invalid range response without partial data
   else if (Server->Result == 416)
   {
      struct stat SBuf;
      if (stat(Queue->DestFile.c_str(),&SBuf) >= 0 && SBuf.st_size > 0)
      {
	 if ((unsigned long long)SBuf.st_size == Server->Size)
	 {
	    // the file is completely downloaded, but was not moved
	    Server->StartPos = Server->Size;
	    Server->Result = 200;
	    Server->HaveContent = false;
	 }
	 else if (unlink(Queue->DestFile.c_str()) == 0)
	 {
	    NextURI = Queue->Uri;
	    return TRY_AGAIN_OR_REDIRECT;
	 }
      }
   }

   /* We have a reply we dont handle. This should indicate a perm server
      failure */
   if (Server->Result < 200 || Server->Result >= 300)
   {
      char err[255];
      snprintf(err,sizeof(err)-1,"HttpError%i",Server->Result);
      SetFailReason(err);
      _error->Error("%u %s",Server->Result,Server->Code);
      if (Server->HaveContent == true)
	 return ERROR_WITH_CONTENT_PAGE;
      return ERROR_UNRECOVERABLE;
   }

   // This is some sort of 2xx 'data follows' reply
   Res.LastModified = Server->Date;
   Res.Size = Server->Size;
   
   // Open the file
   delete File;
   File = new FileFd(Queue->DestFile,FileFd::WriteAny);
   if (_error->PendingError() == true)
      return ERROR_NOT_FROM_SERVER;

   FailFile = Queue->DestFile;
   FailFile.c_str();   // Make sure we dont do a malloc in the signal handler
   FailFd = File->Fd();
   FailTime = Server->Date;

   if (Server->InitHashes(*File) == false)
   {
      _error->Errno("read",_("Problem hashing file"));
      return ERROR_NOT_FROM_SERVER;
   }
   if (Server->StartPos > 0)
      Res.ResumePoint = Server->StartPos;

   SetNonBlock(File->Fd(),true);
   return FILE_IS_OPEN;
}
									/*}}}*/
// ServerMethod::SigTerm - Handle a fatal signal			/*{{{*/
// ---------------------------------------------------------------------
/* This closes and timestamps the open file. This is necessary to get
   resume behavoir on user abort */
void ServerMethod::SigTerm(int)
{
   if (FailFd == -1)
      _exit(100);

   struct timeval times[2];
   times[0].tv_sec = FailTime;
   times[1].tv_sec = FailTime;
   times[0].tv_usec = times[1].tv_usec = 0;
   utimes(FailFile.c_str(), times);
   close(FailFd);

   _exit(100);
}
									/*}}}*/
// ServerMethod::Fetch - Fetch an item					/*{{{*/
// ---------------------------------------------------------------------
/* This adds an item to the pipeline. We keep the pipeline at a fixed
   depth. */
bool ServerMethod::Fetch(FetchItem *)
{
   if (Server == 0)
      return true;

   // Queue the requests
   int Depth = -1;
   for (FetchItem *I = Queue; I != 0 && Depth < (signed)PipelineDepth; 
	Depth++)
   {
      // If pipelining is disabled, we only queue 1 request
      if (Server->Pipeline == false && Depth >= 0)
	 break;
      
      // Make sure we stick with the same server
      if (Server->Comp(I->Uri) == false)
	 break;
      if (QueueBack == I)
      {
	 QueueBack = I->Next;
         if (PipelineDepth == 0 && Server->RFC6249Rewriters.size())
         {
            bool Rewritten = false;
            std::list<HttpLink6249Header>::iterator LIt;
            for (LIt = Server->RFC6249Rewriters.begin();
                 LIt != Server->RFC6249Rewriters.end(); ++LIt)
            {
               URI NewURI = LIt->Rewrite(I->Uri);
               
               if (NewURI.empty())
                  continue;
               
               // Point to the next item before redirecting, as the
               // current item will be destroyed:
               I = I->Next;
               Redirect(NewURI);
               // Revert the effect of the increase in Depth at the end
               // of the for loop. The pipeline limit can be applied by
               // the method handling the final request:
               --Depth;
               Rewritten = true;
               break;
            }
            if (Rewritten)
               continue;
         }
	 SendReq(I);
         I = I->Next;
	 continue;
      }
   }
   
   return true;
}
									/*}}}*/
// ServerMethod::Loop - Main loop					/*{{{*/
int ServerMethod::Loop()
{
   typedef vector<string> StringVector;
   typedef vector<string>::iterator StringVectorIterator;
   map<string, StringVector> Redirected;

   signal(SIGTERM,SigTerm);
   signal(SIGINT,SigTerm);
   
   Server = 0;
   
   int FailCounter = 0;
   while (1)
   {      
      // We have no commands, wait for some to arrive
      if (Queue == 0)
      {
	 if (WaitFd(STDIN_FILENO) == false)
	    return 0;
      }
      
      /* Run messages, we can accept 0 (no message) if we didn't
         do a WaitFd above.. Otherwise the FD is closed. */
      int Result = Run(true);
      if (Result != -1 && (Result != 0 || Queue == 0))
      {
	 if(FailReason.empty() == false ||
	    _config->FindB("Acquire::http::DependOnSTDIN", true) == true)
	    return 100;
	 else
	    return 0;
      }

      if (Queue == 0)
	 continue;
      
      // Connect to the server
      if (Server == 0 || Server->Comp(Queue->Uri) == false)
      {
	 delete Server;
	 Server = CreateServerState(Queue->Uri);
      }
      /* If the server has explicitly said this is the last connection
         then we pre-emptively shut down the pipeline and tear down 
	 the connection. This will speed up HTTP/1.0 servers a tad
	 since we don't have to wait for the close sequence to
         complete */
      if (Server->Persistent == false)
	 Server->Close();

      // Reset the pipeline
      if (Server->IsOpen() == false)
	 QueueBack = Queue;

      // Connnect to the host
      if (Server->Open() == false)
      {
	 Fail(true);
	 delete Server;
	 Server = 0;
	 continue;
      }

      // Fill the pipeline.
      Fetch(0);
      
      if (Queue == 0)
         continue;

      // Fetch the next URL header data from the server.
      switch (Server->RunHeaders(File))
      {
	 case ServerState::RUN_HEADERS_OK:
	 break;
	 
	 // The header data is bad
	 case ServerState::RUN_HEADERS_PARSE_ERROR:
	 {
	    _error->Error(_("Bad header data"));
	    Fail(true);
	    RotateDNS();
	    continue;
	 }
	 
	 // The server closed a connection during the header get..
	 default:
	 case ServerState::RUN_HEADERS_IO_ERROR:
	 {
	    FailCounter++;
	    _error->Discard();
	    Server->Close();
	    Server->Pipeline = false;
	    
	    if (FailCounter >= 2)
	    {
	       Fail(_("Connection failed"),true);
	       FailCounter = 0;
	    }
	    
	    RotateDNS();
	    continue;
	 }
      };

      // Decide what to do.
      FetchResult Res;
      Res.Filename = Queue->DestFile;
      switch (DealWithHeaders(Res))
      {
	 // Ok, the file is Open
	 case FILE_IS_OPEN:
	 {
	    URIStart(Res);

	    // Run the data
	    bool Result = true;
            if (Server->HaveContent)
	       Result = Server->RunData(File);

	    /* If the server is sending back sizeless responses then fill in
	       the size now */
	    if (Res.Size == 0)
	       Res.Size = File->Size();
	    
	    // Close the file, destroy the FD object and timestamp it
	    FailFd = -1;
	    delete File;
	    File = 0;
	    
	    // Timestamp
	    struct timeval times[2];
	    times[0].tv_sec = times[1].tv_sec = Server->Date;
	    times[0].tv_usec = times[1].tv_usec = 0;
	    utimes(Queue->DestFile.c_str(), times);

	    // Send status to APT
	    if (Result == true)
	    {
	       Res.TakeHashes(*Server->GetHashes());
	       URIDone(Res);
	    }
	    else
	    {
	       if (Server->IsOpen() == false)
	       {
		  FailCounter++;
		  _error->Discard();
		  Server->Close();
		  
		  if (FailCounter >= 2)
		  {
		     Fail(_("Connection failed"),true);
		     FailCounter = 0;
		  }
		  
		  QueueBack = Queue;
	       }
	       else
		  Fail(true);
	    }
	    break;
	 }
	 
	 // IMS hit
	 case IMS_HIT:
	 {
	    URIDone(Res);
	    break;
	 }
	 
	 // Hard server error, not found or something
	 case ERROR_UNRECOVERABLE:
	 {
	    Fail();
	    break;
	 }
	  
	 // Hard internal error, kill the connection and fail
	 case ERROR_NOT_FROM_SERVER:
	 {
	    delete File;
	    File = 0;

	    Fail();
	    RotateDNS();
	    Server->Close();
	    break;
	 }

	 // We need to flush the data, the header is like a 404 w/ error text
	 case ERROR_WITH_CONTENT_PAGE:
	 {
	    Fail();
	    
	    // Send to content to dev/null
	    File = new FileFd("/dev/null",FileFd::WriteExists);
	    Server->RunData(File);
	    delete File;
	    File = 0;
	    break;
	 }
	 
         // Try again with a new URL
         case TRY_AGAIN_OR_REDIRECT:
         {
            // Clear rest of response if there is content
            if (Server->HaveContent)
            {
               File = new FileFd("/dev/null",FileFd::WriteExists);
               Server->RunData(File);
               delete File;
               File = 0;
            }

            /* Detect redirect loops.  No more redirects are allowed
               after the same URI is seen twice in a queue item. */
            StringVector &R = Redirected[Queue->DestFile];
            bool StopRedirects = false;
            if (R.empty() == true)
               R.push_back(Queue->Uri);
            else if (R[0] == "STOP" || R.size() > 10)
               StopRedirects = true;
            else
            {
               for (StringVectorIterator I = R.begin(); I != R.end(); ++I)
                  if (Queue->Uri == *I)
                  {
                     R[0] = "STOP";
                     break;
                  }
 
               R.push_back(Queue->Uri);
            }
 
            if (StopRedirects == false)
               Redirect(NextURI);
            else
               Fail();
 
            break;
         }

	 default:
	 Fail(_("Internal error"));
	 break;
      }
      
      FailCounter = 0;
   }
   
   return 0;
}
									/*}}}*/
