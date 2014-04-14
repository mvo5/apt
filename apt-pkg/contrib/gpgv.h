// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
/* ######################################################################

   Helpers to deal with gpgv better and more easily

   ##################################################################### */
									/*}}}*/
#ifndef CONTRIB_GPGV_H
#define CONTRIB_GPGV_H

#include <string>

#include <apt-pkg/fileutl.h>

#if __GNUC__ >= 4
	#define APT_noreturn	__attribute__ ((noreturn))
#else
	#define APT_noreturn	/* no support */
#endif

/** \brief generates and run the command to verify a file with gpgv */
bool ExecGPGV(std::string const &File, std::string const &FileOut,
      int const &statusfd, int fd[2]);

inline bool ExecGPGV(std::string const &File, std::string const &FileOut,
      int const &statusfd = -1) {
   int fd[2];
   return ExecGPGV(File, FileOut, statusfd, fd);
}

#undef APT_noreturn

/** \brief Split an inline signature into message and signature
 *
 *  Takes a clear-signed message and puts the first signed message
 *  in the content file and all signatures following it into the
 *  second. Unsigned messages, additional messages as well as
 *  whitespaces are discarded. The resulting files are suitable to
 *  be checked with gpgv.
 *
 *  If one or all Fds are -1 they will not be used and the content
 *  which would have been written to them is discarded.
 *
 *  The code doesn't support dash-encoded lines as these are not
 *  expected to be present in files we have to deal with.
 *
 *  The content of the split files is undefined if the splitting was
 *  unsuccessful.
 *
 *  Note that trying to split an unsigned file will fail, but
 *  not generate an error message.
 *
 *  @param InFile is the clear-signed file
 *  @param ContentFile is the Fd the message will be written to
 *  @param ContentHeader is a list of all required Amored Headers for the message
 *  @param SignatureFile is the Fd all signatures will be written to
 *  @return true if the splitting was successful, false otherwise
 */
bool SplitClearSignedFile(std::string const &InFile, int const ContentFile,
      std::vector<std::string> * const ContentHeader, int const SignatureFile);

/** \brief recombines message and signature to an inline signature
 *
 *  Reverses the splitting down by #SplitClearSignedFile by writing
 *  a well-formed clear-signed message without unsigned messages,
 *  additional signed messages or just trailing whitespaces
 *
 *  @param OutFile will be clear-signed file
 *  @param ContentFile is the Fd the message will be read from
 *  @param ContentHeader is a list of all required Amored Headers for the message
 *  @param SignatureFile is the Fd all signatures will be read from
 */
bool RecombineToClearSignedFile(std::string const &OutFile, int const ContentFile,
      std::vector<std::string> const &ContentHeader, int const SignatureFile);

/** \brief open a file which might be clear-signed
 *
 * This method tries to extract the (signed) message of a file.
 * If the file isn't signed it will just open the given filename.
 * Otherwise the message is extracted to a temporary file which
 * will be opened instead.
 *
 * @param ClearSignedFileName is the name of the file to open
 * @param[out] MessageFile is the FileFd in which the file will be opened
 * @return true if opening was successful, otherwise false
 */
bool OpenMaybeClearSignedFile(std::string const &ClearSignedFileName, FileFd &MessageFile);

#endif
