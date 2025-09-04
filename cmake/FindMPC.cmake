find_path(MPC_INCLUDE_DIR mpc.h)
find_library(MPC_LIBRARY NAMES mpc)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MPC DEFAULT_MSG
                                  MPC_LIBRARY MPC_INCLUDE_DIR)

mark_as_advanced(MPC_INCLUDE_DIR MPC_LIBRARY)