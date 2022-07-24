#
# Copyright (c) CERN 2013-2015
#
# Copyright (c) Members of the EMI Collaboration. 2010-2013
#  See  http://www.eu-emi.eu/partners for details on the copyright
#  holders.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

cmake_minimum_required(VERSION 2.8)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

file(GLOB alto_client_sources
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "*.cpp"
    "*.c"
)
list(REMOVE_ITEM alto_client_sources "main.cpp")

# Find libuuid
# find_package(LibUuid REQUIRED)
# include_directories(${LIBUUID_INCLUDE_DIRS})

# Find boost
# find_package(Boost COMPONENTS regex iostreams system thread filesystem REQUIRED)
find_package(Boost COMPONENTS regex REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP jsoncpp)
include_directories(${JSONCPP_INCLUDE_DIRS})
link_libraries(${JSONCPP_LIBRARIES})

add_definitions(-DCURL_STATICLIB)

include_directories(${CURL_INCLUDE_DIR})

set(alto_client_libs
    ${CURL_LIBRARY}
    ${Boost_LIBRARIES}
    # ${LIBUUID_LIBRARIES}
    ${GLIB2_LIBRARIES}
    ${JSONCPP_LIBRARIES}
    -lpthread
    -lssl
    -lcrypto
)

# Client common library
add_library(alto_client_common SHARED ${alto_client_sources})
target_link_libraries(alto_client_common ${alto_client_libs})
set_target_properties(alto_client_common PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/src/alto
    VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}
    SOVERSION ${VERSION_MAJOR}
    CLEAN_DIRECT_OUTPUT 1
)

# Client executable
add_executable(alto_client main.cpp)
target_link_libraries(alto_client
    alto_client_common
)

# Man pages
set(MAN_INPUT_DIR "${PROJECT_SOURCE_DIR}/doc/man/alto")
install(FILES "${MAN_INPUT_DIR}/alto_client.8" DESTINATION ${MAN_INSTALL_DIR}/man8/)
# install(FILES "${MAN_INPUT_DIR}/fts_bdii_cache_updater.8" DESTINATION ${MAN_INSTALL_DIR}/man8/)
# install(FILES "${MAN_INPUT_DIR}/fts_db_cleaner.8" DESTINATION ${MAN_INSTALL_DIR}/man8/)
# install(FILES "${MAN_INPUT_DIR}/fts_msg_bulk.8" DESTINATION ${MAN_INSTALL_DIR}/man8/)
# install(FILES "${MAN_INPUT_DIR}/fts_url_copy.8" DESTINATION ${MAN_INSTALL_DIR}/man8/)
# install(FILES "${MAN_INPUT_DIR}/fts_bringonline.8" DESTINATION ${MAN_INSTALL_DIR}/man8/)
# install(FILES "${MAN_INPUT_DIR}/fts_qos.8" DESTINATION ${MAN_INSTALL_DIR}/man8/)
# install(FILES "${MAN_INPUT_DIR}/fts_info_publisher.8" DESTINATION ${MAN_INSTALL_DIR}/man8/)


# Artifacts
install(TARGETS alto_client_common
	RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}
	LIBRARY DESTINATION ${LIB_INSTALL_DIR}
)

install(TARGETS alto_client
	RUNTIME DESTINATION ${BIN_INSTALL_DIR}
)
