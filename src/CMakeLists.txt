include(YAODAQ)

# Fetch csv-parser and make it available to my-executable
set(ENABLE_CODE_COVERAGE OFF)
set(CSV_BUILD_PROGRAMS OFF)
set(CSV_BUILD_TESTS OFF)
set(CSV_CXX_STANDARD 20) #TODO Fing who is doing wrong stuffs
FetchContent_Declare(
  csv
  GIT_REPOSITORY https://github.com/vincentlaucsb/csv-parser.git
  GIT_TAG 5.3.0
)
FetchContent_MakeAvailable(csv)

find_package(ROOT)

add_library(Data SHARED)
target_compile_features(Data PUBLIC cxx_std_20)
root_generate_dictionary(RawData_Dict RawData.hpp Data.hpp MODULE Data LINKDEF LinkDef.h)

add_executable(FileWriter DCT_FileWriter.cpp)
target_link_libraries(FileWriter PRIVATE Data yaodaq::yaodaq spdlog::spdlog CLI11::CLI11 cpp-terminal::cpp-terminal ROOT::Core ROOT::RIO ROOT::ROOTNTuple)
# Attach dictionaries to the executable. First, tell it where to look for headers required by the dictionaries:
target_include_directories(FileWriter PRIVATE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
install(TARGETS FileWriter RUNTIME DESTINATION bin)

add_executable(Analysor Analysor.cpp)
target_link_libraries(Analysor PRIVATE yaodaq::yaodaq Data spdlog::spdlog  CLI11::CLI11 cpp-terminal::cpp-terminal ROOT::RHTTP ROOT::Net ROOT::Core ROOT::RIO ROOT::Hist ROOT::Gpad)
install(TARGETS Analysor RUNTIME DESTINATION bin)

add_executable(RPCAnalyzer RPCAnalyzer.cpp)
target_link_libraries(RPCAnalyzer PRIVATE yaodaq::yaodaq spdlog::spdlog  Data CLI11::CLI11 cpp-terminal::cpp-terminal ROOT::Core ROOT::RIO ROOT::Tree ROOT::ROOTNTuple ROOT::Gpad ROOT::Hist)
install(TARGETS RPCAnalyzer RUNTIME DESTINATION bin)

add_executable(DCT DCT.cpp)
target_link_libraries(DCT PRIVATE csv yaodaq::yaodaq spdlog::spdlog  yaodaq::transport::process CLI11::CLI11 cpp-terminal::cpp-terminal)
install(TARGETS DCT RUNTIME DESTINATION bin)

set(DESTINATION_DIR "${PROJECT_BINARY_DIR}/firmwares")
set(FIRMWARE_URLS
    # DCT_ALONE
    "https://gitlab.cern.ch/atlas-tdaq-muon-barrel-trigger/bi_dct/bi_dct_data_acquisition/-/raw/8ee1b97c4411122fd2a18504b067a49fa613f6eb/BI_DCT_FW/top.bit"
    "https://gitlab.cern.ch/atlas-tdaq-muon-barrel-trigger/bi_dct/bi_dct_data_acquisition/-/raw/8ee1b97c4411122fd2a18504b067a49fa613f6eb/BI_DCT_FW/top.ltx"
    # DCT_ALONE_SELF_TRIGGER
    "https://gitlab.cern.ch/atlas-tdaq-muon-barrel-trigger/bi_dct/bi_dct_data_acquisition/-/raw/8ee1b97c4411122fd2a18504b067a49fa613f6eb/BI_DCT_FW_selftrigger/top.bit"
    "https://gitlab.cern.ch/atlas-tdaq-muon-barrel-trigger/bi_dct/bi_dct_data_acquisition/-/raw/8ee1b97c4411122fd2a18504b067a49fa613f6eb/BI_DCT_FW_selftrigger/top.ltx"
    # DCT_AND_OPTICAL_FIBER_BOARD
    "https://gitlab.cern.ch/atlas-tdaq-muon-barrel-trigger/bi_dct/bi_dct_data_acquisition/-/raw/gif_acquisition/BI_DCT_FW/top_sha_8525cd57.bit?ref_type=heads"
    "https://gitlab.cern.ch/atlas-tdaq-muon-barrel-trigger/bi_dct/bi_dct_data_acquisition/-/raw/gif_acquisition/BI_DCT_FW/top_sha_8525cd57.ltx?ref_type=heads"
    "https://gitlab.cern.ch/atlas-tdaq-muon-barrel-trigger/bi_dct/bi_dct_data_acquisition/-/raw/gif_acquisition/KCU116_FW/daq_kcu116_sha_0e17e7d.bit?ref_type=heads"
    "https://gitlab.cern.ch/atlas-tdaq-muon-barrel-trigger/bi_dct/bi_dct_data_acquisition/-/raw/gif_acquisition/KCU116_FW/daq_kcu116_sha_0e17e7d.ltx?ref_type=heads"
    "https://gitlab.cern.ch/atlas-tdaq-muon-barrel-trigger/bi_dct/bi_dct_data_acquisition/-/raw/gif_acquisition/KCU116_FW/daq_kcu116_sha_1f11706.bit?ref_type=heads"
    "https://gitlab.cern.ch/atlas-tdaq-muon-barrel-trigger/bi_dct/bi_dct_data_acquisition/-/raw/gif_acquisition/KCU116_FW/daq_kcu116_sha_1f11706.ltx?ref_type=heads"
)
set(FIRMWARE_FILES
    "${DESTINATION_DIR}/BI_DCT/top.bit"
    "${DESTINATION_DIR}/BI_DCT/top.ltx"
    "${DESTINATION_DIR}/BI_DCT/top_selftrigger.bit"
    "${DESTINATION_DIR}/BI_DCT/top_selftrigger.ltx"
    "${DESTINATION_DIR}/BI_DCT/top_sha_8525cd57.bit"
    "${DESTINATION_DIR}/BI_DCT/top_sha_8525cd57.ltx"
    "${DESTINATION_DIR}/KCU116/daq_kcu116_sha_0e17e7d.bit"
    "${DESTINATION_DIR}/KCU116/daq_kcu116_sha_0e17e7d.ltx"
    "${DESTINATION_DIR}/KCU116/daq_kcu116_sha_1f11706.bit"
    "${DESTINATION_DIR}/KCU116/daq_kcu116_sha_1f11706.ltx"
)
set(FIRMWARE_SHA256S)
list(LENGTH FIRMWARE_URLS URL_COUNT)
math(EXPR LAST_INDEX "${URL_COUNT} - 1")
foreach(INDEX RANGE ${LAST_INDEX})
  list(GET FIRMWARE_URLS ${INDEX} URL)
  list(GET FIRMWARE_FILES ${INDEX} FILE)
  if(NOT EXISTS "${FILE}")
    message(STATUS "Downloading ${URL}")
    file(DOWNLOAD "${URL}" "${FILE}" STATUS DOWNLOAD_STATUS SHOW_PROGRESS)
    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    if(NOT STATUS_CODE EQUAL 0)
      message(FATAL_ERROR "Failed to download ${URL}: ${DOWNLOAD_STATUS}")
    endif()
  endif()
endforeach()
# Install all downloaded binaries
install(FILES ${FIRMWARE_FILES} DESTINATION bin/firmwares)