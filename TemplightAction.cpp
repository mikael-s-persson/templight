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
#include <clang/Sema/TemplateInstCallback.h>

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
bool TemplightAction::BeginSourceFileAction(CompilerInstance &CI) {
  return WrapperFrontendAction::BeginSourceFileAction(CI);
}

std::string TemplightAction::CreateOutputFilename(
    CompilerInstance *CI,
    const std::string& OptOutputName,
    bool OptInstProfiler, bool OptOutputToStdOut, bool OptMemoryProfile) {
  std::string result;

  if ( !OptInstProfiler )
    return result; // no need for an output-filename.

  if ( OptOutputToStdOut ) {
    return "-";
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

  if( result.rfind(".trace.") == std::string::npos ) {
    result += (OptMemoryProfile ? ".memory.trace." : ".trace.");
    result += "pbf";
  }

  return result;
}

void TemplightAction::EnsureHasSema(CompilerInstance& CI) {
  if (!CI.hasSema()) {
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

    CI.createSema(getTranslationUnitKind(), CompletionConsumer);
    //<<--------------------------------------------------------------
  }
}

void TemplightAction::ExecuteAction() {

  CompilerInstance &CI = WrapperFrontendAction::getCompilerInstance();
  if (!CI.hasPreprocessor())
    return;

  if ( InstProfiler ) {
    EnsureHasSema(CI);

    std::unique_ptr<TemplightTracer> p_t(new TemplightTracer(CI.getSema(), OutputFilename,
      MemoryProfile, OutputInSafeMode, IgnoreSystemInst));
    p_t->readBlacklists(BlackListFilename);
    CI.getSema().TemplateInstCallbacks.push_back(std::move(p_t));
  }
  if ( InteractiveDebug ) {
    EnsureHasSema(CI);

    std::unique_ptr<TemplightDebugger> p_t(new TemplightDebugger(CI.getSema(),
      MemoryProfile, IgnoreSystemInst));
    p_t->readBlacklists(BlackListFilename);
    CI.getSema().TemplateInstCallbacks.push_back(std::move(p_t));
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

TemplightAction::TemplightAction(std::unique_ptr<FrontendAction> WrappedAction) :
    WrapperFrontendAction(std::move(WrappedAction)) {

}

}
