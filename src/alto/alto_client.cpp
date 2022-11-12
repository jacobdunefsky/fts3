#include <cstdio>
#include <iostream>
#include <sstream>
#include <curl/curl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <boost/regex.hpp>
#include "multipart_parser.h"
#include "alto_client.hpp"
#include "json/json.h"

using std::map;
using std::pair;
using std::make_pair;
using std::string;
using std::vector;
using std::cout;
using std::endl;
using std::stringstream;


namespace alto {

//
// Get IP address by DNS lookup
//

static std::string get_host_ip(const std::string& hostname)
{
  struct addrinfo *res;
  int error = getaddrinfo(hostname.c_str(), NULL, NULL, &res);
  if (error) {
    return "ipv4:127.0.0.1";
  }

  std::string addr_str;
  switch (res->ai_family) {
  case AF_INET6:
    addr_str = "ipv6:";
    break;
  default:
    addr_str = "ipv4:";
    break;
  }
  struct sockaddr_in *addr;
  addr = (struct sockaddr_in *) res->ai_addr;
  addr_str += std::string(inet_ntoa((struct in_addr) addr->sin_addr));
  return addr_str;
}

// Build Path Vector request

std::string build_path_vector_request(std::list<EndpointFlow> flows, std::list<std::string> props)
{
  Json::Value request;
  Json::Value &cost_type = request["cost-type"];

  cost_type["cost-metric"] = "ane-path";
  cost_type["cost-mode"] = "array";

  Json::Value &flows_json =  request["endpoint-flows"];
  int idx = 0;
  for (auto it = flows.begin(); it != flows.end(); ++it) {
    Json::Value &flow = flows_json[idx];

    Json::Value &srcs = flow["srcs"];
    int src_idx = 0;
    for (auto src_it = it->srcs.begin(); src_it != it->srcs.end(); ++src_it) {
      srcs[src_idx] = get_host_ip(*src_it);
      ++src_idx;
    }

    Json::Value &dsts = flow["dsts"];
    int dst_idx = 0;
    for (auto dst_it = it->dsts.begin(); dst_it != it->dsts.end(); ++dst_it) {
      dsts[dst_idx] = get_host_ip(*dst_it);
      ++dst_idx;
    }

    ++idx;
  }

  Json::Value &prop_names = request["ane-property-names"];
  int prop_idx = 0;
  for (auto p = props.begin(); p != props.end(); ++p) {
    prop_names[prop_idx] = *p;
  }

  Json::StreamWriterBuilder wbuilder;
  wbuilder["indentation"] = "  ";
  return Json::writeString(wbuilder, request);
}

//
//  libcurl variables for error strings and returned data

static char errorBuffer[CURL_ERROR_SIZE];
static std::string buffer;

//
//  libcurl write callback function
//

static int
writer(char *data, size_t size, size_t nmemb, std::string *writerData)
{
  if (writerData == NULL)
    return 0;

  writerData->append(data, size * nmemb);

  return size * nmemb;
}

//
//  libcurl connection initialization
//

static bool init(CURL *&conn, char *url, struct curl_slist *headers, const char *body)
{
  CURLcode code;

  conn = curl_easy_init();

  if (conn == NULL) {
    fprintf(stderr, "Failed to create CURL connection\n");
    exit(EXIT_FAILURE);
  }

  code = curl_easy_setopt(conn, CURLOPT_ERRORBUFFER, errorBuffer);
  if(code != CURLE_OK) {
    fprintf(stderr, "Failed to set error buffer [%d]\n", code);
    return false;
  }

  code = curl_easy_setopt(conn, CURLOPT_HTTPHEADER, headers);
  if(code != CURLE_OK) {
    fprintf(stderr, "Failed to set HTTP headers [%d]\n", code);
    return false;
  }

  code = curl_easy_setopt(conn, CURLOPT_POSTFIELDS, body);
  if(code != CURLE_OK) {
    fprintf(stderr, "Failed to set HTTP request body [%d]\n", code);
    return false;
  }

  code = curl_easy_setopt(conn, CURLOPT_URL, url);
  if(code != CURLE_OK) {
    fprintf(stderr, "Failed to set URL [%s]\n", errorBuffer);
    return false;
  }

  code = curl_easy_setopt(conn, CURLOPT_FOLLOWLOCATION, 1L);
  if(code != CURLE_OK) {
    fprintf(stderr, "Failed to set redirect option [%s]\n", errorBuffer);
    return false;
  }

  code = curl_easy_setopt(conn, CURLOPT_WRITEFUNCTION, writer);
  if(code != CURLE_OK) {
    fprintf(stderr, "Failed to set writer [%s]\n", errorBuffer);
    return false;
  }

  code = curl_easy_setopt(conn, CURLOPT_WRITEDATA, &buffer);
  if(code != CURLE_OK) {
    fprintf(stderr, "Failed to set write data [%s]\n", errorBuffer);
    return false;
  }

  return true;
}

//
// Handle regex error
//

void show_regex_error(const boost::regex_error& e) {
  std::string err_message = e.what();

#define CASE(type, msg)                                                 \
  case boost::regex_constants::type: err_message = err_message + " (" + #type + "):\n  " + msg; \
    break
  switch (e.code()) {
    CASE(error_collate, "The expression contains an invalid collating element name");
    CASE(error_ctype, "The expression contains an invalid character class name");
    CASE(error_escape, "The expression contains an invalid escaped character or a trailing escape");
    CASE(error_backref, "The expression contains an invalid back reference");
    CASE(error_brack, "The expression contains mismatched square brackets ('[' and ']')");
    CASE(error_paren, "The expression contains mismatched parentheses ('(' and ')')");
    CASE(error_brace, "The expression contains mismatched curly braces ('{' and '}')");
    CASE(error_badbrace, "The expression contains an invalid range in a {} expression");
    CASE(error_range, "The expression contains an invalid character range (e.g. [b-a])");
    CASE(error_space, "There was not enough memory to convert the expression into a finite state machine");
    CASE(error_badrepeat, "one of *?+{ was not preceded by a valid regular expression");
    CASE(error_complexity, "The complexity of an attempted match exceeded a predefined level");
    CASE(error_stack, "There was not enough memory to perform a match");
  }
#undef CASE

  /* std::cerr */ std::cout << err_message << ". \n\n";
}

//
// Handle messages
//

int
get_field(const std::string & ctype,
          const std::string & field, std::string & value)
{
  std::cerr << "Parse regex pattern: " << field + "=([^;]+);" << std::endl;
  try {
    boost::regex re{field + "=([^;]+);"};
    std::cerr << "regex pattern ready" << std::endl;
    std::string ctype_ = ctype + ";";
    boost::smatch matches;
    if (boost::regex_search(ctype_, matches, re)) {
      value = matches[1];
      return 0;
    }
  }
  catch (const boost::regex_error& e) {
    show_regex_error(e);
    return 0;
  }
  return 1;
}

// Begin of AltoMultipartParser

AltoMultipartParser::AltoMultipartParser(const std::string & boundary): m_root_first(true)
{
  init_parser("--" + boundary, "");
}

AltoMultipartParser::AltoMultipartParser(const std::string & boundary,
                      const std::string & start): m_root_first(false)
{
  init_parser("--" + boundary, start);
}

AltoMultipartParser::~AltoMultipartParser()
{
  multipart_parser_free(m_parser);
}

int AltoMultipartParser::parse(const std::string & data) {
  return multipart_parser_execute(m_parser, data.c_str(), data.length());
}

MultipartMessage AltoMultipartParser::get() const {
  return m_message;
}

int AltoMultipartParser::init_parser(const std::string & boundary,
                const std::string & start) {
  memset(&m_callbacks, 0, sizeof(multipart_parser_settings));
  m_callbacks.on_header_field = &on_header_field;
  m_callbacks.on_header_value = &on_header_value;
  m_callbacks.on_part_data = &on_part_data;
  m_callbacks.on_part_data_begin = &on_part_data_begin;
  m_callbacks.on_headers_complete = &on_headers_complete;
  m_callbacks.on_part_data_end = &on_part_data_end;

  m_parser = multipart_parser_init(boundary.c_str(), &m_callbacks);
  multipart_parser_set_data(m_parser, this);
  m_message.root = start;

  return 0;
}

int
AltoMultipartParser::on_header_field(multipart_parser *parser, const char *at, size_t length) {
  auto self = (AltoMultipartParser*) multipart_parser_get_data(parser);
  self->m_cur_header = std::string(at, length);
  std::cout << "on header field: " << self->m_cur_header << std::endl;
  return 0;
}

int
AltoMultipartParser::on_header_value(multipart_parser *parser, const char *at, size_t length) {
  auto self = (AltoMultipartParser*) multipart_parser_get_data(parser);
  self->m_current.headers[self->m_cur_header] = std::string(at, length);
  std::cout << self->m_cur_header << ": "
            << self->m_current.headers[self->m_cur_header] << std::endl;
  return 0;
}

int
AltoMultipartParser::on_part_data(multipart_parser *parser, const char* at, size_t length) {
  auto self = (AltoMultipartParser*) multipart_parser_get_data(parser);

  self->m_current.data = self->m_current.data + std::string(at, length);
  return 0;
}

int
AltoMultipartParser::on_part_data_begin(multipart_parser *parser) {
  auto self = (AltoMultipartParser*) multipart_parser_get_data(parser);
  std::cout << "a new part is being parsed" << std::endl;

  self->m_current.headers.clear();
  self->m_current.data = "";
  return 0;
}

int
AltoMultipartParser::on_headers_complete(multipart_parser *parser) {
  auto self = (AltoMultipartParser*) multipart_parser_get_data(parser);
  std::cout << "Preparing to read data" << std::endl;
  return 0;
}

int
AltoMultipartParser::on_part_data_end(multipart_parser *parser) {
  auto self = (AltoMultipartParser*) multipart_parser_get_data(parser);
  string part_id = self->m_current.headers["Content-ID"];
  self->m_message.parts[part_id] = self->m_current;
  if (self->m_root_first) {
    self->m_message.root = part_id;
    self->m_root_first = false;
  }
  std::cout << self->m_current.data << std::endl;
  return 0;
}

// End of AltoMultipartParser

//
// Parse Multipart Message
//

int
parse_multipart(const std::string & ctype, const std::string & data, MultipartMessage& msg)
{
  std::string boundary;
  if (get_field(ctype, "boundary", boundary)) {
    fprintf(stderr, "Boundary is not specified in content type\n");
    exit(EXIT_FAILURE);
  }
  std::cout << "boundary: " << boundary << std::endl;

  bool default_start = false;
  std::string start;
  if (get_field(ctype, "start", start)) {
    std::cout << "Root object is the first part" << std::endl;
    default_start = true;
  }

  std::string type;
  if (get_field(ctype, "type", type)) {
    fprintf(stderr, "Type is not specified in content type\n");
    exit(EXIT_FAILURE);
  }
  std::cout << "type: " << type << std::endl;

  AltoMultipartParser parser(boundary);

  int err = parser.parse(data);
  if (err != data.length()) {
    std::cout << err << std::endl;
    std::cout << "Parsing error at: " << data.substr(err, 10) << std::endl;
    return 1;
  }

  msg = parser.get();
  return 0;
}

static const std::string CONTENT_TYPE_ECS("application/alto-endpointcost+json");
static const std::string CONTENT_TYPE_PROPMAP("application/alto-propmap+json");

static int
get_part(const MultipartMessage &msg,
         const std::string & ctype, PartMessage & part)
{
  for (auto itr = msg.parts.begin(); itr != msg.parts.end(); ++itr) {
    auto part_ctype = itr->second.headers.find("Content-Type");
    if (part_ctype == itr->second.headers.end()) {
      continue;
    }
    if (part_ctype->second == ctype) {
      part = itr->second;
      return 0;
    }
  }
  return 1;
}


static const std::string FIELD_ENDPOINT_COST_MAP = "endpoint-cost-map";
static const std::string FIELD_PROPERTY_MAP = "property-map";
static const std::string FIELD_BANDWIDTH = "bandwidth";

struct PathVector {
  std::map<std::string, std::map<std::string, std::vector<std::string>>> paths;
  std::map<std::string, std::map<std::string, Json::Value>> propmaps;
};

static int
parse_path_vector(const MultipartMessage & msg, PathVector & pv)
{
  PartMessage ecs_part, prop_part;

  if (get_part(msg, CONTENT_TYPE_ECS, ecs_part)) {
    fprintf(stderr, "Error when extracting endpoint costs\n");
    exit(EXIT_FAILURE);
  }
  if (get_part(msg, CONTENT_TYPE_PROPMAP, prop_part)) {
    fprintf(stderr, "Error when extracting the property map\n");
    exit(EXIT_FAILURE);
  }

  Json::Value ecs;
  std::stringstream s_ecs(ecs_part.data);
  s_ecs >> ecs;

  auto ecs_map = ecs[FIELD_ENDPOINT_COST_MAP];
  auto srcs = ecs_map.getMemberNames();
  for (auto src = srcs.begin(); src != srcs.end(); ++src) {
    pv.paths[*src] = std::map<string, std::vector<std::string>>();
    auto paths = ecs_map[*src];
    auto dsts = paths.getMemberNames();
    for (auto dst = dsts.begin(); dst != dsts.end(); ++dst) {
      std::cout << *src << " -> " << *dst << ": " << paths[*dst] << std::endl;
      pv.paths[*src][*dst] = std::vector<std::string>();
      for (auto itr = paths[*dst].begin(); itr != paths[*dst].end(); ++itr) {
        pv.paths[*src][*dst].push_back(itr->asString());
      }
    }
  }

  Json::Value pm;
  std::stringstream s_pm(prop_part.data);
  s_pm >> pm;

  auto prop_map = pm[FIELD_PROPERTY_MAP];
  auto anes = prop_map.getMemberNames();
  for (auto ane = anes.begin(); ane != anes.end(); ++ane) {
    pv.propmaps[*ane] = std::map<std::string, Json::Value>();

    auto ane_props = prop_map[*ane];
    auto properties = ane_props.getMemberNames();
    for (auto prop = properties.begin(); prop != properties.end(); ++prop) {
      std::cout << *ane << "." << *prop << " = " << ane_props[*prop] << std::endl;
      pv.propmaps[*ane][*prop] = ane_props[*prop];
    }
  }

  return 0;
}


void dump_path_constraints(const PathConstraints & pc)
{
  size_t NF = pc.flow_map.size();
  size_t NL = pc.b.size();

  for (size_t i = 0; i < NL; ++i) {
    for (size_t j = 0; j < NF; ++j) {
      std::cout << pc.A[j][i] << " ";
    }

    std::cout << "    " << pc.b[i] << std::endl;
  }
}

static int
build_path_constraints(const PathVector & pv, PathConstraints & pc)
{
  std::map<std::string, int> link_id_map;
  size_t link_id = 0;

  auto & ecs_map = pv.paths;
  auto & props = pv.propmaps;

  for (auto ane = props.begin(); ane != props.end(); ++ane) {
    link_id_map[ane->first] = link_id++;
    auto ane_prop = ane->second;
    pc.b.push_back(ane_prop[FIELD_BANDWIDTH].asDouble());
  }

  for (auto i = ecs_map.begin(); i != ecs_map.end(); ++i) {
    auto src = i->first;
    auto paths = i->second;
    for (auto j = paths.begin(); j != paths.end(); ++j) {
      auto dst = j->first;
      auto path = j->second;

      pc.flow_map.push_back(std::make_pair(src, dst));

      pc.A.push_back(std::vector<int>(link_id, 0));
      auto &coeff = pc.A.back();

      for (auto k = path.begin(); k != path.end(); ++k) {
        auto itr = link_id_map.find(".ane:" + *k);
        if (itr == link_id_map.end()) {
          fprintf(stderr, "ANE name not found %s\n", k->c_str());
          return 1;
        }
        coeff[itr->second] = 1;
      }
    }
  }
  pc.status = true;
  return 0;
}


PathConstraints get_path_constraints(const char *uri, std::list<EndpointFlow> flows, std::list<std::string> props)
{
  CURL *conn = NULL;
  CURLcode code;
  struct curl_slist *headers = NULL;
  std::string title;

  PathConstraints pc;

  curl_global_init(CURL_GLOBAL_DEFAULT);

  std::cout << "Setting request HTTP headers" << std::endl;

  // Initialize HTTP headers for path vector query
  headers = curl_slist_append(headers, "Accept: multipart/related;type=application/alto-endpointcost+json,application/alto-error+json");
  headers = curl_slist_append(headers, "Content-Type: application/alto-endpointcostparams+json");

  std::string json_body = build_path_vector_request(flows, props);

  std::cout << "Sending Request: " << json_body << std::endl;

  // Initialize CURL connection

  if (!init(conn, const_cast<char*>(uri), headers, json_body.c_str())) {
    fprintf(stderr, "Connection initializion failed\n");
    return pc;
  }

  // Retrieve content for the URL

  code = curl_easy_perform(conn);

  if (code != CURLE_OK) {
    fprintf(stderr, "Failed to get '%s' [%s]\n", uri, errorBuffer);
    return pc;
  }

  char *ctype = NULL;
  code = curl_easy_getinfo(conn, CURLINFO_CONTENT_TYPE, &ctype);

  if (code != CURLE_OK) {
    fprintf(stderr, "Failed to get '%s' [%s]\n", uri, errorBuffer);
    return pc;
  }

  std::cout << buffer << std::endl;
  std::cout << std::string(ctype) << std::endl;

  MultipartMessage msg;
  if (parse_multipart(ctype, buffer, msg)) {
    fprintf(stderr, "Error when parsing the multipart message\n");
    return pc;
  }
  std::cout << msg.root << std::endl;

  PathVector pv;
  if (parse_path_vector(msg, pv)) {
    fprintf(stderr, "Error when extracting path vector data\n");
    return pc;
  }

  if (build_path_constraints(pv, pc)) {
    fprintf(stderr, "Error when building path constraints\n");
    return pc;
  }

  curl_easy_cleanup(conn);

  return pc;
}

}
