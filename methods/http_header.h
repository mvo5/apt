
#ifndef APT_HTTP_HEADER_H
#define APT_HTTP_HEADER_H

#include <apt-pkg/strutl.h>

#include <vector>
#include <string>
#include <map>

class HttpHeader {
    std::string HeaderName;
    std::string HeaderValue;

    public:
    HttpHeader();
    HttpHeader(const std::string &Header);
    HttpHeader(const std::string &HeaderName, const std::string &HeaderValue);

    bool Empty();
    std::string Name();
    std::string Value();
    std::vector<HttpHeader> Split();
};

class HttpLinkHeader {
    HttpHeader Header;
    std::string URIRef;
    std::map<std::string, std::string> Params;

    public:
    HttpLinkHeader();
    HttpLinkHeader(HttpHeader NewHeader);

    bool Empty();
    std::string GetURI();
    bool Has(std::string Key);
    std::string Param(std::string Key);
    size_t ParamsCount();
};

class HttpLink6249Header {
    HttpLinkHeader Link;
    URI OrigUri;

    public:
    HttpLink6249Header();
    HttpLink6249Header(HttpLinkHeader NewLink);

    bool Empty();
    unsigned long Depth();
    std::string DepthPath();
    std::string PathAtDepth(URI UriRef);
    void SetOrigURI(URI NewOrigUri);
    URI Rewrite(URI RequestedUri);
};

#endif
