//===- TemplightDebugger.cpp ------ Clang Templight Debugger -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TemplightDebugger.h"

#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclLookups.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Sema/Sema.h"
#include "clang/Lex/Lexer.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Regex.h"

#include <string>
#include <vector>
#include <algorithm>

#include <cctype>
#include <cstdio>
#include <cstring>


namespace clang {


namespace {
  

const char* const SynthesisKindStrings[] = { 
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
  Sema::CodeSynthesisContext Inst;
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
                        const Sema::CodeSynthesisContext& aInst) :
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

static std::string getSrcPointer(const Sema& TheSema, SourceLocation SLoc) {
  PresumedLoc PLoc = TheSema.getSourceManager().getPresumedLoc(SLoc);
  FileID fid = TheSema.getSourceManager().getFileID(SLoc);
  std::string SrcPointer = Lexer::getSourceText(
    CharSourceRange::getTokenRange(
      TheSema.getSourceManager().translateLineCol(fid, PLoc.getLine(), 1),
      TheSema.getSourceManager().translateLineCol(fid, PLoc.getLine(), 256)
    ), TheSema.getSourceManager(), TheSema.getLangOpts()).str();
  SrcPointer.push_back('\n');
  SrcPointer.append(PLoc.getColumn()-1, ' ');
  SrcPointer.push_back('^');
  return SrcPointer;
}


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



class TemplateArgRecorder : public RecursiveASTVisitor<TemplateArgRecorder> {
public:
  typedef RecursiveASTVisitor<TemplateArgRecorder> Base;
  
  const Sema &TheSema;
  
  bool shouldVisitTemplateInstantiations() const { return false; }
  
  struct PrintableQueryResult {
    std::string Name;
    std::string FileName;
    int Line;
    int Column;
    std::string SrcPointer;
    
    PrintableQueryResult() : Name(""), FileName("<no-file>"), Line(0), Column(0), SrcPointer("") { }
    
    void nullLocation(const std::string& NullName = "<no-file>") {
      FileName = NullName;
      Line = 0;
      Column = 0;
      SrcPointer = "";
    }
    
    void fromLocation(const Sema &TheSema, SourceLocation SLoc, const std::string& NullName = "") {
      PresumedLoc PLoc = TheSema.getSourceManager().getPresumedLoc(SLoc);
      
      if(!PLoc.isInvalid()) {
        FileName = PLoc.getFilename();
        Line = PLoc.getLine();
        Column = PLoc.getColumn();
        
        FileID fid = TheSema.getSourceManager().getFileID(SLoc);
        SrcPointer = Lexer::getSourceText(
          CharSourceRange::getTokenRange(
            TheSema.getSourceManager().translateLineCol(fid, PLoc.getLine(), 1),
            TheSema.getSourceManager().translateLineCol(fid, PLoc.getLine(), 256)
          ), TheSema.getSourceManager(), TheSema.getLangOpts()).str();
        SrcPointer.push_back('\n');
        SrcPointer.append(PLoc.getColumn()-1, ' ');
        SrcPointer.push_back('^');
        
      } else {
        FileName = NullName;
        Line = 0;
        Column = 0;
        SrcPointer = "";
      }
    }
    
  };
  
  
  llvm::Regex QueryReg;
  
  enum LookupKind {
    LookForDecl = 1,
    LookForType = 2,
    LookForValue = 4
  };
  
  int QueryKind;
  std::vector< PrintableQueryResult > QueryResults;
  // MAYBE change (or add) with Decl* or QualType, etc..
  
  TemplateArgRecorder(const Sema &aSema, const std::string& aReg, int aKind = LookForDecl) : 
    TheSema(aSema), QueryReg(aReg), QueryKind(aKind) { }
  
  
  void RegisterQualTypeQueryResult(PrintableQueryResult& cur_result, QualType q_type) {
    if( QueryKind & LookForType )
      q_type = q_type.getCanonicalType();
    cur_result.Name += q_type.getAsString(TheSema.getLangOpts());
    if( cur_result.Line < 1 ) {
      SourceLocation sl;
      if(const Type* tp = q_type.getTypePtr()) {
        if(const CXXRecordDecl* cxx_decl = tp->getAsCXXRecordDecl()) {
          sl = cxx_decl->getLocation();
        }
      }
      cur_result.fromLocation(TheSema, sl, "<unknown-location>");
    }
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
      
      if(QueryReg.match(param_name)) {
        
        PrintableQueryResult cur_result;
        
        if( QueryKind & LookForDecl ) {
          cur_result.Name = Params->getParam(I)->getName();
          cur_result.fromLocation(TheSema, Params->getParam(I)->getLocation(), "<unknown-location>");
        } 
        
        if( QueryKind != LookForDecl ) { // not (only) look-for-decl 
          switch (Args[I].getKind()) {
            case TemplateArgument::Null: {
              // not much better to do.
              if( !cur_result.Name.empty() )
                cur_result.Name += " with value ";
              cur_result.Name += "<empty>";
              llvm::raw_string_ostream OS_arg(cur_result.Name);
              Args[I].print(TheSema.getPrintingPolicy(), OS_arg);
              OS_arg.str(); // flush to string.
              break;
            }
            
            case TemplateArgument::Integral: {
              if( QueryKind & LookForValue ) {
                if( !cur_result.Name.empty() )
                  cur_result.Name += " with value ";
                cur_result.Name += Args[I].getAsIntegral().toString(10);
              } 
              if( QueryKind & LookForType ) {
                if( !cur_result.Name.empty() )
                  cur_result.Name += " of type ";
                RegisterQualTypeQueryResult(cur_result, Args[I].getIntegralType());
              }
              break;
            }
            
            case TemplateArgument::NullPtr: {
              if( QueryKind & LookForValue ) {
                if( !cur_result.Name.empty() )
                  cur_result.Name += " with value ";
                cur_result.Name += "nullptr";
              }
              if( QueryKind & LookForType ) {
                if( !cur_result.Name.empty() )
                  cur_result.Name += " of type ";
                cur_result.Name += Args[I].getNullPtrType().getAsString(TheSema.getLangOpts());
              }
              break;
            }
            
            case TemplateArgument::Declaration: {
              ValueDecl* vdecl = Args[I].getAsDecl();
              if( QueryKind & LookForValue ) {
                // Attempt to print out the constant-expression value of the declaration:
                //  the meaningful ValueDecl would be NonTypeTemplateParmDecl, VarDecl, EnumConstantDecl, and maybe FunctionDecl
                if( !cur_result.Name.empty() )
                  cur_result.Name += " with value ";
                if( VarDecl* vardecl = dyn_cast<VarDecl>(vdecl) ) {
                  if( APValue* valptr =  vardecl->evaluateValue() ) {
                    llvm::raw_string_ostream OS_arg(cur_result.Name);
                    valptr->printPretty(OS_arg, TheSema.getASTContext(), vdecl->getType());
                    OS_arg.str(); // flush to string.
                  } else {
                    cur_result.Name = "<could not evaluate>";
                  }
                } else {
                  llvm::raw_string_ostream OS_arg(cur_result.Name);
                  vdecl->getNameForDiagnostic(OS_arg, TheSema.getLangOpts(), true);
                  OS_arg.str(); // flush to string.
                  if( cur_result.Line < 1 )
                    cur_result.fromLocation(TheSema, vdecl->getLocation(), "<unknown-location>");
                }
              }
              if( QueryKind & LookForType ) {
                if( !cur_result.Name.empty() )
                  cur_result.Name += " of type ";
                RegisterQualTypeQueryResult(cur_result, vdecl->getType());
              }
              break;
            }
            
            case TemplateArgument::Type: {
              if( !cur_result.Name.empty() )
                cur_result.Name += " standing for ";
              RegisterQualTypeQueryResult(cur_result, Args[I].getAsType());
              break;
            }
            
            case TemplateArgument::Template:
            case TemplateArgument::TemplateExpansion: {
              if( !cur_result.Name.empty() )
                cur_result.Name += " standing for ";
              llvm::raw_string_ostream OS_arg(cur_result.Name);
              TemplateName tname = Args[I].getAsTemplateOrTemplatePattern();
              tname.print(OS_arg, TheSema.getLangOpts());
              OS_arg.str(); // flush to string.
              SourceLocation sl;
              if(TemplateDecl* tmp_decl = tname.getAsTemplateDecl()) {
                sl = tmp_decl->getLocation();
              }
              if( cur_result.Line < 1 )
                cur_result.fromLocation(TheSema, sl, "<unknown-location>");
              break;
            }
            
            case TemplateArgument::Expression: {
              if( QueryKind & LookForValue ) {
                // Evaluate the expression to get the value printed.
                if( !cur_result.Name.empty() )
                  cur_result.Name += " with value ";
                llvm::raw_string_ostream OS_arg(cur_result.Name);
                Args[I].getAsExpr()->printPretty(OS_arg, NULL, TheSema.getLangOpts());
                Expr::EvalResult expr_res;
                if( Args[I].getAsExpr()->EvaluateAsRValue(expr_res, TheSema.getASTContext()) ) {
                  OS_arg << " == ";
                  expr_res.Val.printPretty(OS_arg, TheSema.getASTContext(), Args[I].getAsExpr()->getType());
                } else {
                  OS_arg << " == <could not evaluate>";
                }
                OS_arg.str(); // flush to string.
                if( cur_result.Line < 1 )
                  cur_result.fromLocation(
                    TheSema, Args[I].getAsExpr()->getExprLoc(), "<unknown-location>");
              }
              if( QueryKind & LookForType ) {
                if( !cur_result.Name.empty() )
                  cur_result.Name += " of type ";
                RegisterQualTypeQueryResult(cur_result, Args[I].getAsExpr()->getType());
              }
              break;
            }
            
            case TemplateArgument::Pack: {
              // not much better to do.
              if( !cur_result.Name.empty() )
                cur_result.Name += " standing for ";
              llvm::raw_string_ostream OS_arg(cur_result.Name);
              Args[I].print(TheSema.getPrintingPolicy(), OS_arg);
              OS_arg.str(); // flush to string.
              break;
            }
            
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
        if(QueryReg.match(it.getLookupName().getAsString())) {
          for(DeclContext::lookup_result::iterator ri = (*it).begin(), 
              ri_end = (*it).end(); ri != ri_end; ++ri) {
            NamedDecl* ndecl = *ri;
            if(!ndecl)
              continue;
            PrintableQueryResult cur_result;
            if( QueryKind & LookForDecl ) {
              llvm::raw_string_ostream OS_arg(cur_result.Name);
              ndecl->getNameForDiagnostic(OS_arg, TheSema.getLangOpts(), true);
              OS_arg.str(); // flush to string.
              cur_result.fromLocation(TheSema, ndecl->getLocation(), "<unknown-location>");
            }
            
            if( QueryKind != LookForDecl ) { // if not (only) look-for-decl
              if(TypeDecl* tdecl = dyn_cast<TypeDecl>(ndecl)) {
                if(TypedefNameDecl* utp = dyn_cast<TypedefNameDecl>(tdecl)) {
                  if( !cur_result.Name.empty() )
                    cur_result.Name += " alias for ";
                  RegisterQualTypeQueryResult(cur_result, utp->getUnderlyingType());
                } else if(const Type* tp = tdecl->getTypeForDecl()) {
                  if( !cur_result.Name.empty() )
                    cur_result.Name += " standing for ";
                  RegisterQualTypeQueryResult(cur_result, tp->getCanonicalTypeInternal());
                }
              } else
              if(ValueDecl* vdecl = dyn_cast<ValueDecl>(ndecl)) {
                if( QueryKind & LookForValue ) {
                  // Attempt to print out the constant-expression value of the declaration:
                  //  the meaningful ValueDecl would be NonTypeTemplateParmDecl, VarDecl, EnumConstantDecl, and maybe FunctionDecl
                  if( !cur_result.Name.empty() )
                    cur_result.Name += " with value ";
                  if( VarDecl* vardecl = dyn_cast<VarDecl>(vdecl) ) {
                    if( APValue* valptr =  vardecl->evaluateValue() ) {
                      llvm::raw_string_ostream OS_arg(cur_result.Name);
                      valptr->printPretty(OS_arg, TheSema.getASTContext(), vdecl->getType());
                      OS_arg.str(); // flush to string.
                    } else {
                      cur_result.Name = "<could not evaluate>";
                    }
                  } else {
                    llvm::raw_string_ostream OS_arg(cur_result.Name);
                    vdecl->getNameForDiagnostic(OS_arg, TheSema.getLangOpts(), true);
                    OS_arg.str(); // flush to string.
                    if( cur_result.Line < 1 )
                      cur_result.fromLocation(TheSema, vdecl->getLocation(), "<unknown-location>");
                  }
                } 
                if( QueryKind & LookForType ) {
                  if( !cur_result.Name.empty() )
                    cur_result.Name += " of type ";
                  RegisterQualTypeQueryResult(cur_result, vdecl->getType());
                }
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
  
  
  bool TraverseActiveTempInstantiation(const Sema::CodeSynthesisContext& Inst) {
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
      << SynthesisKindStrings[Entry.Inst.Kind]  << " of " << Entry.Name << '\n'
      << "  at " << Entry.FileName << '|' << Entry.Line << '|' << Entry.Column 
      << " (Memory usage: " << Entry.MemoryUsage << ")\n";
    if ( verboseMode ) {
      std::string SrcPointer = getSrcPointer(TheSema, Entry.Inst.PointOfInstantiation);
      if( !SrcPointer.empty() )
        llvm::outs() << SrcPointer << '\n';
    }
  };
  
  void skipEntry(const TemplateDebuggerEntry &Entry) {
    if ( CurrentSkippedEntry.IsTemplateBegin )
      return; // Already skipping entries.
    if ( !Entry.IsTemplateBegin )
      return; // Cannot skip entry that has ended already.
    CurrentSkippedEntry = Entry;
  };
  
  struct breakpointMatchesStr {
    llvm::StringRef str;
    breakpointMatchesStr(const std::string& aStr) : str(&aStr[0], aStr.size()) { };
    bool operator()(std::pair<std::string,llvm::Regex>& aReg) const {
      return aReg.second.match(str);
    };
  };
  
  bool shouldIgnoreEntry(const TemplateDebuggerEntry &Entry) {
    // Check if it's been commanded to ignore things:
    if ( ignoreAll )
      return true;
    if ( ignoreUntilBreakpoint ) {
      std::vector< std::pair<std::string,llvm::Regex> >::iterator it = std::find_if(
        Breakpoints.begin(), Breakpoints.end(), breakpointMatchesStr(Entry.Name));
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
    
    // Check the black-lists:
    // (1) Is currently ignoring entries?
    if ( CurrentSkippedEntry.IsTemplateBegin ) {
      // Should skip the entry, but need to check if it's the last entry to skip:
      if ( !Entry.IsTemplateBegin
           && ( CurrentSkippedEntry.Inst.Kind == Entry.Inst.Kind )
           && ( CurrentSkippedEntry.Inst.Entity == Entry.Inst.Entity ) ) {
        CurrentSkippedEntry.IsTemplateBegin = false;
      }
      return true;
    }
    // (2) Context:
    if ( CoRegex ) {
      if ( NamedDecl* p_context = dyn_cast_or_null<NamedDecl>(Entry.Inst.Entity->getDeclContext()) ) {
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
      if ( NamedDecl* p_ndecl = dyn_cast_or_null<NamedDecl>(Entry.Inst.Entity) ) {
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
    if ( Entry.Inst.Kind == Sema::CodeSynthesisContext::Memoization ) {
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
      llvm::outs().flush();
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
            if ( !Breakpoints[i].second.match("-") )
              llvm::outs() << "Breakpoint " << i 
                           << " for " << Breakpoints[i].first << '\n';
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
      
      if ( user_command == "setmode" ) {
        if ( com_arg == "verbose" ) {
          verboseMode = true;
          continue;
        }
        else if ( com_arg == "quiet" ) {
          verboseMode = false;
          continue;
        }
        else {
          llvm::outs() << "Invalid setmode command!\n";
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
        TemplateArgRecorder rec(TheSema, "^" + llvm::Regex::escape(com_arg) + "$", 
                                TemplateArgRecorder::LookForDecl);
        if( EntriesStack.empty() )
          rec.LookupInDeclContext(TheSema.getCurLexicalContext(), true);
        else
          rec.TraverseActiveTempInstantiation(EntriesStack.back().Inst);
        for(std::vector<TemplateArgRecorder::PrintableQueryResult>::const_iterator it = rec.QueryResults.begin();
            it != rec.QueryResults.end(); ++it) {
          llvm::outs() 
            << "Found " << it->Name << '\n'
            << "  at " << it->FileName << '|' << it->Line << '|' << it->Column << '\n';
          if( verboseMode && !it->SrcPointer.empty() )
            llvm::outs() << it->SrcPointer << '\n';
        };
        continue;
      }
      else if ( ( user_command == "rl" ) ||
                ( user_command == "rlookup" ) ) {
        TemplateArgRecorder rec(TheSema, com_arg, TemplateArgRecorder::LookForDecl);
        if( EntriesStack.empty() )
          rec.LookupInDeclContext(TheSema.getCurLexicalContext(), true);
        else
          rec.TraverseActiveTempInstantiation(EntriesStack.back().Inst);
        for(std::vector<TemplateArgRecorder::PrintableQueryResult>::const_iterator it = rec.QueryResults.begin();
            it != rec.QueryResults.end(); ++it) {
          llvm::outs() 
            << "Found " << it->Name << '\n'
            << "  at " << it->FileName << '|' << it->Line << '|' << it->Column << '\n';
          if( verboseMode && !it->SrcPointer.empty() )
            llvm::outs() << it->SrcPointer << '\n';
        };
        continue;
      }
      else if ( ( user_command == "t" ) ||
                ( user_command == "typeof" ) ) {
        TemplateArgRecorder rec(TheSema, "^" + llvm::Regex::escape(com_arg) + "$",
                                TemplateArgRecorder::LookForType);
        if( EntriesStack.empty() )
          rec.LookupInDeclContext(TheSema.getCurLexicalContext(), true);
        else
          rec.TraverseActiveTempInstantiation(EntriesStack.back().Inst);
        for(std::vector<TemplateArgRecorder::PrintableQueryResult>::const_iterator it = rec.QueryResults.begin();
            it != rec.QueryResults.end(); ++it) {
          llvm::outs() 
            << "Found " << it->Name << '\n'
            << "  at " << it->FileName << '|' << it->Line << '|' << it->Column << '\n';
          if( verboseMode && !it->SrcPointer.empty() )
            llvm::outs() << it->SrcPointer << '\n';
        };
        continue;
      }
      else if ( ( user_command == "rt" ) ||
                ( user_command == "rtypeof" ) ) {
        TemplateArgRecorder rec(TheSema, com_arg, TemplateArgRecorder::LookForType);
        if( EntriesStack.empty() )
          rec.LookupInDeclContext(TheSema.getCurLexicalContext(), true);
        else
          rec.TraverseActiveTempInstantiation(EntriesStack.back().Inst);
        for(std::vector<TemplateArgRecorder::PrintableQueryResult>::const_iterator it = rec.QueryResults.begin();
            it != rec.QueryResults.end(); ++it) {
          llvm::outs() 
            << "Found " << it->Name << '\n'
            << "  at " << it->FileName << '|' << it->Line << '|' << it->Column << '\n';
          if( verboseMode && !it->SrcPointer.empty() )
            llvm::outs() << it->SrcPointer << '\n';
        };
        continue;
      }
      else if ( ( user_command == "e" ) ||
                ( user_command == "eval" ) ) {
        TemplateArgRecorder rec(TheSema, "^" + llvm::Regex::escape(com_arg) + "$", 
                                TemplateArgRecorder::LookForValue);
        if( EntriesStack.empty() )
          rec.LookupInDeclContext(TheSema.getCurLexicalContext(), true);
        else
          rec.TraverseActiveTempInstantiation(EntriesStack.back().Inst);
        for(std::vector<TemplateArgRecorder::PrintableQueryResult>::const_iterator it = rec.QueryResults.begin();
            it != rec.QueryResults.end(); ++it) {
          llvm::outs() << it->Name << '\n';
        };
        continue;
      }
      else if ( ( user_command == "re" ) ||
                ( user_command == "reval" ) ) {
        TemplateArgRecorder rec(TheSema, com_arg, TemplateArgRecorder::LookForValue);
        if( EntriesStack.empty() )
          rec.LookupInDeclContext(TheSema.getCurLexicalContext(), true);
        else
          rec.TraverseActiveTempInstantiation(EntriesStack.back().Inst);
        for(std::vector<TemplateArgRecorder::PrintableQueryResult>::const_iterator it = rec.QueryResults.begin();
            it != rec.QueryResults.end(); ++it) {
          llvm::outs() << it->Name << '\n';
        };
        continue;
      }
      else if ( ( user_command == "w" ) ||
                ( user_command == "whois" ) ) {
        TemplateArgRecorder rec(TheSema, "^" + llvm::Regex::escape(com_arg) + "$", 
                                TemplateArgRecorder::LookForDecl | 
                                TemplateArgRecorder::LookForType | 
                                TemplateArgRecorder::LookForValue);
        if( EntriesStack.empty() )
          rec.LookupInDeclContext(TheSema.getCurLexicalContext(), true);
        else
          rec.TraverseActiveTempInstantiation(EntriesStack.back().Inst);
        for(std::vector<TemplateArgRecorder::PrintableQueryResult>::const_iterator it = rec.QueryResults.begin();
            it != rec.QueryResults.end(); ++it) {
          llvm::outs() 
            << "Found " << it->Name << '\n'
            << "  at " << it->FileName << '|' << it->Line << '|' << it->Column << '\n';
          if( verboseMode && !it->SrcPointer.empty() )
            llvm::outs() << it->SrcPointer << '\n';
        };
        continue;
      }
      else if ( ( user_command == "rw" ) ||
                ( user_command == "rwhois" ) ) {
        TemplateArgRecorder rec(TheSema, com_arg,
                                TemplateArgRecorder::LookForDecl | 
                                TemplateArgRecorder::LookForType | 
                                TemplateArgRecorder::LookForValue);
        if( EntriesStack.empty() )
          rec.LookupInDeclContext(TheSema.getCurLexicalContext(), true);
        else
          rec.TraverseActiveTempInstantiation(EntriesStack.back().Inst);
        for(std::vector<TemplateArgRecorder::PrintableQueryResult>::const_iterator it = rec.QueryResults.begin();
            it != rec.QueryResults.end(); ++it) {
          llvm::outs() 
            << "Found " << it->Name << '\n'
            << "  at " << it->FileName << '|' << it->Line << '|' << it->Column << '\n';
          if( verboseMode && !it->SrcPointer.empty() )
            llvm::outs() << it->SrcPointer << '\n';
        };
        continue;
      }
      else if ( ( user_command == "b" ) ||
                ( user_command == "break" ) ) {
        std::vector< std::pair<std::string,llvm::Regex> >::iterator it = std::find_if(
          Breakpoints.begin(), Breakpoints.end(), breakpointMatchesStr(com_arg));
        if ( it == Breakpoints.end() ) {
          it = std::find_if(Breakpoints.begin(), Breakpoints.end(), breakpointMatchesStr("-"));
          if ( it == Breakpoints.end() )
            it = Breakpoints.insert(Breakpoints.end(), 
                                    std::pair<std::string,llvm::Regex>(llvm::Regex::escape(com_arg), 
                                                                       llvm::Regex(llvm::Regex::escape(com_arg))));
          else
            *it = std::pair<std::string,llvm::Regex>(llvm::Regex::escape(com_arg), 
                                                     llvm::Regex(llvm::Regex::escape(com_arg)));
        }
        llvm::outs() << "Breakpoint " << (it - Breakpoints.begin()) 
                     << " for " << com_arg << '\n';
        continue;
      }
      else if ( ( user_command == "rb" ) ||
                ( user_command == "rbreak" ) ) {
        std::vector< std::pair<std::string,llvm::Regex> >::iterator it = 
          std::find_if(Breakpoints.begin(), Breakpoints.end(), breakpointMatchesStr("-"));
        if ( it == Breakpoints.end() )
          it = Breakpoints.insert(Breakpoints.end(), 
                                  std::pair<std::string,llvm::Regex>(com_arg, 
                                                                     llvm::Regex(com_arg)));
        else
          *it = std::pair<std::string,llvm::Regex>(com_arg, 
                                                   llvm::Regex(com_arg));
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
                     << " for " << Breakpoints[ind].first << '\n';
        Breakpoints[ind] = std::pair<std::string,llvm::Regex>("-", llvm::Regex("^-$"));
        continue;
      }
      else if ( ( user_command == "bt" ) ||
                ( user_command == "backtrace" ) ||
                ( user_command == "where" ) ) {
        for(std::vector<TemplateDebuggerEntry>::reverse_iterator it = EntriesStack.rbegin();
            it != EntriesStack.rend(); ++it) {
          llvm::outs()
            << SynthesisKindStrings[it->Inst.Kind] << " of " << it->Name 
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
         ( Entry.Inst.Kind == Sema::CodeSynthesisContext::Memoization ) )
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
    ignoreUntilLastEnds(false), ignoreUntilBreakpoint(false), verboseMode(false) {
    CurrentSkippedEntry.IsTemplateBegin = false;
  };
  
  ~InteractiveAgent() { };
  
  const Sema &TheSema;
  
  std::vector<TemplateDebuggerEntry> EntriesStack;
  TemplateDebuggerEntry LastBeginEntry;
  
  std::vector< std::pair<std::string,llvm::Regex> > Breakpoints;
  
  std::string LastUserCommand;
  
  TemplateDebuggerEntry CurrentSkippedEntry;
  std::unique_ptr<llvm::Regex> CoRegex;
  std::unique_ptr<llvm::Regex> IdRegex;
  
  unsigned ignoreAll : 1;
  unsigned ignoreUntilLastEnds : 1;
  unsigned ignoreUntilBreakpoint : 1;
  unsigned verboseMode : 1;
};






void TemplightDebugger::initialize(const Sema &) {
  Interactor->startTrace();
}

void TemplightDebugger::finalize(const Sema &) {
  Interactor->endTrace();
}

void TemplightDebugger::atTemplateBegin(const Sema &TheSema,
                          const Sema::CodeSynthesisContext& Inst) {
  if ( IgnoreSystemFlag && !Inst.PointOfInstantiation.isInvalid() && 
       TheSema.getSourceManager()
         .isInSystemHeader(Inst.PointOfInstantiation) )
    return;
  
  TemplateDebuggerEntry Entry(
    true, (MemoryFlag ? llvm::sys::Process::GetMallocUsage() : 0), TheSema, Inst);
  
  Interactor->printRawEntry(Entry);
}

void TemplightDebugger::atTemplateEnd(const Sema &TheSema,
                          const Sema::CodeSynthesisContext& Inst) {
  if ( IgnoreSystemFlag && !Inst.PointOfInstantiation.isInvalid() && 
       TheSema.getSourceManager()
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

void TemplightDebugger::readBlacklists(const std::string& BLFilename) {
  if ( !Interactor || BLFilename.empty() ) {
    Interactor->CoRegex.reset();
    Interactor->IdRegex.reset();
    return;
  }
  
  std::string CoPattern, IdPattern;
  
  llvm::ErrorOr< std::unique_ptr<llvm::MemoryBuffer> >
    file_epbuf = llvm::MemoryBuffer::getFile(llvm::Twine(BLFilename));
  if(!file_epbuf || (!file_epbuf.get())) {
    llvm::errs() << "Error: [Templight-Action] Could not open the blacklist file!\n";
    Interactor->CoRegex.reset();
    Interactor->IdRegex.reset();
    return;
  }
  
  llvm::Regex findCo("^context ");
  llvm::Regex findId("^identifier ");
  
  const char* it      = file_epbuf.get()->getBufferStart();
  const char* it_mark = file_epbuf.get()->getBufferStart();
  const char* it_end  = file_epbuf.get()->getBufferEnd();
  
  while( it_mark != it_end ) {
    it_mark = std::find(it, it_end, '\n');
    if(*(it_mark-1) == '\r')
      --it_mark;
    llvm::StringRef curLine(&(*it), it_mark - it);
    if( findCo.match(curLine) ) {
      if(!CoPattern.empty())
        CoPattern += '|';
      CoPattern += '(';
      CoPattern.append(&(*(it+8)), it_mark - it - 8);
      CoPattern += ')';
    } else if( findId.match(curLine) ) {
      if(!IdPattern.empty())
        IdPattern += '|';
      IdPattern += '(';
      IdPattern.append(&(*(it+11)), it_mark - it - 11);
      IdPattern += ')';
    }
    while( (it_mark != it_end) && 
           ((*it_mark == '\n') || (*it_mark == '\r')) )
      ++it_mark;
    it = it_mark;
  }
  
  Interactor->CoRegex.reset(new llvm::Regex(CoPattern));
  Interactor->IdRegex.reset(new llvm::Regex(IdPattern));
  return;
}


} // namespace clang

