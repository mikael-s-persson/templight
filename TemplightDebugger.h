//===- TemplightDebugger.h ------ Clang Templight Debugger -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TEMPLIGHT_DEBUGGER_H
#define LLVM_CLANG_TEMPLIGHT_DEBUGGER_H

#include "clang/Sema/TemplateInstCallback.h"

#include <memory>
#include <string>

namespace clang {


class TemplightDebugger : public TemplateInstantiationCallback {
public:
  
  class InteractiveAgent; // forward-decl.
  
  void initialize(const Sema &TheSema) override;
  void finalize(const Sema &TheSema) override;
  void atTemplateBegin(const Sema &TheSema, const Sema::CodeSynthesisContext& Inst) override;
  void atTemplateEnd(const Sema &TheSema, const Sema::CodeSynthesisContext& Inst) override;
  
private:
  
  unsigned MemoryFlag : 1;
  unsigned IgnoreSystemFlag : 1;
  
  std::unique_ptr<InteractiveAgent> Interactor;
  
public:
  
  /// \brief Construct the templight debugger.
  TemplightDebugger(const Sema &TheSema, 
                    bool Memory = false, 
                    bool IgnoreSystem = false);
  
  ~TemplightDebugger() override;
  
  bool getMemoryFlag() const { return MemoryFlag; };
  
  void readBlacklists(const std::string& BLFilename);
  
};




}


#endif



