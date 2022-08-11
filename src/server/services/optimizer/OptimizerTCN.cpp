#include <iostream>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <random>
#include <chrono>
#include <list>
#include <map>
#include <string>

#include "Optimizer.h"
#include "alto/alto_client.hpp"

using namespace std;

namespace fts3 {
namespace optimizer {

// FIXME: move to ftsconfig or mysql database later
const std::string PATH_VECTOR_URI = "http://mininet:9090/pv/pathvector";

float
TCNOptimizer::explorationProbability() {
  if (started) {
    float base_prob =
        exp(-(stepCount - lastActiveChange) / explorationDecaySteps) *
        maxExplorationProbability;
    return max(base_prob, minExplorationProbability);
  } else {
    // started = true;
    return 1.0f;
  }
}

//decides to wether take a random exploration or follow the
//momentum
bool TCNOptimizer::explorationDecision()
{
    if (stepCount == lastActiveChange) {
        return true;
    }
    std::binomial_distribution<int> distribution(1, explorationProbability());
    return distribution(generator) == 1;
}

void TCNOptimizer::gradientEstimate(std::map<Pair, PairState> &current)
{

    bool nsEqual = true;
    std::map<Pair, PairState>::iterator it;

    if (current.size() == lastState.size()) {
        cout << std::endl << "inside cond" << std::endl;
        for (it = current.begin(); it != current.end(); it++) {
            std::map<Pair, PairState>::iterator it2 = lastState.find(it->first);
            if (it2 != lastState.end() && it->second.activeCount != it2->second.activeCount)
            {
               nsEqual = false;
               break;
           }
       }
   }

   gradients.clear();

   if (nsEqual) {
       cout << std::endl << "ns equal" << std::endl;
       for (it = current.begin(); it != current.end(); it++) {
           gradients.insert(std::pair<Pair, float>(it->first, 0.0f));
       }
   } else {
       float norm2C = norm2(current);
       cout << "norm2c:\t" << norm2C << endl;
       float utilityDiff = (getUtility(current) - getUtility(lastState)) / norm2C;
       cout << "utility diff:\t" << utilityDiff << endl;
       cout << "penalty:\t" << aggregatedBarrierPenalty(current) << std::endl;
       cout << "penalty:\t" << aggregatedBarrierPenalty(lastState) << std::endl;
       for (it = current.begin(); it != current.end(); it++) {
           float pDerivative = utilityDiff * (it->second.activeCount - lastState[it->first].activeCount);
           gradients.insert(std::pair<Pair, float>(it->first, pDerivative));
           momentums[it->first] = \
               alphaT() * gradients[it->first] + eta * momentums[it->first];
       }
   }
   float tmpAlpha = alphaT();
   for (it = current.begin(); it != current.end(); it++) {
       momentums[it->first] = \
           tmpAlpha * gradients[it->first] + eta * momentums[it->first];
   }
}

bool TCNOptimizer::listsAreEqual(std::set<Pair>& lhs, std::map<Pair, PairState>& rhs, std::set<Pair>& rhsSet)
{
    // std::set<Pair> rhsList;
    // std::copy(rhs.begin(), rhs.end(),
    //                 std::inserter(rhsList, rhsList.begin()));
    map<Pair, PairState>::iterator it;
    for (it = rhs.begin(); it != rhs.end(); it++) {
        rhsSet.insert(it->first);
    }
    return (lhs == rhsSet);
}

float TCNOptimizer::norm2(std::map<Pair, PairState>& states)
{

    map<Pair, PairState>::iterator it;
    float sum = 0;

    for (it = states.begin(); it != states.end(); it++) {
        sum += pow(it->second.activeCount - lastState[it->first].activeCount, 2);
    }

    //return sqrt(sum);
    return sum;
}

int TCNOptimizer::expectInt(float input)
{
    float prob = input - floor(input);
    std::binomial_distribution<int> distribution(1, prob);
    int decision = distribution(generator);
    int rout;
    if (decision == 1) {
        rout = ceil(input);
    } else {
        rout = floor(input);
    }
    return rout;
}


void TCNOptimizer::randomPermutation(std::map<Pair, PairState>& activePairs, std::map<Pair, int>& permutations)
{
    std::map<Pair, PairState>::iterator it;
    //std::map<Pair, int> permutations;
    std::normal_distribution<double> distribution(0.0, permutationStd);
    for (it = activePairs.begin(); it != activePairs.end(); it++)
    {
        permutations.insert(std::pair<Pair, int>(it->first, expectInt(distribution(generator))));
    }

    //return &permutations;
}

void TCNOptimizer::randomStep(std::map<Pair, PairState> &conns, std::map<Pair, int> &decisions)
{
    cout << "random step:\n";
    decisions.clear();
    std::map<Pair, PairState>::iterator it;
    std::map<Pair, int> permutations;
    randomPermutation(conns, permutations);
    for (it = conns.begin(); it != conns.end(); it++)
    {
        permutations[it->first] += it->second.activeCount;
        cout << "permutations: " << permutations[it->first] << std::endl;
    }

    boundDecision(permutations);

    for (it = conns.begin(); it != conns.end(); it++)
    {
        //it->second.activeCount = *permutations[it->first];
        decisions.insert(std::pair<Pair, int>(it->first, permutations[it->first]));
    }

    return;
}

void TCNOptimizer::boundDecision(std::map<Pair, int> &rawNs)
{
    std::map<Pair, int>::iterator it;
    for (it = rawNs.begin(); it != rawNs.end(); it++)
    {
        it->second = min(max(minOpt, it->second), maxOpt);
    }
    return ;
}

float TCNOptimizer::alphaT()
{
    return alpha * pow(10,
                -float(maxMagnitudeAlphaDecay) * \
                min(float(stepCount - lastActiveChange)/float(decayStopLimit), 1.0f));
}

void TCNOptimizer::gradientStep(std::map<Pair, PairState> &conns, std::map<Pair, int> &decisions)
{
    cout << "gradient step" << std::endl << std::endl;
    std::map<Pair, PairState>::iterator it;
    decisions.clear();
    float tmpAlpha = alphaT();

    for (it = conns.begin(); it != conns.end(); it++)
    {
        //cout << "gradient: " << gradients[it->first] << std::endl;
        cout << "alphaT: " << tmpAlpha << std::endl;
        /*momentums[it->first] = \
            tmpAlpha * gradients[it->first] + eta * momentums[it->first];*/
        cout << "gradient: " << gradients[it->first] << " momentum: " << momentums[it->first] << std::endl;
        int decision = expectInt(momentums[it->first] + it->second.activeCount);
        decisions.insert(std::pair<Pair, int>(it->first, min(max(minOpt, decision), maxOpt)));
        // it->second.activeCount = decision;
    }

    return ;
}

float TCNOptimizer::penaltyValue(float barrier)
{
    if (barrier <= 0.0f) {
        return 0.0f;
    }
    if (penaltyMethod == "quadratic") {
        return -pow(barrier, 2);
    } else {
        return -barrier;
    }
}

float TCNOptimizer::networkBarrier(PathConstraints pc, std::map<Pair, PairState> &conns)
{
    size_t NF = pc.flow_map.size();
    size_t NL = pc.b.size();
    std::vector<float> tputs;
    for (size_t j = 0; j < NF; ++j) {
        auto f = pc.flow_map[j];
        Pair p(f.first, f.second);
        auto state = conns.find(p);
        if (state != conns.end()) {
            tputs.push_back((state->second).throughput);
        }
        else {
            tputs.push_back(0.0f);
        }
    }
    float penalty = 0.0f;
    for (size_t i = 0; i < NL; ++i) {
        float link_util = 0.0f;
        for (size_t j = 0; j < NF; ++j) {
            link_util += pc.A[j][i] * tputs[j];
        }
        penalty += penaltyValue(link_util - pc.b[i]);
    }
    return penalty;
}

// float TCNOptimizer::barrierPartialDerivative(const Pair &pair, const PairState &state)
// {
//     return 0.0f;
// }

float TCNOptimizer::barrierValue(const Pair &pair, const PairState &state)
{
    cout << "conditions for pair: tput[" << pair << "] <=" << conditions[pair] << endl;
    float barrier = penaltyValue(state.throughput - conditions[pair]);
    cout << "penalty for pair (" << pair << "): " << barrier << endl;
    return barrier;
}

float TCNOptimizer::aggregatedBarrierPenalty(std::map<Pair, PairState> &conns)
{
    float diff = 0.0f;
    std::map<Pair, float>::iterator it;

    for (it = conditions.begin(); it != conditions.end(); it++) {
        diff += barrierValue(it->first, conns[it->first]);
    }

    std::list<EndpointFlow> flows;
    for (it = conns.begin(); it != conns.end(); it++) {
        auto pair = it->first;
        EndpointFlow flow;
        flow.srcs.push_back(pair.source);
        flow.dsts.push_back(pair.destination);
        flows.push_back(flow);
    }

    std::list<std::string> props;
    props.push_back("bandwidth")

    PathConstraints pc = get_path_constraints(PATH_VECTOR_URI, flows, props);
    diff += networkBarrier(pc, conns);

    return diff;
}

//
// Public members of TCNOptimizer
//
TCNOptimizer::TCNOptimizer(std::string penaltyMethod,
    float alpha,
    int maxMagnitudeAlphaDecay,
    float eta,
    float omega,
    int minOpt,
    int maxOpt,
    float maxExplorationProbability,
    float minExplorationProbability,
    int explorationDecaySteps,
    int decayStopLimit,
    int lastActiveChange,
    float permutationStd,
    float explorationDeclineCoeff
)
{
    TCNOptimizer::alpha = alpha;
    TCNOptimizer::maxMagnitudeAlphaDecay = maxMagnitudeAlphaDecay;
    TCNOptimizer::eta = eta;
    TCNOptimizer::omega = omega;
    TCNOptimizer::minOpt = minOpt;
    TCNOptimizer::maxOpt = maxOpt;
    TCNOptimizer::maxExplorationProbability = maxExplorationProbability;
    TCNOptimizer::minExplorationProbability = minExplorationProbability;
    TCNOptimizer::explorationDecaySteps = explorationDecaySteps;
    TCNOptimizer::decayStopLimit = decayStopLimit;
    TCNOptimizer::lastActiveChange = lastActiveChange;
    TCNOptimizer::permutationStd = permutationStd;
    TCNOptimizer::explorationDeclineCoeff = explorationDeclineCoeff;
    TCNOptimizer::penaltyMethod = penaltyMethod;
    unsigned seedValue = std::chrono::system_clock::now().time_since_epoch().count();
    seed = seedValue;
    started = false;
    stepCount = 0;
    lastActiveChange = 0;
    generator.seed(seedValue);
}

float TCNOptimizer::objective(std::map<Pair, PairState> &connStates)
{
    // return std::accumulate(thrs.begin(), thrs.end(), 0.0f);
    std::map<Pair, PairState>::iterator it;
    float obj = 0.0f;
    for (it = connStates.begin(); it != connStates.end(); it++) {
        obj += it->second.throughput;
    }
    return obj;
}

void TCNOptimizer::step(std::map<Pair, PairState> &activeTCNPipes, std::map<Pair, int> &decisions)
{
    // check if active pipes have changed
    std::set<Pair> currentActives;
    bool activeChange = listsAreEqual(activePipes, activeTCNPipes, currentActives);
    if (activeChange == false) {
        activePipes.clear();
        activePipes = currentActives;
        lastActiveChange = stepCount;
        std::map<Pair, PairState>::iterator it;
        gradients.clear();
        momentums.clear();
        for (it = activeTCNPipes.begin(); it != activeTCNPipes.end(); it++) {
            gradients[it->first] = 0.0f;
            momentums[it->first] = 0.0f;
        }
    } else {
        // update gradients
        cout << "estimating gradients" << std::endl;
        gradientEstimate(activeTCNPipes);
    }
    //do the step
    // std::map<Pair, int> *decisions;
    if (explorationDecision() == true) {
        randomStep(activeTCNPipes, decisions);
    } else {
        gradientStep(activeTCNPipes, decisions);
    }
    //update last states
    lastState.clear();
    lastState = activeTCNPipes;

    stepCount ++;
    if (started == false){
        started = true;
    }
    return ;
    //set the logs
}

float TCNOptimizer::getUtility(std::map<Pair, PairState> &connStates)
{
    return objective(connStates) + omega * aggregatedBarrierPenalty(connStates);
}

void TCNOptimizer::setConditionForPair(Pair pair, float limit)
{
    std::map<Pair, float>::iterator itr;
    itr = conditions.find(pair);
    if (itr != conditions.end()){
        itr->second = limit;
    } else {
        conditions.insert(std::pair<Pair, float>(pair, limit));
    }
    return ;
}

void TCNOptimizer::setConditions(const std::map<Pair, float> &newConditions)
{
    conditions.clear();
    conditions = newConditions;
    return ;
}

void TCNOptimizer::getConditions(std::map<Pair, float> &loadedConditions)
{
    loadedConditions.clear();
    loadedConditions = conditions;
    return ;
}

float TCNOptimizer::getAlpha()
{
    return alpha;
}

void TCNOptimizer::getGradients(std::map<Pair, double> &loadedGradients)
{
    loadedGradients.clear();
    loadedGradients = gradients;
    return;
}

bool TCNOptimizer::getExplorationDecision()
{
    return explorationDecision();
}

float TCNOptimizer::getExplorationProbability()
{
    return explorationProbability();
}

void TCNOptimizer::setLastState(std::map<Pair, PairState> &pushedLastStates)
{
    lastState.clear();
    lastState = pushedLastStates;
    return;
}

void TCNOptimizer::clearConditions()
{
    conditions.clear();
}

// int TCNOptimizer::getLastActiveChange()
// {
//     return std::
// }

}
}
