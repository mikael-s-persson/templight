
#include "TemplightDebugger.h"

#include "clang/AST/DataRecursiveASTVisitor.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclLookups.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Sema/ActiveTemplateInst.h"
#include "clang/Sema/Sema.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Process.h"

#include <string>
#include <vector>
#include <algorithm>

#include <cctype>
#include <cstdio>
#include <cstring>


namespace clang {


namespace {
  

const char* const InstantiationKindStrings[] = { 
  "template instantiation",
  "default template-argument instantiation",
  "default function-argument instantiation",
  "explicit template-argument substitution",
  "deduced template-argument substitution", 
  "prior template-argument substitution",
  "default template-argument checking", 
  "exception specification instantiation",
  "memoization" };

struct TemplateDebuggerEntry {
  bool IsTemplateBegin;
  ActiveTemplateInstantiation Inst;
  std::string Name;
  std::string FileName;
  int Line;
  int Column;
  std::size_t MemoryUsage;
  
  TemplateDebuggerEntry() : IsTemplateBegin(true), 
    Inst(), Name(), FileName(), Line(0), Column(0), 
    MemoryUsage(0) { };
  
  TemplateDebuggerEntry(bool aIsBegin, std::size_t aMemUsage,
                        const Sema &TheSema, 
                        const ActiveTemplateInstantiation& aInst) :
                        IsTemplateBegin(aIsBegin), Inst(aInst), Name(), 
                        FileName(), Line(0), Column(0), MemoryUsage(aMemUsage) { 
    NamedDecl *NamedTemplate = dyn_cast_or_null<NamedDecl>(Inst.Entity);
    if (NamedTemplate) {
      llvm::raw_string_ostream OS(Name);
      NamedTemplate->getNameForDiagnostic(OS, TheSema.getLangOpts(), true);
    }
    
    PresumedLoc Loc = TheSema.getSourceManager().getPresumedLoc(Inst.PointOfInstantiation);
    if(!Loc.isInvalid()) {
      FileName = Loc.getFilename();
      Line     = Loc.getLine();
      Column   = Loc.getColumn();
    };
  };
};


template <typename OutputIter>
void fillWithTemplateArgumentPrints(const TemplateArgument *Args,
                                    unsigned NumArgs,
                                    const PrintingPolicy &Policy,
                                    OutputIter It) {
  for (unsigned Arg = 0; Arg < NumArgs; ++Arg) {
    // Print the argument into a string.
    if (Args[Arg].getKind() == TemplateArgument::Pack) {
      fillWithTemplateArgumentPrints(Args[Arg].pack_begin(), 
                                     Args[Arg].pack_size(), 
                                     Policy, It);
    } else {
      std::string Buf;
      llvm::raw_string_ostream ArgOS(Buf);
      Args[Arg].print(Policy, ArgOS);
      *(It++) = Buf;
    }
  }
}



class TemplateArgRecorder : public DataRecursiveASTVisitor<TemplateArgRecorder> {
public:
  typedef DataRecursiveASTVisitor<TemplateArgRecorder> Base;
  
  const Sema &TheSema;
  
  TemplateArgRecorder(const Sema &aSema) : TheSema(aSema) { }
  
  bool shouldVisitTemplateInstantiations() const { return false; }
  
  struct PrintableQueryResult {
    std::string Name;
    std::string FileName;
    int Line;
    int Column;
    
    void nullLocation(const std::string& NullName = "<no-file>") {
      FileName = NullName;
      Line = 0;
      Column = 0;
    }
    
    void fromLocation(PresumedLoc PLoc, const std::string& NullName = "") {
      if(!PLoc.isInvalid()) {
        FileName = PLoc.getFilename();
        Line = PLoc.getLine();
        Column = PLoc.getColumn();
      } else {
        FileName = NullName;
        Line = 0;
        Column = 0;
      }
    }
  };
  
  
  std::string QueryName;
  bool lookForType;
  std::vector< PrintableQueryResult > QueryResults;
  // MAYBE change (or add) with Decl* or QualType, etc..
  
  void RegisterQualTypeQueryResult(PrintableQueryResult& cur_result, QualType q_type) {
    if( lookForType ) {
      if(const Type* tp = q_type.getTypePtr())
        q_type = tp->getCanonicalTypeInternal();
    }
    cur_result.Name = q_type.getAsString(TheSema.getLangOpts());
    SourceLocation sl;
    if(const Type* tp = q_type.getTypePtr()) {
      if(const CXXRecordDecl* cxx_decl = tp->getAsCXXRecordDecl()) {
        sl = cxx_decl->getLocation();
      }
    }
    cur_result.fromLocation(TheSema.getSourceManager().getPresumedLoc(sl), "<unknown-location>");
  };
  
  void LookupInParamArgLists(const TemplateParameterList *Params,
                             const TemplateArgument *Args, unsigned NumArgs) {
    for (unsigned I = 0, N = Params->size(); I != N; ++I) {
      if (I >= NumArgs)
        break;
      
      std::string param_name;
      if (const IdentifierInfo *Id = Params->getParam(I)->getIdentifier()) {
        param_name = Id->getName();
      } else {
        llvm::raw_string_ostream OS_param(param_name);
        OS_param << '$' << I;
        OS_param.str();
      }
      
      if(param_name == QueryName) {
        
        PrintableQueryResult cur_result;
        
        switch (Args[I].getKind()) {
          case TemplateArgument::Null: {
            // not much better to do.
            llvm::raw_string_ostream OS_arg(cur_result.Name);
            Args[I].print(TheSema.getPrintingPolicy(), OS_arg);
            OS_arg.str(); // flush to string.
            cur_result.nullLocation();
            break;
          }
          
          case TemplateArgument::Integral: {
            if( !lookForType ) {
              cur_result.Name = Args[I].getAsIntegral().toString(10);
              cur_result.nullLocation();
            } else {
              RegisterQualTypeQueryResult(cur_result, Args[I].getIntegralType());
            }
            break;
          }
          
          case TemplateArgument::NullPtr: {
            if( !lookForType ) {
              cur_result.Name = "nullptr";
              cur_result.nullLocation();
            } else {
              cur_result.Name = Args[I].getNullPtrType().getAsString(TheSema.getLangOpts());
              cur_result.nullLocation();
            }
            break;
          }
          
          case TemplateArgument::Declaration: {
            ValueDecl* vdecl = Args[I].getAsDecl();
            if( !lookForType ) {
              llvm::raw_string_ostream OS_arg(cur_result.Name);
              vdecl->getNameForDiagnostic(OS_arg, TheSema.getLangOpts(), true);
              OS_arg.str(); // flush to string.
              cur_result.fromLocation(
                TheSema.getSourceManager().getPresumedLoc(vdecl->getLocation()), 
                "<unknown-location>");
            } else {
              RegisterQualTypeQueryResult(cur_result, vdecl->getType());
            }
            break;
          }
          
          case TemplateArgument::Type: {
            RegisterQualTypeQueryResult(cur_result, Args[I].getAsType());
            break;
          }
          
          case TemplateArgument::Template:
          case TemplateArgument::TemplateExpansion: {
            llvm::raw_string_ostream OS_arg(cur_result.Name);
            TemplateName tname = Args[I].getAsTemplateOrTemplatePattern();
            tname.print(OS_arg, TheSema.getLangOpts());
            OS_arg.str(); // flush to string.
            SourceLocation sl;
            if(TemplateDecl* tmp_decl = tname.getAsTemplateDecl()) {
              sl = tmp_decl->getLocation();
            }
            cur_result.fromLocation(TheSema.getSourceManager().getPresumedLoc(sl), 
                                    "<unknown-location>");
            break;
          }
          
          case TemplateArgument::Expression: {
            if( !lookForType ) {
              llvm::raw_string_ostream OS_arg(cur_result.Name);
              Args[I].getAsExpr()->printPretty(OS_arg, NULL, TheSema.getLangOpts());
              OS_arg.str(); // flush to string.
              cur_result.fromLocation(
                TheSema.getSourceManager().getPresumedLoc(Args[I].getAsExpr()->getExprLoc()), 
                "<unknown-location>");
            } else {
              RegisterQualTypeQueryResult(cur_result, Args[I].getAsExpr()->getType());
            }
            break;
          }
          
          case TemplateArgument::Pack: {
            // not much better to do.
            llvm::raw_string_ostream OS_arg(cur_result.Name);
            Args[I].print(TheSema.getPrintingPolicy(), OS_arg);
            OS_arg.str(); // flush to string.
            cur_result.nullLocation();
            break;
          }
          
        }
        
        QueryResults.push_back(cur_result);
        return;
      }
    }
  }
  
  void LookupInDeclContext(DeclContext* decl, bool shouldGoUp = false) {
    DeclContext* cur_decl = decl;
    while(cur_decl) {
      for(DeclContext::all_lookups_iterator it = cur_decl->lookups_begin(), 
          it_end = cur_decl->lookups_end(); it != it_end; ++it) {
        if(it.getLookupName().getAsString() == QueryName) {
          for(DeclContext::lookup_result::iterator ri = (*it).begin(), 
              ri_end = (*it).end(); ri != ri_end; ++ri) {
            NamedDecl* ndecl = *ri;
            PrintableQueryResult cur_result;
            if( !lookForType ) {
              if( ndecl ) {
                llvm::raw_string_ostream OS_arg(cur_result.Name);
                ndecl->getNameForDiagnostic(OS_arg, TheSema.getLangOpts(), true);
                OS_arg.str(); // flush to string.
                cur_result.fromLocation(
                  TheSema.getSourceManager().getPresumedLoc(ndecl->getLocation()), 
                  "<unknown-location>");
              }
            } else {
              if(TypeDecl* tdecl = dyn_cast<TypeDecl>(ndecl)) {
                if(TypedefNameDecl* utp = dyn_cast<TypedefNameDecl>(tdecl)) {
                  RegisterQualTypeQueryResult(cur_result, utp->getUnderlyingType());
                } else if(const Type* tp = tdecl->getTypeForDecl()) {
                  RegisterQualTypeQueryResult(cur_result, tp->getCanonicalTypeInternal());
                }
              } else
              if(ValueDecl* vdecl = dyn_cast<ValueDecl>(ndecl)) {
                RegisterQualTypeQueryResult(cur_result, vdecl->getType());
              }
            }
            QueryResults.push_back(cur_result);
          }
          return;
        }
      }
      if(shouldGoUp) {
        cur_decl = cur_decl->getParent(); // recurse
      } else
        return;
    }
  }
  
  
  bool TraverseActiveTempInstantiation(const ActiveTemplateInstantiation& Inst) {
    TemplateDecl *TemplatePtr = dyn_cast_or_null<TemplateDecl>(Inst.Entity);
    if( TemplatePtr && TemplatePtr->getTemplateParameters() &&
        ( TemplatePtr->getTemplateParameters()->size() != 0 ) && 
        ( Inst.NumTemplateArgs != 0 ) ) {
      LookupInParamArgLists(TemplatePtr->getTemplateParameters(), 
                            Inst.TemplateArgs, Inst.NumTemplateArgs);
      if( QueryResults.size() )
        return true;
    }
    
    return TraverseDecl(Inst.Entity);
  }
  
  
  bool VisitFunctionDecl(FunctionDecl* decl) {
    
    LookupInDeclContext(decl);
    if( QueryResults.size() )
      return true;
    
    if( decl->isFunctionTemplateSpecialization() ) {
      FunctionTemplateSpecializationInfo* SpecInfo = decl->getTemplateSpecializationInfo();
      FunctionTemplateDecl* TempDecl = SpecInfo->getTemplate();
      if( TempDecl && TempDecl->getTemplateParameters() &&
          ( TempDecl->getTemplateParameters()->size() != 0 ) && 
          ( SpecInfo->TemplateArguments->size() != 0 ) ) {
        LookupInParamArgLists(TempDecl->getTemplateParameters(), 
                              SpecInfo->TemplateArguments->data(), SpecInfo->TemplateArguments->size());
        if( QueryResults.size() )
          return true;
      }
    }
    
    DeclContext* parent = decl->getParent();
    if( TagDecl* t_decl = dyn_cast_or_null<TagDecl>(parent) )
      return getDerived().VisitTagDecl(t_decl);
    else {
      LookupInDeclContext(parent, true);
      if( QueryResults.size() )
        return true;
    }
    
    return true;
  }
  
  bool VisitTagDecl(TagDecl* decl) {
    
    LookupInDeclContext(decl);
    if( QueryResults.size() )
      return true;
    
    if( ClassTemplateSpecializationDecl* spec_decl = dyn_cast<ClassTemplateSpecializationDecl>(decl) ) {
      ClassTemplateDecl* TempDecl = spec_decl->getSpecializedTemplate();
      if( TempDecl && TempDecl->getTemplateParameters() &&
          ( TempDecl->getTemplateParameters()->size() != 0 ) && 
          ( spec_decl->getTemplateArgs().size() != 0 ) ) {
        LookupInParamArgLists(TempDecl->getTemplateParameters(), 
                              spec_decl->getTemplateArgs().data(), 
                              spec_decl->getTemplateArgs().size());
        if( QueryResults.size() )
          return true;
      }
    }
    
    // TODO Shouldn't this be in the LookupInDeclContext function (or maybe even VisitDeclContex?)
    DeclContext* parent = decl->getParent();
    if( TagDecl* t_decl = dyn_cast_or_null<TagDecl>(parent) )
      return getDerived().VisitTagDecl(t_decl);
    else {
      LookupInDeclContext(parent, true);
      if( QueryResults.size() )
        return true;
    }
    
    return true;
  }
  
  
};



}




class TemplightDebugger::InteractiveAgent {
public:
  
  void printEntryImpl(const TemplateDebuggerEntry& Entry) { 
    llvm::outs() 
      << ( (Entry.IsTemplateBegin) ? "Entering " : "Leaving  " )
      << InstantiationKindStrings[Entry.Inst.Kind]  << " of " << Entry.Name << '\n'
      << "  at " << Entry.FileName << '|' << Entry.Line << '|' << Entry.Column 
      << " (Memory usage: " << Entry.MemoryUsage << ")\n";
  };
  
  bool shouldIgnoreEntry(const TemplateDebuggerEntry &Entry) const {
    if ( ignoreAll )
      return true;
    if ( ignoreUntilBreakpoint ) {
      std::vector< std::string >::const_iterator it = std::find(
        Breakpoints.begin(), Breakpoints.end(), Entry.Name);
      if ( it != Breakpoints.end() )
        return false;
      else
        return true;
    }
    if ( ignoreUntilLastEnds ) {
      if ( ( EntriesStack.size() > 0 )
           && ( EntriesStack.back().Inst.Kind == Entry.Inst.Kind )
           && ( EntriesStack.back().Inst.Entity == Entry.Inst.Entity ) )
        return false;
      else
        return true;
    }
    if ( Entry.Inst.Kind == ActiveTemplateInstantiation::Memoization ) {
      if ( !LastBeginEntry.IsTemplateBegin
           && ( LastBeginEntry.Inst.Kind == Entry.Inst.Kind )
           && ( LastBeginEntry.Inst.Entity == Entry.Inst.Entity ) ) {
        return true;
      }
    }
    return false;
  };
  
  static void getLineFromStdIn(std::string& s) {
    char c = 0;
    s = "";
    int i = std::getchar();
    if ( i == EOF ) {
      std::clearerr(stdin);
      return;
    };
    c = char(i);
    while ( c != '\n' ) {
      s.push_back(c);
      i = std::getchar();
      if ( i == EOF ) {
        std::clearerr(stdin);
        return;
      };
      c = char(i);
    };
  };
  
  static void flushStdIn() {
    std::string s;
    getLineFromStdIn(s);
  };
  
  static void getTokenizeCommand(const std::string& s, std::string& com, std::string& arg) {
    com = "";
    unsigned i = 0;
    while( ( i < s.size() ) && ( std::isspace(s[i]) ) )
      ++i;
    while( ( i < s.size() ) && ( !std::isspace(s[i]) ) ) {
      com += s[i];
      ++i;
    };
    while( ( i < s.size() ) && ( std::isspace(s[i]) ) )
      ++i;
    unsigned e = s.size();
    while( ( e > i ) && ( std::isspace(s[e-1]) ) )
      --e;
    arg = s.substr(i, e - i);
  };
  
  void processInputs() {
    std::string user_in;
    while(true) {
      llvm::outs() << "(tdb) ";
      getLineFromStdIn(user_in);
      if ( user_in == "" )
        user_in = LastUserCommand;
      
      LastUserCommand = user_in;
      
      std::string user_command, com_arg;
      getTokenizeCommand(user_in, user_command, com_arg);
      
      if ( ( user_command == "i" ) ||
           ( user_command == "info" ) ) {
        if ( ( com_arg == "f" ) ||
             ( com_arg == "frame" ) ) {
          if ( EntriesStack.size() > 0 )
            printEntryImpl(EntriesStack.back());
          continue;
        }
        else if ( ( com_arg == "b" ) ||
                  ( com_arg == "break" ) ) {
          for(unsigned i = 0; i < Breakpoints.size(); ++i)
            if ( Breakpoints[i] != "-" )
              llvm::outs() << "Breakpoint " << i 
                           << " for " << Breakpoints[i] << '\n';
          continue;
        }
        else if ( ( com_arg == "s" ) ||
                  ( com_arg == "stack" ) ) {
          user_command = "bt";
        }
        else {
          llvm::outs() << "Invalid input!\n";
          continue;
        }
        
      }
      
      if ( ( user_command == "r" ) ||
           ( user_command == "c" ) ||
           ( user_command == "run" ) ||
           ( user_command == "continue" ) ) {
        ignoreAll = false;
        ignoreUntilLastEnds = false;
        ignoreUntilBreakpoint = true;
        return;
      }
      else if ( ( user_command == "k" ) ||
                ( user_command == "q" ) ||
                ( user_command == "kill" ) ||
                ( user_command == "quit" ) ) {
        ignoreAll = true;
        ignoreUntilLastEnds = false;
        ignoreUntilBreakpoint = false;
        return;
      }
      else if ( ( user_command == "n" ) ||
                ( user_command == "next" ) ) {
        ignoreAll = false;
        ignoreUntilLastEnds = (EntriesStack.size() > 0);
        ignoreUntilBreakpoint = false;
        return;
      }
      else if ( ( user_command == "s" ) ||
                ( user_command == "step" ) ) {
        ignoreAll = false;
        ignoreUntilLastEnds = false;
        ignoreUntilBreakpoint = false;
        return;
      }
      else if ( ( user_command == "l" ) ||
                ( user_command == "lookup" ) ) {
        TemplateArgRecorder rec(TheSema);
        rec.QueryName = com_arg;
        rec.lookForType = false;
        rec.TraverseActiveTempInstantiation(EntriesStack.back().Inst);
        for(std::vector<TemplateArgRecorder::PrintableQueryResult>::const_iterator it = rec.QueryResults.begin();
            it != rec.QueryResults.end(); ++it) {
          llvm::outs() 
            << "Found " << it->Name << '\n'
            << "  at " << it->FileName << '|' << it->Line << '|' << it->Column << '\n';
        };
        continue;
      }
      else if ( ( user_command == "t" ) ||
                ( user_command == "typeof" ) ) {
        TemplateArgRecorder rec(TheSema);
        rec.QueryName = com_arg;
        rec.lookForType = true;
        rec.TraverseActiveTempInstantiation(EntriesStack.back().Inst);
        for(std::vector<TemplateArgRecorder::PrintableQueryResult>::const_iterator it = rec.QueryResults.begin();
            it != rec.QueryResults.end(); ++it) {
          llvm::outs() 
            << "Found " << it->Name << '\n'
            << "  at " << it->FileName << '|' << it->Line << '|' << it->Column << '\n';
        };
        continue;
      }
      else if ( ( user_command == "b" ) ||
                ( user_command == "break" ) ) {
        std::vector< std::string >::iterator it = std::find(
          Breakpoints.begin(), Breakpoints.end(), com_arg);
        if ( it == Breakpoints.end() ) {
          it = std::find(Breakpoints.begin(), Breakpoints.end(), "-");
          if ( it == Breakpoints.end() )
            it = Breakpoints.insert(Breakpoints.end(), com_arg);
          else
            *it = com_arg;
        }
        llvm::outs() << "Breakpoint " << (it - Breakpoints.begin()) 
                     << " for " << com_arg << '\n';
        continue;
      }
      else if ( ( user_command == "d" ) ||
                ( user_command == "delete" ) ) {
        int ind = 0;
        if ( std::sscanf(com_arg.c_str(), "%d", &ind) < 1 ) {
          llvm::outs() << "Invalid input!\n";
          continue;
        }
        llvm::outs() << "Deleted breakpoint " << ind 
                     << " for " << Breakpoints[ind] << '\n';
        Breakpoints[ind] = "-";
        continue;
      }
      else if ( ( user_command == "bt" ) ||
                ( user_command == "backtrace" ) ||
                ( user_command == "where" ) ) {
        for(std::vector<TemplateDebuggerEntry>::reverse_iterator it = EntriesStack.rbegin();
            it != EntriesStack.rend(); ++it) {
          llvm::outs()
            << InstantiationKindStrings[it->Inst.Kind] << " of " << it->Name 
            << " at " << it->FileName << '|' 
            << it->Line << '|' << it->Column << '\n';
        }
        continue;
      }
      else {
        llvm::outs() << "Invalid input!\n";
        continue; 
      }
      
    };
  };
  
  void printRawEntry(const TemplateDebuggerEntry &Entry) {
    if ( shouldIgnoreEntry(Entry) )
      return;
    
    if ( Entry.IsTemplateBegin ) {
      EntriesStack.push_back(Entry);
      LastBeginEntry = Entry;
    };
    
    printEntryImpl(Entry);
    
    if ( !Entry.IsTemplateBegin && 
         ( Entry.Inst.Kind == ActiveTemplateInstantiation::Memoization ) )
      LastBeginEntry.IsTemplateBegin = false;
    
    if ( !Entry.IsTemplateBegin && 
         ( EntriesStack.size() > 0 ) &&
         ( Entry.Inst.Kind == EntriesStack.back().Inst.Kind ) &&
         ( Entry.Inst.Entity == EntriesStack.back().Inst.Entity ) ) {
      EntriesStack.pop_back();
    };
    
    processInputs();
  };
  
  void startTrace() {
    llvm::outs() << "Welcome to the Templight debugger!\n"
                 << "Begin by entering 'run' after setting breakpoints.\n";
    processInputs();
  };
  
  void endTrace() {
    llvm::outs() << "Templight debugging session has ended. Goodbye!\n";
  };
  
  InteractiveAgent(const Sema &aTheSema) : TheSema(aTheSema), ignoreAll(false), 
    ignoreUntilLastEnds(false), ignoreUntilBreakpoint(false) { };
  
  ~InteractiveAgent() { };
  
  const Sema &TheSema;
  
  std::vector<TemplateDebuggerEntry> EntriesStack;
  TemplateDebuggerEntry LastBeginEntry;
  
  std::vector< std::string > Breakpoints;
  
  std::string LastUserCommand;
  
  unsigned ignoreAll : 1;
  unsigned ignoreUntilLastEnds : 1;
  unsigned ignoreUntilBreakpoint : 1;
};






void TemplightDebugger::initializeImpl(const Sema &) {
  Interactor->startTrace();
}

void TemplightDebugger::finalizeImpl(const Sema &) {
  Interactor->endTrace();
}

void TemplightDebugger::atTemplateBeginImpl(const Sema &TheSema, 
                          const ActiveTemplateInstantiation& Inst) {
  if ( IgnoreSystemFlag && TheSema.getSourceManager()
                            .isInSystemHeader(Inst.PointOfInstantiation) )
    return;
  
  TemplateDebuggerEntry Entry(
    true, (MemoryFlag ? llvm::sys::Process::GetMallocUsage() : 0), TheSema, Inst);
  
  Interactor->printRawEntry(Entry);
}

void TemplightDebugger::atTemplateEndImpl(const Sema &TheSema, 
                          const ActiveTemplateInstantiation& Inst) {
  if ( IgnoreSystemFlag && TheSema.getSourceManager()
                            .isInSystemHeader(Inst.PointOfInstantiation) )
    return;
  
  TemplateDebuggerEntry Entry(
    false, (MemoryFlag ? llvm::sys::Process::GetMallocUsage() : 0), TheSema, Inst);
  
  Interactor->printRawEntry(Entry);
}


TemplightDebugger::TemplightDebugger(const Sema &TheSema, 
                                     bool Memory, bool IgnoreSystem) :
                                     MemoryFlag(Memory),
                                     IgnoreSystemFlag(IgnoreSystem) {
  Interactor.reset(new InteractiveAgent(TheSema));
}


TemplightDebugger::~TemplightDebugger() { 
  // must be defined here due to TracePrinter being incomplete in header.
}

} // namespace clang

