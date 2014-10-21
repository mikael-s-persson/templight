//===- TemplightProtobufReader.h ------ Clang Templight Protobuf Reader -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TEMPLIGHT_PROTOBUF_READER_H
#define LLVM_CLANG_TEMPLIGHT_PROTOBUF_READER_H

#include "PrintableTemplightEntries.h"

#include <llvm/ADT/StringRef.h>

#include <string>

namespace clang {


class TemplightProtobufReader {
private:
  
  llvm::StringRef buffer;
  llvm::StringRef remainder_buffer;
  
  void loadHeader(llvm::StringRef aSubBuffer);
  void loadBeginEntry(llvm::StringRef aSubBuffer);
  void loadEndEntry(llvm::StringRef aSubBuffer);
  
public:
  
  enum LastChunkType {
    EndOfFile = 0,
    Header,
    BeginEntry,
    EndEntry
  } LastChunk;
  
  unsigned int Version;
  std::string SourceName;
  
  PrintableTemplightEntryBegin LastBeginEntry;
  PrintableTemplightEntryEnd   LastEndEntry;
  
  TemplightProtobufReader();
  
  LastChunkType startOnBuffer(llvm::StringRef aBuffer);
  LastChunkType next();
  
};


}

#endif



