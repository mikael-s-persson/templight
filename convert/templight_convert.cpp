//===-- templight_driver.cpp - Clang GCC-Compatible Driver --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the entry point to the templight driver; it is a thin wrapper
// for functionality in the Driver clang library with modifications to invoke Templight.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"

#include "TemplightProtobufReader.h"

#include <set>
#include <system_error>

using namespace llvm::opt;
using namespace llvm;


// Mark all Templight options with this category, everything else will be handled as clang driver options.
static cl::OptionCategory TemplightConvCategory("Templight/Convert options (USAGE: templight-convert [options] [input-file])");

static cl::opt<bool> IgnoreSystemInst("ignore-system",
  cl::desc("Ignore any template instantiation coming from \n" 
           "system-includes (-isystem)."),
  cl::cat(TemplightConvCategory));

static cl::opt<std::string> OutputFilename("output",
  cl::desc("Write Templight profiling traces to <output-file>."), cl::value_desc("output-file"),
  cl::cat(TemplightConvCategory));
static cl::alias OutputFilenameA("o", cl::desc("Alias for -output"), cl::aliasopt(OutputFilename));

static cl::opt<std::string> OutputFormat("format",
  cl::desc("Specify the format of Templight outputs (yaml / xml / text / graphml / graphviz / nestedxml / protobuf, default is yaml)."),
  cl::init("yaml"), cl::cat(TemplightConvCategory));
static cl::alias OutputFormatA("f", cl::desc("Alias for -format"), cl::aliasopt(OutputFormat));

static cl::opt<std::string> BlackListFilename("blacklist",
  cl::desc("Use regex expressions in <file> to filter out undesirable traces."), cl::value_desc("blacklist-file"),
  cl::cat(TemplightConvCategory));
static cl::alias BlackListFilenameA("b", cl::desc("Alias for -blacklist"), cl::aliasopt(BlackListFilename));

static cl::list<std::string> InputFilenames(cl::Positional, 
  cl::desc("<input files>"),
  cl::cat(TemplightConvCategory));


int main(int argc_, const char **argv_) {
//   sys::PrintStackTraceOnErrorSignal();
//   PrettyStackTraceProgram X(argc_, argv_);
  
  cl::ParseCommandLineOptions(
      argc_, argv_,
      "A tool to convert the template instantiation profiles produced by the templight tool.\n");
  
  if ( InputFilenames.empty() ) {
    InputFilenames.push_back("-");
  }
  
  for(unsigned int i = 0; i < InputFilenames.size(); ++i) {
    ErrorOr< std::unique_ptr< MemoryBuffer > > p_buf = MemoryBuffer::getFileOrSTDIN(Twine(InputFilenames[i]));
    if ( !p_buf || !p_buf.get() )
      continue;
    clang::TemplightProtobufReader pbf_reader;
    pbf_reader.startOnBuffer(StringRef(p_buf.get()->getBufferStart(), p_buf.get()->getBufferSize()));
    while ( pbf_reader.LastChunk != clang::TemplightProtobufReader::EndOfFile ) {
      switch ( pbf_reader.LastChunk ) {
        case clang::TemplightProtobufReader::EndOfFile:
          break;
        case clang::TemplightProtobufReader::Header:
          /* FIXME not sure there is much to do here */
          pbf_reader.next();
          break;
        case clang::TemplightProtobufReader::BeginEntry:
          outs() << "Beginning entry: " << pbf_reader.LastBeginEntry.Name << "\n";
          pbf_reader.next();
          break;
        case clang::TemplightProtobufReader::EndEntry:
          outs() << "Ending entry.\n";
          pbf_reader.next();
          break;
        case clang::TemplightProtobufReader::Other:
        default:
          pbf_reader.next();
          break;
      }
    }
  }
  
  
  return 0;
}


