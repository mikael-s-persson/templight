//===- TemplightEntryPrinter.cpp --------------------*- C++ -*-------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TemplightEntryPrinter.h"

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Regex.h>
#include <llvm/Support/raw_ostream.h>

namespace clang {

void TemplightEntryPrinter::skipEntry() {
  if (SkippedEndingsCount) {
    ++SkippedEndingsCount; // note: this is needed because skipped entries are
                           // begin entries for which "shouldIgnoreEntry" is
                           // never called.
    return;                // Already skipping entries.
  }
  SkippedEndingsCount = 1;
}

bool TemplightEntryPrinter::shouldIgnoreEntry(
    const PrintableTemplightEntryBegin &Entry) {
  // Check the black-lists:
  // (1) Is currently ignoring entries?
  if (SkippedEndingsCount) {
    ++SkippedEndingsCount;
    return true;
  }
  // (2) Regexes:
  if ((CoRegex && (CoRegex->match(Entry.Name))) ||
      (IdRegex && (IdRegex->match(Entry.Name)))) {
    skipEntry();
    return true;
  }

  return false;
}

bool TemplightEntryPrinter::shouldIgnoreEntry(
    const PrintableTemplightEntryEnd &Entry) {
  // Check the black-lists:
  // (1) Is currently ignoring entries?
  if (SkippedEndingsCount) {
    --SkippedEndingsCount;
    return true;
  }
  return false;
}

void TemplightEntryPrinter::printEntry(
    const PrintableTemplightEntryBegin &Entry) {
  if (shouldIgnoreEntry(Entry))
    return;

  if (p_writer)
    p_writer->printEntry(Entry);
}

void TemplightEntryPrinter::printEntry(
    const PrintableTemplightEntryEnd &Entry) {
  if (shouldIgnoreEntry(Entry))
    return;

  if (p_writer)
    p_writer->printEntry(Entry);
}

void TemplightEntryPrinter::initialize(const std::string &SourceName) {
  if (p_writer)
    p_writer->initialize(SourceName);
}

void TemplightEntryPrinter::finalize() {
  if (p_writer)
    p_writer->finalize();
}

TemplightEntryPrinter::TemplightEntryPrinter(const std::string &Output)
    : SkippedEndingsCount(0), TraceOS(0) {
  if (Output == "-") {
    TraceOS = &llvm::outs();
  } else {
    std::error_code error;
    TraceOS = new llvm::raw_fd_ostream(Output, error, llvm::sys::fs::OF_None);
    if (error) {
      llvm::errs() << "Error: [Templight] Can not open file to write trace of "
                      "template instantiations: "
                   << Output << " Error: " << error.message();
      TraceOS = 0;
      return;
    }
  }
}

TemplightEntryPrinter::~TemplightEntryPrinter() {
  p_writer.reset(); // Delete writer before the trace-OS.
  if (TraceOS) {
    TraceOS->flush();
    if (TraceOS != &llvm::outs())
      delete TraceOS;
  }
}

bool TemplightEntryPrinter::isValid() const { return p_writer.get(); }

llvm::raw_ostream *TemplightEntryPrinter::getTraceStream() const {
  return TraceOS;
}

void TemplightEntryPrinter::takeWriter(TemplightWriter *aPWriter) {
  p_writer.reset(aPWriter);
}

void TemplightEntryPrinter::readBlacklists(const std::string &BLFilename) {
  if (BLFilename.empty()) {
    CoRegex.reset();
    IdRegex.reset();
    return;
  }

  std::string CoPattern, IdPattern;

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> file_epbuf =
      llvm::MemoryBuffer::getFile(llvm::Twine(BLFilename));
  if (!file_epbuf || (!file_epbuf.get())) {
    llvm::errs()
        << "Error: [Templight-Action] Could not open the blacklist file!\n";
    CoRegex.reset();
    IdRegex.reset();
    return;
  }

  llvm::Regex findCo("^context ");
  llvm::Regex findId("^identifier ");

  const char *it = file_epbuf.get()->getBufferStart();
  const char *it_mark = file_epbuf.get()->getBufferStart();
  const char *it_end = file_epbuf.get()->getBufferEnd();

  while (it_mark != it_end) {
    it_mark = std::find(it, it_end, '\n');
    if (*(it_mark - 1) == '\r')
      --it_mark;
    llvm::StringRef curLine(&(*it), it_mark - it);
    if (findCo.match(curLine)) {
      if (!CoPattern.empty())
        CoPattern += '|';
      CoPattern += '(';
      CoPattern.append(&(*(it + 8)), it_mark - it - 8);
      CoPattern += ')';
    } else if (findId.match(curLine)) {
      if (!IdPattern.empty())
        IdPattern += '|';
      IdPattern += '(';
      IdPattern.append(&(*(it + 11)), it_mark - it - 11);
      IdPattern += ')';
    }
    while ((it_mark != it_end) && ((*it_mark == '\n') || (*it_mark == '\r')))
      ++it_mark;
    it = it_mark;
  }

  CoRegex.reset(new llvm::Regex(CoPattern));
  IdRegex.reset(new llvm::Regex(IdPattern));
  return;
}

} // namespace clang
