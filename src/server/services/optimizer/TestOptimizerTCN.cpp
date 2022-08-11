#include <chrono>

#include "Optimizer.h"

using namespace std;

using namespace fts3;
using namespace optimizer;

float throughputForSingleConn(int n, int bufferSize = 32*pow(2, 20), float linkCap = 10*pow(2, 30),
                                float detroriationCoeff=0.05, int detoriariationLimit = 300, float linkDelay = 100 //ms
)
{
    float refCap = max(min(linkCap * (1 - ((n - detoriariationLimit) * detroriationCoeff)), linkCap), 0.0f);
    //cout << "ref cap:\t" << refCap << std::endl ;
    float bufferLimit = (float(n) * float(bufferSize)) / (linkDelay / 1000);
    return min(refCap, bufferLimit);
}

int main() {
    TCNOptimizer optimizer;
    cout << optimizer.getAlpha() << "\n";
    map<Pair, double> checkedGradients;
    optimizer.getGradients(checkedGradients);
    cout << checkedGradients.size() << "\n";
    map<Pair, float> conditions;
    Pair pair1("a", "b");
    Pair pair2("a", "c");
    optimizer.setConditionForPair(pair1, pow(2, 33));
    optimizer.setConditionForPair(pair2, pow(2, 31));
    optimizer.getConditions(conditions);
    cout << conditions.size() << "\n";
    for(std::map<Pair, float>::iterator it = conditions.begin(); it != conditions.end(); ++it)
    {
        std::cout << it->first << " " << it->second << "\n";
    }
    optimizer.setConditionForPair(pair1, pow(2, 32));
    optimizer.getConditions(conditions);
    for(std::map<Pair, float>::iterator it = conditions.begin(); it != conditions.end(); ++it)
    {
        std::cout << it->first << " " << it->second << "\n";
    }
    for (int i=0; i <= 10; i++) {
        cout << optimizer.getExplorationProbability() << "\t" << optimizer.getExplorationDecision() << "\n";
    }
    std::default_random_engine generator;
    generator.seed(std::chrono::system_clock::now().time_since_epoch().count());
    for (int i=0; i <= 10; i++) {
        std::normal_distribution<double> distribution(float(i), 1.0f);
        float num = distribution(generator);
        cout << "num: " << num << "\texpect int: " << optimizer.expectInt(num) << "\n";
        cout << "i: " << i << "\texpect int: " << optimizer.expectInt(float(i)) << "\n";
        cout << "i+1-ep: " << i + 0.999 << "\texpect int: " << optimizer.expectInt(float(i) + 0.999) << "\n";
    }

    /*PairState(time_t ts, double thr, time_t ad, double sr, int rc, int ac, int qs, double ema, int conn):
        timestamp(ts), throughput(thr), avgDuration(ad), successRate(sr), retryCount(rc),
        activeCount(ac), queueSize(qs), ema(ema), filesizeAvg(0), filesizeStdDev(0), connections(conn) {}*/
    std::binomial_distribution<int> distribution(100, 0.5f);
    std::normal_distribution<float> thrDistribution(float(pow(2, 30)* 5), float(pow(2, 30) * 2));
    for (int i=0; i<100; i++)   {
        int numConn = distribution(generator);
        float sum = 0;
        std::map<Pair, PairState> tmpConns;
        for (int j = 0; j < numConn; j++) {
            Pair tmpPair("a"+std::to_string(j), "b"+std::to_string(j));
            float thr = max(thrDistribution(generator), 0.0f);
            PairState tmpState(0, thr, 0, 0, 0, 0, 0, 0, 0);
            sum += pow(thr, 2);
            tmpConns.insert(std::pair<Pair, PairState>(tmpPair, tmpState));
        }
        cout << "norm: " << (optimizer.norm2(tmpConns)) << " \tsum: " << sum << "\n";
    }
    for (int i = 0; i<=320; i+=5) {
        float tmpthr = throughputForSingleConn(i);
        cout << "n: " << i << "\tthr: " << tmpthr << "\n";
    }

    Pair singlePair("a", "b");
    optimizer.clearConditions();
    int n0 = 10;
    // PairState singleState(0, throughputForSingleConn(n0), 0, 0, 0, n0, 0, 0, 0);
    // std::map<Pair, PairState> testState;
    // testState.insert(std::pair<Pair, PairState>(singlePair, singleState));
    // std::map<Pair, int> optDecision;
    // for (int i = 0; i<20; i++) {
    //     optimizer.step(testState, optDecision);
    //     cout << "actives: " << testState[singlePair].activeCount << "\tthr: " << testState[singlePair].throughput << "\tdecision: " << optDecision[singlePair] << std::endl;
    //     PairState singleState(0, throughputForSingleConn(optDecision[singlePair]), 0, 0, 0, optDecision[singlePair], 0, 0, 0);
    //     testState[singlePair] = singleState;

    //     cout << std::endl << std::endl << std::endl;
    // }

    optimizer = TCNOptimizer();
    PairState singleState2(0, throughputForSingleConn(n0), 0, 0, 0, n0, 0, 0, 0);
    std::map<Pair, PairState> testState2;
    testState2.insert(std::pair<Pair, PairState>(singlePair, singleState2));
    optimizer.setConditionForPair(singlePair, float(5*pow(2, 30)));
    std::map<Pair, int> optDecision2;
    for (int i = 0; i<1000; i++) {
        optimizer.step(testState2, optDecision2);
        cout << std::endl << std::endl <<  "actives: " << testState2[singlePair].activeCount << "\tthr: " << testState2[singlePair].throughput << "\tdecision: " << optDecision2[singlePair] << std::endl << std::endl << std::endl;
        PairState singleState2(0, throughputForSingleConn(optDecision2[singlePair]), 0, 0, 0, optDecision2[singlePair], 0, 0, 0);
        testState2[singlePair] = singleState2;

        cout << std::endl << std::endl << std::endl;
    }
    return 0;
}
