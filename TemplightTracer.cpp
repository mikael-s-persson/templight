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
    io.mapOptional("FileName", Entry.FileName);
    io.mapOptional("Line", Entry.Line);
    io.mapOptional("Column", Entry.Column);
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
  virtual void startTraceImpl(const Sema &) = 0;
  virtual void endTraceImpl(const Sema &) = 0;
  virtual void printEntryImpl(const Sema &, const PrintableTraceEntryBegin &) = 0;
  virtual void printEntryImpl(const Sema &, const PrintableTraceEntryEnd &) = 0;
  
public:
  
  bool shouldIgnoreEntry(const RawTraceEntry &Entry) const {
    if ( Entry.InstantiationKind == ActiveTemplateInstantiation::Memoization ) {
      if ( !LastBeginEntry.IsTemplateBegin
           && ( LastBeginEntry.InstantiationKind == Entry.InstantiationKind )
           && ( LastBeginEntry.Entity == Entry.Entity ) ) {
        return true;
      }
    }
    return false;
  };
  
  void printRawEntry(const Sema &TheSema, const RawTraceEntry &Entry) {
    if ( shouldIgnoreEntry(Entry) )
      return;

    if ( Entry.IsTemplateBegin )
      this->printEntryImpl(TheSema, rawToPrintableBegin(TheSema, Entry));
    else
      this->printEntryImpl(TheSema, rawToPrintableEnd(TheSema, Entry));
    
    TraceOS->flush();
    if ( Entry.IsTemplateBegin )
      LastBeginEntry = Entry;
    if ( !Entry.IsTemplateBegin && 
         ( Entry.InstantiationKind == ActiveTemplateInstantiation::Memoization ) )
      LastBeginEntry.IsTemplateBegin = false;
  };
  
  void printCachedEntries(const Sema &TheSema) {
    for(std::vector<RawTraceEntry>::iterator it = TraceEntries.begin();
        it != TraceEntries.end(); ++it) {
      if ( it->IsTemplateBegin )
        this->printEntryImpl(TheSema, rawToPrintableBegin(TheSema, *it));
      else
        this->printEntryImpl(TheSema, rawToPrintableEnd(TheSema, *it));
    }
    TraceEntries.clear();
  };
  
  void cacheEntry(const Sema &TheSema, const RawTraceEntry &Entry) {
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
      printCachedEntries(TheSema);
  };
  
  void startTrace(const Sema &TheSema) {
    this->startTraceImpl(TheSema);
  };
  
  void endTrace(const Sema &TheSema) {
    printCachedEntries(TheSema);
    this->endTraceImpl(TheSema);
    TraceOS->flush();
  };
  
  TracePrinter(const std::string &Output) : TraceOS(0) {
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
  
  std::vector<RawTraceEntry> TraceEntries;
  RawTraceEntry LastBeginEntry;
  
  llvm::raw_ostream* TraceOS;
  
  bool isValid() const { return TraceOS; };
};




namespace {

class YamlPrinter : public TemplightTracer::TracePrinter {
protected:
  void startTraceImpl(const Sema &) {
    Output.reset(new llvm::yaml::Output(*this->TraceOS));
    Output->beginDocuments();
    Output->beginSequence();
  };
  void endTraceImpl(const Sema &) {
    Output->endSequence();
    Output->endDocuments();
  };
  
  void printEntryImpl(const Sema &, const PrintableTraceEntryBegin& Entry) {
    void *SaveInfo;
    if ( Output->preflightElement(1, SaveInfo) ) {
      llvm::yaml::yamlize(*Output, const_cast<PrintableTraceEntryBegin&>(Entry), 
                          true);
      Output->postflightElement(SaveInfo);
    }
  };

  void printEntryImpl(const Sema &, const PrintableTraceEntryEnd& Entry) {
    void *SaveInfo;
    if ( Output->preflightElement(1, SaveInfo) ) {
      llvm::yaml::yamlize(*Output, const_cast<PrintableTraceEntryEnd&>(Entry), 
                          true);
      Output->postflightElement(SaveInfo);
    }
  };
  
public:
  
  YamlPrinter(const std::string &Output) : 
              TemplightTracer::TracePrinter(Output) { };
  
private:
  std::unique_ptr<llvm::yaml::Output> Output;
};



class XmlPrinter : public TemplightTracer::TracePrinter {
protected:
  void startTraceImpl(const Sema &) {
    (*this->TraceOS) <<
      "<?xml version=\"1.0\" standalone=\"yes\"?>\n"
      "<Trace>\n";
  };
  void endTraceImpl(const Sema &) {
    (*this->TraceOS) << "</Trace>\n";
  };
  
  void printEntryImpl(const Sema &, const PrintableTraceEntryBegin& Entry) {
    std::string EscapedName = escapeXml(Entry.Name);
    (*this->TraceOS) << llvm::format(
      "<TemplateBegin>\n"
      "    <Kind>%s</Kind>\n"
      "    <Context context = \"%s\"/>\n"
      "    <PointOfInstantiation>%s|%d|%d</PointOfInstantiation>\n",
      Entry.InstantiationKind.c_str(), EscapedName.c_str(),
      Entry.FileName.c_str(), Entry.Line, Entry.Column);

    (*this->TraceOS) << llvm::format(
      "    <TimeStamp time = \"%.9f\"/>\n"
      "    <MemoryUsage bytes = \"%d\"/>\n"
      "</TemplateBegin>\n", 
      Entry.TimeStamp, Entry.MemoryUsage);
  };

  void printEntryImpl(const Sema &, const PrintableTraceEntryEnd& Entry) {
    (*this->TraceOS) << llvm::format(
      "<TemplateEnd>\n"
      "    <TimeStamp time = \"%.9f\"/>\n"
      "    <MemoryUsage bytes = \"%d\"/>\n"
      "</TemplateEnd>\n", 
      Entry.TimeStamp, Entry.MemoryUsage);
  }
  
public:
  
  XmlPrinter(const std::string &Output) : 
             TemplightTracer::TracePrinter(Output) { };
  
};




class TextPrinter : public TemplightTracer::TracePrinter {
protected:
  void startTraceImpl(const Sema &) { };
  void endTraceImpl(const Sema &) { };
  
  void printEntryImpl(const Sema &, const PrintableTraceEntryBegin& Entry) { 
    (*this->TraceOS) << llvm::format(
      "TemplateBegin\n"
      "  Kind = %s\n"
      "  Name = %s\n"
      "  PointOfInstantiation = %s|%d|%d\n",
      Entry.InstantiationKind.c_str(), Entry.Name.c_str(),
      Entry.FileName.c_str(), Entry.Line, Entry.Column);

    (*this->TraceOS) << llvm::format(
      "  TimeStamp = %.9f\n"
      "  MemoryUsage = %d\n", 
      Entry.TimeStamp, Entry.MemoryUsage);
  };

  void printEntryImpl(const Sema &, const PrintableTraceEntryEnd& Entry) { 
    (*this->TraceOS) << llvm::format(
      "TemplateEnd\n"
      "  TimeStamp = %.9f\n"
      "  MemoryUsage = %d\n",
      Entry.TimeStamp, Entry.MemoryUsage);
  }
  
public:
  
  TextPrinter(const std::string &Output) : 
              TemplightTracer::TracePrinter(Output) { };
  
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
    TemplateTracePrinter->printRawEntry(TheSema, Entry);
  } else {
    TemplateTracePrinter->cacheEntry(TheSema, Entry);
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
    TemplateTracePrinter->printRawEntry(TheSema, Entry);
  } else {
    TemplateTracePrinter->cacheEntry(TheSema, Entry);
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
  
  std::string postfix;
  if ( Output.empty() ) {
    // then, derive output name from the input name:
    FileID fileID = TheSema.getSourceManager().getMainFileID();
    postfix = (MemoryFlag ? ".memory.trace." : ".trace.");
    
    Output =
      TheSema.getSourceManager().getFileEntryForID(fileID)->getName();
  }
  
  if ( ( Format.empty() ) || ( Format == "yaml" ) ) {
    TemplateTracePrinter.reset(new YamlPrinter(
      ( postfix == "" ? Output : (Output + postfix + "yaml") )
    ));
    return;
  }
  else if ( Format == "xml" ) {
    TemplateTracePrinter.reset(new XmlPrinter(
      ( postfix == "" ? Output : (Output + postfix + "xml") )
    ));
    return;
  }
  else if ( Format == "text" ) {
    TemplateTracePrinter.reset(new TextPrinter(
      ( postfix == "" ? Output : (Output + postfix + "txt") )
    ));
    return;
  }
  else {
    llvm::errs() << "Error: [Templight] Unrecoginized template trace format:" << Format;
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


void TemplightTracer::initializeImpl(const Sema &TheSema) {
  if ( TemplateTracePrinter )
    TemplateTracePrinter->startTrace(TheSema);
}

void TemplightTracer::finalizeImpl(const Sema &TheSema) {
  if ( TemplateTracePrinter )
    TemplateTracePrinter->endTrace(TheSema);
}


} // namespace clang

