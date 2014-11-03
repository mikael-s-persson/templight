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

#include <llvm/Option/ArgList.h>
#include <llvm/Option/OptTable.h>
#include <llvm/Option/Option.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/raw_ostream.h>

#include "TemplightProtobufReader.h"
#include "TemplightEntryPrinter.h"

#include "TemplightExtraWriters.h"
#include "TemplightProtobufWriter.h"

#include <memory>
#include <string>
#include <set>
#include <system_error>

using namespace llvm::opt;
using namespace llvm;


// Mark all Templight options with this category, everything else will be handled as clang driver options.
static cl::OptionCategory TemplightConvCategory("Templight/Convert options (USAGE: templight-convert [options] [input-file])");

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

static cl::opt<int> OutputCompression("compression",
  cl::desc("Specify the compression level of Templight outputs whenever the format allows."),
  cl::init(0), cl::cat(TemplightConvCategory));
static cl::alias OutputCompressionA("c", cl::desc("Alias for -compression"), cl::aliasopt(OutputCompression));


static void CreateTemplightWriter(clang::TemplightEntryPrinter& printer) {
  const std::string& Format = OutputFormat;
  int Compression = OutputCompression;
  
  if ( !printer.getTraceStream() ) {
    llvm::errs() << "Error: [Templight-Tracer] Failed to create template trace file!";
    return;
  }
  
  if ( ( Format.empty() ) || ( Format == "yaml" ) ) {
    printer.takeWriter(new clang::TemplightYamlWriter(*printer.getTraceStream()));
  }
  else if ( Format == "xml" ) {
    printer.takeWriter(new clang::TemplightXmlWriter(*printer.getTraceStream()));
  }
  else if ( Format == "text" ) {
    printer.takeWriter(new clang::TemplightTextWriter(*printer.getTraceStream()));
  }
  else if ( Format == "graphml" ) {
    printer.takeWriter(new clang::TemplightGraphMLWriter(*printer.getTraceStream()));
  }
  else if ( Format == "graphviz" ) {
    printer.takeWriter(new clang::TemplightGraphVizWriter(*printer.getTraceStream()));
  }
  else if ( Format == "nestedxml" ) {
    printer.takeWriter(new clang::TemplightNestedXMLWriter(*printer.getTraceStream()));
  }
  else if ( Format == "protobuf" ) {
    printer.takeWriter(new clang::TemplightProtobufWriter(*printer.getTraceStream(),Compression));
  }
  else {
    llvm::errs() << "Error: [Templight-Tracer] Unrecognized template trace format:" << Format;
    return;
  }
}


int main(int argc_, const char **argv_) {
//   sys::PrintStackTraceOnErrorSignal();
//   PrettyStackTraceProgram X(argc_, argv_);
  
  cl::ParseCommandLineOptions(
      argc_, argv_,
      "A tool to convert the template instantiation profiles produced by the templight tool.\n");
  
  if ( InputFilenames.empty() ) {
    InputFilenames.push_back("-");
  }
  
  {
    clang::TemplightEntryPrinter printer(OutputFilename);
    CreateTemplightWriter(printer);
    bool was_inited = false;
    
    if( ! BlackListFilename.empty() ) {
      printer.readBlacklists(BlackListFilename);
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
            if ( was_inited ) 
              printer.finalize();
            printer.initialize(pbf_reader.SourceName);
            was_inited = true;
            pbf_reader.next();
            break;
          case clang::TemplightProtobufReader::BeginEntry:
            printer.printEntry(pbf_reader.LastBeginEntry);
            pbf_reader.next();
            break;
          case clang::TemplightProtobufReader::EndEntry:
            printer.printEntry(pbf_reader.LastEndEntry);
            pbf_reader.next();
            break;
          case clang::TemplightProtobufReader::Other:
          default:
            pbf_reader.next();
            break;
        }
      }
    }
    if ( was_inited )
      printer.finalize();
    
  }
  
  return 0;
}


