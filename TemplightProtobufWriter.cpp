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

#include <clang/Sema/ActiveTemplateInst.h>
#include <clang/Sema/Sema.h>

#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Endian.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/Process.h>

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace clang {

namespace protobuf {

namespace {

union float_to_ulong {
  float    f;
  std::uint32_t ui32;
};

union double_to_ulong {
  double   d;
  std::uint64_t ui64;
  std::uint32_t ui32[2];
};

union int64_to_uint64 {
  std::int64_t i64;
  std::uint64_t ui64;
};

}

#if 0
static void loadVarInt(const char*& p_buf, std::uint64_t& u) {
  u = 0;
  std::uint8_t shifts = 0;
  u = (*p_buf) & 0x7F;
  while( (*p_buf) & 0x80 ) {
    shifts += 7; 
    ++p_buf;
    u |= ((*p_buf) & 0x7F) << shifts;
  };
  ++p_buf;
}

static void loadSInt(const char*& p_buf, std::int64_t& i) {
  std::uint64_t u = 0;
  loadVarInt(p_buf, u);
  i = (u >> 1) ^ (-static_cast<std::uint64_t>(u & 1));
}
#endif



static void saveVarInt(llvm::raw_ostream& OS, std::uint64_t u) {
  std::uint8_t buf[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};  // 80-bits, supports at most a 64-bit varint.
  std::uint8_t* pbuf = buf;
  *pbuf = (u & 0x7F); u >>= 7;
  while(u) {
    *pbuf |= 0x80; // set first msb because there is more to come.
    pbuf++;
    *pbuf = (u & 0x7F); u >>= 7;
  };
  OS.write(reinterpret_cast<char*>(buf), pbuf - buf + 1);
}

static void saveVarInt(llvm::raw_ostream& OS, unsigned int tag, std::uint64_t u) {
  saveVarInt(OS, (tag << 3)); // wire-type 0: Varint.
  saveVarInt(OS, u);
}

#if 0
static void saveInt(llvm::raw_ostream& OS, unsigned int tag, std::int64_t i) {
  saveVarInt(OS, (tag << 3)); // wire-type 0: Varint.
  int64_to_uint64 tmp; tmp.i64 = i;
  saveVarInt(OS, tmp.ui64);
}

static void saveSInt(llvm::raw_ostream& OS, std::int64_t i) {
  // Apply the ZigZag encoding for the sign:
  saveVarInt(OS, (i << 1) ^ (i >> (sizeof(std::int64_t) * 8 - 1)));
}

static void saveSInt(llvm::raw_ostream& OS, unsigned int tag, std::int64_t i) {
  saveVarInt(OS, (tag << 3)); // wire-type 0: Varint.
  saveSInt(OS, i);
}
#endif

static void saveDouble(llvm::raw_ostream& OS, double d) {
  double_to_ulong tmp = { d };
  tmp.ui64 = llvm::support::endian::byte_swap< std::uint64_t, llvm::support::little >(tmp.ui64);
  OS.write(reinterpret_cast<char*>(&tmp), sizeof(double_to_ulong));
}

static void saveDouble(llvm::raw_ostream& OS, unsigned int tag, double d) {
  saveVarInt(OS, (tag << 3) | 1); // wire-type 1: 64-bit.
  saveDouble(OS, d);
}

#if 0
static void saveFloat(llvm::raw_ostream& OS, float d) {
  float_to_ulong tmp = { d };
  tmp.ui32 = llvm::support::endian::byte_swap< std::uint32_t, llvm::support::little >(tmp.ui32);
  OS.write(reinterpret_cast<char*>(&tmp), sizeof(float_to_ulong));
}

static void saveFloat(llvm::raw_ostream& OS, unsigned int tag, float d) {
  saveVarInt(OS, (tag << 3) | 5); // wire-type 5: 32-bit.
  saveFloat(OS, d);
}

static void saveBool(llvm::raw_ostream& OS, bool b) {
  char tmp = 0;
  if(b) tmp = 1;
  OS.write(&tmp, 1);
}

static void saveBool(llvm::raw_ostream& OS, unsigned int tag, bool b) {
  saveVarInt(OS, (tag << 3)); // wire-type 0: varint.
  saveBool(OS, b);
}
#endif

static void saveString(llvm::raw_ostream& OS, const std::string& s) {
  unsigned int u = s.length();
  saveVarInt(OS, u);
  OS.write(s.data(), u);
}

static void saveString(llvm::raw_ostream& OS, unsigned int tag, const std::string& s) {
  saveVarInt(OS, (tag << 3) | 2); // wire-type 2: length-delimited.
  saveString(OS, s);
}



}



TemplightProtobufWriter::TemplightProtobufWriter() { }

TemplightProtobufWriter::~TemplightProtobufWriter() { }

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
    
    protobuf::saveVarInt(OS_inner, 1, 1); // version
    if ( !aSourceName.empty() ) 
      protobuf::saveString(OS_inner, 2, aSourceName); // source_file
  }
  
  llvm::raw_string_ostream OS(buffer);
  
  // required TemplightHeader header = 1;
  protobuf::saveString(OS, 1, hdr_contents);
  
}

std::string& TemplightProtobufWriter::finalize() {
  return buffer;
}

void TemplightProtobufWriter::dumpOnStream(llvm::raw_ostream& OS) {
  // repeated TemplightTrace traces = 1;
  protobuf::saveString(OS, 1, buffer);
//   OS.write(buffer.data(), buffer.size()); // if not within repeated sequence of traces
}

static std::string printEntryLocation(const std::string& FileName, int Line, int Column) {
  
  /*
  message SourceLocation {
    required string file_name = 1;
    required uint32 line = 2;
    optional uint32 column = 3;
  }
  */
  
  std::string location_contents;
  llvm::raw_string_ostream OS_inner(location_contents);
    
  protobuf::saveString(OS_inner, 1, FileName); // file_name
  protobuf::saveVarInt(OS_inner, 2, Line);     // line
  protobuf::saveVarInt(OS_inner, 3, Column);   // column
  
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
    
    protobuf::saveVarInt(OS_inner, 1, aEntry.InstantiationKind); // kind
    protobuf::saveString(OS_inner, 2, aEntry.Name);              // name
    protobuf::saveString(OS_inner, 3, 
                         printEntryLocation(aEntry.FileName, 
                                            aEntry.Line, aEntry.Column)); // location
    protobuf::saveDouble(OS_inner, 4, aEntry.TimeStamp);         // time_stamp
    if ( aEntry.MemoryUsage > 0 )
      protobuf::saveVarInt(OS_inner, 5, aEntry.MemoryUsage);     // memory_usage
    
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
    
    protobuf::saveString(OS_inner, 1, entry_contents); // begin
    
  }
  
  llvm::raw_string_ostream OS(buffer);
  
  // repeated TemplightEntry entries = 2;
  protobuf::saveString(OS, 2, oneof_contents);
  
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
    
    protobuf::saveDouble(OS_inner, 1, aEntry.TimeStamp);     // time_stamp
    if ( aEntry.MemoryUsage > 0 )
      protobuf::saveVarInt(OS_inner, 2, aEntry.MemoryUsage); // memory_usage
    
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
    
    protobuf::saveString(OS_inner, 2, entry_contents); // end
    
  }
  
  llvm::raw_string_ostream OS(buffer);
  
  // repeated TemplightEntry entries = 2;
  protobuf::saveString(OS, 2, oneof_contents);
  
}



} // namespace clang

