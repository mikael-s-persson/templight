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

#include "clang/Sema/TemplateInstObserver.h"

#include <memory>
#include <string>

namespace clang {


class TemplightDebugger : public TemplateInstantiationObserver {
public:
  
  class InteractiveAgent; // forward-decl.
  
protected:
  
  void initializeImpl(const Sema &TheSema) override;
  void finalizeImpl(const Sema &TheSema) override;
  void atTemplateBeginImpl(const Sema &TheSema, const ActiveTemplateInstantiation& Inst) override;
  void atTemplateEndImpl(const Sema &TheSema, const ActiveTemplateInstantiation& Inst) override;
  
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



