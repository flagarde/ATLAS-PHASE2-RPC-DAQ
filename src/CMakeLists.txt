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


