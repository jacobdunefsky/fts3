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
 * FtsDrain.h
 *
 *  Created on: Aug 17, 2012
 *      Author: Michał Simon
 */

#ifndef FTSDRAIN_H_
#define FTSDRAIN_H_

#include "common/ThreadSafeInstanceHolder.h"
#include "SingleDbInstance.h"

#include <sys/sysinfo.h>

using namespace db;

namespace fts3
{
namespace common
{

static const unsigned long REQUIRED_FREE_RAM = 150;


/**
 * The DrainMode class is a thread safe singleton
 * that provides access to an boolean flag. The flag can be
 * set using assignment operator. The (bool)operator has
 * been overloaded so instance of DrainMode can be used
 * in an 'if' statement. Setting and reading the flag is
 * not synchronized since its an atomic value and theres no
 * risk of run condition.
 */
class DrainMode : public ThreadSafeInstanceHolder<DrainMode>
{

    friend class ThreadSafeInstanceHolder<DrainMode>;

    unsigned long getFreeRamInMb(void) const
    {
        struct sysinfo info;
        sysinfo(&info);
        return info.freeram / (1024 * 1024);
    }

public:

    /**
     * Assign operator for converting boolean value into FtsDrain
     *
     * @param drain - the value tht has to be converted
     *
     * @return reference to this
     */
    DrainMode& operator= (const bool drain)
    {
        DBSingleton::instance().getDBObjectInstance()->setDrain(drain);
        return *this;
    }

    /**
     * boolean casting
     * 	casts the FtsDrain instance to bool value
     *
     * 	@return true if drain mode is on, otherwise false
     */
    operator bool() const
    {
        unsigned long availableRam = getFreeRamInMb();
        bool drain = DBSingleton::instance().getDBObjectInstance()->getDrain();

        if (availableRam < REQUIRED_FREE_RAM) {
            FTS3_COMMON_LOGGER_NEWLOG(CRIT)
                << "Auto-drain mode: available RAM is not enough ("
                << availableRam << " < " <<  REQUIRED_FREE_RAM
                << ");" << commit;
            return true;
        }
        return drain;
    }

    /**
     * Destructor
     */
    virtual ~DrainMode() {};

private:

    /**
     * Default constructor
     *
     * Private, should not be used
     */
    DrainMode() {} ;

    /**
     * Copying constructor
     *
     * Private, should not be used
     */
    DrainMode(DrainMode const&);

    /**
     * Assignment operator
     *
     * Private, should not be used
     */
    DrainMode& operator=(DrainMode const&);

};

}
}

#endif /* FTSDRAIN_H_ */
