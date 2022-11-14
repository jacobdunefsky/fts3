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


Optimizer::Optimizer(OptimizerDataSource *ds, OptimizerCallbacks *callbacks, TCNOptimizer *tcnOptimizer):
    dataSource(ds), callbacks(callbacks), tcnOptimizer(tcnOptimizer),
    optimizerSteadyInterval(boost::posix_time::seconds(60)), maxNumberOfStreams(10),
    maxSuccessRate(100), lowSuccessRate(97), baseSuccessRate(96),
    decreaseStepSize(1), increaseStepSize(1), increaseAggressiveStepSize(2),
    emaAlpha(EMA_ALPHA), resourceIntervalSize(10), resourceIntervalStart(time(NULL))
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

    int lastDecision = dataSource->getOptimizerValue(pair);
    if (lastDecision == 0) {
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
		bool newInterval = false;
		if(now - resourceIntervalStart > resourceIntervalSize) {
			// we have ended our resource interval
			// time to start a new one
			newInterval = true;
			sleepingPipes.clear();
			initialTransferred.clear();
			resourceIntervalStart = now;
		}

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

				// see if it's time for this pipe to stop transferring
				// (in order to respect bandwidth limits)
				if(newInterval){
					initialTransferred[*i] = dataSource->getTransferredInfo(*i, resourceIntervalStart);
				}
				else {
					int64_t curTransferred = dataSource->getTransferredInfo(*i, resourceIntervalStart) - initialTransferred[*i];
					// TODO: get resource limits from database
					//uint64_t bwLimit = dataSource->getBwLimitForPipe(*i);
					uint64_t bwLimit = 20;
					if(curTransferred > resourceIntervalSize * bwLimit && bwLimit != 0){
						// we've gone over our bandwidth limit
						// add to the set of sleeping pipes
						sleepingPipes.add(*i);
					}
				}
				// if our pipe isn't sleeping, then send it to the optimizer
				if(auto f = sleepingPipes.find(*i); f == example.end()) {
					aggregatedPairState[*i] = getPairState(*i);
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

        // Optimizer working values
        Range range;
        StorageLimits limits;
        getOptimizerWorkingRange(pair, &range, &limits);

        FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Optimizer range for " << pair << ": " << range  << commit;

        DecisionState decisionInfo(current);

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
