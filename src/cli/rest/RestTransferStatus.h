/*
 *	Copyright notice:
 *	Copyright © Members of the EMI Collaboration, 2010.
 *
 *	See www.eu-emi.eu for details on the copyright holders
 *
 *	Licensed under the Apache License, Version 2.0 (the "License");
 *	you may not use this file except in compliance with the License.
 *	You may obtain a copy of the License at
 *
 *		http://www.apache.org/licenses/LICENSE-2.0
 *
 *	Unless required by applicable law or agreed to in writing, software
 *	distributed under the License is distributed on an "AS IS" BASIS,
 *	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *	See the License for the specific language governing permissions and
 *	limitations under the License.
 *
 * RestTransferStatus.h
 *
 *  Created on: Feb 6, 2014
 *      Author: Michal Simon
 */

#ifndef RESTTRANSFERSTATUS_H_
#define RESTTRANSFERSTATUS_H_

#include <curl/curl.h>

#include <string>
#include <sstream>
#include <iostream>

#include "MsgPrinter.h"

namespace fts3 { namespace cli {

using namespace std;

class RestTransferStatus {

public:

	RestTransferStatus(string endpoint, string jobId, MsgPrinter& printer);
	virtual ~RestTransferStatus();

	inline void print()
	{
		cout << ss.str() << endl;
	}

private:

	static size_t write_data(void *ptr, size_t size, size_t nmemb, stringstream* ss);

	void setPort(string& endpoint);

	static const string PORT;
	static const string CERTIFICATE;
	static const string CAPATH;

	// the response is written to this string stream
	stringstream ss;
    // curl context
    CURL *curl;
    // message printer
    MsgPrinter& printer;
};

} /* namespace cli  */
} /* namespace fts3 */
#endif /* RESTTRANSFERSTATUS_H_ */
