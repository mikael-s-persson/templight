//===- TemplightActionTest.cpp ---------------------*- C++ -*--------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TemplightAction.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringRef.h"
#include "gtest/gtest.h"

using namespace clang;

TEST(TemplightActionTest, SimpleInvocation) {
  EXPECT_TRUE(tooling::runToolOnCode(
      new TemplightAction{llvm::make_unique<TemplightDumpAction>()},
      "void f() {;}"));
}
