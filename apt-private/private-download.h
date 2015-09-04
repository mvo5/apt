#ifndef APT_PRIVATE_DOWNLOAD_H
#define APT_PRIVATE_DOWNLOAD_H

#include <apt-pkg/macros.h>

#include <string>
#include <vector>

class pkgAcquire;

// Check if all files in the fetcher are authenticated
APT_PUBLIC bool CheckAuth(pkgAcquire& Fetcher, bool const PromptUser);

// show a authentication warning prompt and return true if the system
// should continue
APT_PUBLIC bool AuthPrompt(std::vector<std::string> const &UntrustedList, bool const PromptUser);

APT_PUBLIC bool AcquireRun(pkgAcquire &Fetcher, int const PulseInterval, bool * const Failure, bool * const TransientNetworkFailure);

APT_PUBLIC bool CheckFreeSpaceBeforeDownload(std::string const &Dir, unsigned long long FetchBytes);

#endif
