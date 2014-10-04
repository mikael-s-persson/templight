
set( LLVM_LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  Analysis
  CodeGen
  Core
  IPA
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
  TemplightTracer.cpp
  TemplightDebugger.cpp
  )

target_link_libraries(templight
  clangBasic
  clangFrontend
  clangFrontendTool
  )

set_target_properties(templight PROPERTIES VERSION ${CLANG_EXECUTABLE_VERSION})

add_dependencies(templight clang-headers)

install(TARGETS templight
  RUNTIME DESTINATION bin)

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
    "-Wl,-sectcreate,__TEXT,__info_plist,${TOOL_INFO_PLIST_OUT}")
  configure_file("${TOOL_INFO_PLIST}.in" "${TOOL_INFO_PLIST_OUT}" @ONLY)
  
  set(TOOL_INFO_UTI)
  set(TOOL_INFO_NAME)
  set(TOOL_INFO_VERSION)
  set(TOOL_INFO_BUILD_VERSION)
endif()

if(CLANG_ORDER_FILE)
  target_link_libraries(templight "-Wl,-order_file,${CLANG_ORDER_FILE}")
endif()

if(WITH_POLLY AND LINK_POLLY_INTO_TOOLS)
  target_link_libraries(templight Polly)
  if(POLLY_LINK_LIBS)
    foreach(lib ${POLLY_LINK_LIBS})
      target_link_libraries(templight ${lib})
    endforeach(lib)
  endif(POLLY_LINK_LIBS)
endif(WITH_POLLY AND LINK_POLLY_INTO_TOOLS)


