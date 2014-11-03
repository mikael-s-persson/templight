set(SOURCES
  TemplightAction.cpp
  TemplightDebugger.cpp
  TemplightEntryPrinter.cpp
  TemplightExtraWriters.cpp
  TemplightProtobufReader.cpp
  TemplightProtobufWriter.cpp
  TemplightTracer.cpp

  ADDITIONAL_HEADERS
  PrintableTemplightEntries.h
  TemplightAction.h
  TemplightDebugger.h
  TemplightEntryPrinter.h
  TemplightExtraWriters.h
  TemplightProtobufReader.h
  TemplightProtobufWriter.h
  TemplightTracer.h
  ThinProtobuf.h
  )

add_clang_library(templight-static STATIC
  ${SOURCES}
  DEPENDS clang-headers

  LINK_LIBS
  ${LIBS}
  )

