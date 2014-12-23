//===- CIndex.cpp - Clang-C Source Indexing Library -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the main API hooks in the Clang-C Source Indexing
// library.
//
//===----------------------------------------------------------------------===//

#include "CIndexer.h"
#include "CIndexDiagnostic.h"
#include "CLog.h"
#include "CXCursor.h"
#include "CXSourceLocation.h"
#include "CXString.h"
#include "CXTranslationUnit.h"
#include "CXType.h"
#include "CursorVisitor.h"
#include "TemplightAPI.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticCategories.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/Version.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Index/CommentToXML.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/PreprocessingRecord.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Serialization/SerializationDiagnostic.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"

#if LLVM_ENABLE_THREADS != 0 && defined(__APPLE__)
#define USE_DARWIN_THREADS
#endif

#ifdef USE_DARWIN_THREADS
#include <pthread.h>
#endif

using namespace clang;
using namespace clang::cxtu;
using namespace clang::cxindex;

//===----------------------------------------------------------------------===//
// Misc. API hooks.
//===----------------------------------------------------------------------===//               


extern "C" {

CXTranslationUnit
templight_createTranslationUnitFromSourceFile(CXIndex CIdx,
                                              const char *source_filename,
                                              int num_command_line_args,
                                              const char * const *command_line_args,
                                              unsigned num_unsaved_files,
                                              struct CXUnsavedFile *unsaved_files,
                                              int num_templight_command_line_args,
                                              const char * const *templight_command_line_args) {
  unsigned Options = CXTranslationUnit_DetailedPreprocessingRecord;
  return templight_parseTranslationUnit(CIdx, source_filename,
                                        command_line_args, num_command_line_args,
                                        unsaved_files, num_unsaved_files,
                                        num_templight_command_line_args, templight_command_line_args,
                                        Options);
}

struct ParseTranslationUnitInfo {
  CXIndex CIdx;
  const char *source_filename;
  const char *const *command_line_args;
  int num_command_line_args;
  ArrayRef<CXUnsavedFile> unsaved_files;
  const char *const *templight_com_line_args;
  int num_templight_com_line_args;
  unsigned options;
  CXTranslationUnit *out_TU;
  CXErrorCode &result;
};
static void templight_parseTranslationUnit_Impl(void *UserData) {
  const ParseTranslationUnitInfo *PTUI =
      static_cast<ParseTranslationUnitInfo *>(UserData);
  CXIndex CIdx = PTUI->CIdx;
  const char *source_filename = PTUI->source_filename;
  const char * const *command_line_args = PTUI->command_line_args;
  int num_command_line_args = PTUI->num_command_line_args;
  const char *const *templight_com_line_args = PTUI->templight_com_line_args;
  int num_templight_com_line_args = PTUI->num_templight_com_line_args;
  unsigned options = PTUI->options;
  CXTranslationUnit *out_TU = PTUI->out_TU;
  
  
  bool OutputInSafeMode = false; // "safe-mode"
  bool IgnoreSystemInst = false; // "ignore-system"
  std::string OutputFilename; // "output"
  std::string OutputFormat = "protobuf"; // "format"
  std::string BlackListFilename; // "blacklist"
  for(int i = 0; i < num_templight_com_line_args; ++i) {
    if( strcmp(templight_com_line_args[i], "-safe-mode") == 0 ) {
      OutputInSafeMode = true;
    } else if( strcmp(templight_com_line_args[i], "-ignore-system") == 0 ) {
      IgnoreSystemInst = true;
    } else if( strcmp(templight_com_line_args[i], "-output") == 0 ) {
      OutputFilename = templight_com_line_args[++i];
    } else if( strcmp(templight_com_line_args[i], "-format") == 0 ) {
      OutputFormat = templight_com_line_args[++i];
    } else if( strcmp(templight_com_line_args[i], "-blacklist") == 0 ) {
      BlackListFilename = templight_com_line_args[++i];
    } else {
      fprintf(stderr, "libtemplight: unrecognized argument '%s'.\n", templight_com_line_args[i]);
      PTUI->result = CXError_InvalidArguments;
      return;
    }
  }
  
  if( OutputFilename.empty() ) {
    if(!source_filename) {
      fprintf(stderr, "libtemplight: no output file specified and missing a source file-name to use.\n");
      PTUI->result = CXError_InvalidArguments;
      return;
    }
    OutputFilename = source_filename; // extension automatically added.
  }
  
  
  // Set up the initial return values.
  if (out_TU)
    *out_TU = nullptr;

  // Check arguments.
  if (!CIdx || !out_TU) {
    PTUI->result = CXError_InvalidArguments;
    return;
  }

  CIndexer *CXXIdx = static_cast<CIndexer *>(CIdx);

  if (CXXIdx->isOptEnabled(CXGlobalOpt_ThreadBackgroundPriorityForIndexing))
    setThreadBackgroundPriority();

  bool PrecompilePreamble = options & CXTranslationUnit_PrecompiledPreamble;
  // FIXME: Add a flag for modules.
  TranslationUnitKind TUKind
    = (options & CXTranslationUnit_Incomplete)? TU_Prefix : TU_Complete;
  bool CacheCodeCompletionResults
    = options & CXTranslationUnit_CacheCompletionResults;
  bool IncludeBriefCommentsInCodeCompletion
    = options & CXTranslationUnit_IncludeBriefCommentsInCodeCompletion;
  bool SkipFunctionBodies = options & CXTranslationUnit_SkipFunctionBodies;
  bool ForSerialization = options & CXTranslationUnit_ForSerialization;

  // Configure the diagnostics.
  IntrusiveRefCntPtr<DiagnosticsEngine>
    Diags(CompilerInstance::createDiagnostics(new DiagnosticOptions));

  // Recover resources if we crash before exiting this function.
  llvm::CrashRecoveryContextCleanupRegistrar<DiagnosticsEngine,
    llvm::CrashRecoveryContextReleaseRefCleanup<DiagnosticsEngine> >
    DiagCleanup(Diags.get());

  std::unique_ptr<std::vector<ASTUnit::RemappedFile>> RemappedFiles(
      new std::vector<ASTUnit::RemappedFile>());

  // Recover resources if we crash before exiting this function.
  llvm::CrashRecoveryContextCleanupRegistrar<
    std::vector<ASTUnit::RemappedFile> > RemappedCleanup(RemappedFiles.get());

  for (auto &UF : PTUI->unsaved_files) {
    std::unique_ptr<llvm::MemoryBuffer> MB =
        llvm::MemoryBuffer::getMemBufferCopy(getContents(UF), UF.Filename);
    RemappedFiles->push_back(std::make_pair(UF.Filename, MB.release()));
  }

  std::unique_ptr<std::vector<const char *>> Args(
      new std::vector<const char *>());

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<std::vector<const char*> >
    ArgsCleanup(Args.get());

  // Since the Clang C library is primarily used by batch tools dealing with
  // (often very broken) source code, where spell-checking can have a
  // significant negative impact on performance (particularly when 
  // precompiled headers are involved), we disable it by default.
  // Only do this if we haven't found a spell-checking-related argument.
  bool FoundSpellCheckingArgument = false;
  for (int I = 0; I != num_command_line_args; ++I) {
    if (strcmp(command_line_args[I], "-fno-spell-checking") == 0 ||
        strcmp(command_line_args[I], "-fspell-checking") == 0) {
      FoundSpellCheckingArgument = true;
      break;
    }
  }
  if (!FoundSpellCheckingArgument)
    Args->push_back("-fno-spell-checking");
  
  Args->insert(Args->end(), command_line_args,
               command_line_args + num_command_line_args);

  // The 'source_filename' argument is optional.  If the caller does not
  // specify it then it is assumed that the source file is specified
  // in the actual argument list.
  // Put the source file after command_line_args otherwise if '-x' flag is
  // present it will be unused.
  if (source_filename)
    Args->push_back(source_filename);

  // Do we need the detailed preprocessing record?
  if (options & CXTranslationUnit_DetailedPreprocessingRecord) {
    Args->push_back("-Xclang");
    Args->push_back("-detailed-preprocessing-record");
  }
  
  unsigned NumErrors = Diags->getClient()->getNumErrors();
  std::unique_ptr<ASTUnit> ErrUnit;
  std::unique_ptr<ASTUnit> Unit(ASTUnit::LoadFromCommandLine(
      Args->data(), Args->data() + Args->size(), Diags,
      CXXIdx->getClangResourcesPath(), CXXIdx->getOnlyLocalDecls(),
      /*CaptureDiagnostics=*/true, *RemappedFiles.get(),
      /*RemappedFilesKeepOriginalName=*/true, PrecompilePreamble, TUKind,
      CacheCodeCompletionResults, IncludeBriefCommentsInCodeCompletion,
      /*AllowPCHWithCompilerErrors=*/true, SkipFunctionBodies,
      /*UserFilesAreVolatile=*/true, ForSerialization, &ErrUnit));

  if (NumErrors != Diags->getClient()->getNumErrors()) {
    // Make sure to check that 'Unit' is non-NULL.
    if (CXXIdx->getDisplayDiagnostics())
      printDiagsToStderr(Unit ? Unit.get() : ErrUnit.get());
  }

  if (isASTReadError(Unit ? Unit.get() : ErrUnit.get())) {
    PTUI->result = CXError_ASTReadError;
  } else {
    *PTUI->out_TU = MakeCXTranslationUnit(CXXIdx, Unit.release());
    PTUI->result = *PTUI->out_TU ? CXError_Success : CXError_Failure;
  }
}

CXTranslationUnit
templight_parseTranslationUnit(CXIndex CIdx,
                               const char *source_filename,
                               const char *const *command_line_args,
                               int num_command_line_args,
                               struct CXUnsavedFile *unsaved_files,
                               unsigned num_unsaved_files,
                               int num_templight_command_line_args,
                               const char * const *templight_command_line_args,
                               unsigned options) {
  CXTranslationUnit TU;
  enum CXErrorCode Result = templight_parseTranslationUnit2(
      CIdx, source_filename, command_line_args, num_command_line_args,
      unsaved_files, num_unsaved_files, num_templight_command_line_args, 
      templight_command_line_args, options, &TU);
  (void)Result;
  assert((TU && Result == CXError_Success) ||
         (!TU && Result != CXError_Success));
  return TU;
}

enum CXErrorCode templight_parseTranslationUnit2(
    CXIndex CIdx,
    const char *source_filename,
    const char *const *command_line_args,
    int num_command_line_args,
    struct CXUnsavedFile *unsaved_files,
    unsigned num_unsaved_files,
    int num_templight_command_line_args,
    const char * const *templight_command_line_args,
    unsigned options,
    CXTranslationUnit *out_TU) {
  LOG_FUNC_SECTION {
    *Log << source_filename << ": ";
    for (int i = 0; i != num_command_line_args; ++i)
      *Log << command_line_args[i] << " ";
  }

  if (num_unsaved_files && !unsaved_files)
    return CXError_InvalidArguments;

  CXErrorCode result = CXError_Failure;
  ParseTranslationUnitInfo PTUI = {
      CIdx,
      source_filename,
      command_line_args,
      num_command_line_args,
      llvm::makeArrayRef(unsaved_files, num_unsaved_files),
      templight_command_line_args,
      num_templight_command_line_args,
      options,
      out_TU,
      result};
  llvm::CrashRecoveryContext CRC;

  if (!RunSafely(CRC, templight_parseTranslationUnit_Impl, &PTUI)) {
    fprintf(stderr, "libtemplight: crash detected during parsing: {\n");
    fprintf(stderr, "  'source_filename' : '%s'\n", source_filename);
    fprintf(stderr, "  'command_line_args' : [");
    for (int i = 0; i != num_command_line_args; ++i) {
      if (i)
        fprintf(stderr, ", ");
      fprintf(stderr, "'%s'", command_line_args[i]);
    }
    fprintf(stderr, "],\n");
    fprintf(stderr, "  'unsaved_files' : [");
    for (unsigned i = 0; i != num_unsaved_files; ++i) {
      if (i)
        fprintf(stderr, ", ");
      fprintf(stderr, "('%s', '...', %ld)", unsaved_files[i].Filename,
              unsaved_files[i].Length);
    }
    fprintf(stderr, "],\n");
    fprintf(stderr, "  'options' : %d,\n", options);
    fprintf(stderr, "}\n");

    return CXError_Crashed;
  } else if (getenv("LIBCLANG_RESOURCE_USAGE")) {
    if (CXTranslationUnit *TU = PTUI.out_TU)
      PrintLibclangResourceUsage(*TU);
  }

  return result;
}

struct ReparseTranslationUnitInfo {
  CXTranslationUnit TU;
  ArrayRef<CXUnsavedFile> unsaved_files;
  unsigned options;
  CXErrorCode &result;
};

static void templight_reparseTranslationUnit_Impl(void *UserData) {
  const ReparseTranslationUnitInfo *RTUI =
      static_cast<ReparseTranslationUnitInfo *>(UserData);
  CXTranslationUnit TU = RTUI->TU;
  unsigned options = RTUI->options;
  (void) options;

  // Check arguments.
  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    RTUI->result = CXError_InvalidArguments;
    return;
  }

  // Reset the associated diagnostics.
  delete static_cast<CXDiagnosticSetImpl*>(TU->Diagnostics);
  TU->Diagnostics = nullptr;

  CIndexer *CXXIdx = TU->CIdx;
  if (CXXIdx->isOptEnabled(CXGlobalOpt_ThreadBackgroundPriorityForEditing))
    setThreadBackgroundPriority();

  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);
  ASTUnit::ConcurrencyCheck Check(*CXXUnit);

  std::unique_ptr<std::vector<ASTUnit::RemappedFile>> RemappedFiles(
      new std::vector<ASTUnit::RemappedFile>());

  // Recover resources if we crash before exiting this function.
  llvm::CrashRecoveryContextCleanupRegistrar<
    std::vector<ASTUnit::RemappedFile> > RemappedCleanup(RemappedFiles.get());

  for (auto &UF : RTUI->unsaved_files) {
    std::unique_ptr<llvm::MemoryBuffer> MB =
        llvm::MemoryBuffer::getMemBufferCopy(getContents(UF), UF.Filename);
    RemappedFiles->push_back(std::make_pair(UF.Filename, MB.release()));
  }

  if (!CXXUnit->Reparse(*RemappedFiles.get()))
    RTUI->result = CXError_Success;
  else if (isASTReadError(CXXUnit))
    RTUI->result = CXError_ASTReadError;
}

int templight_reparseTranslationUnit(CXTranslationUnit TU,
                                     unsigned num_unsaved_files,
                                     struct CXUnsavedFile *unsaved_files,
                                     unsigned options) {
  LOG_FUNC_SECTION {
    *Log << TU;
  }

  if (num_unsaved_files && !unsaved_files)
    return CXError_InvalidArguments;

  CXErrorCode result = CXError_Failure;
  ReparseTranslationUnitInfo RTUI = {
      TU, llvm::makeArrayRef(unsaved_files, num_unsaved_files), options,
      result};

  if (getenv("LIBCLANG_NOTHREADS")) {
    templight_reparseTranslationUnit_Impl(&RTUI);
    return result;
  }

  llvm::CrashRecoveryContext CRC;

  if (!RunSafely(CRC, templight_reparseTranslationUnit_Impl, &RTUI)) {
    fprintf(stderr, "libtemplight: crash detected during reparsing\n");
    cxtu::getASTUnit(TU)->setUnsafeToFree(true);
    return CXError_Crashed;
  } else if (getenv("LIBCLANG_RESOURCE_USAGE"))
    PrintLibclangResourceUsage(TU);

  return result;
}


} // end: extern "C"


