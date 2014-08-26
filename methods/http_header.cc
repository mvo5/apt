
/**
 *  Copyright 2012 Raphael Geissert <geissert@debian.org>
 *
 *  HttpHeader parser based on code from APT 0.9.3 which has the
 *  following copyright notice:
 *  Apt is copyright 1997, 1998, 1999 Jason Gunthorpe and others.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */


//#include <config.h>

#include <apt-pkg/strutl.h>

#include <limits>
#include <string>
#include "http_header.h"

using namespace std;

#define SKIP_SPACES(string, pos) { \
	    while (pos < string.length() && isspace(string[pos]) != 0) \
		pos++; \
	    }

HttpHeader::HttpHeader()
{
}

HttpHeader::HttpHeader(const string &HdrName, const std::string &HdrValue)
{
    HeaderName = HdrName;
    HeaderValue = HdrValue;
}

HttpHeader::HttpHeader(const string &Header)
{
    string::size_type Pos = Header.find(':');
    if (Pos == string::npos || (Pos + 2) > Header.length())
	return;

    // Parse off any trailing spaces between the : and the next word.
    string::size_type Pos2 = Pos + 1;
    SKIP_SPACES(Header, Pos2);

    HeaderName = Header.substr(0, Pos);
    HeaderValue = Header.substr(Pos2);

    // remove trailing white space
    string::size_type Pos3 = HeaderValue.length();
    while (Pos3 > 0 && isspace(HeaderValue[Pos3 - 1]))
	Pos3--;
    HeaderValue.resize(Pos3);

    return;
}

bool HttpHeader::Empty()
{
    return (HeaderName.empty() || HeaderValue.empty());
}

string HttpHeader::Name()
{
    return HeaderName;
}

string HttpHeader::Value()
{
    return HeaderValue;
}

vector<HttpHeader> HttpHeader::Split()
{
    vector<HttpHeader> HeadersVec;
    vector<string> ValuesVec;

    ValuesVec = VectorizeString(HeaderValue, ',');

    int VVecSz = ValuesVec.size();
    for (int i = 0; i < VVecSz; i++) {
	HttpHeader NewHeader = HttpHeader(HeaderName, ValuesVec[i]);
	if (!NewHeader.Empty())
	    HeadersVec.push_back(NewHeader);
    }

    return HeadersVec;
}

HttpLinkHeader::HttpLinkHeader()
{
}

HttpLinkHeader::HttpLinkHeader(HttpHeader NewHeader)
{
    if (stringcasecmp(NewHeader.Name(), "link") != 0)
	return;

    string value = NewHeader.Value();
    if (value[0] != '<')
	return;

    string::size_type Pos = value.find('>');
    if (Pos == string::npos)
	return;

    URIRef = value.substr(1, Pos-1);

    Pos = value.find(';', Pos);
    if (Pos != string::npos) {
	do {
	    Pos++;
	    SKIP_SPACES(value, Pos);
	    string::size_type Pos2 = Pos;
	    while (Pos2 < value.length() && value[Pos2] != '='
		    && value[Pos2] != ';' &&
		    isspace(value[Pos2]) == 0)
		Pos2++;

	    string ParamKey, ParamVal;
	    ParamKey = value.substr(Pos, Pos2 - Pos);

	    SKIP_SPACES(value, Pos2);

	    if (Pos2 < value.length() && value[Pos2] != ';') {
		char delimiter = ';';

		Pos2++;
		SKIP_SPACES(value, Pos2);

		if (Pos2 < value.length() && value[Pos2] == '"') {
		    delimiter = '"';
		    Pos2++;
		}
		Pos = Pos2;

		while (Pos2 < value.length() && value[Pos2] != delimiter &&
			(delimiter != ';' || isspace(value[Pos2]) == 0))
		    Pos2++;
		if (Pos2 <= value.length()) {
		    ParamVal = value.substr(Pos, Pos2 - Pos);
		    // there must always be a ; except for when we
		    // reach the end of the header
		    Pos2 = value.find(';', Pos2);
		}
	    }
	    if (!ParamKey.empty())
		Params[ParamKey] = ParamVal;
	    Pos = Pos2;
	} while (Pos < value.length());
    }

    Header = NewHeader;
}

bool HttpLinkHeader::Empty()
{
    return Header.Empty();
}

string HttpLinkHeader::GetURI()
{
    return URIRef;
}

bool HttpLinkHeader::Has(string Key)
{
    return (Params.count(Key) != 0);
}

string HttpLinkHeader::Param(string Key)
{
    return Params.find(Key)->second;
}

size_t HttpLinkHeader::ParamsCount()
{
    return Params.size();
}

HttpLink6249Header::HttpLink6249Header()
{
}

HttpLink6249Header::HttpLink6249Header(HttpLinkHeader NewLink)
{
    if (!NewLink.Has("rel") || NewLink.Param("rel") != "duplicate")
	return;

    Link = NewLink;
}

bool HttpLink6249Header::Empty()
{
    return (Link.Empty() || Link.GetURI().empty());
}

unsigned long HttpLink6249Header::Depth()
{
    if (!Link.Has("depth"))
	return 0;

    unsigned long Depth;
    Depth = strtoul(Link.Param("depth").c_str(), NULL, 10);

    // invalid depth? treat is a 0
    if (Depth >= std::numeric_limits<unsigned long>::max())
	Depth = 0;

    return Depth;
}

string HttpLink6249Header::DepthPath()
{
    return PathAtDepth(Link.GetURI());
}

string HttpLink6249Header::PathAtDepth(URI UriRef)
{
    unsigned long depth = Depth();
    string RefPath = UriRef.Path;

    string::const_reverse_iterator rit = RefPath.rbegin();
    if (*rit == '/')
	return "";

    if (depth == 0)
	return RefPath;

    unsigned long Slashes = 0;
    string::size_type SlPos = string::npos;

    // count the number of slashes in the URI ref and find the
    // start position of the stuff that we would have to ignore
    for (; Slashes < depth && SlPos != 0; Slashes++) {
	SlPos = RefPath.rfind('/', SlPos-1);
	if (SlPos == string::npos)
	    break;
    }

    // broken
    if (depth > Slashes)
	return "";

    return UriRef.Path.substr(0, SlPos+1);
}

void HttpLink6249Header::SetOrigURI(URI NewOrigUri)
{
    OrigUri = NewOrigUri;
}

URI HttpLink6249Header::Rewrite(URI NewlyRequestedUri)
{
    URI DupUri = Link.GetURI();
    // shorten its path to that of the correct depth
    DupUri.Path = DepthPath();

    if (Depth() == 0)
	return DupUri;

    // find, for the original request (on the one where we got the rel=dup)
    // its path at the specified depth
    string OrigBasePath = PathAtDepth(OrigUri);
    string::size_type OBPLength = OrigBasePath.length();

    // if they don't share the same base then it is an invalid operation.
    // Return an empty URI in that case
    if (NewlyRequestedUri.Path.compare(0, OBPLength, OrigBasePath, 0, OBPLength) != 0)
	return URI();

    // from the newly requested uri, get rid of the OrigBasePath
    // and append it to the dup uri's base path
    DupUri.Path.append(NewlyRequestedUri.Path.substr(OBPLength));

    return DupUri;
}
