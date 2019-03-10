//===- TemplightProtobufWriter.h --------------------*- C++ -*-------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TEMPLIGHT_PROTOBUF_WRITER_H
#define LLVM_CLANG_TEMPLIGHT_PROTOBUF_WRITER_H

#include "PrintableTemplightEntries.h"

#include <string>
#include <unordered_map>

namespace llvm {
class raw_ostream;
}

namespace clang {

class TemplightProtobufWriter : public TemplightWriter {
private:
  std::string buffer;
  std::unordered_map<std::string, std::size_t> fileNameMap;
  std::unordered_map<std::string, std::size_t> templateNameMap;
  int compressionMode;

  std::size_t createDictionaryEntry(const std::string &Name);
  std::string printEntryLocation(const std::string &FileName, int Line,
                                 int Column);
  std::string printTemplateName(const std::string &Name);

public:
  TemplightProtobufWriter(llvm::raw_ostream &aOS, int aCompressLevel = 2);

  void initialize(const std::string &aSourceName = "") override;
  void finalize() override;

  void printEntry(const PrintableTemplightEntryBegin &aEntry) override;
  void printEntry(const PrintableTemplightEntryEnd &aEntry) override;
};

} // namespace clang

#endif
