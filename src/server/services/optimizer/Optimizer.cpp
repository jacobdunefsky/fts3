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

using namespace fts3::common;
using namespace fts3::config;

namespace fts3 {
namespace optimizer {


Optimizer::Optimizer(OptimizerDataSource *ds, OptimizerCallbacks *callbacks):
    dataSource(ds), callbacks(callbacks),
    optimizerSteadyInterval(boost::posix_time::seconds(60)), maxNumberOfStreams(10),
    maxSuccessRate(100), lowSuccessRate(97), baseSuccessRate(96),
    decreaseStepSize(1), increaseStepSize(1), increaseAggressiveStepSize(2),
    emaAlpha(EMA_ALPHA)
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


PairState Optimizer::getPairState(const Pair &pair)
{
    return inMemoryStore[pair];
}


void Optimizer::run(void)
{
    FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Optimizer run" << commit;
    try {
        std::list<Pair> pairs = dataSource->getActivePairs();
        // Make sure the order is always the same
        // See FTS-1094
        pairs.sort();

        // Retrieve pair state
        std::map<Pair, PairState> aggregatedPairState;
        for (auto i = pairs.begin(); i != pairs.end(); ++i) {
            FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Test run " << *i << " using traditional optimizer" << commit;
            auto optMode = runOptimizerForPair(*i);
            FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Optimizer mode of " << *i << ": " << optMode << commit;
            if (optMode == kOptimizerAggregated) {
                FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Put " << *i << " to TCN aggregated optimizer" << commit;
                aggregatedPairState[*i] = getPairState(*i);
            }
        }

        // Start ticking!
        boost::timer::cpu_timer timer;

        // Run TCN control loop
        std::map<Pair, DecisionState> decisionVector = runTCNOptimizer(aggregatedPairState);

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
    std::map<Pair, DecisionState> decisionVector;

    // Implement TCN control loop
    // FIXME: Naive implementation
    for (auto it=aggregatedPairState.begin(); it != aggregatedPairState.end(); ++it) {
        auto pair = it->first;
        auto lastState = it->second;

        int lastDecision = dataSource->getOptimizerValue(pair);
        int decision = 0;
        std::stringstream rationale;

        // Optimizer working values
        Range range;
        StorageLimits limits;
        getOptimizerWorkingRange(pair, &range, &limits);

        FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Optimizer range for " << pair << ": " << range  << commit;

        DecisionState decisionInfo;

        // Initialize current state
        // PairState current;
        decisionInfo.current.timestamp = time(NULL);
        decisionInfo.current.avgDuration = dataSource->getAverageDuration(pair, boost::posix_time::minutes(30));

        boost::posix_time::time_duration timeFrame = calculateTimeFrame(decisionInfo.current.avgDuration);

        dataSource->getThroughputInfo(pair, timeFrame,
            &decisionInfo.current.throughput, &decisionInfo.current.filesizeAvg, &decisionInfo.current.filesizeStdDev);
        decisionInfo.current.successRate = dataSource->getSuccessRateForPair(pair, timeFrame, &decisionInfo.current.retryCount);
        decisionInfo.current.activeCount = dataSource->getActive(pair);
        decisionInfo.current.queueSize = dataSource->getSubmitted(pair);

        // There is no value yet. In this case, pick the high value if configured, mid-range otherwise.
        if (lastDecision == 0) {
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

            decisionInfo.current.ema = decisionInfo.current.throughput;
        } else {
            rationale << "Naive implementation. Keep previous decision.";
            decisionInfo.decision = lastDecision;
            decisionInfo.diff = 0;
            decisionInfo.rationale = rationale.str();

            // Calculate new Exponential Moving Average
            decisionInfo.current.ema = exponentialMovingAverage(decisionInfo.current.throughput, emaAlpha, lastState.ema);
        }

        // Remember to store new state back to inMemoryStore
        inMemoryStore[pair] = decisionInfo.current;

        decisionVector[pair] = decisionInfo;
    }
    return decisionVector;
}


void Optimizer::applyDecisions(std::map<Pair, DecisionState> decisionVector, boost::timer::cpu_timer timer)
{
    for (auto it=decisionVector.begin(); it != decisionVector.end(); ++it) {
        auto pair = it->first;
        auto decisionInfo = it->second;
        setOptimizerDecision(pair, decisionInfo.decision, decisionInfo.current,
            decisionInfo.diff, decisionInfo.rationale, timer.elapsed());
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
