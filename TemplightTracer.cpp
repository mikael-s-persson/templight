
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

namespace clang {

namespace {

struct RawTraceEntry {
  bool IsTemplateBegin;
  ActiveTemplateInstantiation::InstantiationKind InstantiationKind;
  Decl *Entity;
  SourceLocation PointOfInstantiation;
  double TimeStamp;
  size_t MemoryUsage;
  
  RawTraceEntry() : IsTemplateBegin(true), 
    InstantiationKind(ActiveTemplateInstantiation::TemplateInstantiation),
    Entity(0), TimeStamp(0.0), MemoryUsage(0) { };
};

struct PrintableTraceEntry {
  bool IsTemplateBegin;
  std::string InstantiationKind;
  std::string Name;
  std::string FileName;
  int Line;
  int Column;
  double TimeStamp;
  size_t MemoryUsage;
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


PrintableTraceEntry rawToPrintable(const Sema &TheSema, const RawTraceEntry& Entry) {
  PrintableTraceEntry Ret;
  
  Ret.IsTemplateBegin = Entry.IsTemplateBegin;
  Ret.InstantiationKind = InstantiationKindStrings[Entry.InstantiationKind];
  
  if (Entry.IsTemplateBegin) {
    NamedDecl *NamedTemplate = dyn_cast_or_null<NamedDecl>(Entry.Entity);
    if (NamedTemplate) {
      llvm::raw_string_ostream OS(Ret.Name);
      NamedTemplate->getNameForDiagnostic(OS, TheSema.getLangOpts(), true);
    }
    
    PresumedLoc Loc = TheSema.getSourceManager().getPresumedLoc(Entry.PointOfInstantiation);
    if(!Loc.isInvalid()) {
      Ret.FileName = Loc.getFilename();
      Ret.Line = Loc.getLine();
      Ret.Column = Loc.getColumn();
    } else {
      Ret.FileName = "";
      Ret.Line = 0;
      Ret.Column = 0;
    }
  } else {
    Ret.Name = "";
    Ret.FileName = "";
    Ret.Line = 0;
    Ret.Column = 0;
  }
  
  
  Ret.TimeStamp = Entry.TimeStamp;
  Ret.MemoryUsage = Entry.MemoryUsage;
  
  return Ret;
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
    using namespace clang;
    io.enumCase(value, "TemplateInstantiation",
      ActiveTemplateInstantiation::TemplateInstantiation);
    io.enumCase(value, "DefaultTemplateArgumentInstantiation",
      ActiveTemplateInstantiation::DefaultTemplateArgumentInstantiation);
    io.enumCase(value, "DefaultFunctionArgumentInstantiation",
      ActiveTemplateInstantiation::DefaultFunctionArgumentInstantiation);
    io.enumCase(value, "ExplicitTemplateArgumentSubstitution",
      ActiveTemplateInstantiation::ExplicitTemplateArgumentSubstitution);
    io.enumCase(value, "DeducedTemplateArgumentSubstitution",
      ActiveTemplateInstantiation::DeducedTemplateArgumentSubstitution);
    io.enumCase(value, "PriorTemplateArgumentSubstitution",
      ActiveTemplateInstantiation::PriorTemplateArgumentSubstitution);
    io.enumCase(value, "DefaultTemplateArgumentChecking",
      ActiveTemplateInstantiation::DefaultTemplateArgumentChecking);
    io.enumCase(value, "ExceptionSpecInstantiation",
      ActiveTemplateInstantiation::ExceptionSpecInstantiation);
    io.enumCase(value, "Memoization",
      ActiveTemplateInstantiation::Memoization);
  }
};

template <>
struct MappingTraits<clang::PrintableTraceEntry> {
  static void mapping(IO &io, clang::PrintableTraceEntry &Entry) {
    io.mapRequired("IsTemplateBegin", Entry.IsTemplateBegin);
    io.mapRequired("Kind", Entry.InstantiationKind);
    io.mapOptional("Name", Entry.Name);
    io.mapOptional("FileName", Entry.FileName);
    io.mapOptional("Line", Entry.Line);
    io.mapOptional("Column", Entry.Column);
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
  virtual void printEntryImpl(const Sema &, const PrintableTraceEntry &) = 0;
  
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
    this->printEntryImpl(TheSema, rawToPrintable(TheSema, Entry));
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
      this->printEntryImpl(TheSema, rawToPrintable(TheSema, *it));
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
    Output->beginFlowSequence();
  };
  void endTraceImpl(const Sema &) {
    Output->endFlowSequence();
    Output->endDocuments();
  };
  void printEntryImpl(const Sema &, const PrintableTraceEntry& Entry) {
    void *SaveInfo;
    if ( Output->preflightFlowElement(1, SaveInfo) ) {
      llvm::yaml::yamlize(*Output, const_cast<PrintableTraceEntry&>(Entry), 
                          true);
      Output->postflightFlowElement(SaveInfo);
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
  void printEntryImpl(const Sema &, const PrintableTraceEntry& Entry) {
    if (Entry.IsTemplateBegin) {
      std::string EscapedName = escapeXml(Entry.Name);
      (*this->TraceOS)
        << llvm::format("<TemplateBegin>\n"
          "    <Kind>%s</Kind>\n"
          "    <Context context = \"%s\"/>\n"
          "    <PointOfInstantiation>%s|%d|%d</PointOfInstantiation>\n",
          Entry.InstantiationKind.c_str(), EscapedName.c_str(),
          Entry.FileName.c_str(), Entry.Line, Entry.Column);

      (*this->TraceOS) << llvm::format("    <TimeStamp time = \"%f\"/>\n"
        "    <MemoryUsage bytes = \"%d\"/>\n"
        "</TemplateBegin>\n", Entry.TimeStamp, Entry.MemoryUsage);
    } else {
      (*this->TraceOS)
        << llvm::format("<TemplateEnd>\n"
          "    <Kind>%s</Kind>\n"
          "    <TimeStamp time = \"%f\"/>\n"
          "    <MemoryUsage bytes = \"%d\"/>\n"
          "</TemplateEnd>\n", Entry.InstantiationKind.c_str(),
          Entry.TimeStamp, Entry.MemoryUsage);
    }
  };
  
public:
  
  XmlPrinter(const std::string &Output) : 
             TemplightTracer::TracePrinter(Output) { };
  
};




class TextPrinter : public TemplightTracer::TracePrinter {
protected:
  void startTraceImpl(const Sema &) { };
  void endTraceImpl(const Sema &) { };
  void printEntryImpl(const Sema &, const PrintableTraceEntry& Entry) { 
    if (Entry.IsTemplateBegin) {
      (*this->TraceOS)
        << llvm::format(
          "TemplateBegin\n"
          "  Kind = %s\n"
          "  Name = %s\n"
          "  PointOfInstantiation = %s|%d|%d\n",
          Entry.InstantiationKind.c_str(), Entry.Name.c_str(),
          Entry.FileName.c_str(), Entry.Line, Entry.Column);

      (*this->TraceOS) << llvm::format(
        "  TimeStamp = %f\n"
        "  MemoryUsage = %d\n"
        , Entry.TimeStamp, Entry.MemoryUsage);
    } else {
      (*this->TraceOS)
        << llvm::format(
          "TemplateEnd\n"
          "  Kind = %s\n"
          "  TimeStamp = %f\n"
          "  MemoryUsage = %d\n"
          , Entry.InstantiationKind.c_str(),
          Entry.TimeStamp, Entry.MemoryUsage);
    }
  };
  
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

  llvm::TimeRecord timeRecord = llvm::TimeRecord::getCurrentTime();

  Entry.IsTemplateBegin = true;
  Entry.InstantiationKind = Inst.Kind;
  Entry.Entity = Inst.Entity;
  Entry.PointOfInstantiation = Inst.PointOfInstantiation;
  Entry.TimeStamp = timeRecord.getWallTime();
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

  llvm::TimeRecord timeRecord = llvm::TimeRecord::getCurrentTime();

  Entry.IsTemplateBegin = false;
  Entry.InstantiationKind = Inst.Kind;
  Entry.Entity = Inst.Entity;
  Entry.TimeStamp = timeRecord.getWallTime();
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

