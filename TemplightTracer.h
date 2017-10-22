//===- TemplightTracer.h ------ Clang Templight Profiler / Tracer -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TEMPLIGHT_TRACER_H
#define LLVM_CLANG_TEMPLIGHT_TRACER_H

#include "clang/Sema/TemplateInstCallback.h"

#include <memory>
#include <string>

namespace clang {


class TemplightTracer : public TemplateInstantiationCallback {
public:
  
  class TracePrinter; // forward-decl.
  
  void initialize(const Sema &TheSema) override;
  void finalize(const Sema &TheSema) override;
  void atTemplateBegin(const Sema &TheSema, const Sema::CodeSynthesisContext& Inst) override;
  void atTemplateEnd(const Sema &TheSema, const Sema::CodeSynthesisContext& Inst) override;
  
private:
  
  unsigned MemoryFlag : 1;
  unsigned SafeModeFlag : 1;
  
  std::unique_ptr<TracePrinter> Printer;
  
public:
  
  /// \brief Sets the format type of the template trace file.
  /// The argument can be xml/yaml/text
  TemplightTracer(const Sema &TheSema, 
                  std::string Output = "", 
                  bool Memory = false, 
                  bool Safemode = false,
                  bool IgnoreSystem = false);
  
  ~TemplightTracer() override;
  
  bool getMemoryFlag() const { return MemoryFlag; };
  bool getSafeModeFlag() const { return SafeModeFlag; };
  
  void readBlacklists(const std::string& BLFilename);
  
};


}

#endif



