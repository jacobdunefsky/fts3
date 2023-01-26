#ifndef TCN_OPTIMIZER_H
#define TCN_OPTIMIZER_H


class TCNOptimizer {

    private:
        int stepCount;
        float alpha;
        int maxMagnitudeAlphaDecay;
        float eta;
        float omega;
        int minOpt;
        int maxOpt;
        float maxExplorationProbability;
        float minExplorationProbability;
        int explorationDecaySteps;
        int decayStopLimit;
        int lastActiveChange;
        float perturbationStd;
        float explorationDeclineCoeff;
        std::string penaltyMethod;
        std::map<Pair, double> gradients;
        std::map<Pair, double> momentums;
        //std::map<Pair, PairState> states;
        std::map<Pair, float> conditions;
        std::map<Pair, PairState> lastState;
        std::set<Pair> activePipes;
        unsigned seed;
        std::default_random_engine generator;
        bool started;

    protected:
        float explorationProbability();

        //decides to wether take a random exploration or follow the
        //momentum
        bool explorationDecision();

        void gradientEstimate(std::map<Pair, PairState> &current);

        bool listsAreEqual(std::set<Pair>& lhs, std::map<Pair, PairState>& rhs, std::set<Pair>& rhsSet);

        void randomPerturbation(std::map<Pair, PairState>& activePairs, std::map<Pair, int>& perturbations);

        void randomStep(std::map<Pair, PairState> &conns, std::map<Pair, int> &decisions);

        void boundDecision(std::map<Pair, int> &rawNs);

        float alphaT();

        void gradientStep(std::map<Pair, PairState> &conns, std::map<Pair, int> &decisions);

        float penaltyValue(float barrier);

        float networkBarrier(alto::PathConstraints pc, std::map<Pair, PairState> &conns);

        float barrierValue(const Pair &pair, const PairState &state);

        float aggregatedBarrierPenalty(std::map<Pair, PairState> &conns);

    public:
        TCNOptimizer(std::string penaltyMethod = "linear",
            float alpha = 1e-9,
            int maxMagnitudeAlphaDecay = 1,
            float eta = 0.9,
            float omega = 2,
            int minOpt = 0,
            int maxOpt = 1000,
            float maxExplorationProbability = 0.2,
            float minExplorationProbability = 0.01,
            int explorationDecaySteps = 500,
            int decayStopLimit = 4000,
            int lastActiveChange = 0,
            float perturbationStd = 0.5,
            float explorationDeclineCoeff = 500
        );

        float norm2(std::map<Pair, PairState>& states);

        int expectInt(float input);

        float objective(std::map<Pair, PairState> &connStates);

        void step(std::map<Pair, PairState> &activeTCNPipes, std::map<Pair, int> &decisions);

        float getUtility(std::map<Pair, PairState> &connStates);

        void setConditionForPair(Pair pair, float limit);

        void setConditions(const std::map<Pair, float> &newConditions);

        void getConditions(std::map<Pair, float> &loadedConditions);

        float getAlpha();

        void getGradients(std::map<Pair, double> &loadedGradients);

        bool getExplorationDecision();

        float getExplorationProbability();

        void setLastState(std::map<Pair, PairState> &pushedLastStates);

        void clearConditions();

        // int getLastActiveChange();
};

#endif // TCN_OPTIMIZER_H
