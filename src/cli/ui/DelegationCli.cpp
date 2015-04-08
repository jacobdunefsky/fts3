/*
 * DelegationCli.cpp
 *
 *  Created on: Mar 5, 2014
 *      Author: simonm
 */

#include "DelegationCli.h"

namespace fts3
{
namespace cli
{

DelegationCli::DelegationCli()
{
    // add commandline options specific for fts3-transfer-submit
    specific.add_options()
    ("id,I", po::value<std::string>(), "Delegation with ID as the delegation identifier.")
    ("expire,e", po::value<long>(), "Expiration time of the delegation in minutes.")
    ;
}

DelegationCli::~DelegationCli()
{

}


std::string DelegationCli::getDelegationId() const
{

    // check if destination was passed via command line options
    if (vm.count("id"))
        {
            return vm["id"].as<std::string>();
        }
    return "";
}

long DelegationCli::getExpirationTime() const
{

    if (vm.count("expire"))
        {
            return vm["expire"].as<long>();
        }
    return 0;
}

} /* namespace cli */
} /* namespace fts3 */
