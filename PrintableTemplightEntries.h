//===- PrintableTemplightEntries.h ------ Clang Templight Printable Entries -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_PRINTABLE_TEMPLIGHT_ENTRIES_H
#define LLVM_CLANG_PRINTABLE_TEMPLIGHT_ENTRIES_H

#include <string>
#include <cstdint>

namespace llvm {
  class raw_ostream;
}

namespace clang {

struct PrintableTemplightEntryBegin {
  int SynthesisKind;
  std::string Name;
  std::string FileName;
  int Line;
  int Column;
  double TimeStamp;
  std::uint64_t MemoryUsage;
  std::string TempOri_FileName;
  int TempOri_Line;
  int TempOri_Column;
};

struct PrintableTemplightEntryEnd {
  double TimeStamp;
  std::uint64_t MemoryUsage;
};


class TemplightWriter {
public:
  
  TemplightWriter(llvm::raw_ostream& aOS) : OutputOS(aOS) { };
  virtual ~TemplightWriter() { };
  
  virtual void initialize(const std::string& aSourceName = "") = 0;
  virtual void finalize() = 0;
  
  virtual void printEntry(const PrintableTemplightEntryBegin& aEntry) = 0;
  virtual void printEntry(const PrintableTemplightEntryEnd& aEntry) = 0;
  
protected:
  llvm::raw_ostream& OutputOS;
};


}

#endif



