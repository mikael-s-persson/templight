//===- TemplightEntryPrinter.cpp ------ Clang Templight Entry Printer -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TemplightEntryPrinter.h"

#include "TemplightExtraWriters.h"
// #include "TemplightProtobufWriter.h"

#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Regex.h>


namespace clang {


void TemplightEntryPrinter::skipEntry(const PrintableTemplightEntryBegin &Entry) {
  if ( !CurrentSkippedEntry.Name.empty() )
    return; // Already skipping entries.
  CurrentSkippedEntry = Entry;
  SkippedEndingsCount = 1;
}

bool TemplightEntryPrinter::shouldIgnoreEntry(const PrintableTemplightEntryBegin &Entry) {
  // Check the black-lists:
  // (1) Is currently ignoring entries?
  if ( !CurrentSkippedEntry.Name.empty() ) {
    ++SkippedEndingsCount;
    return true;
  }
  // (2) Regexes:
  if ( ( CoRegex && ( CoRegex->match(Entry.Name) ) ) || 
       ( IdRegex && ( IdRegex->match(Entry.Name) ) ) ) {
    skipEntry(Entry);
    return true;
  }
  
  return false;
}

bool TemplightEntryPrinter::shouldIgnoreEntry(const PrintableTemplightEntryEnd &Entry) {
  // Check the black-lists:
  // (1) Is currently ignoring entries?
  if ( !CurrentSkippedEntry.Name.empty() ) {
    --SkippedEndingsCount;
    // Should skip the entry, but need to check if it's the last entry to skip:
    if ( SkippedEndingsCount == 0 ) {
      CurrentSkippedEntry.Name = "";
    }
    return true;
  }
  return false;
}


void TemplightEntryPrinter::printEntry(const PrintableTemplightEntryBegin &Entry) {
  if ( shouldIgnoreEntry(Entry) )
    return;
  
  if ( p_writer )
    p_writer->printEntry(Entry);
}

void TemplightEntryPrinter::printEntry(const PrintableTemplightEntryEnd &Entry) {
  if ( shouldIgnoreEntry(Entry) )
    return;
  
  if ( p_writer )
    p_writer->printEntry(Entry);
}

void TemplightEntryPrinter::initialize(const std::string& SourceName) {
  if ( p_writer )
    p_writer->initialize(SourceName);
}

void TemplightEntryPrinter::finalize() {
  if ( p_writer )
    p_writer->finalize();
}

TemplightEntryPrinter::TemplightEntryPrinter(const std::string &Output, const std::string &Format) : TraceOS(0) {
  CurrentSkippedEntry.Name = "";
  if ( Output == "-" ) {
    TraceOS = &llvm::outs();
  } else {
    std::error_code error;
    TraceOS = new llvm::raw_fd_ostream(Output, error, llvm::sys::fs::F_None);
    if ( error ) {
      llvm::errs() <<
        "Error: [Templight] Can not open file to write trace of template instantiations: "
        << Output << " Error: " << error.message();
      TraceOS = 0;
      return;
    }
  }
  
  if ( ( Format.empty() ) || ( Format == "yaml" ) ) {
    p_writer.reset(new TemplightYamlWriter(*TraceOS));
  }
  else if ( Format == "xml" ) {
    p_writer.reset(new TemplightXmlWriter(*TraceOS));
  }
  else if ( Format == "text" ) {
    p_writer.reset(new TemplightTextWriter(*TraceOS));
  }
  else if ( Format == "graphml" ) {
    p_writer.reset(new TemplightGraphMLWriter(*TraceOS));
  }
  else if ( Format == "graphviz" ) {
    p_writer.reset(new TemplightGraphVizWriter(*TraceOS));
  }
  else if ( Format == "nestedxml" ) {
    p_writer.reset(new TemplightNestedXMLWriter(*TraceOS));
  }
  else {
    llvm::errs() << "Error: [Templight-Tracer] Unrecognized template trace format:" << Format;
    return;
  }
  
}

TemplightEntryPrinter::~TemplightEntryPrinter() {
  p_writer.reset(); // Delete writer before the trace-OS.
  if ( TraceOS ) {
    TraceOS->flush();
    if ( TraceOS != &llvm::outs() )
      delete TraceOS;
  }
}

bool TemplightEntryPrinter::isValid() const { return p_writer.get(); }


}

