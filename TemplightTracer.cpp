//===- TemplightTracer.cpp ------ Clang Templight Profiler / Tracer -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TemplightTracer.h"

#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Sema/ActiveTemplateInst.h"
#include "clang/Sema/Sema.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/YAMLTraits.h"

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace clang {

namespace {

struct RawTraceEntry {
  bool IsTemplateBegin;
  ActiveTemplateInstantiation::InstantiationKind InstantiationKind;
  Decl *Entity;
  SourceLocation PointOfInstantiation;
  double TimeStamp;
  std::uint64_t MemoryUsage;
  
  RawTraceEntry() : IsTemplateBegin(true), 
    InstantiationKind(ActiveTemplateInstantiation::TemplateInstantiation),
    Entity(0), TimeStamp(0.0), MemoryUsage(0) { };
};

struct PrintableTraceEntryBegin {
  std::string InstantiationKind;
  std::string Name;
  std::string FileName;
  int Line;
  int Column;
  double TimeStamp;
  std::uint64_t MemoryUsage;
};

struct PrintableTraceEntryEnd {
  double TimeStamp;
  std::uint64_t MemoryUsage;
};


const char* const InstantiationKindStrings[] = { 
  "TemplateInstantiation",
  "DefaultTemplateArgumentInstantiation",
  "DefaultFunctionArgumentInstantiation",
  "ExplicitTemplateArgumentSubstitution",
  "DeducedTemplateArgumentSubstitution", 
  "PriorTemplateArgumentSubstitution",
  "DefaultTemplateArgumentChecking", 
  "ExceptionSpecInstantiation",
  "Memoization" };


PrintableTraceEntryBegin rawToPrintableBegin(const Sema &TheSema, const RawTraceEntry& Entry) {
  PrintableTraceEntryBegin Ret;
  
  Ret.InstantiationKind = InstantiationKindStrings[Entry.InstantiationKind];
  
  NamedDecl *NamedTemplate = dyn_cast_or_null<NamedDecl>(Entry.Entity);
  if (NamedTemplate) {
    llvm::raw_string_ostream OS(Ret.Name);
    NamedTemplate->getNameForDiagnostic(OS, TheSema.getLangOpts(), true);
  }
  
  PresumedLoc Loc = TheSema.getSourceManager().getPresumedLoc(Entry.PointOfInstantiation);
  if (!Loc.isInvalid()) {
    Ret.FileName = Loc.getFilename();
    Ret.Line = Loc.getLine();
    Ret.Column = Loc.getColumn();
  } else {
    Ret.FileName = "";
    Ret.Line = 0;
    Ret.Column = 0;
  }
  
  Ret.TimeStamp = Entry.TimeStamp;
  Ret.MemoryUsage = Entry.MemoryUsage;
  
  return Ret;
}

PrintableTraceEntryEnd rawToPrintableEnd(const Sema &TheSema, const RawTraceEntry& Entry) {
  return {Entry.TimeStamp, Entry.MemoryUsage};
}


std::string escapeXml(const std::string& Input) {
  std::string Result;
  Result.reserve(64);

  unsigned i, pos = 0;
  for (i = 0; i < Input.length(); ++i) {
    if (Input[i] == '<' || Input[i] == '>' || Input[i] == '"'
      || Input[i] == '\'' || Input[i] == '&') {
      Result.insert(Result.length(), Input, pos, i - pos);
      pos = i + 1;
      switch (Input[i]) {
      case '<':
        Result += "&lt;";
        break;
      case '>':
        Result += "&gt;";
        break;
      case '\'':
        Result += "&apos;";
        break;
      case '"':
        Result += "&quot;";
        break;
      case '&':
        Result += "&amp;";
        break;
      default:
        break;
      }
    }
  }
  Result.insert(Result.length(), Input, pos, i - pos);
  return Result;
}


} // unnamed namespace

} // namespace clang




namespace llvm {
namespace yaml {

template <>
struct ScalarEnumerationTraits<
    clang::ActiveTemplateInstantiation::InstantiationKind> {
  static void enumeration(IO &io,
    clang::ActiveTemplateInstantiation::InstantiationKind &value) {

#define def_enum_case(e) \
  io.enumCase(value, InstantiationKindStrings[e], e)

    using namespace clang;
    def_enum_case(ActiveTemplateInstantiation::TemplateInstantiation);
    def_enum_case(ActiveTemplateInstantiation::DefaultTemplateArgumentInstantiation);
    def_enum_case(ActiveTemplateInstantiation::DefaultFunctionArgumentInstantiation);
    def_enum_case(ActiveTemplateInstantiation::ExplicitTemplateArgumentSubstitution);
    def_enum_case(ActiveTemplateInstantiation::DeducedTemplateArgumentSubstitution);
    def_enum_case(ActiveTemplateInstantiation::PriorTemplateArgumentSubstitution);
    def_enum_case(ActiveTemplateInstantiation::DefaultTemplateArgumentChecking);
    def_enum_case(ActiveTemplateInstantiation::ExceptionSpecInstantiation);
    def_enum_case(ActiveTemplateInstantiation::Memoization);

#undef def_enum_case
  }
};

template <>
struct MappingTraits<clang::PrintableTraceEntryBegin> {
  static void mapping(IO &io, clang::PrintableTraceEntryBegin &Entry) {
    bool b = true;
    io.mapRequired("IsBegin", b);
    io.mapRequired("Kind", Entry.InstantiationKind);
    io.mapOptional("Name", Entry.Name);
    std::string loc = Entry.FileName + "|" + 
                      std::to_string(Entry.Line) + "|" + 
                      std::to_string(Entry.Column);
    io.mapOptional("Location", loc);
    io.mapRequired("TimeStamp", Entry.TimeStamp);
    io.mapOptional("MemoryUsage", Entry.MemoryUsage);
  }
};

template <>
struct MappingTraits<clang::PrintableTraceEntryEnd> {
  static void mapping(IO &io, clang::PrintableTraceEntryEnd &Entry) {
    bool b = false;
    io.mapRequired("IsBegin", b);
    io.mapRequired("TimeStamp", Entry.TimeStamp);
    io.mapOptional("MemoryUsage", Entry.MemoryUsage);
  }
};

} // namespace yaml
} // namespace llvm



namespace clang {

class TemplightTracer::TracePrinter {
protected:
  virtual void startTraceImpl() = 0;
  virtual void endTraceImpl() = 0;
  virtual void printEntryImpl(const PrintableTraceEntryBegin &) = 0;
  virtual void printEntryImpl(const PrintableTraceEntryEnd &) = 0;
  
public:
  
  void skipEntry(const RawTraceEntry &Entry) {
    if ( CurrentSkippedEntry.IsTemplateBegin )
      return; // Already skipping entries.
    if ( !Entry.IsTemplateBegin )
      return; // Cannot skip entry that has ended already.
    CurrentSkippedEntry = Entry;
  };
  
  bool shouldIgnoreEntry(const RawTraceEntry &Entry) {
    
    // Check the black-lists:
    // (1) Is currently ignoring entries?
    if ( CurrentSkippedEntry.IsTemplateBegin ) {
      // Should skip the entry, but need to check if it's the last entry to skip:
      if ( !Entry.IsTemplateBegin
           && ( CurrentSkippedEntry.InstantiationKind == Entry.InstantiationKind )
           && ( CurrentSkippedEntry.Entity == Entry.Entity ) ) {
        CurrentSkippedEntry.IsTemplateBegin = false;
      }
      return true;
    }
    // (2) Context:
    if ( CoRegex ) {
      if ( NamedDecl* p_context = dyn_cast_or_null<NamedDecl>(Entry.Entity->getDeclContext()) ) {
        std::string co_name;
        llvm::raw_string_ostream OS(co_name);
        p_context->getNameForDiagnostic(OS, TheSema.getLangOpts(), true);
        OS.str(); // flush to string.
        if ( CoRegex->match(co_name) ) {
          skipEntry(Entry);
          return true;
        }
      }
    }
    // (3) Identifier:
    if ( IdRegex ) {
      if ( NamedDecl* p_ndecl = dyn_cast_or_null<NamedDecl>(Entry.Entity) ) {
        std::string id_name;
        llvm::raw_string_ostream OS(id_name);
        p_ndecl->getNameForDiagnostic(OS, TheSema.getLangOpts(), true);
        OS.str(); // flush to string.
        if ( IdRegex->match(id_name) ) {
          skipEntry(Entry);
          return true;
        }
      }
    }
    
    // Avoid some duplication of memoization entries:
    if ( Entry.InstantiationKind == ActiveTemplateInstantiation::Memoization ) {
      if ( !LastBeginEntry.IsTemplateBegin
           && ( LastBeginEntry.InstantiationKind == Entry.InstantiationKind )
           && ( LastBeginEntry.Entity == Entry.Entity ) ) {
        return true;
      }
    }
    
    return false;
  };
  
  virtual void printRawEntry(const RawTraceEntry &Entry) {
    if ( shouldIgnoreEntry(Entry) )
      return;

    if ( Entry.IsTemplateBegin )
      this->printEntryImpl(rawToPrintableBegin(TheSema, Entry));
    else
      this->printEntryImpl(rawToPrintableEnd(TheSema, Entry));
    
    TraceOS->flush();
    if ( Entry.IsTemplateBegin )
      LastBeginEntry = Entry;
    if ( !Entry.IsTemplateBegin && 
         ( Entry.InstantiationKind == ActiveTemplateInstantiation::Memoization ) )
      LastBeginEntry.IsTemplateBegin = false;
  };
  
  virtual void printCachedEntries() {
    for(std::vector<RawTraceEntry>::iterator it = TraceEntries.begin();
        it != TraceEntries.end(); ++it) {
      if ( it->IsTemplateBegin )
        this->printEntryImpl(rawToPrintableBegin(TheSema, *it));
      else
        this->printEntryImpl(rawToPrintableEnd(TheSema, *it));
    }
    TraceEntries.clear();
  };
  
  virtual void cacheEntry(const RawTraceEntry &Entry) {
    if ( shouldIgnoreEntry(Entry) )
      return;
    TraceEntries.push_back(Entry);
    if ( Entry.IsTemplateBegin )
      LastBeginEntry = Entry;
    if ( !Entry.IsTemplateBegin && 
         ( Entry.InstantiationKind == ActiveTemplateInstantiation::Memoization ) )
      LastBeginEntry.IsTemplateBegin = false;
    if ( !Entry.IsTemplateBegin &&
         ( Entry.InstantiationKind == TraceEntries.front().InstantiationKind ) &&
         ( Entry.Entity == TraceEntries.front().Entity ) )
      printCachedEntries();
  };
  
  void startTrace() {
    this->startTraceImpl();
  };
  
  void endTrace() {
    printCachedEntries();
    this->endTraceImpl();
    TraceOS->flush();
  };
  
  TracePrinter(const Sema &aSema, const std::string &Output) : TheSema(aSema), TraceOS(0) {
    CurrentSkippedEntry.IsTemplateBegin = false;
    if ( Output == "stdout" ) {
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
  };
  
  virtual ~TracePrinter() {
    if ( TraceOS ) {
      TraceOS->flush();
      if ( TraceOS != &llvm::outs() )
        delete TraceOS;
    }
  };
  
  const Sema &TheSema;
  
  std::vector<RawTraceEntry> TraceEntries;
  RawTraceEntry LastBeginEntry;
  
  RawTraceEntry CurrentSkippedEntry;
  std::unique_ptr<llvm::Regex> CoRegex;
  std::unique_ptr<llvm::Regex> IdRegex;
  
  llvm::raw_ostream* TraceOS;
  
  bool isValid() const { return TraceOS; };
};




namespace {

class YamlPrinter : public TemplightTracer::TracePrinter {
protected:
  void startTraceImpl() override {
    Output.reset(new llvm::yaml::Output(*this->TraceOS));
    Output->beginDocuments();
    Output->beginSequence();
  };
  void endTraceImpl() override {
    Output->endSequence();
    Output->endDocuments();
  };
  
  void printEntryImpl(const PrintableTraceEntryBegin& Entry) override {
    void *SaveInfo;
    if ( Output->preflightElement(1, SaveInfo) ) {
      llvm::yaml::yamlize(*Output, const_cast<PrintableTraceEntryBegin&>(Entry), 
                          true);
      Output->postflightElement(SaveInfo);
    }
  };

  void printEntryImpl(const PrintableTraceEntryEnd& Entry) override {
    void *SaveInfo;
    if ( Output->preflightElement(1, SaveInfo) ) {
      llvm::yaml::yamlize(*Output, const_cast<PrintableTraceEntryEnd&>(Entry), 
                          true);
      Output->postflightElement(SaveInfo);
    }
  };
  
public:
  
  YamlPrinter(const Sema &aSema, const std::string &Output) : 
              TemplightTracer::TracePrinter(aSema, Output) { };
  
private:
  std::unique_ptr<llvm::yaml::Output> Output;
};



class XmlPrinter : public TemplightTracer::TracePrinter {
protected:
  void startTraceImpl() override {
    (*this->TraceOS) <<
      "<?xml version=\"1.0\" standalone=\"yes\"?>\n"
      "<Trace>\n";
  };
  void endTraceImpl() override {
    (*this->TraceOS) << "</Trace>\n";
  };
  
  void printEntryImpl(const PrintableTraceEntryBegin& Entry) override {
    std::string EscapedName = escapeXml(Entry.Name);
    (*this->TraceOS) << llvm::format(
      "<TemplateBegin>\n"
      "    <Kind>%s</Kind>\n"
      "    <Context context = \"%s\"/>\n"
      "    <Location>%s|%d|%d</Location>\n",
      Entry.InstantiationKind.c_str(), EscapedName.c_str(),
      Entry.FileName.c_str(), Entry.Line, Entry.Column);

    (*this->TraceOS) << llvm::format(
      "    <TimeStamp time = \"%.9f\"/>\n"
      "    <MemoryUsage bytes = \"%d\"/>\n"
      "</TemplateBegin>\n", 
      Entry.TimeStamp, Entry.MemoryUsage);
  };

  void printEntryImpl(const PrintableTraceEntryEnd& Entry) override {
    (*this->TraceOS) << llvm::format(
      "<TemplateEnd>\n"
      "    <TimeStamp time = \"%.9f\"/>\n"
      "    <MemoryUsage bytes = \"%d\"/>\n"
      "</TemplateEnd>\n", 
      Entry.TimeStamp, Entry.MemoryUsage);
  }
  
public:
  
  XmlPrinter(const Sema &aSema, const std::string &Output) : 
             TemplightTracer::TracePrinter(aSema, Output) { };
  
};


struct EntryTraverseTask {
  typedef std::vector<RawTraceEntry>::const_iterator Iter;
  Iter it_begin, it_end;
  int nd_id;
  int parent_id;
  EntryTraverseTask(Iter aItBegin, Iter aItEnd, int aNdId, int aParentId) : 
    it_begin(aItBegin), it_end(aItEnd), nd_id(aNdId), parent_id(aParentId) { };
  
  static std::vector<EntryTraverseTask> recordDFSEntryTree(
      const std::vector<RawTraceEntry>& TraceEntries,
      int& last_node_id) {
    
    Iter it     = TraceEntries.begin();
    Iter it_end = TraceEntries.end();
    
    int start_id = last_node_id;
    int cur_top = 0;
    std::vector<EntryTraverseTask> parent_stack;
    parent_stack.push_back(EntryTraverseTask(it, it_end, last_node_id++, -1));
    while ( ++it != it_end ) {
      if ( it->IsTemplateBegin ) {
        parent_stack.push_back(EntryTraverseTask(
          it, it_end, last_node_id++, parent_stack[cur_top].nd_id));
        cur_top = parent_stack.size()-1;
      } else {
        parent_stack[cur_top].it_end = it;
        cur_top = parent_stack[cur_top].parent_id - start_id;
      }
    }
    
    return parent_stack;
  };
};



class NestedXMLPrinter : public TemplightTracer::TracePrinter {
protected:
  void startTraceImpl() override {
    (*this->TraceOS) <<
      "<?xml version=\"1.0\" standalone=\"yes\"?>\n"
      "<Trace>\n";
  };
  void endTraceImpl() override {
    (*this->TraceOS) << "</Trace>\n";
  };
  
  void printEntryImpl(const PrintableTraceEntryBegin& Entry) override { };
  void printEntryImpl(const PrintableTraceEntryEnd& Entry) override { };
  
  int last_node_id;
  int last_edge_id;
  
  void printNodeEntryImpl(const EntryTraverseTask& Task) {
    PrintableTraceEntryBegin BegEntry = rawToPrintableBegin(TheSema, *Task.it_begin);
    PrintableTraceEntryEnd EndEntry = rawToPrintableEnd(TheSema, *Task.it_end);
    std::string EscapedName = escapeXml(BegEntry.Name);
    
    (*this->TraceOS) << llvm::format(
      "<Entry Kind=\"%s\" Name=\"%s\" ",
      BegEntry.InstantiationKind.c_str(), EscapedName.c_str());
    (*this->TraceOS) << llvm::format(
      "Location=\"%s|%d|%d\" ", 
      BegEntry.FileName.c_str(), BegEntry.Line, BegEntry.Column);
    (*this->TraceOS) << llvm::format(
      "Time=\"%.9f\" Memory=\"%d\">\n", 
      EndEntry.TimeStamp - BegEntry.TimeStamp,
      EndEntry.MemoryUsage - BegEntry.MemoryUsage);
    
    // Print only first part (heading).
  };
  
public:
  
  
  void printRawEntry(const RawTraceEntry &Entry) override {
    cacheEntry(Entry);  // no immediate / safe mode supported for GraphML output.
  };
  
  void printCachedEntries() override {
    if ( TraceEntries.empty() )
      return;
    
    std::vector<EntryTraverseTask> parent_stack = 
      EntryTraverseTask::recordDFSEntryTree(TraceEntries, last_node_id);
    
    typedef std::vector<EntryTraverseTask>::iterator Iter;
    std::vector<Iter> sit_open;
    for(Iter sit = parent_stack.begin(),
        sit_end = parent_stack.end(); sit != sit_end; ++sit ) {
      while ( !sit_open.empty() && (sit->it_begin > sit_open.back()->it_end) ) {
        (*this->TraceOS) << "</Entry>\n";
        sit_open.pop_back();
      }
      printNodeEntryImpl(*sit);
      sit_open.push_back(parent_stack.begin());
    }
    while ( !sit_open.empty() ) {
      (*this->TraceOS) << "</Entry>\n";
      sit_open.pop_back();
    }
    TraceEntries.clear();
  };
  
  NestedXMLPrinter(const Sema &aSema, const std::string &Output) : 
                   TemplightTracer::TracePrinter(aSema, Output), 
                   last_node_id(0) { };
  
};




class GraphMLPrinter : public TemplightTracer::TracePrinter {
protected:
  void startTraceImpl() override {
    (*this->TraceOS) <<
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<graphml xmlns=\"http://graphml.graphdrawing.org/xmlns\""
      " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      " xsi:schemaLocation=\"http://graphml.graphdrawing.org/xmlns"
      " http://graphml.graphdrawing.org/xmlns/1.0/graphml.xsd\">\n";
    (*this->TraceOS) <<
      "<key id=\"d0\" for=\"node\" attr.name=\"Kind\" attr.type=\"string\"/>\n"
      "<key id=\"d1\" for=\"node\" attr.name=\"Name\" attr.type=\"string\"/>\n"
      "<key id=\"d2\" for=\"node\" attr.name=\"Location\" attr.type=\"string\"/>\n"
      "<key id=\"d3\" for=\"node\" attr.name=\"Time\" attr.type=\"double\">\n"
        "<default>0.0</default>\n"
      "</key>\n"
      "<key id=\"d4\" for=\"node\" attr.name=\"Memory\" attr.type=\"long\">\n"
        "<default>0</default>\n"
      "</key>\n";
    (*this->TraceOS) << "<graph>\n";
  };
  void endTraceImpl() override {
    (*this->TraceOS) << "</graph>\n</graphml>\n";
  };
  
  void printEntryImpl(const PrintableTraceEntryBegin& Entry) override { };
  void printEntryImpl(const PrintableTraceEntryEnd& Entry) override { };
  
  int last_node_id;
  int last_edge_id;
  
  void printNodeEntryImpl(const EntryTraverseTask& Task) {
    PrintableTraceEntryBegin BegEntry = rawToPrintableBegin(TheSema, *Task.it_begin);
    PrintableTraceEntryEnd EndEntry = rawToPrintableEnd(TheSema, *Task.it_end);
    
    (*this->TraceOS) << llvm::format("<node id=\"n%d\">\n", Task.nd_id);
    
    std::string EscapedName = escapeXml(BegEntry.Name);
    (*this->TraceOS) << llvm::format(
      "  <data key=\"d0\">%s</data>\n"
      "  <data key=\"d1\">\"%s\"</data>\n"
      "  <data key=\"d2\">\"%s|%d|%d\"</data>\n",
      BegEntry.InstantiationKind.c_str(), EscapedName.c_str(),
      BegEntry.FileName.c_str(), BegEntry.Line, BegEntry.Column);
    (*this->TraceOS) << llvm::format(
      "  <data key=\"d3\">%.9f</data>\n"
      "  <data key=\"d4\">%d</data>\n", 
      EndEntry.TimeStamp - BegEntry.TimeStamp,
      EndEntry.MemoryUsage - BegEntry.MemoryUsage);
    
    (*this->TraceOS) << "</node>\n";
    if ( Task.parent_id < 0 )
      return;
    (*this->TraceOS) << llvm::format(
      "<edge id=\"e%d\" source=\"n%d\" target=\"n%d\"/>\n",
      last_edge_id++, Task.parent_id, Task.nd_id);
  };
  
public:
  
  
  void printRawEntry(const RawTraceEntry &Entry) override {
    cacheEntry(Entry);  // no immediate / safe mode supported for GraphML output.
  };
  
  void printCachedEntries() override {
    if ( TraceEntries.empty() )
      return;
    
    std::vector<EntryTraverseTask> parent_stack = 
      EntryTraverseTask::recordDFSEntryTree(TraceEntries, last_node_id);
    
    for(std::vector<EntryTraverseTask>::iterator sit = parent_stack.begin(),
        sit_end = parent_stack.end(); sit != sit_end; ++sit ) {
      printNodeEntryImpl(*sit);
    }
    TraceEntries.clear();
  };
  
  GraphMLPrinter(const Sema &aSema, const std::string &Output) : 
                 TemplightTracer::TracePrinter(aSema, Output), 
                 last_node_id(0), last_edge_id(0) { };
  
};




class GraphVizPrinter : public TemplightTracer::TracePrinter {
protected:
  void startTraceImpl() override {
    (*this->TraceOS) << "digraph Templight {\n";
  };
  void endTraceImpl() override {
    (*this->TraceOS) << "}\n";
  };
  
  void printEntryImpl(const PrintableTraceEntryBegin&) override {};
  void printEntryImpl(const PrintableTraceEntryEnd&) override {};
  
  int last_node_id;
  
  void printNodeEntryImpl(const EntryTraverseTask& Task) {
    
    PrintableTraceEntryBegin BegEntry = rawToPrintableBegin(TheSema, *Task.it_begin);
    PrintableTraceEntryEnd EndEntry = rawToPrintableEnd(TheSema, *Task.it_end);
    std::string EscapedName = escapeXml(BegEntry.Name);
    (*this->TraceOS) 
      << llvm::format("n%d [label = ", Task.nd_id)
      << llvm::format(
          "\"%s\\n"
          "%s\\n"
          "At %s Line %d Column %d\\n",
        BegEntry.InstantiationKind.c_str(), EscapedName.c_str(),
        BegEntry.FileName.c_str(), BegEntry.Line, BegEntry.Column)
      << llvm::format(
          "Time: %.9f seconds Memory: %d bytes\" ];\n",
        EndEntry.TimeStamp - BegEntry.TimeStamp,
        EndEntry.MemoryUsage - BegEntry.MemoryUsage);
    
    if ( Task.parent_id < 0 )
      return;
    
    (*this->TraceOS) << llvm::format(
      "n%d -> n%d;\n",
      Task.parent_id, Task.nd_id);
  };
  
public:
  
  
  void printRawEntry(const RawTraceEntry &Entry) override {
    cacheEntry(Entry);  // no immediate / safe mode supported for GraphML output.
  };
  
  void printCachedEntries() override {
    if ( TraceEntries.empty() )
      return;
    
    std::vector<EntryTraverseTask> parent_stack = 
      EntryTraverseTask::recordDFSEntryTree(TraceEntries, last_node_id);
    
    for(std::vector<EntryTraverseTask>::iterator sit = parent_stack.begin(),
        sit_end = parent_stack.end(); sit != sit_end; ++sit ) {
      printNodeEntryImpl(*sit);
    }
    TraceEntries.clear();
  };
  
  GraphVizPrinter(const Sema &aSema, const std::string &Output) : 
                  TemplightTracer::TracePrinter(aSema, Output), 
                  last_node_id(0) { };
  
};




class TextPrinter : public TemplightTracer::TracePrinter {
protected:
  void startTraceImpl() override { };
  void endTraceImpl() override { };
  
  void printEntryImpl(const PrintableTraceEntryBegin& Entry) override { 
    (*this->TraceOS) << llvm::format(
      "TemplateBegin\n"
      "  Kind = %s\n"
      "  Name = %s\n"
      "  Location = %s|%d|%d\n",
      Entry.InstantiationKind.c_str(), Entry.Name.c_str(),
      Entry.FileName.c_str(), Entry.Line, Entry.Column);

    (*this->TraceOS) << llvm::format(
      "  TimeStamp = %.9f\n"
      "  MemoryUsage = %d\n", 
      Entry.TimeStamp, Entry.MemoryUsage);
  };

  void printEntryImpl(const PrintableTraceEntryEnd& Entry) override { 
    (*this->TraceOS) << llvm::format(
      "TemplateEnd\n"
      "  TimeStamp = %.9f\n"
      "  MemoryUsage = %d\n",
      Entry.TimeStamp, Entry.MemoryUsage);
  }
  
public:
  
  TextPrinter(const Sema &aSema, const std::string &Output) : 
              TemplightTracer::TracePrinter(aSema, Output) { };
  
};


} // unnamed namespace




void TemplightTracer::atTemplateBeginImpl(const Sema &TheSema, 
                          const ActiveTemplateInstantiation& Inst) {
  if ( !TemplateTracePrinter || 
       ( IgnoreSystemFlag && TheSema.getSourceManager()
                              .isInSystemHeader(Inst.PointOfInstantiation) ) )
    return;
  
  RawTraceEntry Entry;
  
  // NOTE: Use this function because it produces time since start of process.
  llvm::sys::TimeValue now = llvm::sys::process::get_self()->get_wall_time();
  
  Entry.IsTemplateBegin = true;
  Entry.InstantiationKind = Inst.Kind;
  Entry.Entity = Inst.Entity;
  Entry.PointOfInstantiation = Inst.PointOfInstantiation;
  Entry.TimeStamp = now.seconds() + now.nanoseconds() / 1000000000.0;
  Entry.MemoryUsage = (MemoryFlag ? llvm::sys::Process::GetMallocUsage() : 0);
  
  if ( SafeModeFlag ) {
    TemplateTracePrinter->printRawEntry(Entry);
  } else {
    TemplateTracePrinter->cacheEntry(Entry);
  }
}

void TemplightTracer::atTemplateEndImpl(const Sema &TheSema, 
                          const ActiveTemplateInstantiation& Inst) {
  if ( !TemplateTracePrinter || 
       ( IgnoreSystemFlag && TheSema.getSourceManager()
                              .isInSystemHeader(Inst.PointOfInstantiation) ) )
    return;
  
  RawTraceEntry Entry;

  // NOTE: Use this function because it produces time since start of process.
  llvm::sys::TimeValue now = llvm::sys::process::get_self()->get_wall_time();

  Entry.IsTemplateBegin = false;
  Entry.InstantiationKind = Inst.Kind;
  Entry.Entity = Inst.Entity;
  Entry.TimeStamp = now.seconds() + now.nanoseconds() / 1000000000.0;
  Entry.MemoryUsage = (MemoryFlag ? llvm::sys::Process::GetMallocUsage() : 0);

  if ( SafeModeFlag ) {
    TemplateTracePrinter->printRawEntry(Entry);
  } else {
    TemplateTracePrinter->cacheEntry(Entry);
  }
}


TemplightTracer::TemplightTracer(const Sema &TheSema, 
                                 std::string Output, 
                                 const std::string& Format,
                                 bool Memory, bool Safemode, 
                                 bool IgnoreSystem) :
                                 MemoryFlag(Memory),
                                 SafeModeFlag(Safemode),
                                 IgnoreSystemFlag(IgnoreSystem)  {
  
  if ( ( Format.empty() ) || ( Format == "yaml" ) ) {
    TemplateTracePrinter.reset(new YamlPrinter(TheSema, Output));
    return;
  }
  else if ( Format == "xml" ) {
    TemplateTracePrinter.reset(new XmlPrinter(TheSema, Output));
    return;
  }
  else if ( Format == "text" ) {
    TemplateTracePrinter.reset(new TextPrinter(TheSema, Output));
    return;
  }
  else if ( Format == "graphml" ) {
    TemplateTracePrinter.reset(new GraphMLPrinter(TheSema, Output));
    return;
  }
  else if ( Format == "graphviz" ) {
    TemplateTracePrinter.reset(new GraphVizPrinter(TheSema, Output));
    return;
  }
  else if ( Format == "nestedxml" ) {
    TemplateTracePrinter.reset(new NestedXMLPrinter(TheSema, Output));
    return;
  }
  else {
    llvm::errs() << "Error: [Templight-Tracer] Unrecognized template trace format:" << Format;
  }
  
  if ( !TemplateTracePrinter || !TemplateTracePrinter->isValid() ) {
    if ( TemplateTracePrinter )
      TemplateTracePrinter.reset();
    llvm::errs() << "Note: [Templight] Template trace has been disabled.";
  }
  
}


TemplightTracer::~TemplightTracer() { 
  // must be defined here due to TracePrinter being incomplete in header.
}


void TemplightTracer::initializeImpl(const Sema &) {
  if ( TemplateTracePrinter )
    TemplateTracePrinter->startTrace();
}

void TemplightTracer::finalizeImpl(const Sema &) {
  if ( TemplateTracePrinter )
    TemplateTracePrinter->endTrace();
}

void TemplightTracer::setBlacklists(const std::string& ContextPattern, 
                                    const std::string& IdentifierPattern) {
  if ( TemplateTracePrinter ) {
    TemplateTracePrinter->CoRegex.reset(new llvm::Regex(ContextPattern));
    TemplateTracePrinter->IdRegex.reset(new llvm::Regex(IdentifierPattern));
  }
}


} // namespace clang

