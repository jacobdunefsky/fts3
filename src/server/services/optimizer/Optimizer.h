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

#ifndef FTS3_OPTIMIZER_H
#define FTS3_OPTIMIZER_H

#include <list>
#include <map>
#include <set>
#include <string>

#include <boost/noncopyable.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/timer/timer.hpp>
#include <db/generic/LinkConfig.h>
#include <db/generic/Pair.h>
#include <msg-bus/producer.h>

#include "common/Uri.h"


namespace fts3 {
namespace optimizer {

// borrowed from http://oroboro.com/irregular-ema/
static inline double exponentialMovingAverage(double sample, double alpha, double cur)
{
    if (sample > 0)
        cur = (sample * alpha) + ((1 - alpha) * cur);
    return cur;
}


static boost::posix_time::time_duration calculateTimeFrame(time_t avgDuration)
{
    if(avgDuration > 0 && avgDuration < 30) {
        return boost::posix_time::minutes(5);
    }
    else if(avgDuration > 30 && avgDuration < 900) {
        return boost::posix_time::minutes(15);
    }
    else {
        return boost::posix_time::minutes(30);
    }
}


struct Range {
    int min, max;
    // Set to true if min,max is configured specifically, or is a *->* configuration
    bool specific;
    // Set to true if min,max is configured with SE limits instead of link configuration
    bool storageSpecific;

    Range(): min(0), max(0), specific(false), storageSpecific(false) {}
};


struct StorageLimits {
    int source, destination;
    double throughputSource, throughputDestination;

    StorageLimits(): source(0), destination(0),
              throughputSource(0), throughputDestination(0) {}
};

struct PairState {
    time_t timestamp;
    double throughput;
    time_t avgDuration;
    double successRate;
    int retryCount;
    int activeCount;
    int queueSize;
    // Exponential Moving Average
    double ema;
    // Filesize statistics
    double filesizeAvg, filesizeStdDev;
    // Optimizer last decision
    int connections;

    PairState(): timestamp(0), throughput(0), avgDuration(0), successRate(0), retryCount(0), activeCount(0),
                 queueSize(0), ema(0), filesizeAvg(0), filesizeStdDev(0), connections(1) {}

    PairState(time_t ts, double thr, time_t ad, double sr, int rc, int ac, int qs, double ema, int conn):
        timestamp(ts), throughput(thr), avgDuration(ad), successRate(sr), retryCount(rc),
        activeCount(ac), queueSize(qs), ema(ema), filesizeAvg(0), filesizeStdDev(0), connections(conn) {}
};

struct DecisionState {
    PairState current;
    int decision;
    int diff;
    std::string rationale;

    DecisionState(): decision(0), diff(0) {}
    DecisionState(PairState current): decision(0), diff(0), current(current) {}
    DecisionState(PairState current, int decision): current(current), decision(decision), diff(0) {}
    DecisionState(PairState current, int decision, int diff): current(current), decision(decision), diff(diff) {}
    DecisionState(PairState current, int decision, int diff, std::string rationale): decision(decision), diff(diff), current(current), rationale(rationale) {}
};

#include "OptimizerTCN.h"

// To decouple the optimizer core logic from the data storage/representation
class OptimizerDataSource {
public:
    virtual ~OptimizerDataSource()
    {}

    // Return a list of pairs with active or submitted transfers
    virtual std::list<Pair> getActivePairs(void) = 0;

    // Return the optimizer configuration value
    virtual OptimizerMode getOptimizerMode(const std::string &source, const std::string &dest) = 0;

    // Get configured limits
    virtual void getPairLimits(const Pair &pair, Range *range, StorageLimits *limits) = 0;

    // Get the stored optimizer value (current value)
    virtual int getOptimizerValue(const Pair&) = 0;

    // Get the weighted throughput for the pair
    virtual void getThroughputInfo(const Pair &, const boost::posix_time::time_duration &,
        double *throughput, double *filesizeAvg, double *filesizeStdDev) = 0;

    virtual time_t getAverageDuration(const Pair&, const boost::posix_time::time_duration&) = 0;

    // Get the success rate for the pair
    virtual double getSuccessRateForPair(const Pair&, const boost::posix_time::time_duration&, int *retryCount) = 0;

    // Get the number of transfers in the given state
    virtual int getActive(const Pair&) = 0;
    virtual int getSubmitted(const Pair&) = 0;

    // Get current throughput
    virtual double getThroughputAsSource(const std::string&) = 0;
    virtual double getThroughputAsDestination(const std::string&) = 0;

    // Permanently register the optimizer decision
    virtual void storeOptimizerDecision(const Pair &pair, int activeDecision,
        const PairState &newState, int diff, const std::string &rationale) = 0;

    // Permanently register the number of streams per active
    virtual void storeOptimizerStreams(const Pair &pair, int streams) = 0;
};

// Used by the optimizer to notify decisions
class OptimizerCallbacks {
public:
    virtual void notifyDecision(const Pair &pair, int decision, const PairState &current,
        int diff, const std::string &rationale) = 0;
};

// Optimizer implementation
class Optimizer: public boost::noncopyable {
protected:
    std::map<Pair, PairState> inMemoryStore;
    OptimizerDataSource *dataSource;
    OptimizerCallbacks *callbacks;
    boost::posix_time::time_duration optimizerSteadyInterval;
    int maxNumberOfStreams;
    int maxSuccessRate;
    int lowSuccessRate;
    int baseSuccessRate;

    int decreaseStepSize;
    int increaseStepSize, increaseAggressiveStepSize;
    double emaAlpha;

    //
    // Member for TCN optimizer specific
    //
    TCNOptimizer *tcnOptimizer;


    // Run the optimization algorithm for the number of connections.
    // Returns true if a decision is stored
    bool optimizeConnectionsForPair(OptimizerMode optMode, const Pair &);

    // Run the optimization algorithm for the number of streams.
    void optimizeStreamsForPair(OptimizerMode optMode, const Pair &);

    // Stores into rangeActiveMin and rangeActiveMax the working range for the optimizer
    void getOptimizerWorkingRange(const Pair &pair, Range *range, StorageLimits *limits);

    // Retrieve previous running state of the pair
    PairState getPairState(const Pair &pair);

    // Updates decision
    void setOptimizerDecision(const Pair &pair, int decision, const PairState &current,
        int diff, const std::string &rationale, boost::timer::cpu_times elapsed);

    // Apply decisions to pairs
    void applyDecisions(std::map<Pair, DecisionState> decisionVector, boost::timer::cpu_timer timer);

public:
    Optimizer(OptimizerDataSource *ds, OptimizerCallbacks *callbacks, TCNOptimizer *tcnOptimizer=NULL);
    ~Optimizer();

    void setSteadyInterval(boost::posix_time::time_duration);
    void setMaxNumberOfStreams(int);
    void setMaxSuccessRate(int);
    void setLowSuccessRate(int);
    void setBaseSuccessRate(int);
    void setStepSize(int increase, int increaseAggressive, int decrease);
    void setEmaAlpha(double);
    void run(void);
    OptimizerMode runOptimizerForPair(const Pair&);
    std::map<Pair, DecisionState> runTCNOptimizer(std::map<Pair, PairState> aggregatedPairState);
    std::map<Pair, DecisionState> runNaiveTCNOptimizer(std::map<Pair, PairState> aggregatedPairState);
};


inline std::ostream& operator << (std::ostream &os, const Range &range) {
    return (os << range.min << "/" << range.max);
}

}
}

#endif // FTS3_OPTIMIZER_H
