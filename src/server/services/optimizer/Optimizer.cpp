/*
 * Copyright (c) CERN 2013-2016
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

#include "config/ServerConfig.h"
#include "Optimizer.h"
#include "OptimizerConstants.h"
#include "common/Exceptions.h"
#include "common/Logger.h"

#include <time.h>

using namespace fts3::common;
using namespace fts3::config;

namespace fts3 {
namespace optimizer {


Optimizer::Optimizer(OptimizerDataSource *ds, OptimizerCallbacks *callbacks, TCNOptimizer *tcnOptimizer, time_t qosInterval, double defaultBwLimit):
    dataSource(ds), callbacks(callbacks), tcnOptimizer(tcnOptimizer),
    optimizerSteadyInterval(boost::posix_time::seconds(60)), maxNumberOfStreams(10),
    maxSuccessRate(100), lowSuccessRate(97), baseSuccessRate(96),
    decreaseStepSize(1), increaseStepSize(1), increaseAggressiveStepSize(2), defaultBwLimit(defaultBwLimit),
    emaAlpha(EMA_ALPHA), qosInterval(qosInterval), qosIntervalStart(time(NULL))
{
}


Optimizer::~Optimizer()
{
}


void Optimizer::setSteadyInterval(boost::posix_time::time_duration newValue)
{
    optimizerSteadyInterval = newValue;
}


void Optimizer::setMaxNumberOfStreams(int newValue)
{
    maxNumberOfStreams = newValue;
}


void Optimizer::setMaxSuccessRate(int newValue)
{
    maxSuccessRate = newValue;
}


void Optimizer::setLowSuccessRate(int newValue)
{
    lowSuccessRate = newValue;
}


void Optimizer::setBaseSuccessRate(int newValue)
{
    baseSuccessRate = newValue;
}


void Optimizer::setStepSize(int increase, int increaseAggressive, int decrease)
{
    increaseStepSize = increase;
    increaseAggressiveStepSize = increaseAggressive;
    decreaseStepSize = decrease;
}


void Optimizer::setEmaAlpha(double alpha)
{
    emaAlpha = alpha;
}


PairState Optimizer::getPairState(const Pair &pair, bool timeMultiplexing, bool newInterval)
{
    PairState lastState = inMemoryStore[pair];
    PairState current;
    current.timestamp = time(NULL);
    current.avgDuration = dataSource->getAverageDuration(pair, boost::posix_time::minutes(30));

    boost::posix_time::time_duration timeFrame = calculateTimeFrame(current.avgDuration);

    dataSource->getThroughputInfo(pair, timeFrame,
                                  &current.throughput, &current.filesizeAvg, &current.filesizeStdDev);
    current.successRate = dataSource->getSuccessRateForPair(pair, timeFrame, &current.retryCount);
    current.activeCount = dataSource->getActive(pair);
    current.queueSize = dataSource->getSubmitted(pair);

    if (timeMultiplexing) {
        auto start = qosIntervalStart;
        if (newInterval) {
            start -= qosInterval;
        }
        double curTransferredBytes = dataSource->getTransferredInfo(pair, start);
        current.throughput = curTransferredBytes / (current.timestamp - start);
    }

    int lastDecision = dataSource->getOptimizerValue(pair);
    if (lastDecision == 0 && !timeMultiplexing) {
        current.ema = current.throughput;
    }
    else {
        // Calculate new Exponential Moving Average
        current.ema = exponentialMovingAverage(current.throughput, emaAlpha, lastState.ema);
    }

    return current;
}


void Optimizer::run(void)
{
    FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Optimizer run" << commit;
    try {
        // take care of current resource interval
        time_t now = time(NULL);
        bool timeMultiplexing = false;
        if (qosInterval > 0) {
            timeMultiplexing = true;
            FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Time multiplexing enabled" << commit;
        }

        bool newInterval = false;
        if (timeMultiplexing && now - qosIntervalStart > qosInterval) {
            // we have ended our resource interval
            // time to start a new one
            newInterval = true;
            sleepingPipes.clear();
            qosIntervalStart = now;
            FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Time multiplexing: new QoS innterval starts" << commit;
        }

        std::list<Pair> pairs = dataSource->getActivePairs();
        // Make sure the order is always the same
        // See FTS-1094
        pairs.sort();

        std::map<string, std::list<Pair>> pairsOfProject;

        // Retrieve pair state
        std::map<Pair, PairState> aggregatedPairState;
        for (auto i = pairs.begin(); i != pairs.end(); ++i) {
            FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Test run " << *i << " using traditional optimizer" << commit;
            auto optMode = runOptimizerForPair(*i);
            FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Optimizer mode of " << *i << ": " << optMode << commit;
            if (optMode == kOptimizerAggregated) {
                FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Put " << *i << " to TCN aggregated optimizer" << commit;

                aggregatedPairState[*i] = getPairState(*i, timeMultiplexing, newInterval);

                std::string project = dataSource->getTcnProject(*i);

                pairsOfProject[project].push_back(*i);
            }
        }

        if (!newInterval) {
            for (auto it = pairsOfProject.begin(); it != pairsOfProject.end(); ++it) {
                auto project = it->first;
                auto projectPairs = it->second;

                std::map<std::string, double> resourceLimits;
                dataSource->getTcnResourceSpec(project, resourceLimits);

                std::map<std::string, std::list<Pair>> resourcePairs;

                for (auto p = projectPairs.begin(); p != projectPairs.end(); ++p) {
                    std::vector<std::string> usedResources;
                    dataSource->getTcnPipeResource(*p, usedResources);

                    for (auto resc = usedResources.begin(); resc != usedResources.end(); ++resc) {
                        resourcePairs[*resc].push_back(*p);
                    }
                }

                bool sleeping = false;

                for (auto rl = resourceLimits.begin(); rl != resourceLimits.end(); ++rl) {
                    auto resc = rl->first;
                    double bwLimit = rl->second / 1024;
                    auto pairsUsingResource = resourcePairs[resc];

                    double actualMBps = 0;
                    for (auto p = pairsUsingResource.begin(); p != pairsUsingResource.end(); ++p) {
                        actualMBps += aggregatedPairState[*p].throughput / 1024 / 1024;
                    }

                    if (actualMBps > bwLimit) {
                        sleeping = true;
                        FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Time multiplexing: project " << project
                            << " exceeds limit on resource " << resc
                            << " (target: " << bwLimit << " MB/s, actual: " << actualMBps << " MB/s),"
                            << " all pipes of the project will sleep." << commit;
                        break;
                    }
                }

                if (sleeping) {
                    for (auto p = projectPairs.begin(); p != projectPairs.end(); ++p) {
                        sleepingPipes.insert(*p);
                        FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Time multiplexing: pipe " << *p
                            << " in project " << project << " is sleeping." << commit;
                    }
                }
            }
        }

        // Start ticking!
        boost::timer::cpu_timer timer;

        std::map<Pair, DecisionState> decisionVector;
        // Run TCN control loop
        if (tcnOptimizer == NULL) {
            FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "TCN Optimizer is not configured. Calling naive optimizer." << commit;
            decisionVector = runNaiveTCNOptimizer(aggregatedPairState);
        }
        else {
            decisionVector = runTCNOptimizer(aggregatedPairState);
        }

        // for(auto sleepingPipe = sleepingPipes.begin(); sleepingPipe != sleepingPipes.end(); sleepingPipe++){
            // set decision for sleeping pipes to zero
            // decisionVector[*sleepingPipe] = 0;
        // }

        boost::timer::cpu_times const elapsed(timer.elapsed());
        FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Global time elapsed: " << elapsed.system << ", " << elapsed.user << commit;

        // Apply the decision to each pair
        applyDecisions(decisionVector, timer);
    }
    catch (std::exception &e) {
        throw SystemError(std::string(__func__) + ": Caught exception " + e.what());
    }
    catch (...) {
        throw SystemError(std::string(__func__) + ": Caught exception ");
    }
}


std::map<Pair, DecisionState> Optimizer::runTCNOptimizer(std::map<Pair, PairState> aggregatedPairState)
{
    std::map<Pair, int> decisions;
    std::map<Pair, DecisionState> decisionVector;

    tcnOptimizer->step(aggregatedPairState, decisions);

    for (auto it = decisions.begin(); it != decisions.end(); ++it) {
        auto pair = it->first;
        int decision = it->second;
        int lastDecision = dataSource->getOptimizerValue(pair);
        auto current = aggregatedPairState[pair];

        std::stringstream rationale;
        rationale << "Calculated by zero order gradient optimizer.";

        DecisionState decisionInfo(current, decision, decision - lastDecision, rationale.str());
        decisionVector[pair] = decisionInfo;

        // Remember to store new state back to inMemoryStore
        inMemoryStore[pair] = current;
    }
    return decisionVector;
}


std::map<Pair, DecisionState> Optimizer::runNaiveTCNOptimizer(std::map<Pair, PairState> aggregatedPairState)
{
    std::map<Pair, DecisionState> decisionVector;

    // Implement TCN control loop
    // FIXME: Naive implementation
    for (auto it=aggregatedPairState.begin(); it != aggregatedPairState.end(); ++it) {
        auto pair = it->first;
        auto current = it->second;

        int lastDecision = dataSource->getOptimizerValue(pair);
        int decision = 0;
        std::stringstream rationale;

        bool sleeping = false;

        // Check if the pipe is sleeping
        auto f = sleepingPipes.find(pair);
        if (f != sleepingPipes.end()) {
            sleeping = true;
            FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Time multiplexing: pipe " << pair << " is sleeping..." << commit;
        }

        // Optimizer working values
        Range range;
        StorageLimits limits;
        getOptimizerWorkingRange(pair, &range, &limits);

        FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Optimizer range for " << pair << ": " << range  << commit;

        DecisionState decisionInfo(current);

        // There is no value yet. In this case, pick the high value if configured, mid-range otherwise.
        if (sleeping) {
            rationale << "Sleep because of time multiplexing. Not start any new transfers";
            decisionInfo.decision = 0;
            decisionInfo.diff = 0 - lastDecision;
            decisionInfo.rationale = rationale.str();
        } else if (lastDecision == 0) {
            if (range.specific) {
                decision = range.max;
                rationale << "No information. Use configured range max.";
            } else {
                decision = range.min + (range.max - range.min) / 2;
                rationale << "No information. Start halfway.";
            }

            // setOptimizerDecision(pair, decision, current, decision, rationale.str(), timer.elapsed());
            decisionInfo.decision = decision;
            decisionInfo.diff = decision;
            decisionInfo.rationale = rationale.str();

        } else {
            rationale << "Naive implementation. Keep previous decision.";
            decisionInfo.decision = lastDecision;
            decisionInfo.diff = 0;
            decisionInfo.rationale = rationale.str();

        }

        // Remember to store new state back to inMemoryStore
        inMemoryStore[pair] = current;

        decisionVector[pair] = decisionInfo;
    }
    return decisionVector;
}


void Optimizer::applyDecisions(std::map<Pair, DecisionState> decisionVector, boost::timer::cpu_timer timer)
{
    for (auto it=decisionVector.begin(); it != decisionVector.end(); ++it) {
        auto pair = it->first;
        auto decisionInfo = it->second;

        boost::timer::cpu_times const elapsed(timer.elapsed());

        FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Apply decision for pair " << pair << ":\n" \
                                         << "\tdecision: " << decisionInfo.decision << "\n" \
                                         << "\tdiff: " << decisionInfo.diff << "\n" \
                                         << "\trationale: " << decisionInfo.rationale << "\n" \
                                         << "\ttime elapsed: " << elapsed.system << ", " << elapsed.user << "\n" \
                                         << "\tcurrent: " << decisionInfo.current.activeCount << ", " << decisionInfo.current.ema << "\n" \
                                         << commit;

        setOptimizerDecision(pair, decisionInfo.decision, decisionInfo.current,
            decisionInfo.diff, decisionInfo.rationale, elapsed);
        optimizeStreamsForPair(kOptimizerAggregated, pair);
    }
}


OptimizerMode Optimizer::runOptimizerForPair(const Pair &pair)
{
    OptimizerMode optMode = dataSource->getOptimizerMode(pair.source, pair.destination);
    if(optMode < kOptimizerAggregated && optimizeConnectionsForPair(optMode, pair)) {
        // Optimize streams only if optimizeConnectionsForPair did store something
        optimizeStreamsForPair(optMode, pair);
    }
    return optMode;
}

}
}
