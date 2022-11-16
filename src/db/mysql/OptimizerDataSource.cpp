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

#include <numeric>
#include <string>
#include <algorithm>
#include <vector>
#include <map>
#include "MySqlAPI.h"
#include "db/generic/DbUtils.h"
#include "db/generic/ThrInfo.h"
#include "common/Exceptions.h"
#include "common/Logger.h"
#include "sociConversions.h"

using namespace db;
using namespace fts3;
using namespace fts3::common;
using namespace fts3::optimizer;

// Set the new number of actives
static void setNewOptimizerValue(soci::session &sql,
    const Pair &pair, int optimizerDecision, double ema)
{
    sql.begin();
    sql <<
        "INSERT INTO t_optimizer (source_se, dest_se, active, ema, datetime) "
        "VALUES (:source, :dest, :active, :ema, UTC_TIMESTAMP()) "
        "ON DUPLICATE KEY UPDATE "
        "   active = :active, ema = :ema, datetime = UTC_TIMESTAMP()",
        soci::use(pair.source, "source"), soci::use(pair.destination, "dest"),
        soci::use(optimizerDecision, "active"), soci::use(ema, "ema");
    sql.commit();
}

// Insert the optimizer decision into the historical table, so we can follow
// the progress
static void updateOptimizerEvolution(soci::session &sql,
    const Pair &pair, int active, int diff, const std::string &rationale, const PairState &newState)
{
    try {
        sql.begin();
        sql << " INSERT INTO t_optimizer_evolution "
            " (datetime, source_se, dest_se, "
            "  ema, active, throughput, success, "
            "  filesize_avg, filesize_stddev, "
            "  actual_active, queue_size, "
            "  rationale, diff) "
            " VALUES "
            " (UTC_TIMESTAMP(), :source, :dest, "
            "  :ema, :active, :throughput, :success, "
            "  :filesize_avg, :filesize_stddev, "
            "  :actual_active, :queue_size, "
            "  :rationale, :diff)",
            soci::use(pair.source), soci::use(pair.destination),
            soci::use(newState.ema), soci::use(active), soci::use(newState.throughput), soci::use(newState.successRate),
            soci::use(newState.filesizeAvg), soci::use(newState.filesizeStdDev),
            soci::use(newState.activeCount), soci::use(newState.queueSize),
            soci::use(rationale), soci::use(diff);
        sql.commit();
    }
    catch (std::exception &e) {
        sql.rollback();
        FTS3_COMMON_LOGGER_NEWLOG(WARNING) << "Could not update the optimizer evolution: " << e.what() << commit;
    }
    catch (...) {
        sql.rollback();
        FTS3_COMMON_LOGGER_NEWLOG(WARNING) << "Could not update the optimizer evolution: unknown reason" << commit;
    }
}


// Count how many files are in the given state for the given pair
// Only non terminal!
static int getCountInState(soci::session &sql, const Pair &pair, const std::string &state)
{
    int count = 0;

    sql << "SELECT count(*) FROM t_file "
    "WHERE source_se = :source AND dest_se = :dest_se AND file_state = :state",
    soci::use(pair.source), soci::use(pair.destination), soci::use(state), soci::into(count);

    return count;
}


class MySqlOptimizerDataSource: public OptimizerDataSource {
private:
    soci::session sql;

public:
    MySqlOptimizerDataSource(soci::connection_pool* connectionPool): sql(*connectionPool)
    {
    }

    ~MySqlOptimizerDataSource() {
    }

    std::list<Pair> getActivePairs(void) {
        std::list<Pair> result;

        soci::rowset<soci::row> rs = (sql.prepare <<
            "SELECT DISTINCT source_se, dest_se "
            "FROM t_file "
            "WHERE file_state IN ('ACTIVE', 'SUBMITTED') "
            "GROUP BY source_se, dest_se, file_state "
            "ORDER BY NULL"
        );

        for (auto i = rs.begin(); i != rs.end(); ++i) {
            result.push_back(Pair(i->get<std::string>("source_se"), i->get<std::string>("dest_se")));
        }

        return result;
    }


    OptimizerMode getOptimizerMode(const std::string &source, const std::string &dest) {
        return getOptimizerModeInner(sql, source, dest);
    }

    void getPairLimits(const Pair &pair, Range *range, StorageLimits *limits) {
        soci::indicator nullIndicator;

        limits->source = limits->destination = 0;
        limits->throughputSource = 0;
        limits->throughputDestination = 0;

        // Storage limits
        sql <<
            "SELECT outbound_max_throughput, outbound_max_active FROM ("
            "   SELECT outbound_max_throughput, outbound_max_active FROM t_se WHERE storage = :source UNION "
            "   SELECT outbound_max_throughput, outbound_max_active FROM t_se WHERE storage = '*' "
            ") AS se LIMIT 1",
            soci::use(pair.source),
            soci::into(limits->throughputSource, nullIndicator), soci::into(limits->source, nullIndicator);

        sql <<
            "SELECT inbound_max_throughput, inbound_max_active FROM ("
            "   SELECT inbound_max_throughput, inbound_max_active FROM t_se WHERE storage = :dest UNION "
            "   SELECT inbound_max_throughput, inbound_max_active FROM t_se WHERE storage = '*' "
            ") AS se LIMIT 1",
        soci::use(pair.destination),
        soci::into(limits->throughputDestination, nullIndicator), soci::into(limits->destination, nullIndicator);

        // Link working range
        soci::indicator isNullMin, isNullMax;
        sql <<
            "SELECT configured, min_active, max_active FROM ("
            "   SELECT 1 AS configured, min_active, max_active FROM t_link_config WHERE source_se = :source AND dest_se = :dest UNION "
            "   SELECT 1 AS configured, min_active, max_active FROM t_link_config WHERE source_se = :source AND dest_se = '*' UNION "
            "   SELECT 1 AS configured, min_active, max_active FROM t_link_config WHERE source_se = '*' AND dest_se = :dest UNION "
            "   SELECT 0 AS configured, min_active, max_active FROM t_link_config WHERE source_se = '*' AND dest_se = '*' "
            ") AS lc LIMIT 1", 
            soci::use(pair.source, "source"), soci::use(pair.destination, "dest"),
            soci::into(range->specific), soci::into(range->min, isNullMin), soci::into(range->max, isNullMax);

        if (isNullMin == soci::i_null || isNullMax == soci::i_null) {
            range->min = range->max = 0;
        }
    }

    int getOptimizerValue(const Pair &pair) {
        soci::indicator isCurrentNull;
        int currentActive = 0;

        sql << "SELECT active FROM t_optimizer "
            "WHERE source_se = :source AND dest_se = :dest_se",
            soci::use(pair.source),soci::use(pair.destination),
            soci::into(currentActive, isCurrentNull);

        if (isCurrentNull == soci::i_null) {
            currentActive = 0;
        }
        return currentActive;
    }

    std::string getPairProject(cost Pair &pair) 
    {
        std::string project_id;

        soci::indicator isNullProject;
        sql <<
            "SELECT proj_id FROM (SELECT proj_id" 
            " FROM t_projects" 
            "   WHERE vo_name = :qvo AND source_se = :qsrcSe AND dest_se = :qdstSe" 
            " UNION" 
            " SELECT proj_id" 
            " FROM t_projects"
            "   WHERE (vo_name = :qvo AND source_se = :qsrcSe) OR"
            "      (vo_name = :qvo AND dest_se = :qdstSe)"
            " UNION" 
            " SELECT proj_id"
            " FROM t_projects"
            " WHERE (vo_name = :qvo ) ) as tpr"
            " LIMIT 1", 
            soci::use(pair.source, "qsrcSe"), 
            soci::use(pair.destination, "qdstSe"),
            spici::use(pair.vo, "qvo"),
            soci::into(project_id, isNullProject);

        if (isNullProject == soci::i_null) {
            project_id = "vo1";
        }
    
        return project_id;
    }

    /*void getProjectTransfers(const std::string &proj_id, std::vector<Pair> *pairs) 
    {
        pairs.clear();
        std::string psrcSe, pdstSe, pvo, 
        psrcSe = pdstSe = pvo = "";
        sql <<
            "SELECT vo_name, source_se, dest_se"
            " FROM t_projects"
            " WHERE proj_id = :projix",
        soci::use(proj_id, "projix"),
        soci::into(pvo), soci::into(psrcSe), soci::into(pdstSe);

        //find 
        if (psrcSe == "*" && pdstSe == "*") 
        {

        } 
        else if (psrcSe == "*")
        {

        } 
        else if (dstSe == "*")
        {

        } 
        else 
        {

        }

    }*/

    void getPairLinks(const Pair &pair, std::vector<std::string> &link_ids) 
    {
        link_ids.clear();

        soci::rowset<soci::row> links = (sql.prepare <<
        "SELECT plink_id"
        " FROM t_routing "
        " WHERE "
        "   source_se = :sourceSe AND dest_se = :destSe",
        soci::use(pair.source, "sourceSe"), 
        soci::use(pair.destination, "destSe");

        for (auto j = links.begin(); j != links.end(); ++j) {
            auto plink_id = j->get<std::string>("plink_id");

            link_ids.push_back(plink_id);
        } 

        return;
    }

    void getPairBWLimits(const Pair &pair, std::map<std::string, int64_t> &link_limits)
    {
        link_limits.clear();

        std::vector<std::string> plinks;

        getPairLinks(pair, plinks);

        auto proj_id = getPairProject(pair);

        soci::rowset<soci::row> limits = (sql.prepare <<
        " SELECT plink_id, max_throughput"
        " FROM t_bounds"
        " WHERE proj_id = :projid",
        soci::use(proj_id, "projid"));

        for (auto j = limits.begin(); j != limits.end(); ++j) {
            auto plink_id = j->get<std::string>("plink_id");
            auto limit = j->get<long long>(limit, 0.0);

            if ( std::find(plinks.begin(), plinks.end(), plink_id) != plinks.end()) 
            {
                link_limits.insert({plink_id, limit});
            }
        } 

        return;
    }

    /*void getSimilarityVector(std::vector<std::string> &routing1, std::vector<std::string> &routing2, 
                                std::vector<bool> *similarity) 
    {
        *similarity.clear();
        for (const auto &plink_id : routing1)
        {
            if (std::find(routing2.begin(), routing2.end(), plink_id) != routing2.end()) 
            {
                *similarity.push_back(plink_id);
            }
        }

        return;
    }*/


    void getPairLimitOnPLinks(const Pair &pair, 
                        time_t windowStart, 
                        std::map<std::string, TransferredStat> &thr_map) 
    {
        thr_map.clear();

        static struct tm nulltm = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

        time_t now = time(NULL);
		time_t total_seconds = now - windowStart;
        //select all physical links that this queue (project+pipe) is traversing.
        //std::vector<std::string> plinks;
        //getPairLinks(pair, plinks);
        //std::string proj_id=getPairProject(pair);

        //get and set the limits - initalize the transferred bytes 
        std::map<std::string, int64_t> link_limits; 
        getPairBWLimits(pair, link_limits);

        std::map<std::string, int64_t>::iterator it;

        for (it = link_limits.begin(); it != link_limits.end(); it++)
        {
            thr_map.insert(std::make_pair(it->first, TransferredStat(0, it->second)));
        }

        soci::rowset<soci::row> transfers = (sql.prepare <<
        " SELECT t2.plink_id, t3.start_time, t3.finish_time, t3.transferred, t3.filesize"
        " FROM t_routing t1"
        " INNER JOIN t_routing t2 ON t2.plink_id = t1.plink_id"
        " INNER JOIN t_file t3 ON t3.source_se=t2.source_se AND t3.dest_se = t2.dest_se"
        " WHERE t3.source_se = :sourceSe"
        "      AND t3.dest_se = :destSe"
        "      AND t3.vo_name = :vo_name"
        "      AND t3.file_state = 'ACTIVE'"
        " UNION ALL"
        " SELECT t2.plink_id, t3.start_time, t3.finish_time, t3.transferred, t3.filesize"
        " FROM t_routing t1"
        " INNER JOIN t_routing t2 ON t2.plink_id = t1.plink_id"
        " INNER JOIN t_file t3 ON t3.source_se=t2.source_se AND t3.dest_se = t2.dest_se"
        " WHERE t3.source_se = :sourceSe"
        "      AND t3.dest_se = :destSe" 
        "      AND t3.vo_name = :vo_name" 
        "      AND t3.file_state IN  ('FINISHED', 'ARCHIVING')"
        "      AND t3.finish_time >= (UTC_TIMESTAMP() - INTERVAL :interval SECOND)",
        soci::use(pair.source, "sourceSe"), soci::use(pair.destination, "destSe"), soci::use(pair.vo_name, "vo_name"),
        soci::use(totalSeconds, "interval"));

        for (auto j = transfers.begin(); j != transfers.end(); ++j) {
            auto plink_id = j->get<std::string>("plink_id");
            auto transferred = j->get<long long>("transferred", 0.0);
            auto filesize = j->get<long long>("filesize", 0.0);
            auto starttm = j->get<struct tm>("start_time");
            auto endtm = j->get<struct tm>("finish_time", nulltm);

            time_t start = timegm(&starttm);
            time_t end = timegm(&endtm);
            time_t periodInWindow = 0;
            double bytesInWindow = 0;

            // Not finish information
            if (endtm.tm_year <= 0) {
				bytesInWindow = transferred
            }
            // Finished
            else {
				bytesInWindow = filesize;
            }

            std::map<std::string, TrnasferredStat>::iterator itf = thr_map.find(plink_id); 
            if (itf != m.end())
            {
                itf->second.transferred += bytesInWindow};
            }
        }
		
        return;
    }

	int64_t getTransferredInfo(const Pair &pair, time_t windowStart)
    {
        static struct tm nulltm = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

        *throughput = *filesizeAvg = *filesizeStdDev = 0;

        time_t now = time(NULL);
		time_t total_seconds = now-windowStart;

        soci::rowset<soci::row> transfers = (sql.prepare <<
        "SELECT start_time, finish_time, transferred, filesize "
        " FROM t_file "
        " WHERE "
        "   source_se = :sourceSe AND dest_se = :destSe AND vo_name = :vo_name AND file_state = 'ACTIVE' "
        "UNION ALL "
        "SELECT start_time, finish_time, transferred, filesize "
        " FROM t_file USE INDEX(idx_finish_time)"
        " WHERE "
        "   source_se = :sourceSe AND dest_se = :destSe AND vo_name = :vo_name "
        "   AND file_state IN ('FINISHED', 'ARCHIVING') AND finish_time >= (UTC_TIMESTAMP() - INTERVAL :interval SECOND)",
        soci::use(pair.source, "sourceSe"), soci::use(pair.destination, "destSe"), soci::use(pair.vo_name, "vo_name"),
        soci::use(totalSeconds, "interval"));

        int64_t totalBytes = 0;
        std::vector<int64_t> filesizes;

        for (auto j = transfers.begin(); j != transfers.end(); ++j) {
            auto transferred = j->get<long long>("transferred", 0.0);
            auto filesize = j->get<long long>("filesize", 0.0);
            auto starttm = j->get<struct tm>("start_time");
            auto endtm = j->get<struct tm>("finish_time", nulltm);

            time_t start = timegm(&starttm);
            time_t end = timegm(&endtm);
            time_t periodInWindow = 0;
            double bytesInWindow = 0;

            // Not finish information
            if (endtm.tm_year <= 0) {
				bytesInWindow = transferred
            }
            // Finished
            else {
				bytesInWindow = filesize;
            }

            totalBytes += bytesInWindow;
        }
		return totalBytes;
    }
   

    void getThroughputInfo(const Pair &pair, const boost::posix_time::time_duration &interval,
        double *throughput, double *filesizeAvg, double *filesizeStdDev)
    {
        static struct tm nulltm = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

        *throughput = *filesizeAvg = *filesizeStdDev = 0;

        time_t now = time(NULL);
        time_t windowStart = now - interval.total_seconds();

        soci::rowset<soci::row> transfers = (sql.prepare <<
        "SELECT start_time, finish_time, transferred, filesize "
        " FROM t_file "
        " WHERE "
        "   source_se = :sourceSe AND dest_se = :destSe AND file_state = 'ACTIVE' "
        "UNION ALL "
        "SELECT start_time, finish_time, transferred, filesize "
        " FROM t_file USE INDEX(idx_finish_time)"
        " WHERE "
        "   source_se = :sourceSe AND dest_se = :destSe "
        "   AND file_state IN ('FINISHED', 'ARCHIVING') AND finish_time >= (UTC_TIMESTAMP() - INTERVAL :interval SECOND)",
        soci::use(pair.source, "sourceSe"), soci::use(pair.destination, "destSe"),
        soci::use(interval.total_seconds(), "interval"));

        int64_t totalBytes = 0;
        std::vector<int64_t> filesizes;

        for (auto j = transfers.begin(); j != transfers.end(); ++j) {
            auto transferred = j->get<long long>("transferred", 0.0);
            auto filesize = j->get<long long>("filesize", 0.0);
            auto starttm = j->get<struct tm>("start_time");
            auto endtm = j->get<struct tm>("finish_time", nulltm);

            time_t start = timegm(&starttm);
            time_t end = timegm(&endtm);
            time_t periodInWindow = 0;
            double bytesInWindow = 0;

            // Not finish information
            if (endtm.tm_year <= 0) {
                periodInWindow = now - std::max(start, windowStart);
                long duration = now - start;
                if (duration > 0) {
                    bytesInWindow = double(transferred / duration) * periodInWindow;
                }
            }
            // Finished
            else {
                periodInWindow = end - std::max(start, windowStart);
                long duration = end - start;
                if (duration > 0 && filesize > 0) {
                    bytesInWindow = double(filesize / duration) * periodInWindow;
                }
                else if (duration <= 0) {
                    bytesInWindow = filesize;
                }
            }

            totalBytes += bytesInWindow;
            if (filesize > 0) {
                filesizes.push_back(filesize);
            }
        }

        *throughput = totalBytes / interval.total_seconds();
        // Statistics on the file size
        if (!filesizes.empty()) {
            for (auto i = filesizes.begin(); i != filesizes.end(); ++i) {
                *filesizeAvg += *i;
            }
            *filesizeAvg /= filesizes.size();

            double deviations = 0.0;
            for (auto i = filesizes.begin(); i != filesizes.end(); ++i) {
                deviations += pow(*filesizeAvg - *i, 2);

            }
            *filesizeStdDev = sqrt(deviations / filesizes.size());
        }
    }

    time_t getAverageDuration(const Pair &pair, const boost::posix_time::time_duration &interval) {
        double avgDuration = 0.0;
        soci::indicator isNullAvg = soci::i_ok;

        sql << "SELECT AVG(tx_duration) FROM t_file USE INDEX(idx_finish_time)"
            " WHERE source_se = :source AND dest_se = :dest AND file_state IN ('FINISHED', 'ARCHIVING') AND "
            "   tx_duration > 0 AND tx_duration IS NOT NULL AND "
            "   finish_time > (UTC_TIMESTAMP() - INTERVAL :interval SECOND) LIMIT 1",
            soci::use(pair.source), soci::use(pair.destination), soci::use(interval.total_seconds()),
            soci::into(avgDuration, isNullAvg);

        return avgDuration;
    }

    double getSuccessRateForPair(const Pair &pair, const boost::posix_time::time_duration &interval,
        int *retryCount) {
        soci::rowset<soci::row> rs = (sql.prepare <<
            "SELECT file_state, retry, current_failures AS recoverable FROM t_file USE INDEX(idx_finish_time)"
            " WHERE "
            "      source_se = :source AND dest_se = :dst AND "
            "      finish_time > (UTC_TIMESTAMP() - interval :calculateTimeFrame SECOND) AND "
            "file_state <> 'NOT_USED' ",
            soci::use(pair.source), soci::use(pair.destination), soci::use(interval.total_seconds())
        );

        int nFailedLastHour = 0;
        int nFinishedLastHour = 0;

        // we need to exclude non-recoverable errors so as not to count as failures and affect efficiency
        *retryCount = 0;
        for (auto i = rs.begin(); i != rs.end(); ++i)
        {
            const int retryNum = i->get<int>("retry", 0);
            const bool isRecoverable = i->get<bool>("recoverable", false);
            const std::string state = i->get<std::string>("file_state", "");

            // Recoverable FAILED
            if (state == "FAILED" && isRecoverable) {
                ++nFailedLastHour;
            }
            // Submitted, with a retry set
            else if (state == "SUBMITTED" && retryNum) {
                ++nFailedLastHour;
                *retryCount += retryNum;
            }
            // FINISHED
            else if (state == "FINISHED" || state == "ARCHIVING") {
                ++nFinishedLastHour;
            }
        }

        // Round up efficiency
        int nTotal = nFinishedLastHour + nFailedLastHour;
        if (nTotal > 0) {
            return ceil((nFinishedLastHour * 100.0) / nTotal);
        }
        // If there are no terminal, use 100% success rate rather than 0 to avoid
        // the optimizer stepping back
        else {
            return 100.0;
        }
    }

    int getActive(const Pair &pair) {
        return getCountInState(sql, pair, "ACTIVE");
    }

    int getSubmitted(const Pair &pair) {
        return getCountInState(sql, pair, "SUBMITTED");
    }

    double getThroughputAsSource(const std::string &se) {
        double throughput = 0;
        soci::indicator isNull;

        sql <<
            "SELECT SUM(throughput) FROM t_file "
            "WHERE source_se= :name AND file_state='ACTIVE' AND throughput IS NOT NULL",
            soci::use(se), soci::into(throughput, isNull);

        return throughput;
    }

    double getThroughputAsDestination(const std::string &se) {
        double throughput = 0;
        soci::indicator isNull;

        sql << "SELECT SUM(throughput) FROM t_file "
               "WHERE dest_se= :name AND file_state='ACTIVE' AND throughput IS NOT NULL",
            soci::use(se), soci::into(throughput, isNull);

        return throughput;
    }

    void storeOptimizerDecision(const Pair &pair, int activeDecision,
        const PairState &newState, int diff, const std::string &rationale) {

        setNewOptimizerValue(sql, pair, activeDecision, newState.ema);
        updateOptimizerEvolution(sql, pair, activeDecision, diff, rationale, newState);
    }

    void storeOptimizerStreams(const Pair &pair, int streams) {
        sql.begin();

        sql << "UPDATE t_optimizer "
               "SET nostreams = :nostreams, datetime = UTC_TIMESTAMP() "
               "WHERE source_se = :source AND dest_se = :dest",
            soci::use(pair.source, "source"), soci::use(pair.destination, "dest"),
            soci::use(streams, "nostreams");

        sql.commit();
    }
};


OptimizerDataSource *MySqlAPI::getOptimizerDataSource()
{
    return new MySqlOptimizerDataSource(connectionPool);
}
