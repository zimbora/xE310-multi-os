#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "modem::modem_xe310" for configuration "Release"
set_property(TARGET modem::modem_xe310 APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(modem::modem_xe310 PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libmodem_xe310.a"
  )

list(APPEND _cmake_import_check_targets modem::modem_xe310 )
list(APPEND _cmake_import_check_files_for_modem::modem_xe310 "${_IMPORT_PREFIX}/lib/libmodem_xe310.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
