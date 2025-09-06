find_path(MPC_INCLUDE_DIR mpc.h
  PATHS
    /usr/include
    /usr/local/include
    C:/msys64/mingw64/include
)

find_library(MPC_LIBRARY NAMES mpc
  PATHS
    /usr/lib
    /usr/local/lib
    C:/msys64/mingw64/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MPC DEFAULT_MSG
                                  MPC_LIBRARY MPC_INCLUDE_DIR)

mark_as_advanced(MPC_INCLUDE_DIR MPC_LIBRARY)
