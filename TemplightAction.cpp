//===- TemplightAction.h ------ Clang Templight Frontend Action -*- C++ -*-===//
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

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/TemplateInstObserver.h"

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
    llvm::outs() << " Appending the templight tracer as a template instantiation observer...\n";
    TemplateInstantiationObserver::appendNewObserver(
      CI.getSema().TemplateInstObserverChain,
      new TemplightTracer(CI.getSema(), 
        ( OutputToStdOut ? std::string("stdout") : OutputFilename ),
        OutputFormat, MemoryProfile, OutputInSafeMode, IgnoreSystemInst));
    llvm::outs() << " Succeeded!\n";
  }
  if ( InteractiveDebug ) {
    llvm::outs() << " Appending the templight debugger as a template instantiation observer...\n";
    TemplateInstantiationObserver::appendNewObserver(
      CI.getSema().TemplateInstObserverChain,
      new TemplightDebugger(CI.getSema(), 
        MemoryProfile, IgnoreSystemInst));
    llvm::outs() << " Succeeded!\n";
  }
  
  llvm::outs() << " Executing the wrapped front-end action...\n";
  WrapperFrontendAction::ExecuteAction();
  llvm::outs() << " Done!\n";
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

