//===- TemplightAction.h ------ Clang Templight Frontend Action -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TEMPLIGHT_TEMPLIGHT_ACTION_H
#define LLVM_CLANG_TEMPLIGHT_TEMPLIGHT_ACTION_H

#include <memory>

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "llvm/ADT/StringRef.h"

namespace clang {

class TemplightAction : public WrapperFrontendAction {
protected:
  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                        StringRef InFile) override;
  bool BeginInvocation(CompilerInstance &CI) override;
  bool BeginSourceFileAction(CompilerInstance &CI) override;
  void ExecuteAction() override;
  void EndSourceFileAction() override;

public:
  /// Construct a TemplightAction from an existing action, taking
  /// ownership of it.
  TemplightAction(std::unique_ptr<FrontendAction> WrappedAction);

  bool usesPreprocessorOnly() const override;
  TranslationUnitKind getTranslationUnitKind() override;
  bool hasPCHSupport() const override;
  bool hasASTFileSupport() const override;
  bool hasIRSupport() const override;
  bool hasCodeCompletionSupport() const override;

  static std::string CreateOutputFilename(
    CompilerInstance *CI,
    const std::string& OptOutputName,
    bool OptInstProfiler,
    bool OptOutputToStdOut,
    bool OptMemoryProfile);

  unsigned InstProfiler : 1;
  unsigned OutputToStdOut : 1;
  unsigned MemoryProfile : 1;
  unsigned OutputInSafeMode : 1;
  unsigned IgnoreSystemInst : 1;
  unsigned InteractiveDebug : 1;
  std::string OutputFilename;
  std::string BlackListFilename;

private:
  void EnsureHasSema(CompilerInstance& CI);
};

}

#endif
