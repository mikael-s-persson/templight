
set( LLVM_LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  Support
  )

include_directories("../")

add_clang_executable(templight-convert
  templight_convert.cpp
  TemplightProtobufReader.cpp
  TemplightExtraWriters.cpp
  TemplightEntryPrinter.cpp
  )

set_target_properties(templight-convert PROPERTIES VERSION ${CLANG_EXECUTABLE_VERSION})

install(TARGETS templight-convert RUNTIME DESTINATION bin)

# Configure plist creation for OS X.
set (TOOL_INFO_PLIST "Info.plist" CACHE STRING "Plist name")
if (APPLE)  
  if (CLANG_VENDOR)
    set(TOOL_INFO_NAME "${CLANG_VENDOR} templight-convert")
  else()
    set(TOOL_INFO_NAME "templight-convert")
  endif()

  set(TOOL_INFO_UTI "${CLANG_VENDOR_UTI}")
  set(TOOL_INFO_VERSION "${CLANG_VERSION}")
  if (LLVM_SUBMIT_VERSION)
    set(TOOL_INFO_BUILD_VERSION
      "${LLVM_SUBMIT_VERSION}.${LLVM_SUBMIT_SUBVERSION}")
  endif()
  
  set(TOOL_INFO_PLIST_OUT "${CMAKE_CURRENT_BINARY_DIR}/${TOOL_INFO_PLIST}")
  target_link_libraries(templight-convert
    "-Wl,-sectcreate,__TEXT,__info_plist,${TOOL_INFO_PLIST_OUT}")
  configure_file("${TOOL_INFO_PLIST}.in" "${TOOL_INFO_PLIST_OUT}" @ONLY)
  
  set(TOOL_INFO_UTI)
  set(TOOL_INFO_NAME)
  set(TOOL_INFO_VERSION)
  set(TOOL_INFO_BUILD_VERSION)
endif()



