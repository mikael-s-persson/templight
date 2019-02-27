add_clang_library(clangTemplight
  TemplightAction.cpp
  TemplightDebugger.cpp
  TemplightEntryPrinter.cpp
  TemplightProtobufWriter.cpp
  TemplightTracer.cpp

  LINK_LIBS
  clangAST
  clangBasic
  clangFrontend
  clangLex
  clangSema
  clangSerialization
  )
