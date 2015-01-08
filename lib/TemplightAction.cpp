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
#include <clang/Sema/TemplateInstCallbacks.h>

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

std::string TemplightAction::CreateOutputFilename(
    CompilerInstance *CI,
    const std::string& OptOutputName, const std::string& OptOutputFormat,
    bool OptInstProfiler, bool OptOutputToStdOut, bool OptMemoryProfile) {
  std::string result;
  
  if ( !OptInstProfiler )
    return result; // no need for an output-filename.
  
  if ( OptOutputToStdOut ) {
    result = "-";
  } else if ( CI && OptOutputName.empty() ) {
    result = CI->getFrontendOpts().OutputFile;
  } else {
    result = OptOutputName;
  }
  
  // Should never get executed. 
  if ( CI && result.empty() ) {
    // then, derive output name from the input name:
    if ( CI->hasSourceManager() ) {
      FileID fileID = CI->getSourceManager().getMainFileID();
      result = CI->getSourceManager().getFileEntryForID(fileID)->getName();
    } else { // or, last resort:
      result = "a";
    }
  }
  
  std::string postfix;
  if( result.rfind(".trace.") == std::string::npos ) {
    result += (OptMemoryProfile ? ".memory.trace." : ".trace.");
    if ( ( OptOutputFormat.empty() ) || ( OptOutputFormat == "protobuf" ) ) {
      result += "pbf";
    }
    else if ( OptOutputFormat == "xml" ) {
      result += "xml";
    }
    else if ( OptOutputFormat == "text" ) {
      result += "txt";
    }
    else if ( OptOutputFormat == "graphml" ) {
      result += "graphml";
    }
    else if ( OptOutputFormat == "graphviz" ) {
      result += "gv";
    }
    else if ( OptOutputFormat == "nestedxml" ) {
      result += "xml";
    }
    else if ( OptOutputFormat == "yaml" ) {
      result += "yaml";
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
  
  if ( InstProfiler ) {
    TemplightTracer* p_t = new TemplightTracer(CI.getSema(), OutputFilename,
      OutputFormat, MemoryProfile, OutputInSafeMode, IgnoreSystemInst, TraceTemplateOrigins);
    p_t->readBlacklists(BlackListFilename);
    TemplateInstantiationCallbacks::appendNewCallbacks(
      CI.getSema().TemplateInstCallbacksChain, p_t);
  }
  if ( InteractiveDebug ) {
    TemplightDebugger* p_t = new TemplightDebugger(CI.getSema(), 
      MemoryProfile, IgnoreSystemInst);
    p_t->readBlacklists(BlackListFilename);
    TemplateInstantiationCallbacks::appendNewCallbacks(
      CI.getSema().TemplateInstCallbacksChain, p_t);
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

