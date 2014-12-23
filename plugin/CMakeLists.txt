set(SOURCES
  TemplightPluginActions.cpp
  
  ADDITIONAL_HEADERS
  TemplightPluginActions.h
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

add_clang_library(templight-plugin
  ${SOURCES}
  DEPENDS clang-headers

  LINK_LIBS
  templight-static
  ${LIBS}
  )

