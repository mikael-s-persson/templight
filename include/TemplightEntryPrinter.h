//===- TemplightProtobufReader.h --------------------------*- C++ -*-------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TEMPLIGHT_ENTRY_PRINTER_H
#define LLVM_CLANG_TEMPLIGHT_ENTRY_PRINTER_H

#include "PrintableTemplightEntries.h"

#include <memory>
#include <string>

namespace llvm {
class Regex;
}

namespace clang {

class TemplightWriter;

class TemplightEntryPrinter {
public:
  void skipEntry();
  bool shouldIgnoreEntry(const PrintableTemplightEntryBegin &Entry);
  bool shouldIgnoreEntry(const PrintableTemplightEntryEnd &Entry);

  void printEntry(const PrintableTemplightEntryBegin &Entry);
  void printEntry(const PrintableTemplightEntryEnd &Entry);

  void initialize(const std::string &SourceName = "");
  void finalize();

  TemplightEntryPrinter(const std::string &Output);
  ~TemplightEntryPrinter();

  bool isValid() const;

  llvm::raw_ostream *getTraceStream() const;
  void takeWriter(TemplightWriter *aPWriter);

  void readBlacklists(const std::string &BLFilename);

private:
  std::size_t SkippedEndingsCount;
  std::unique_ptr<llvm::Regex> CoRegex;
  std::unique_ptr<llvm::Regex> IdRegex;

  llvm::raw_ostream *TraceOS;

  std::unique_ptr<TemplightWriter> p_writer;
};

} // namespace clang

#endif
