//===- TemplightAction.cpp ------ Clang Templight Frontend Action -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TemplightAction.h"
#include "TemplightTracer.h"
#include "TemplightDebugger.h"

#include <clang/Frontend/CompilerInstance.h>
#include <clang/Sema/Sema.h>
#include <clang/Sema/TemplateInstObserver.h>

#include <llvm/ADT/Twine.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Regex.h>

#include <algorithm>

namespace clang {

std::unique_ptr<clang::ASTConsumer> TemplightAction::CreateASTConsumer(
    CompilerInstance &CI, StringRef InFile) {
  return WrapperFrontendAction::CreateASTConsumer(CI, InFile);
}
bool TemplightAction::BeginInvocation(CompilerInstance &CI) {
  return WrapperFrontendAction::BeginInvocation(CI);
}
bool TemplightAction::BeginSourceFileAction(CompilerInstance &CI,
                                            StringRef Filename) {
  return WrapperFrontendAction::BeginSourceFileAction(CI, Filename);
}

static void CreateBlackListRegexes(const std::string& BlackListFilename, 
                                   std::string& CoPattern,
                                   std::string& IdPattern) {
  CoPattern = "";
  IdPattern = "";
  
  llvm::ErrorOr< std::unique_ptr<llvm::MemoryBuffer> >
    file_epbuf = llvm::MemoryBuffer::getFile(llvm::Twine(BlackListFilename));
  if(!file_epbuf || (!file_epbuf.get())) {
    llvm::errs() << "Error: [Templight-Action] Could not open the blacklist file!\n";
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
  
  return;
}

std::string TemplightAction::CreateOutputFilename(
    CompilerInstance *CI,
    const std::string& OptOutputName, const std::string& OptOutputFormat,
    bool OptInstProfiler, bool OptOutputToStdOut, bool OptMemoryProfile) {
  std::string result;
  
  if ( !OptInstProfiler )
    return result; // no need for an output-filename.
  
  if ( OptOutputToStdOut ) {
    result = "stdout";
  } else if ( CI && OptOutputName.empty() ) {
    result = CI->getFrontendOpts().OutputFile;
  } else {
    result = OptOutputName;
  }
  
  // Should never get executed. 
  if ( CI && result.empty() ) {
    // then, derive output name from the input name:
    FileID fileID = CI->getSourceManager().getMainFileID();
    result =
      CI->getSourceManager().getFileEntryForID(fileID)->getName();
  }
  
  std::string postfix;
  if( result.rfind(".trace.") == std::string::npos ) {
    result += (OptMemoryProfile ? ".memory.trace." : ".trace.");
    if ( ( OptOutputFormat.empty() ) || ( OptOutputFormat == "yaml" ) ) {
      result += "yaml";
    }
    else if ( OptOutputFormat == "xml" ) {
      result += "xml";
    }
    else if ( OptOutputFormat == "text" ) {
      result += "txt";
    }
    else {
      llvm::errs() << "Error: [Templight-Action] Unrecognized template trace format:" 
                   << OptOutputFormat << "\n";
    }
  }
  
  return result;
}

void TemplightAction::ExecuteAction() {
  
  CompilerInstance &CI = WrapperFrontendAction::getCompilerInstance();
  if (!CI.hasPreprocessor())
    return;
  
  // This part is normally done by ASTFrontEndAction, but needs to happen 
  //  before Templight observers can be created ----------------------->>
  // FIXME: Move the truncation aspect of this into Sema, we delayed this till
  // here so the source manager would be initialized.
  if (hasCodeCompletionSupport() &&
      !CI.getFrontendOpts().CodeCompletionAt.FileName.empty())
    CI.createCodeCompletionConsumer();
  
  // Use a code completion consumer?
  CodeCompleteConsumer *CompletionConsumer = nullptr;
  if (CI.hasCodeCompletionConsumer())
    CompletionConsumer = &CI.getCodeCompletionConsumer();
  
  if (!CI.hasSema())
    CI.createSema(getTranslationUnitKind(), CompletionConsumer);
  //<<--------------------------------------------------------------
  
  std::string CoPattern, IdPattern;
  if( ! BlackListFilename.empty() ) {
    CreateBlackListRegexes(BlackListFilename, CoPattern, IdPattern);
  }
  
  if ( InstProfiler ) {
    TemplightTracer* p_t = new TemplightTracer(CI.getSema(), OutputFilename,
      OutputFormat, MemoryProfile, OutputInSafeMode, IgnoreSystemInst);
    p_t->setBlacklists(CoPattern, IdPattern);
    TemplateInstantiationObserver::appendNewObserver(
      CI.getSema().TemplateInstObserverChain, p_t);
  }
  if ( InteractiveDebug ) {
    TemplightDebugger* p_t = new TemplightDebugger(CI.getSema(), 
      MemoryProfile, IgnoreSystemInst);
    p_t->setBlacklists(CoPattern, IdPattern);
    TemplateInstantiationObserver::appendNewObserver(
      CI.getSema().TemplateInstObserverChain, p_t);
  }
  
  WrapperFrontendAction::ExecuteAction();
}
void TemplightAction::EndSourceFileAction() {
  WrapperFrontendAction::EndSourceFileAction();
}

bool TemplightAction::usesPreprocessorOnly() const {
  return WrapperFrontendAction::usesPreprocessorOnly();
}
TranslationUnitKind TemplightAction::getTranslationUnitKind() {
  return WrapperFrontendAction::getTranslationUnitKind();
}
bool TemplightAction::hasPCHSupport() const {
  return WrapperFrontendAction::hasPCHSupport();
}
bool TemplightAction::hasASTFileSupport() const {
  return WrapperFrontendAction::hasASTFileSupport();
}
bool TemplightAction::hasIRSupport() const {
  return WrapperFrontendAction::hasIRSupport();
}
bool TemplightAction::hasCodeCompletionSupport() const {
  return WrapperFrontendAction::hasCodeCompletionSupport();
}

TemplightAction::TemplightAction(FrontendAction *WrappedAction) : 
  WrapperFrontendAction(WrappedAction) { 
  
}

}

