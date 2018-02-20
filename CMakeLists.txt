
set( LLVM_LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  Analysis
  CodeGen
  Core
  IPO
  InstCombine
  Instrumentation
  MC
  MCParser
  ObjCARCOpts
  Option
  ScalarOpts
  Support
  TransformUtils
  Vectorize
  )

add_clang_executable(templight
  templight_driver.cpp
  TemplightAction.cpp
  TemplightDebugger.cpp
  TemplightEntryPrinter.cpp
  TemplightProtobufWriter.cpp
  TemplightTracer.cpp
  )

target_link_libraries(templight
  PRIVATE
  clangBasic
  clangFrontend
  clangFrontendTool
  )

set_target_properties(templight PROPERTIES VERSION ${CLANG_EXECUTABLE_VERSION})

add_dependencies(templight clang-headers)

if(UNIX)
  set(CLANGXX_LINK_OR_COPY create_symlink)
# Create a relative symlink
  set(templight_binary "templight${CMAKE_EXECUTABLE_SUFFIX}")
else()
  set(CLANGXX_LINK_OR_COPY copy)
  set(templight_binary "${LLVM_RUNTIME_OUTPUT_INTDIR}/templight${CMAKE_EXECUTABLE_SUFFIX}")
endif()

# Create the templight++ symlink in the build directory.
set(templight_pp "${LLVM_RUNTIME_OUTPUT_INTDIR}/templight++${CMAKE_EXECUTABLE_SUFFIX}")
add_custom_command(TARGET templight POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E ${CLANGXX_LINK_OR_COPY} "${templight_binary}" "${templight_pp}"
    WORKING_DIRECTORY "${LLVM_RUNTIME_OUTPUT_INTDIR}")

set_property(DIRECTORY APPEND
  PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${templight_pp})

# Create the templight-cl symlink in the build directory.
set(templight_cl "${LLVM_RUNTIME_OUTPUT_INTDIR}/templight-cl${CMAKE_EXECUTABLE_SUFFIX}")
add_custom_command(TARGET templight POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E ${CLANGXX_LINK_OR_COPY} "${templight_binary}" "${templight_cl}"
    WORKING_DIRECTORY "${LLVM_RUNTIME_OUTPUT_INTDIR}")

set_property(DIRECTORY APPEND
  PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${templight_cl})

install(TARGETS templight
  RUNTIME DESTINATION bin)

# Create the templight-cl symlinks at installation time.
install(SCRIPT templight_symlink.cmake -DCMAKE_INSTALL_PREFIX=\"${CMAKE_INSTALL_PREFIX}\")

# Configure plist creation for OS X.
set (TOOL_INFO_PLIST "Info.plist" CACHE STRING "Plist name")
if (APPLE)  
  if (CLANG_VENDOR)
    set(TOOL_INFO_NAME "${CLANG_VENDOR} templight")
  else()
    set(TOOL_INFO_NAME "templight")
  endif()

  set(TOOL_INFO_UTI "${CLANG_VENDOR_UTI}")
  set(TOOL_INFO_VERSION "${CLANG_VERSION}")
  if (LLVM_SUBMIT_VERSION)
    set(TOOL_INFO_BUILD_VERSION
      "${LLVM_SUBMIT_VERSION}.${LLVM_SUBMIT_SUBVERSION}")
  endif()
  
  set(TOOL_INFO_PLIST_OUT "${CMAKE_CURRENT_BINARY_DIR}/${TOOL_INFO_PLIST}")
  target_link_libraries(templight
    PRIVATE
    "-Wl,-sectcreate,__TEXT,__info_plist,${TOOL_INFO_PLIST_OUT}")
  configure_file("${TOOL_INFO_PLIST}.in" "${TOOL_INFO_PLIST_OUT}" @ONLY)
  
  set(TOOL_INFO_UTI)
  set(TOOL_INFO_NAME)
  set(TOOL_INFO_VERSION)
  set(TOOL_INFO_BUILD_VERSION)
endif()

if(CLANG_ORDER_FILE)
  target_link_libraries(templight PRIVATE "-Wl,-order_file,${CLANG_ORDER_FILE}")
endif()

if(WITH_POLLY AND LINK_POLLY_INTO_TOOLS)
  target_link_libraries(templight PRIVATE Polly)
  if(POLLY_LINK_LIBS)
    foreach(lib ${POLLY_LINK_LIBS})
      target_link_libraries(templight PRIVATE ${lib})
    endforeach(lib)
  endif(POLLY_LINK_LIBS)
endif(WITH_POLLY AND LINK_POLLY_INTO_TOOLS)
