set(SOURCES
  ../../libclang/ARCMigrate.cpp
  ../../libclang/BuildSystem.cpp
  ../../libclang/CIndex.cpp
  ../../libclang/CIndexCXX.cpp
  ../../libclang/CIndexCodeCompletion.cpp
  ../../libclang/CIndexDiagnostic.cpp
  ../../libclang/CIndexHigh.cpp
  ../../libclang/CIndexInclusionStack.cpp
  ../../libclang/CIndexUSRs.cpp
  ../../libclang/CIndexer.cpp
  ../../libclang/CXComment.cpp
  ../../libclang/CXCursor.cpp
  ../../libclang/CXCompilationDatabase.cpp
  ../../libclang/CXLoadedDiagnostic.cpp
  ../../libclang/CXSourceLocation.cpp
  ../../libclang/CXStoredDiagnostic.cpp
  ../../libclang/CXString.cpp
  ../../libclang/CXType.cpp
  ../../libclang/IndexBody.cpp
  ../../libclang/IndexDecl.cpp
  ../../libclang/IndexTypeSourceInfo.cpp
  ../../libclang/Indexing.cpp
  ../../libclang/IndexingContext.cpp
  TemplightAPI.cpp

  ADDITIONAL_HEADERS
  ../../libclang/CIndexDiagnostic.h
  ../../libclang/CIndexer.h
  ../../libclang/CXCursor.h
  ../../libclang/CXLoadedDiagnostic.h
  ../../libclang/CXSourceLocation.h
  ../../libclang/CXString.h
  ../../libclang/CXTranslationUnit.h
  ../../libclang/CXType.h
  ../../libclang/Index_Internal.h
  ../../libclang/IndexingContext.h
  TemplightAPI.h
  ../../include/clang-c/Index.h
  )

set(LIBS
  clangAST
  clangBasic
  clangFrontend
  clangIndex
  clangLex
  clangSema
  clangTooling
)

include_directories("../../libclang/")

if (CLANG_ENABLE_ARCMT)
  list(APPEND LIBS clangARCMigrate)
endif ()

set(LLVM_EXPORTED_SYMBOL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/libtemplight.exports)

if(MSVC)
  # Avoid LNK4197 not to spceify libclang.def here.
  # Each functions is exported as "dllexport" in include/clang-c.
  # KB835326
  set(LLVM_EXPORTED_SYMBOL_FILE)
endif()

if( LLVM_ENABLE_PIC )
  set(ENABLE_SHARED SHARED)
endif()

if((NOT LLVM_ENABLE_PIC OR LIBCLANG_BUILD_STATIC) AND NOT WIN32)
  set(ENABLE_STATIC STATIC)
endif()

if(WIN32)
  set(output_name "libtemplight")
else()
  set(output_name "templight")
endif()

add_clang_library(libtemplight ${ENABLE_SHARED} ${ENABLE_STATIC}
  OUTPUT_NAME ${output_name}
  ${SOURCES}
  DEPENDS clang-headers

  LINK_LIBS
  templight-static
  ${LIBS}

  LINK_COMPONENTS
  Core
  Support
  )

if(ENABLE_SHARED)
  if(WIN32)
    set_target_properties(libtemplight
      PROPERTIES
      VERSION ${LIBCLANG_LIBRARY_VERSION}
      DEFINE_SYMBOL _CINDEX_LIB_)
  else()
    set_target_properties(libtemplight
      PROPERTIES
      VERSION ${LIBCLANG_LIBRARY_VERSION}
      DEFINE_SYMBOL _CINDEX_LIB_)
  endif()

  if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(LIBCLANG_LINK_FLAGS " -Wl,-compatibility_version -Wl,1")
    if (DEFINED ${LLVM_SUBMIT_VERSION})
      set(LIBCLANG_LINK_FLAGS
        "${LIBCLANG_LINK_FLAGS} -Wl,-current_version -Wl,${LLVM_SUBMIT_VERSION}.${LLVM_SUBMIT_SUBVERSION}")
    endif()

    set_property(TARGET libtemplight APPEND_STRING PROPERTY
                 LINK_FLAGS ${LIBCLANG_LINK_FLAGS})
  endif()
endif()
