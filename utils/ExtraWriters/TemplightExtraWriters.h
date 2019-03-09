//===- TemplightProtobufWriter.h --------------------*- C++ -*-------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TEMPLIGHT_EXTRA_WRITERS_H
#define LLVM_CLANG_TEMPLIGHT_EXTRA_WRITERS_H

#include "PrintableTemplightEntries.h"

#include <llvm/Support/raw_ostream.h>

#include <memory>
#include <string>

namespace llvm {
namespace yaml {
class Output;
}
} // namespace llvm

namespace clang {

class TemplightYamlWriter : public TemplightWriter {
public:
  TemplightYamlWriter(llvm::raw_ostream &aOS);
  ~TemplightYamlWriter();

  void initialize(const std::string &aSourceName = "") override;
  void finalize() override;

  void printEntry(const PrintableTemplightEntryBegin &aEntry) override;
  void printEntry(const PrintableTemplightEntryEnd &aEntry) override;

private:
  std::unique_ptr<llvm::yaml::Output> Output;
};

class TemplightXmlWriter : public TemplightWriter {
public:
  TemplightXmlWriter(llvm::raw_ostream &aOS);
  ~TemplightXmlWriter();

  void initialize(const std::string &aSourceName = "") override;
  void finalize() override;

  void printEntry(const PrintableTemplightEntryBegin &aEntry) override;
  void printEntry(const PrintableTemplightEntryEnd &aEntry) override;
};

class TemplightTextWriter : public TemplightWriter {
public:
  TemplightTextWriter(llvm::raw_ostream &aOS);
  ~TemplightTextWriter();

  void initialize(const std::string &aSourceName = "") override;
  void finalize() override;

  void printEntry(const PrintableTemplightEntryBegin &aEntry) override;
  void printEntry(const PrintableTemplightEntryEnd &aEntry) override;
};

struct RecordedDFSEntryTree;
struct EntryTraversalTask;

class TemplightTreeWriter : public TemplightWriter {
public:
  TemplightTreeWriter(llvm::raw_ostream &aOS);
  ~TemplightTreeWriter();

  void initialize(const std::string &aSourceName = "") override;
  void finalize() override;

  void printEntry(const PrintableTemplightEntryBegin &aEntry) override;
  void printEntry(const PrintableTemplightEntryEnd &aEntry) override;

protected:
  virtual void openPrintedTreeNode(const EntryTraversalTask &aNode) = 0;
  virtual void closePrintedTreeNode(const EntryTraversalTask &aNode) = 0;

  virtual void initializeTree(const std::string &aSourceName) = 0;
  virtual void finalizeTree() = 0;

  std::unique_ptr<RecordedDFSEntryTree> p_tree;
};

class TemplightNestedXMLWriter : public TemplightTreeWriter {
public:
  TemplightNestedXMLWriter(llvm::raw_ostream &aOS);
  ~TemplightNestedXMLWriter();

protected:
  void openPrintedTreeNode(const EntryTraversalTask &aNode) override;
  void closePrintedTreeNode(const EntryTraversalTask &aNode) override;

  void initializeTree(const std::string &aSourceName = "") override;
  void finalizeTree() override;
};

class TemplightGraphMLWriter : public TemplightTreeWriter {
public:
  TemplightGraphMLWriter(llvm::raw_ostream &aOS);
  ~TemplightGraphMLWriter();

protected:
  void openPrintedTreeNode(const EntryTraversalTask &aNode) override;
  void closePrintedTreeNode(const EntryTraversalTask &aNode) override;

  void initializeTree(const std::string &aSourceName = "") override;
  void finalizeTree() override;

private:
  int last_edge_id;
};

class TemplightGraphVizWriter : public TemplightTreeWriter {
public:
  TemplightGraphVizWriter(llvm::raw_ostream &aOS);
  ~TemplightGraphVizWriter();

protected:
  void openPrintedTreeNode(const EntryTraversalTask &aNode) override;
  void closePrintedTreeNode(const EntryTraversalTask &aNode) override;

  void initializeTree(const std::string &aSourceName = "") override;
  void finalizeTree() override;
};

/*
class ProtobufPrinter : public TemplightTracer::TracePrinter {
protected:
  void startTraceImpl() override {
    // get the source name from the source manager:
    FileID fileID = TheSema.getSourceManager().getMainFileID();
    std::string src_name =
      TheSema.getSourceManager().getFileEntryForID(fileID)->getName();
    Writer.initialize(src_name);
  };
  void endTraceImpl() override {
    Writer.finalize();
    Writer.dumpOnStream(*this->TraceOS);
  };

  void printEntryImpl(const PrintableTemplightEntryBegin& Entry) override {
    Writer.printEntry(Entry);
  };

  void printEntryImpl(const PrintableTemplightEntryEnd& Entry) override {
    Writer.printEntry(Entry);
  };

public:

  ProtobufPrinter(const Sema &aSema, const std::string &Output) :
                  TemplightTracer::TracePrinter(aSema, Output) { };

private:
  TemplightProtobufWriter Writer;
};
*/

} // namespace clang

#endif
