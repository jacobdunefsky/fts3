#include <cstdio>
#include <iostream>

#include "alto_client.hpp"

using std::cout;
using std::endl;

int
main(int argc, char **argv)
{
  // Ensure one argument is given

  if (argc != 4) {
    fprintf(stderr, "Usage: %s <url> <flows> <props>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  std::string flows_str = argv[2];
  std::list<EndpointFlow> flows;

  size_t pos = 0;
  while ((pos = flows_str.find(";")) != std::string::npos) {
    EndpointFlow flow = EndpointFlow();
    std::string flow_str = flows_str.substr(0, pos);
    size_t arrow_pos = flow_str.find("->");

    size_t src_pos = 0;
    std::string src_str = flow_str.substr(0, arrow_pos);
    while ((src_pos = src_str.find(",")) != std::string::npos) {
      flow.srcs.push_back(src_str.substr(0, src_pos));
      src_str.erase(0, src_pos + 1);
    }
    flow.srcs.push_back(src_str);
    flow_str.erase(0, arrow_pos + 2);

    size_t dst_pos = 0;
    std::string dst_str = flow_str;
    while ((dst_pos = dst_str.find(",")) != std::string::npos) {
      flow.dsts.push_back(dst_str.substr(0, dst_pos));
      dst_str.erase(0, dst_pos + 1);
    }
    flow.dsts.push_back(dst_str);

    flows.push_back(flow);
    flows_str.erase(0, pos + 1);
  }
  
  EndpointFlow flow = EndpointFlow();
  size_t arrow_pos = flows_str.find("->");
  size_t src_pos = 0;
  std::string src_str = flows_str.substr(0, arrow_pos);
  while ((src_pos = src_str.find(",")) != std::string::npos) {
    flow.srcs.push_back(src_str.substr(0, src_pos));
    src_str.erase(0, src_pos + 1);
  }
  flow.srcs.push_back(src_str);
  flows_str.erase(0, arrow_pos + 2);

  size_t dst_pos = 0;
  std::string dst_str = flows_str;
  while ((dst_pos = dst_str.find(",")) != std::string::npos) {
    flow.dsts.push_back(dst_str.substr(0, dst_pos));
    dst_str.erase(0, dst_pos + 1);
  }
  flow.dsts.push_back(dst_str);

  flows.push_back(flow);

  std::string props_str = argv[3];
  std::list<std::string> props;

  pos = 0;
  while ((pos = props_str.find(",")) != std::string::npos) {
    props.push_back(props_str.substr(0, pos));
    props_str.erase(0, pos + 1);
  }
  props.push_back(props_str);

  PathConstraints pc;
  pc  = get_path_constraints(argv[1], flows, props);

  if (!pc.status) {
      exit(EXIT_FAILURE);
  }

  std::cout << "Dump Path Constraints:" << std::endl;

  dump_path_constraints(pc);

  return 0;
}

