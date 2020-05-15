/*
 * Copyright (c) CERN 2013-2015
 *
 * Copyright (c) Members of the EMI Collaboration. 2010-2013
 *  See  http://www.eu-emi.eu/partners for details on the copyright
 *  holders.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once
#ifndef CDMIQOSTRANSITIONCONTEXT_H_
#define CDMIQOSTRANSITIONCONTEXT_H_


#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "cred/DelegCred.h"

#include "JobContext.h"
#include "../QoSServer.h"


class CDMIQosTransitionContext
{

public:

    CDMIQosTransitionContext(QoSServer &qosServer):
            stateUpdater(qosServer.getStagingStateUpdater()),waitingRoom(qosServer.getCDMIWaitingRoom())
    {
        startTime = time(0);
    }

    CDMIQosTransitionContext(const CDMIQosTransitionContext &copy) :
        waitingRoom(copy.waitingRoom), stateUpdater(copy.stateUpdater), errorCount(copy.errorCount), startTime(copy.startTime)
    {}

    CDMIQosTransitionContext(CDMIQosTransitionContext && copy) :
        waitingRoom(copy.waitingRoom), stateUpdater(copy.stateUpdater), errorCount(std::move(copy.errorCount)), startTime(copy.startTime)
    {}

    virtual ~CDMIQosTransitionContext() {}

    void add(const QosTransitionOperation &qosTransitionOp);

    WaitingRoom<CDMIPollTask>& getWaitingRoom() {
        return waitingRoom;
    }

    int incrementErrorCountForSurl(const std::string &surl) {
        return (errorCount[surl] += 1);
    }

    /**
     * Synchronous update of the state of a QoS transition operation
     */
    void cdmiUpdateFileStateToFinished(const std::string &jobId, uint64_t fileId) const
    {
        stateUpdater.cdmiUpdateFileStateToFinished(jobId, fileId);
    }

    void cdmiUpdateFileStateToFailed(const std::string &jobId, uint64_t fileId) const
    {
        stateUpdater.cdmiUpdateFileStateToFailed(jobId, fileId);
    }

    void cdmiGetFilesForQosRequestSubmitted(std::vector<QosTransitionOperation> &qosTranstionOps, const std::string& qosOp) const
    {
        stateUpdater.cdmiGetFilesForQosRequestSubmitted(qosTranstionOps, qosOp);
    }

    void cdmiUpdateFileStateToQosRequestSubmitted(const std::string &jobId, uint64_t fileId) const
    {
        stateUpdater.cdmiUpdateFileStateToQosRequestSubmitted(jobId, fileId);
    }

    /**
     * @return  Set of tuples <surl, token, target_qos>
     */
    std::set<std::tuple<std::string, std::string, std::string, std::string, uint64_t>> getSurls() const
    {
    	std::set<std::tuple<std::string, std::string, std::string, std::string, uint64_t>> surls;
    	//std::cerr << "This is the map size: " << filesToTransition.size() << std::endl;
        for (auto it_j = filesToTransition.begin(); it_j != filesToTransition.end(); ++it_j)
        	for (auto it_u = it_j->second.begin(); it_u != it_j->second.end(); ++it_u) {
        		//std::cerr << "About to create the tuple " << it_u->surl << it_u->token << it_u->target_qos << std::endl;
        		surls.insert(std::make_tuple(it_u->surl, it_u->token, it_u->target_qos, it_u->jobId, it_u->fileId));
        	}
        return surls;
    }

private:
    StagingStateUpdater &stateUpdater;
    /// Job ID -> surl, target_qos, token
    std::map< std::string, std::vector<QosTransitionOperation>> filesToTransition;
    WaitingRoom<CDMIPollTask> &waitingRoom;
    std::map<std::string, int> errorCount;
    time_t startTime;
};

#endif // CDMIQOSTRANSITIONCONTEXT_H_
