#ifndef TCN_EVENT_LOOP_H
#define TCN_EVENT_LOOP_H

enum class TCNEventPhase {estTOld, estTNew, adjust};
enum class TCNEventType {measureUpdate, fileFinish};

typedef std::map<Pair, int> ConcurrencyVector;
typedef std::map<Pair, double> ThroughputVector;

struct TCNMeasureInfo {
	std::map<Pair, double> bytesSentVector;
	std::time_t measureTime;	
};

class TCNEventLoop {
public:
	TCNEventPhase phase;

	ConcurrencyVector prev_n;
	ConcurrencyVector cur_n;

	ConcurrencyVector decided_n;

	ConcurrencyVector n_old;
	Pair pertPair;
	ConcurrencyVector n_new;
	ConcurrencyVector n_target;

	ThroughputVector T_old;
	ThroughputVector T_new;

	std::vector<TCNMeasureInfo> measureInfos;

	//ThroughputVector T_new;
	
	std::time_t epochStartTime;
	std::time_t qosIntervalStartTime;

	// constants

	// if throughput variance is less than this, then our estimation
	// has converged
	double convergeVariance;
	// even if our measurement has converged, we still want to spend
	// a good amount of time running at our chosen throughput
	std::time_t estTOldMinTime;

	// functions
	
	TCNEventLoop(
		OptimizerDataSource *ds_,
		double convergeVariance_ = 1000,
		std::time_t estTOldMinTime_ = 100,
		TCNEventPhase phase_ = TCNEventPhase::estTOld
	);

	Pair choosePertPair(ThroughputVector n);
	ThroughputVector calculateTau(int index);
	ThroughputVector calculateTput(int index);
	double calculateTputVariance();
	double efficiencyFunction(ThroughputVector tau);
	double utilityFunction(
		ThroughputVector tau,
		ThroughputVector T,
		ThroughputVector T_target,
		double t_target,
		double dt
	);
	ConcurrencyVector gradStep();
	ThroughputVector constructTargetTput();
	void newQosInterval(std::time_t start);
	void setOptimizerDecision(ConcurrencyVector n);
	ConcurrencyVector step(TCNEventType type);
};

#endif