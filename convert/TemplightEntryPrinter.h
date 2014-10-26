//===- TemplightProtobufReader.h ------ Clang Templight Protobuf Reader -*- C++ -*-===//
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
  
  void skipEntry(const PrintableTemplightEntryBegin &Entry);
  bool shouldIgnoreEntry(const PrintableTemplightEntryBegin &Entry);
  bool shouldIgnoreEntry(const PrintableTemplightEntryEnd &Entry);
  
  void printEntry(const PrintableTemplightEntryBegin &Entry);
  void printEntry(const PrintableTemplightEntryEnd &Entry);
  
  void initialize(const std::string& SourceName = "");
  void finalize();
  
  TemplightEntryPrinter(const std::string &Output, const std::string &Format = "yaml");
  ~TemplightEntryPrinter();
  
  bool isValid() const;

private:
  
  PrintableTemplightEntryBegin CurrentSkippedEntry;
  std::size_t SkippedEndingsCount;
  std::unique_ptr<llvm::Regex> CoRegex;
  std::unique_ptr<llvm::Regex> IdRegex;
  
  llvm::raw_ostream* TraceOS;
  
  std::unique_ptr<TemplightWriter> p_writer;
  
};

}

#endif

