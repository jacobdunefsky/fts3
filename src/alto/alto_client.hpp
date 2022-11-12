#ifndef ALTO_CLIENT_H
#define ALTO_CLIENT_H

#include <cstdio>
#include <iostream>
#include <sstream>
#include <map>
#include <list>
#include <string>
#include <vector>
#include "multipart_parser.h"

using std::map;
using std::pair;
using std::string;
using std::vector;

namespace alto {

struct PartMessage {
  std::map<std::string, std::string> headers;
  std::string data;
};

struct MultipartMessage {
  std::map<std::string, PartMessage> parts;
  std::string root;
};

struct PathConstraints {
  std::vector<std::pair<std::string, std::string>> flow_map;

  std::vector<std::vector<int>> A;
  std::vector<double> b;

  bool status;

  PathConstraints(): status(false) {}
};

class AltoMultipartParser {
public:
  AltoMultipartParser(const std::string & boundary);

  AltoMultipartParser(const std::string & boundary,
                      const std::string & start);

  ~AltoMultipartParser();

  int parse(const std::string & data);

  MultipartMessage get() const;

private:
  int init_parser(const std::string & boundary,
                  const std::string & start);

  static int
  on_header_field(multipart_parser *parser, const char *at, size_t length);

  static int
  on_header_value(multipart_parser *parser, const char *at, size_t length);

  static int
  on_part_data(multipart_parser *parser, const char* at, size_t length);

  static int
  on_part_data_begin(multipart_parser *parser);

  static int
  on_headers_complete(multipart_parser *parser);

  static int
  on_part_data_end(multipart_parser *parser);

  multipart_parser *m_parser;
  multipart_parser_settings m_callbacks;
  bool m_root_first;
  MultipartMessage m_message;
  PartMessage m_current;
  std::string m_cur_header;
};

// Structure for Path Vector request

struct EndpointFlow
{
  std::list<std::string> srcs;
  std::list<std::string> dsts;
};

std::string build_path_vector_request(std::list<alto::EndpointFlow> flows, std::list<std::string> props);

PathConstraints get_path_constraints(const char *uri, std::list<EndpointFlow> flows, std::list<std::string> props);

void dump_path_constraints(const PathConstraints & pc);

}
#endif
