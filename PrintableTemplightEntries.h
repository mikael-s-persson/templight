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

namespace clang {

struct PrintableTemplightEntryBegin {
  int InstantiationKind;
  std::string Name;
  std::string FileName;
  int Line;
  int Column;
  double TimeStamp;
  std::uint64_t MemoryUsage;
};

struct PrintableTemplightEntryEnd {
  double TimeStamp;
  std::uint64_t MemoryUsage;
};

}

#endif



