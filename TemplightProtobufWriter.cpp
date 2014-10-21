//===- TemplightProtobufWriter.cpp ------ Clang Templight Protobuf Writer -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TemplightProtobufWriter.h"
#include "PrintableTemplightEntries.h"

#include "ThinProtobuf.h"

#include <llvm/Support/raw_ostream.h>

#include <string>
#include <cstdint>

namespace clang {


TemplightProtobufWriter::TemplightProtobufWriter() { }

void TemplightProtobufWriter::initialize(const std::string& aSourceName) {
  
  std::string hdr_contents;
  {
    llvm::raw_string_ostream OS_inner(hdr_contents);
    
    /*
  message TemplightHeader {
    required uint32 version = 1;
    optional string source_file = 2;
  }
    */
    
    llvm::protobuf::saveVarInt(OS_inner, 1, 1); // version
    if ( !aSourceName.empty() ) 
      llvm::protobuf::saveString(OS_inner, 2, aSourceName); // source_file
  }
  
  llvm::raw_string_ostream OS(buffer);
  
  // required TemplightHeader header = 1;
  llvm::protobuf::saveString(OS, 1, hdr_contents);
  
}

std::string& TemplightProtobufWriter::finalize() {
  return buffer;
}

void TemplightProtobufWriter::dumpOnStream(llvm::raw_ostream& OS) {
  // repeated TemplightTrace traces = 1;
  llvm::protobuf::saveString(OS, 1, buffer);
//   OS.write(buffer.data(), buffer.size()); // if not within repeated sequence of traces
}

static std::string printEntryLocation(
    std::unordered_map< std::string, std::size_t >& fileNameMap, 
    const std::string& FileName, int Line, int Column) {
  
  /*
  message SourceLocation {
    optional string file_name = 1;
    required uint32 file_id = 2;
    required uint32 line = 3;
    optional uint32 column = 4;
  }
  */
  
  std::string location_contents;
  llvm::raw_string_ostream OS_inner(location_contents);
  
  std::unordered_map< std::string, std::size_t >::iterator 
    it = fileNameMap.find(FileName);
  
  if ( it == fileNameMap.end() ) {
    llvm::protobuf::saveString(OS_inner, 1, FileName); // file_name
    std::size_t file_id = fileNameMap.size();
    llvm::protobuf::saveVarInt(OS_inner, 2, file_id);     // file_id
    fileNameMap[FileName] = file_id;
  } else {
    llvm::protobuf::saveVarInt(OS_inner, 2, it->second);     // file_id
  }
  
  llvm::protobuf::saveVarInt(OS_inner, 3, Line);     // line
  llvm::protobuf::saveVarInt(OS_inner, 4, Column);   // column
  
  OS_inner.str();
  
  return location_contents;  // NRVO
}

void TemplightProtobufWriter::printEntry(const PrintableTemplightEntryBegin& aEntry) {
  
  std::string entry_contents;
  {
    llvm::raw_string_ostream OS_inner(entry_contents);
    
    /*
  message Begin {
    required InstantiationKind kind = 1;
    required string name = 2;
    required SourceLocation location = 3;
    optional double time_stamp = 4;
    optional uint64 memory_usage = 5;
  }
    */
    
    llvm::protobuf::saveVarInt(OS_inner, 1, aEntry.InstantiationKind); // kind
    llvm::protobuf::saveString(OS_inner, 2, aEntry.Name);              // name
    llvm::protobuf::saveString(OS_inner, 3, 
                         printEntryLocation(fileNameMap, aEntry.FileName, 
                                            aEntry.Line, aEntry.Column)); // location
    llvm::protobuf::saveDouble(OS_inner, 4, aEntry.TimeStamp);         // time_stamp
    if ( aEntry.MemoryUsage > 0 )
      llvm::protobuf::saveVarInt(OS_inner, 5, aEntry.MemoryUsage);     // memory_usage
    
  }
  
  std::string oneof_contents;
  {
    llvm::raw_string_ostream OS_inner(oneof_contents);
    
    /*
  oneof begin_or_end {
    Begin begin = 1;
    End end = 2;
  } 
     */
    
    llvm::protobuf::saveString(OS_inner, 1, entry_contents); // begin
    
  }
  
  llvm::raw_string_ostream OS(buffer);
  
  // repeated TemplightEntry entries = 2;
  llvm::protobuf::saveString(OS, 2, oneof_contents);
  
}

void TemplightProtobufWriter::printEntry(const PrintableTemplightEntryEnd& aEntry) {
  
  std::string entry_contents;
  {
    llvm::raw_string_ostream OS_inner(entry_contents);
    
    /*
  message End {
    optional double time_stamp = 1;
    optional uint64 memory_usage = 2;
  }
    */
    
    llvm::protobuf::saveDouble(OS_inner, 1, aEntry.TimeStamp);     // time_stamp
    if ( aEntry.MemoryUsage > 0 )
      llvm::protobuf::saveVarInt(OS_inner, 2, aEntry.MemoryUsage); // memory_usage
    
  }
  
  std::string oneof_contents;
  {
    llvm::raw_string_ostream OS_inner(oneof_contents);
    
    /*
  oneof begin_or_end {
    Begin begin = 1;
    End end = 2;
  } 
     */
    
    llvm::protobuf::saveString(OS_inner, 2, entry_contents); // end
    
  }
  
  llvm::raw_string_ostream OS(buffer);
  
  // repeated TemplightEntry entries = 2;
  llvm::protobuf::saveString(OS, 2, oneof_contents);
  
}



} // namespace clang

