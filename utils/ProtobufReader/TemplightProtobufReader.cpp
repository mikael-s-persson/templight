//===- TemplightProtobufWriter.cpp ------ Clang Templight Protobuf Reader -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TemplightProtobufReader.h"

#include "ThinProtobuf.h"

#include <llvm/Support/Compression.h>

#include <string>
#include <cstdint>

namespace clang {


TemplightProtobufReader::TemplightProtobufReader() { }

void TemplightProtobufReader::loadHeader(llvm::StringRef aSubBuffer) {
  // Set default values:
  Version = 0;
  SourceName = "";
  
  while ( aSubBuffer.size() ) {
    unsigned int cur_wire = llvm::protobuf::loadVarInt(aSubBuffer);
    switch( cur_wire ) {
      case llvm::protobuf::getVarIntWire<1>::value:
        Version = llvm::protobuf::loadVarInt(aSubBuffer);
        break;
      case llvm::protobuf::getStringWire<2>::value:
        SourceName = llvm::protobuf::loadString(aSubBuffer);
        break;
      default:
        llvm::protobuf::skipData(aSubBuffer, cur_wire);
        break;
    }
  }
  
  LastChunk = TemplightProtobufReader::Header;
}

void TemplightProtobufReader::loadDictionaryEntry(llvm::StringRef aSubBuffer) {
  // Set default values:
  std::string name = "";
  llvm::SmallVector<std::size_t,8> markers;
  
  while ( aSubBuffer.size() ) {
    unsigned int cur_wire = llvm::protobuf::loadVarInt(aSubBuffer);
    switch( cur_wire ) {
      case llvm::protobuf::getStringWire<1>::value:
        name = llvm::protobuf::loadString(aSubBuffer);
        break;
      case llvm::protobuf::getVarIntWire<2>::value:
        markers.push_back(llvm::protobuf::loadVarInt(aSubBuffer));
        break;
      default:
        llvm::protobuf::skipData(aSubBuffer, cur_wire);
        break;
    }
  }
  
  std::string::iterator it_name = std::find(name.begin(), name.end(), '\0');
  llvm::SmallVector<std::size_t,8>::iterator it_mark = markers.begin();
  while ( ( it_name != name.end() ) && ( it_mark != markers.end() ) ) {
    std::size_t offset = it_name - name.begin();
    name.replace(it_name, it_name + 1, templateNameMap[*it_mark]);
    it_name = std::find(name.begin() + offset, name.end(), '\0');
    ++it_mark;
  }
  
  templateNameMap.push_back(name);
  
}

static void loadLocation(llvm::StringRef aSubBuffer, 
                         std::vector<std::string>& fileNameMap,
                         std::string& FileName, int& Line, int& Column) {
  // Set default values:
  FileName = "";
  std::size_t FileID = std::numeric_limits<std::size_t>::max();
  Line = 0;
  Column = 0;
  
  while ( aSubBuffer.size() ) {
    unsigned int cur_wire = llvm::protobuf::loadVarInt(aSubBuffer);
    switch( cur_wire ) {
      case llvm::protobuf::getStringWire<1>::value:
        FileName = llvm::protobuf::loadString(aSubBuffer);
        break;
      case llvm::protobuf::getVarIntWire<2>::value:
        FileID = llvm::protobuf::loadVarInt(aSubBuffer);
        break;
      case llvm::protobuf::getVarIntWire<3>::value:
        Line = llvm::protobuf::loadVarInt(aSubBuffer);
        break;
      case llvm::protobuf::getVarIntWire<4>::value:
        Column = llvm::protobuf::loadVarInt(aSubBuffer);
        break;
      default:
        llvm::protobuf::skipData(aSubBuffer, cur_wire);
        break;
    }
  }
  
  if ( FileID != std::numeric_limits<std::size_t>::max() ) {
    if ( fileNameMap.size() <= FileID )
      fileNameMap.resize(FileID + 1);
    if ( !FileName.empty() ) {
      fileNameMap[FileID] = FileName;  // overwrite existing names, if any, but there shouldn't be.
    } else {
      FileName = fileNameMap[FileID];
    }
  } // else we don't care?
  
}

void TemplightProtobufReader::loadTemplateName(llvm::StringRef aSubBuffer) {
  // Set default values:
  LastBeginEntry.Name = "";
  
  while ( aSubBuffer.size() ) {
    unsigned int cur_wire = llvm::protobuf::loadVarInt(aSubBuffer);
    switch( cur_wire ) {
      case llvm::protobuf::getStringWire<1>::value:
        LastBeginEntry.Name = llvm::protobuf::loadString(aSubBuffer);
        break;
      case llvm::protobuf::getStringWire<2>::value: {
        LastBeginEntry.Name = llvm::protobuf::loadString(aSubBuffer);
        llvm::SmallVector<char,32> UBuf;
        if ( llvm::zlib::uncompress(LastBeginEntry.Name, UBuf, LastBeginEntry.Name.size() * 2) 
                == llvm::zlib::StatusOK )
          LastBeginEntry.Name.assign(UBuf.begin(), UBuf.end());
        else
          LastBeginEntry.Name = "";
        break;
      }
      case llvm::protobuf::getVarIntWire<3>::value: {
        LastBeginEntry.Name = templateNameMap[llvm::protobuf::loadVarInt(aSubBuffer)];
        break;
      }
      default:
        llvm::protobuf::skipData(aSubBuffer, cur_wire);
        break;
    }
  }
  
}

void TemplightProtobufReader::loadBeginEntry(llvm::StringRef aSubBuffer) {
  // Set default values:
  LastBeginEntry.SynthesisKind = 0;
  LastBeginEntry.Name = "";
  LastBeginEntry.TimeStamp = 0.0;
  LastBeginEntry.MemoryUsage = 0;
  
  while ( aSubBuffer.size() ) {
    unsigned int cur_wire = llvm::protobuf::loadVarInt(aSubBuffer);
    switch( cur_wire ) {
      case llvm::protobuf::getVarIntWire<1>::value:
        LastBeginEntry.SynthesisKind = llvm::protobuf::loadVarInt(aSubBuffer);
        break;
      case llvm::protobuf::getStringWire<2>::value: {
        std::uint64_t cur_size = llvm::protobuf::loadVarInt(aSubBuffer);
        loadTemplateName(aSubBuffer.slice(0, cur_size));
        aSubBuffer = aSubBuffer.drop_front(cur_size);
        break;
      }
      case llvm::protobuf::getStringWire<3>::value: {
        std::uint64_t cur_size = llvm::protobuf::loadVarInt(aSubBuffer);
        loadLocation(aSubBuffer.slice(0, cur_size), fileNameMap, 
          LastBeginEntry.FileName, LastBeginEntry.Line, LastBeginEntry.Column);
        aSubBuffer = aSubBuffer.drop_front(cur_size);
        break;
      }
      case llvm::protobuf::getDoubleWire<4>::value:
        LastBeginEntry.TimeStamp = llvm::protobuf::loadDouble(aSubBuffer);
        break;
      case llvm::protobuf::getVarIntWire<5>::value:
        LastBeginEntry.MemoryUsage = llvm::protobuf::loadVarInt(aSubBuffer);
        break;
      case llvm::protobuf::getStringWire<6>::value: {
        std::uint64_t cur_size = llvm::protobuf::loadVarInt(aSubBuffer);
        loadLocation(aSubBuffer.slice(0, cur_size), fileNameMap, 
          LastBeginEntry.TempOri_FileName, LastBeginEntry.TempOri_Line, LastBeginEntry.TempOri_Column);
        aSubBuffer = aSubBuffer.drop_front(cur_size);
        break;
      }
      default:
        llvm::protobuf::skipData(aSubBuffer, cur_wire);
        break;
    }
  }
  
  LastChunk = TemplightProtobufReader::BeginEntry;
}

void TemplightProtobufReader::loadEndEntry(llvm::StringRef aSubBuffer) {
  // Set default values:
  LastEndEntry.TimeStamp = 0.0;
  LastEndEntry.MemoryUsage = 0;
  
  while ( aSubBuffer.size() ) {
    unsigned int cur_wire = llvm::protobuf::loadVarInt(aSubBuffer);
    switch( cur_wire ) {
      case llvm::protobuf::getDoubleWire<1>::value:
        LastEndEntry.TimeStamp = llvm::protobuf::loadDouble(aSubBuffer);
        break;
      case llvm::protobuf::getVarIntWire<2>::value:
        LastEndEntry.MemoryUsage = llvm::protobuf::loadVarInt(aSubBuffer);
        break;
      default:
        llvm::protobuf::skipData(aSubBuffer, cur_wire);
        break;
    }
  }
  
  LastChunk = TemplightProtobufReader::EndEntry;
}

TemplightProtobufReader::LastChunkType 
    TemplightProtobufReader::startOnBuffer(llvm::StringRef aBuffer) {
  buffer = aBuffer;
  fileNameMap.clear();
  unsigned int cur_wire = llvm::protobuf::loadVarInt(buffer);
  if ( cur_wire != llvm::protobuf::getStringWire<1>::value ) {
    buffer = llvm::StringRef();
    remainder_buffer = llvm::StringRef();
    LastChunk = TemplightProtobufReader::EndOfFile;
    return LastChunk;
  }
  std::uint64_t cur_size = llvm::protobuf::loadVarInt(buffer);
  remainder_buffer = buffer.slice(cur_size, buffer.size());
  buffer = buffer.slice(0, cur_size);
  return next();
}

TemplightProtobufReader::LastChunkType TemplightProtobufReader::next() {
  if ( buffer.empty() ) {
    if ( remainder_buffer.empty() ) {
      LastChunk = TemplightProtobufReader::EndOfFile;
      return LastChunk;
    } else {
      return startOnBuffer(remainder_buffer);
    }
  }
  unsigned int cur_wire = llvm::protobuf::loadVarInt(buffer);
  switch(cur_wire) {
    case llvm::protobuf::getStringWire<1>::value: {
      std::uint64_t cur_size = llvm::protobuf::loadVarInt(buffer);
      loadHeader(buffer.slice(0, cur_size));
      buffer = buffer.drop_front(cur_size);
      return LastChunk;
    };
    case llvm::protobuf::getStringWire<2>::value: {
      std::uint64_t cur_size = llvm::protobuf::loadVarInt(buffer);
      llvm::StringRef sub_buffer = buffer.slice(0, cur_size);
      buffer = buffer.drop_front(cur_size);
      cur_wire = llvm::protobuf::loadVarInt(sub_buffer);
      cur_size = llvm::protobuf::loadVarInt(sub_buffer);
      switch( cur_wire ) {
        case llvm::protobuf::getStringWire<1>::value:
          loadBeginEntry(sub_buffer);
          break;
        case llvm::protobuf::getStringWire<2>::value:
          loadEndEntry(sub_buffer);
          break;
        default: // ignore for fwd-compat.
          break;
      };
      return LastChunk;
    };
    case llvm::protobuf::getStringWire<3>::value: {
      std::uint64_t cur_size = llvm::protobuf::loadVarInt(buffer);
      loadDictionaryEntry(buffer.slice(0, cur_size));
      buffer = buffer.drop_front(cur_size);
      LastChunk = TemplightProtobufReader::Other;
      return LastChunk;
    };
    default: { // ignore for fwd-compat.
      llvm::protobuf::skipData(buffer, cur_wire);
      return next(); // tail-call
    };
  }
}


} // namespace clang

