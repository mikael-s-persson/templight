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
#include <llvm/Support/Compression.h>
#include <llvm/Support/Error.h>

#include <string>
#include <cstdint>

namespace clang {


TemplightProtobufWriter::TemplightProtobufWriter(llvm::raw_ostream& aOS, int aCompressLevel) : 
  TemplightWriter(aOS), compressionMode(aCompressLevel) { }

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

void TemplightProtobufWriter::finalize() {
  // repeated TemplightTrace traces = 1;
  llvm::protobuf::saveString(OutputOS, 1, buffer);
}

std::string TemplightProtobufWriter::printEntryLocation(
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

static void trimSpaces(std::string::iterator& it, std::string::iterator& it_end) {
  while ( it < it_end ) {
    if ( *it == ' ' )
      ++it;
    else if ( *it_end == ' ' )
      --it_end;
    else
      break;
  }
  ++it_end;
}

std::size_t TemplightProtobufWriter::createDictionaryEntry(const std::string& NameOrig) {
  std::unordered_map< std::string, std::size_t >::iterator 
    it_found = templateNameMap.find(NameOrig);
  if ( it_found != templateNameMap.end() )
    return it_found->second;
  
  // FIXME: Convert this code to being constructive of "Name", instead of destructive (replacing sub-strings with '\0' characters).
  std::string Name = NameOrig;
  std::string::iterator it_open = Name.end();
  std::string::iterator it_colon_lo = Name.begin();
  int srch_state = 0;
  llvm::SmallVector<std::size_t, 8> markers;
  for(std::string::iterator it = Name.begin(); it != Name.end(); ++it) {
    switch(srch_state) {
      case 0: 
        if ( *it == '<' ) {
          // check for "operator<<", "operator<" and "operator<="
          llvm::StringRef test_str(Name.data(), it - Name.begin() + 1);
          if ( test_str.endswith("operator<") ) {
            it_open = Name.end();
            srch_state = 0;
          } else {
            it_open = it;
            ++srch_state;
          }
        } else if ( (*it == ':') && (it + 1 < Name.end()) && (*(it+1) == ':') ) {
          if ( it_colon_lo < it ) {
            markers.push_back( createDictionaryEntry(std::string(it_colon_lo, it)) );
            std::size_t offset_lo  = it_colon_lo - Name.begin();
            Name.replace(it_colon_lo, it, 1, '\0');
            it = Name.begin() + offset_lo + 2;
          } else {
            it += 1;
          }
          it_colon_lo = it + 1;
          it_open = Name.end();
        }
        break;
      case 1:
        if ( *it == '<' ) {
          // check for "operator<<" and "operator<"
          llvm::StringRef test_str(Name.data(), it - Name.begin() + 1);
          if ( test_str.endswith("operator<<<") ) {
            it_open = it;
            srch_state = 1;
          } else {
            ++srch_state;
          }
        } else if ( ( *it == ',' ) || ( *it == '>' ) ) {
          if ( it_colon_lo < it_open ) {
            std::size_t offset_end = it - it_open;
            std::size_t offset_lo  = it_colon_lo - Name.begin();
            markers.push_back( createDictionaryEntry(std::string(it_colon_lo, it_open)) );
            Name.replace(it_colon_lo, it_open, 1, '\0');
            it_open = Name.begin() + offset_lo + 1;
            it = it_open + offset_end;
            it_colon_lo = Name.end();
          }
          std::string::iterator it_lo = it_open + 1;
          std::string::iterator it_hi = it - 1;
          trimSpaces(it_lo, it_hi);
          // Create or find the marker entry:
          markers.push_back( createDictionaryEntry(std::string(it_lo, it_hi)) );
          std::size_t offset_end = it - it_hi;
          std::size_t offset_lo  = it_lo - Name.begin();
          Name.replace(it_lo, it_hi, 1, '\0');
          it = Name.begin() + offset_lo + 1 + offset_end;
          it_open = it;
          it_colon_lo = Name.end();
          if ( *it == '>' ) {
            it_open = Name.end();
            srch_state = 0;
            it_colon_lo = it + 1;
          }
        }
        break;
      default:
        if ( *it == '<' ) {
          ++srch_state;
        } else if ( *it == '>' ) {
          --srch_state;
        }
        break;
    }
  }
  if ( !markers.empty() && it_colon_lo != Name.end() ) {
    markers.push_back( createDictionaryEntry(std::string(it_colon_lo, Name.end())) );
    Name.replace(it_colon_lo, Name.end(), 1, '\0');
  }
  
  /*
  message DictionaryEntry {
    required string marked_name = 1;
    repeated uint32 marker_ids = 2;
  }
  */
  std::string dict_entry;
  llvm::raw_string_ostream OS_dict(dict_entry);
  llvm::protobuf::saveString(OS_dict, 1, Name); // marked_name
  for(llvm::SmallVector<std::size_t, 8>::iterator mk = markers.begin(),
      mk_end = markers.end(); mk != mk_end; ++mk) {
    llvm::protobuf::saveVarInt(OS_dict, 2, *mk); // marker_ids
  }
  OS_dict.str();
  
  std::size_t id = templateNameMap.size();
  templateNameMap[NameOrig] = id;
  
  llvm::raw_string_ostream OS_outer(buffer);
  // repeated DictionaryEntry names = 3;
  llvm::protobuf::saveString(OS_outer, 3, dict_entry);
  
  return id;
}

std::string TemplightProtobufWriter::printTemplateName(const std::string& Name) {
  
  /*
  message TemplateName {
    optional string name = 1;
    optional bytes compressed_name = 2;
  }
  */
  
  std::string tname_contents;
  llvm::raw_string_ostream OS_inner(tname_contents);
  
  switch( compressionMode ) {
    case 1: { // zlib-compressed name:
      llvm::SmallVector<char, 32> CompressedBuffer;
      if ( llvm::zlib::compress(llvm::StringRef(Name), CompressedBuffer) ) {
        // optional bytes compressed_name = 2;
        llvm::protobuf::saveString(OS_inner, 2, 
          llvm::StringRef(CompressedBuffer.begin(), CompressedBuffer.size()));
        break;
      } // else, go to case 0:
    }
    case 0:
      // optional string name = 1;
      llvm::protobuf::saveString(OS_inner, 1, Name); // name
      break;
    case 2:
    default:
      // optional uint32 dict_id = 3;
      llvm::protobuf::saveVarInt(OS_inner, 3, 
        createDictionaryEntry(Name));
      break;
  }
  
  OS_inner.str();
  
  return tname_contents;  // NRVO
}

void TemplightProtobufWriter::printEntry(const PrintableTemplightEntryBegin& aEntry) {
  
  std::string entry_contents;
  {
    llvm::raw_string_ostream OS_inner(entry_contents);
    
    /*
  message Begin {
    required SynthesisKind kind = 1;
    required string name = 2;
    required SourceLocation location = 3;
    optional double time_stamp = 4;
    optional uint64 memory_usage = 5;
    optional SourceLocation template_origin = 6;
  }
    */
    
    llvm::protobuf::saveVarInt(OS_inner, 1, aEntry.SynthesisKind);    // kind
    llvm::protobuf::saveString(OS_inner, 2, 
                         printTemplateName(aEntry.Name));                 // name
    llvm::protobuf::saveString(OS_inner, 3, 
                               printEntryLocation(aEntry.FileName, 
                                                  aEntry.Line, 
                                                  aEntry.Column)); // location
    llvm::protobuf::saveDouble(OS_inner, 4, aEntry.TimeStamp);            // time_stamp
    if ( aEntry.MemoryUsage > 0 )
      llvm::protobuf::saveVarInt(OS_inner, 5, aEntry.MemoryUsage);        // memory_usage
    if ( !aEntry.TempOri_FileName.empty() )
      llvm::protobuf::saveString(OS_inner, 6, 
                                 printEntryLocation(aEntry.TempOri_FileName, 
                                                    aEntry.TempOri_Line, 
                                                    aEntry.TempOri_Column)); // template_origin
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

