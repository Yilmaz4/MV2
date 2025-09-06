set(MPFR_PREFIX "" CACHE PATH "The path to the prefix of an MPFR installation")

find_path(MPFR_INCLUDE_DIR mpfr.h
  PATHS
    ${MPFR_PREFIX}/include
    /usr/include
    /usr/local/include
    C:/msys64/mingw64/include
)

find_library(MPFR_LIBRARY NAMES mpfr
  PATHS
    ${MPFR_PREFIX}/lib
    /usr/lib
    /usr/local/lib
    C:/msys64/mingw64/lib
)

if(MPFR_INCLUDE_DIR AND MPFR_LIBRARY)
  get_filename_component(MPFR_LIBRARY_DIR ${MPFR_LIBRARY} PATH)
  set(MPFR_FOUND TRUE)
endif()

if(MPFR_FOUND)
  if(NOT MPFR_FIND_QUIETLY)
    message(STATUS "Found MPFR: ${MPFR_LIBRARY}")
  endif()
else()
  if(MPFR_FIND_REQUIRED)
    message(FATAL_ERROR "Could not find MPFR")
  endif()
endif()
