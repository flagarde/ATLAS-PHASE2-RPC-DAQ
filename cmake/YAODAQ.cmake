include_guard(GLOBAL)

include(CPM)
cpm(SYSTEM SHALLOW PROGRESS EXCLUDE_FROM_ALL)

if(NOT DEFINED YAODAQ_REPOSITORY)
  set(YAODAQ_REPOSITORY "https://github.com/yaodaq/YAODAQ.git")
endif()

if(NOT DEFINED YAODAQ_TAG)
  set(YAODAQ_TAG "main")
endif()

CPMAddPackage(NAME yaodaq
              GIT_SHALLOW TRUE
              GIT_REPOSITORY "${YAODAQ_REPOSITORY}"
              GIT_TAG "${YAODAQ_TAG}"
              OPTIONS "YAODAQ_EXAMPLES FALSE"
              )
