#include<iostream>
#include <cstdio>
#include <signal.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include "common/error.h"
#include "common/logger.h"
#include "config/serverconfig.h"
#include "server.h"
#include "db/generic/SingleDbInstance.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace FTS3_SERVER_NAMESPACE;
using namespace FTS3_COMMON_NAMESPACE;
using namespace db;

/* -------------------------------------------------------------------------- */
void fts3_initialize_db_backend() {
    std::string dbUserName = theServerConfig().get<std::string > ("DbUserName");
    std::string dbPassword = theServerConfig().get<std::string > ("DbPassword");
    std::string dbConnectString = theServerConfig().get<std::string > ("DbConnectString");

    try {
        db::DBSingleton::instance().getDBObjectInstance()->init(dbUserName, dbPassword, dbConnectString);
    } catch (Err_Custom& e) {
        exit(1);
    }    catch (...) {
        FTS3_COMMON_LOGGER_NEWLOG(ERR) << "Something is going on with the database, check username/password/connstring" << commit;
        exit(1);
    }
}

static int fexists(const char *filename) {
    struct stat buffer;
    if (stat(filename, &buffer) == 0) return 0;
    return -1;
}

int main(int argc, char** argv) {

    char hostname[MAXHOSTNAMELEN];
    gethostname(hostname, MAXHOSTNAMELEN);
    std::string hostVM(hostname);
    std::stringstream versionFTS;

    //get fts server version  
    FILE *in;
    char buff[512];

    in = popen("rpm -q --qf '%{VERSION}' fts-server", "r");

    while (fgets(buff, sizeof (buff), in) != NULL) {
        versionFTS << buff;
    }

    pclose(in);


    //get emi-version	 
    string versionEMI;
    ifstream myfile("/etc/emi-version");
    getline(myfile, versionEMI);

    //get fts server health state 
    const char *serverRunning = "/var/lock/subsys/fts-server";
    std::string serverStatus("");
    if (fexists(serverRunning) == 0) {
        serverStatus = "ok";
    }


    //get timestamps
    std::string getTimetamps = "stat -c %z /var/lock/subsys/fts-server | sed -e  's/ /T/g'  -e 's/.000000000T/Z/g'";
    std::stringstream timestamp;
    FILE *inTime;
    char buffTime[512];

    inTime = popen(getTimetamps.c_str(), "r");

    while (fgets(buffTime, sizeof (buffTime), inTime) != NULL) {
        timestamp << buffTime;
    }

    pclose(inTime);

    try {
        std::stringstream stream;
        const char *configfile = "/etc/fts3/fts3config";

        if (fexists(configfile) != 0) {
            std::cerr << "fts3 server config file doesn't exist" << std::endl;
            exit(1);
        }

        FTS3_CONFIG_NAMESPACE::theServerConfig().read(argc, argv);
        /*
	DO not connect to the db for now
	fts3_initialize_db_backend();
	*/

        //std::string publisherHost = theServerConfig().get<std::string > ("InfoPublisher");
        //if (publisherHost.compare("true") == 0) {
            std::string alias = theServerConfig().get<std::string > ("Alias");
            int port = theServerConfig().get<int>("Port");

            stream << "dn: GLUE2ServiceID=https://" << alias << ":" << port << "_org.glite.fts" << ",GLUE2GroupID=resource,o=glue" << "\n";
            stream << "GLUE2ServiceID: https://" << alias << ":" << port << "_org.glite.fts" << "\n";
            stream << "objectClass: GLUE2Service" << "\n";
            stream << "GLUE2ServiceType: org.glite.fts" << "\n";
            stream << "GLUE2ServiceQualityLevel: production" << "\n";
            stream << "GLUE2ServiceAdminDomainForeignKey: CERN-PROD" << "\n";

            stream << "dn: GLUE2EndpointID=" << hostVM << "_org.glite.fts" << ",GLUE2ServiceID=https://" << alias << ":" << port << "_org.glite.fts" << ",GLUE2GroupID=resource,o=glue" << "\n";
            stream << "objectClass: GLUE2Endpoint" << "\n";
            stream << "GLUE2EndpointID: " << hostVM << "_org.glite.fts" << "\n";
            stream << "GLUE2EndpointInterfaceVersion: " << versionFTS.str() << "\n";
            stream << "GLUE2EndpointQualityLevel: production" << "\n";
            stream << "GLUE2EndpointImplementationName: FTS" << "\n";
            stream << "GLUE2EndpointImplementationVersion: " << versionFTS.str() << "\n";
            stream << "GLUE2EntityOtherInfo: MiddlewareName=EMI" << "\n";
            stream << "GLUE2EntityOtherInfo: MiddlewareVersion=" << versionEMI << "\n";
            stream << "GLUE2EndpointInterfaceName: org.glite.fts" << "\n";
	    stream << "GLUE2EndpointURL: https://" << alias << ":" << port << "\n";	    
            stream << "GLUE2EndpointSemantics: https://svnweb.cern.ch/trac/fts3" << "\n";            
            //stream << "GLUE2EndpointWSDL: https://fts2run.cern.ch:8443/glite-data-transfer-fts/services/FileTransfer?wsdl
            stream << "GLUE2EndpointStartTime: " << timestamp.str() << "\n"; //  1970-01-01T00:00:00Z
            stream << "GLUE2EndpointHealthState: " << serverStatus << "\n";
            stream << "GLUE2EndpointServingState: production" << "\n";
            stream << "GLUE2EndpointServiceForeignKey: https://" << alias << ":" << port << "_org.glite.fts" << "\n";

            stream << "dn: GLUE2PolicyID=" << hostVM << "_fts3_policy" << ",GLUE2EndpointID=" << hostVM << "_org.glite.fts" << ",GLUE2ServiceID=https://" << alias << ":" << port << "_org.glite.fts" << ",GLUE2GroupID=resource,o=glue" << "\n";
            stream << "objectClass: GLUE2Policy" << "\n";
            stream << "objectClass: GLUE2AccessPolicy" << "\n";
            stream << "GLUE2PolicyID: " << hostVM << "_fts3_policy" << "\n";
            stream << "GLUE2EntityCreationTime: " << timestamp.str() << "\n";
            stream << "GLUE2PolicyScheme: org.glite.standard" << "\n";
	    stream << "GLUE2PolicyRule: ALL" << "\n";
            stream << "GLUE2AccessPolicyEndpointForeignKey: " << hostVM << "_org.glite.fts" << "\n";

            std::cout << stream.str() << std::endl;
        //}
    }    catch (...) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
