//===-- templight_driver.cpp ------------------------------*- C++ -*-------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the entry point to the templight driver; it is a thin wrapper
// for functionality in the Driver clang library with modifications to invoke
// Templight.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/CharInfo.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Config/config.h"
#include "clang/Driver/Action.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/Tool.h"
#include "clang/Frontend/ChainedDiagnosticConsumer.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendDiagnostic.h" // IWYU pragma: keep
#include "clang/Frontend/SerializedDiagnosticPrinter.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "clang/FrontendTool/Utils.h"
#include "clang/StaticAnalyzer/Frontend/FrontendActions.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"

#include "TemplightAction.h"

#include <memory>
#include <set>
#include <system_error>

using namespace clang;
using namespace clang::driver;
using namespace llvm::opt;
using namespace llvm;

// Mark all Templight options with this category, everything else will be
// handled as clang driver options.
static cl::OptionCategory
    ClangTemplightCategory("Templight options (USAGE: templight [[-Xtemplight "
                           "[templight option]]|[options]] <inputs>)");

static cl::opt<bool> OutputToStdOut(
    "stdout",
    cl::desc("Output template instantiation traces to standard output."),
    cl::cat(ClangTemplightCategory));

static cl::opt<bool> MemoryProfile(
    "memory",
    cl::desc("Profile the memory usage during template instantiations."),
    cl::cat(ClangTemplightCategory));

static cl::opt<bool> OutputInSafeMode(
    "safe-mode",
    cl::desc("Output Templight traces without buffering, \n"
             "not to lose them at failure (note: this will \n"
             "distort the timing profiles due to file I/O latency)."),
    cl::cat(ClangTemplightCategory));

static cl::opt<bool>
    IgnoreSystemInst("ignore-system",
                     cl::desc("Ignore any template instantiation coming from \n"
                              "system-includes (-isystem)."),
                     cl::cat(ClangTemplightCategory));

static cl::opt<bool>
    InstProfiler("profiler",
                 cl::desc("Start an interactive Templight debugging session."),
                 cl::cat(ClangTemplightCategory));

static cl::opt<bool> InteractiveDebug(
    "debugger", cl::desc("Start an interactive Templight debugging session."),
    cl::cat(ClangTemplightCategory));

static cl::opt<std::string>
    OutputFilename("output",
                   cl::desc("Write Templight profiling traces to <file>."),
                   cl::cat(ClangTemplightCategory));

static std::string LocalOutputFilename;
static SmallVector<std::string, 32> TempOutputFiles;

static cl::opt<std::string> BlackListFilename(
    "blacklist",
    cl::desc(
        "Use regex expressions in <file> to filter out undesirable traces."),
    cl::cat(ClangTemplightCategory));

static cl::Option *TemplightOptions[] = {
    &OutputToStdOut, &MemoryProfile,    &OutputInSafeMode, &IgnoreSystemInst,
    &InstProfiler,   &InteractiveDebug, &OutputFilename,   &BlackListFilename};

void PrintTemplightHelp() {
  // Compute the maximum argument length...
  const std::size_t TemplightOptNum =
      sizeof(TemplightOptions) / sizeof(cl::Option *);
  std::size_t MaxArgLen = 0;
  for (std::size_t i = 0, e = TemplightOptNum; i != e; ++i) {
    MaxArgLen = std::max(MaxArgLen, TemplightOptions[i]->getOptionWidth());
  }

  llvm::outs() << '\n' << ClangTemplightCategory.getName() << "\n\n";

  for (std::size_t i = 0, e = TemplightOptNum; i != e; ++i) {
    TemplightOptions[i]->printOptionInfo(MaxArgLen);
  }
  llvm::outs() << '\n';
}

std::string GetExecutablePath(const char *Argv0, bool CanonicalPrefixes) {
  if (!CanonicalPrefixes) {
    SmallString<128> ExecutablePath(Argv0);
    // Do a PATH lookup if Argv0 isn't a valid path.
    if (!llvm::sys::fs::exists(ExecutablePath)) {
      if (llvm::ErrorOr<std::string> P =
              llvm::sys::findProgramByName(ExecutablePath)) {
        ExecutablePath = *P;
      }
    }
    return std::string(ExecutablePath.str());
  }

  // This just needs to be some symbol in the binary; C++ doesn't
  // allow taking the address of ::main however.
  void *P = (void*) (intptr_t) GetExecutablePath;
  return llvm::sys::fs::getMainExecutable(Argv0, P);
}

static const char *GetStableCStr(llvm::StringSet<> &SavedStrings, StringRef S) {
  return SavedStrings.insert(S).first->getKeyData();
}

struct DriverSuffix {
  const char *Suffix;
  const char *ModeFlag;
};

static const DriverSuffix *FindDriverSuffix(StringRef ProgName, size_t &Pos) {
  // A list of known driver suffixes. Suffixes are compared against the
  // program name in order. If there is a match, the frontend type if updated as
  // necessary by applying the ModeFlag.
  static const DriverSuffix DriverSuffixes[] = {
      {"templight", nullptr},
      {"templight++", "--driver-mode=g++"},
      {"templight-c++", "--driver-mode=g++"},
      {"templight-cc", nullptr},
      {"templight-cpp", "--driver-mode=cpp"},
      {"templight-g++", "--driver-mode=g++"},
      {"templight-gcc", nullptr},
      {"templight-cl", "--driver-mode=cl"},
      {"cc", nullptr},
      {"cpp", "--driver-mode=cpp"},
      {"cl", "--driver-mode=cl"},
      {"++", "--driver-mode=g++"},
  };

  for (const auto &DS : DriverSuffixes) {
    StringRef Suffix(DS.Suffix);
    if (ProgName.ends_with(Suffix)) {
      Pos = ProgName.size() - Suffix.size();
      return &DS;
    }
  }
  return nullptr;
}

/// Normalize the program name from argv[0] by stripping the file extension if
/// present and lower-casing the string on Windows.
static std::string normalizeProgramName(llvm::StringRef Argv0) {
  std::string ProgName = std::string(llvm::sys::path::filename(Argv0));
  if (is_style_windows(llvm::sys::path::Style::native)) {
    // Transform to lowercase for case insensitive file systems.
    std::transform(ProgName.begin(), ProgName.end(), ProgName.begin(),
                   ::tolower);
  }
  return ProgName;
}

static const DriverSuffix *parseDriverSuffix(StringRef ProgName, size_t &Pos) {
  // Try to infer frontend type and default target from the program name by
  // comparing it against DriverSuffixes in order.

  // If there is a match, the function tries to identify a target as prefix.
  // E.g. "x86_64-linux-clang" as interpreted as suffix "clang" with target
  // prefix "x86_64-linux". If such a target prefix is found, it may be
  // added via -target as implicit first argument.
  const DriverSuffix *DS = FindDriverSuffix(ProgName, Pos);

  if (!DS && ProgName.ends_with(".exe")) {
    // Try again after stripping the executable suffix:
    // clang++.exe -> clang++
    ProgName = ProgName.drop_back(StringRef(".exe").size());
    DS = FindDriverSuffix(ProgName, Pos);
  }

  if (!DS) {
    // Try again after stripping any trailing version number:
    // clang++3.5 -> clang++
    ProgName = ProgName.rtrim("0123456789.");
    DS = FindDriverSuffix(ProgName, Pos);
  }

  if (!DS) {
    // Try again after stripping trailing -component.
    // clang++-tot -> clang++
    ProgName = ProgName.slice(0, ProgName.rfind('-'));
    DS = FindDriverSuffix(ProgName, Pos);
  }
  return DS;
}

ParsedClangName getTargetAndModeFromProgramName(StringRef PN) {
  std::string ProgName = normalizeProgramName(PN);
  size_t SuffixPos;
  const DriverSuffix *DS = parseDriverSuffix(ProgName, SuffixPos);
  if (!DS)
    return {};
  size_t SuffixEnd = SuffixPos + strlen(DS->Suffix);

  size_t LastComponent = ProgName.rfind('-', SuffixPos);
  if (LastComponent == std::string::npos)
    return ParsedClangName(ProgName.substr(0, SuffixEnd), DS->ModeFlag);
  std::string ModeSuffix = ProgName.substr(LastComponent + 1,
                                           SuffixEnd - LastComponent - 1);

  // Infer target from the prefix.
  StringRef Prefix(ProgName);
  Prefix = Prefix.slice(0, LastComponent);
  std::string IgnoredError;
  bool IsRegistered =
      llvm::TargetRegistry::lookupTarget(std::string(Prefix), IgnoredError);
  return ParsedClangName{std::string(Prefix), ModeSuffix, DS->ModeFlag,
                         IsRegistered};
}

static void insertTargetAndModeArgs(const ParsedClangName &NameParts,
                                    SmallVectorImpl<const char *> &ArgVector,
                                    llvm::StringSet<> &SavedStrings) {
  // Put target and mode arguments at the start of argument list so that
  // arguments specified in command line could override them. Avoid putting
  // them at index 0, as an option like '-cc1' must remain the first.
  int InsertionPoint = 0;
  if (ArgVector.size() > 0)
    ++InsertionPoint;

  if (NameParts.DriverMode) {
    // Add the mode flag to the arguments.
    ArgVector.insert(ArgVector.begin() + InsertionPoint,
                     GetStableCStr(SavedStrings, NameParts.DriverMode));
  }

  if (NameParts.TargetIsValid) {
    const char *arr[] = {"-target", GetStableCStr(SavedStrings,
                                                  NameParts.TargetPrefix)};
    ArgVector.insert(ArgVector.begin() + InsertionPoint,
                     std::begin(arr), std::end(arr));
  }
}

static void getCLEnvVarOptions(std::string &EnvValue, llvm::StringSaver &Saver,
                               SmallVectorImpl<const char *> &Opts) {
  llvm::cl::TokenizeWindowsCommandLine(EnvValue, Saver, Opts);
  // The first instance of '#' should be replaced with '=' in each option.
  for (const char *Opt : Opts) {
    if (char *NumberSignPtr = const_cast<char *>(::strchr(Opt, '#'))) {
      *NumberSignPtr = '=';
    }
  }
}

template <class T>
static T checkEnvVar(const char *EnvOptSet, const char *EnvOptFile,
                     std::string &OptFile) {
  const char *Str = ::getenv(EnvOptSet);
  if (!Str) {
    return T{};
  }

  T OptVal = Str;
  if (const char *Var = ::getenv(EnvOptFile)) {
    OptFile = Var;
  }
  return OptVal;
}

static bool SetBackdoorDriverOutputsFromEnvVars(Driver &TheDriver) {
  TheDriver.CCPrintOptions =
      checkEnvVar<bool>("CC_PRINT_OPTIONS", "CC_PRINT_OPTIONS_FILE",
                        TheDriver.CCPrintOptionsFilename);
  if (checkEnvVar<bool>("CC_PRINT_HEADERS", "CC_PRINT_HEADERS_FILE",
                        TheDriver.CCPrintHeadersFilename)) {
    TheDriver.CCPrintHeadersFormat = HIFMT_Textual;
    TheDriver.CCPrintHeadersFiltering = HIFIL_None;
  } else {
    std::string EnvVar = checkEnvVar<std::string>(
        "CC_PRINT_HEADERS_FORMAT", "CC_PRINT_HEADERS_FILE",
        TheDriver.CCPrintHeadersFilename);
    if (!EnvVar.empty()) {
      TheDriver.CCPrintHeadersFormat =
          stringToHeaderIncludeFormatKind(EnvVar.c_str());
      if (!TheDriver.CCPrintHeadersFormat) {
        TheDriver.Diag(clang::diag::err_drv_print_header_env_var)
            << 0 << EnvVar;
        return false;
      }

      const char *FilteringStr = ::getenv("CC_PRINT_HEADERS_FILTERING");
      HeaderIncludeFilteringKind Filtering;
      if (!stringToHeaderIncludeFiltering(FilteringStr, Filtering)) {
        TheDriver.Diag(clang::diag::err_drv_print_header_env_var)
            << 1 << FilteringStr;
        return false;
      }

      if ((TheDriver.CCPrintHeadersFormat == HIFMT_Textual &&
           Filtering != HIFIL_None) ||
          (TheDriver.CCPrintHeadersFormat == HIFMT_JSON &&
           Filtering != HIFIL_Only_Direct_System)) {
        TheDriver.Diag(clang::diag::err_drv_print_header_env_var_combination)
            << EnvVar << FilteringStr;
        return false;
      }
      TheDriver.CCPrintHeadersFiltering = Filtering;
    }
  }

  TheDriver.CCLogDiagnostics =
      checkEnvVar<bool>("CC_LOG_DIAGNOSTICS", "CC_LOG_DIAGNOSTICS_FILE",
                        TheDriver.CCLogDiagnosticsFilename);
  TheDriver.CCPrintProcessStats =
      checkEnvVar<bool>("CC_PRINT_PROC_STAT", "CC_PRINT_PROC_STAT_FILE",
                        TheDriver.CCPrintStatReportFilename);
  TheDriver.CCPrintInternalStats =
      checkEnvVar<bool>("CC_PRINT_INTERNAL_STAT", "CC_PRINT_INTERNAL_STAT_FILE",
                        TheDriver.CCPrintInternalStatReportFilename);

  return true;
}

static void FixupDiagPrefixExeName(TextDiagnosticPrinter *DiagClient,
                                   const std::string &Path) {
  // If the templight binary happens to be named cl.exe for compatibility
  // reasons, use templight-cl.exe as the prefix to avoid confusion between
  // templight and MSVC.
  StringRef ExeBasename(llvm::sys::path::filename(Path));
  if (ExeBasename.equals_insensitive("cl.exe")) {
    ExeBasename = "templight-cl.exe";
  }
  DiagClient->setPrefix(ExeBasename.str());
}

static int ExecuteTemplightInvocation(CompilerInstance *Clang) {
  // Honor -help.
  if (Clang->getFrontendOpts().ShowHelp) {

    // Print the help for the general clang options:
    getDriverOptTable().printHelp(
        llvm::outs(), "templight",
        "Template Profiler and Debugger based on LLVM 'Clang' "
        "Compiler: http://clang.llvm.org",
        /*Include=*/clang::driver::options::CC1Option, /*Exclude=*/0, false);

    return 0;
  }

  // Honor -version.
  //
  // FIXME: Use a better -version message?
  if (Clang->getFrontendOpts().ShowVersion) {
    llvm::cl::PrintVersionMessage();
    return 0;
  }

  // Load any requested plugins.
  for (unsigned i = 0, e = Clang->getFrontendOpts().Plugins.size(); i != e;
       ++i) {
    const std::string &Path = Clang->getFrontendOpts().Plugins[i];
    std::string Error;
    if (llvm::sys::DynamicLibrary::LoadLibraryPermanently(Path.c_str(), &Error))
      Clang->getDiagnostics().Report(diag::err_fe_unable_to_load_plugin)
          << Path << Error;
  }

  // Honor -mllvm.
  //
  // FIXME: Remove this, one day.
  // This should happen AFTER plugins have been loaded!
  if (!Clang->getFrontendOpts().LLVMArgs.empty()) {
    unsigned NumArgs = Clang->getFrontendOpts().LLVMArgs.size();
    std::vector<const char*> Args(NumArgs + 2);
    Args[0] = "clang (LLVM option parsing)";
    for (unsigned i = 0; i != NumArgs; ++i)
      Args[i + 1] = Clang->getFrontendOpts().LLVMArgs[i].c_str();
    Args[NumArgs + 1] = nullptr;
    llvm::cl::ParseCommandLineOptions(NumArgs + 1, Args.data());
  }

  // If there were errors in processing arguments, don't do anything else.
  if (Clang->getDiagnostics().hasErrorOccurred())
    return 1;

  // Create and execute the frontend action.
  std::unique_ptr<TemplightAction> Act(
      new TemplightAction(CreateFrontendAction(*Clang)));
  if (!Act)
    return 1;

  // Setting up templight action object parameters...
  Act->InstProfiler = InstProfiler;
  Act->OutputToStdOut = OutputToStdOut;
  Act->MemoryProfile = MemoryProfile;
  Act->OutputInSafeMode = OutputInSafeMode;
  Act->IgnoreSystemInst = IgnoreSystemInst;
  Act->InteractiveDebug = InteractiveDebug;
  Act->BlackListFilename = BlackListFilename;

  Act->OutputFilename = TemplightAction::CreateOutputFilename(
      Clang, LocalOutputFilename, InstProfiler, OutputToStdOut, MemoryProfile);

  // Executing the templight action...
  bool Success = Clang->ExecuteAction(*Act);
  if (Clang->getFrontendOpts().DisableFree)
    BuryPointer(Act.release());
  return !Success;
}

static void ExecuteTemplightCommand(
    Driver &TheDriver, DiagnosticsEngine &Diags, Compilation &C, Command &J,
    const char *Argv0,
    SmallVector<std::pair<int, const Command *>, 4> &FailingCommands) {

  // Since commandLineFitsWithinSystemLimits() may underestimate system's
  // capacity if the tool does not support response files, there is a chance/
  // that things will just work without a response file, so we silently just
  // skip it.
  if (J.getResponseFileSupport().ResponseKind !=
          ResponseFileSupport::RF_None &&
      !llvm::sys::commandLineFitsWithinSystemLimits(J.getExecutable(),
                                                   J.getArguments()))
  {
    const std::string TmpName = TheDriver.GetTemporaryPath("response", "txt");
    J.setResponseFile(C.addTempFile(C.getArgs().MakeArgString(TmpName)));
  }

  if (StringRef(J.getCreator().getName()) == "clang") {
    // Initialize a compiler invocation object from the clang (-cc1) arguments.
    const ArgStringList &cc_arguments = J.getArguments();

    std::unique_ptr<CompilerInstance> Clang(new CompilerInstance());

    int Res = !CompilerInvocation::CreateFromArgs(Clang->getInvocation(),
                                                  cc_arguments, Diags);
    if (Res)
      FailingCommands.push_back(std::make_pair(Res, &J));

    Clang->getFrontendOpts().DisableFree = false;

    // Infer the builtin include path if unspecified.
    void *GetExecutablePathVP = (void *)(intptr_t)GetExecutablePath;
    if (Clang->getHeaderSearchOpts().UseBuiltinIncludes &&
        Clang->getHeaderSearchOpts().ResourceDir.empty())
      Clang->getHeaderSearchOpts().ResourceDir =
          CompilerInvocation::GetResourcesPath(Argv0, GetExecutablePathVP);

    // Create the compilers actual diagnostics engine.
    Clang->createDiagnostics(*llvm::vfs::getRealFileSystem());
    if (!Clang->hasDiagnostics()) {
      FailingCommands.push_back(std::make_pair(1, &J));
      return;
    }

    LocalOutputFilename =
        ""; // Let the filename be created from options or output file name.
    std::string TemplightOutFile = TemplightAction::CreateOutputFilename(
        Clang.get(), "", InstProfiler, OutputToStdOut, MemoryProfile);
    // Check if templight filename is in a temporary path:
    if (Clang->getFrontendOpts().UseTemporary) {
      C.addTempFile(TemplightOutFile.c_str());
      TempOutputFiles.push_back(TemplightOutFile);
    }

    // Execute the frontend actions.
    Res = ExecuteTemplightInvocation(Clang.get());
    if (Res)
      FailingCommands.push_back(std::make_pair(Res, &J));

  } else {

    const Command *FailingCommand = nullptr;
    if (int Res = C.ExecuteCommand(J, FailingCommand))
      FailingCommands.push_back(std::make_pair(Res, FailingCommand));
  }
}

int main(int argc_, const char **argv_) {
  llvm::InitLLVM X(argc_, argv_);

  if (llvm::sys::Process::FixupStandardFileDescriptors())
    return 1;

  SmallVector<const char *, 256> Args(argv_, argv_ + argc_);

  auto TargetAndMode = getTargetAndModeFromProgramName(Args[0]);

  llvm::BumpPtrAllocator A;
  llvm::StringSaver Saver(A);

  // Parse response files using the GNU syntax, unless we're in CL mode. There
  // are two ways to put clang in CL compatibility mode: Args[0] is either
  // clang-cl or cl, or --driver-mode=cl is on the command line. The normal
  // command line parsing can't happen until after response file parsing, so we
  // have to manually search for a --driver-mode=cl argument the hard way.
  // Finally, our -cc1 tools don't care which tokenization mode we use because
  // response files written by clang will tokenize the same way in either mode.
  bool ClangCLMode = llvm::StringRef(TargetAndMode.DriverMode).ends_with("cl");

  enum { Default, POSIX, Windows } RSPQuoting = Default;
  for (const char *F : Args) {
    if (strcmp(F, "--rsp-quoting=posix") == 0)
      RSPQuoting = POSIX;
    else if (strcmp(F, "--rsp-quoting=windows") == 0)
      RSPQuoting = Windows;
  }

  bool MarkEOLs = ClangCLMode;

  llvm::cl::TokenizerCallback Tokenizer;
  if (RSPQuoting == Windows || (RSPQuoting == Default && ClangCLMode))
    Tokenizer = &llvm::cl::TokenizeWindowsCommandLine;
  else
    Tokenizer = &llvm::cl::TokenizeGNUCommandLine;

  // Determines whether we want nullptr markers in Args to indicate response
  // files end-of-lines. We only use this for the /LINK driver argument.
  if (MarkEOLs && Args.size() > 1 && StringRef(Args[1]).starts_with("-cc1"))
    MarkEOLs = false;
  llvm::cl::ExpansionContext ECtx(A, Tokenizer);
  ECtx.setMarkEOLs(MarkEOLs);
  if (llvm::Error Err = ECtx.expandResponseFiles(Args)) {
    llvm::errs() << toString(std::move(Err)) << '\n';
    return 1;
  }

  // Handle CL and _CL_ which permits additional command line options to be
  // prepended or appended.
  if (ClangCLMode) {
    // Arguments in "CL" are prepended.
    std::optional<std::string> OptCL = llvm::sys::Process::GetEnv("CL");
    if (OptCL) {
      SmallVector<const char *, 8> PrependedOpts;
      getCLEnvVarOptions(*OptCL, Saver, PrependedOpts);

      // Insert right after the program name to prepend to the argument list.
      Args.insert(Args.begin() + 1, PrependedOpts.begin(), PrependedOpts.end());
    }
    // Arguments in "_CL_" are appended.
    std::optional<std::string> Opt_CL_ = llvm::sys::Process::GetEnv("_CL_");
    if (Opt_CL_) {
      SmallVector<const char *, 8> AppendedOpts;
      getCLEnvVarOptions(*Opt_CL_, Saver, AppendedOpts);

      // Insert at the end of the argument list to append.
      Args.append(AppendedOpts.begin(), AppendedOpts.end());
    }
  }

  llvm::StringSet<> SavedStrings;
  // Handle CCC_OVERRIDE_OPTIONS, used for editing a command line behind the
  // scenes.
  if (const char *OverrideStr = ::getenv("CCC_OVERRIDE_OPTIONS")) {
    // FIXME: Driver shouldn't take extra initial argument.
    clang::driver::applyOverrideOptions(Args, OverrideStr, SavedStrings, &llvm::errs());
  }

  // Separate out templight and clang flags.  templight flags are "-Xtemplight
  // <templight_flag>"
  SmallVector<const char *, 256> TemplightArgs, ClangArgs;
  TemplightArgs.push_back(Args[0]);
  ClangArgs.push_back(Args[0]);
  for (int i = 1, size = Args.size(); i < size; /* in loop */) {
    if ((Args[i] != nullptr) && (strcmp(Args[i], "-Xtemplight") == 0)) {
      while (i < size - 1 && Args[++i] == nullptr) /* skip EOLs */
        ;
      TemplightArgs.push_back(Args[i]); // the word after -Xtemplight
      if (i == size - 1)                 // was this the last argument?
        break;
      while (i < size - 1 && Args[++i] == nullptr) /* skip EOLs */
        ;
    } else {
      if ((Args[i] != nullptr) && ((strcmp(Args[i], "-help") == 0) ||
                                   (strcmp(Args[i], "--help") == 0))) {
        // Print the help for the templight options:
        PrintTemplightHelp();
      }
      ClangArgs.push_back(
          Args[i++]); // also leave -help to driver (to print its help info too)
    }
  }

  cl::ParseCommandLineOptions(
      TemplightArgs.size(), &TemplightArgs[0],
      "A tool to profile template instantiations in C++ code.\n");

  bool CanonicalPrefixes = true;
  for (int i = 1, size = ClangArgs.size(); i < size; ++i) {
    // Skip end-of-line response file markers
    if (ClangArgs[i] == nullptr)
      continue;
    if (StringRef(ClangArgs[i]) == "-canonical-prefixes")
      CanonicalPrefixes = true;
    else if (StringRef(ClangArgs[i]) == "-no-canonical-prefixes")
      CanonicalPrefixes = false;
  }

  std::string Path = GetExecutablePath(ClangArgs[0], CanonicalPrefixes);

  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts =
      CreateAndPopulateDiagOpts(ClangArgs);

  TextDiagnosticPrinter *DiagClient =
      new TextDiagnosticPrinter(llvm::errs(), &*DiagOpts);
  FixupDiagPrefixExeName(DiagClient, Path);

  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());

  DiagnosticsEngine Diags(DiagID, &*DiagOpts, DiagClient);

  if (!DiagOpts->DiagnosticSerializationFile.empty()) {
    auto SerializedConsumer =
        clang::serialized_diags::create(DiagOpts->DiagnosticSerializationFile,
                                        &*DiagOpts, /*MergeChildRecords=*/true);
    Diags.setClient(new ChainedDiagnosticConsumer(
        Diags.takeClient(), std::move(SerializedConsumer)));
  }

  ProcessWarningOptions(Diags, *DiagOpts, *llvm::vfs::getRealFileSystem(), /*ReportDiags=*/false);

  // Prepare a variable for the return value:
  int Res = 0;

  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllAsmParsers();

  // Handle -cc1 integrated tools, even if -cc1 was expanded from a response
  // file.
  auto FirstArg = std::find_if(ClangArgs.begin() + 1, ClangArgs.end(),
                               [](const char *A) { return A != nullptr; });
  bool invokeCC1 =
      (FirstArg != ClangArgs.end() && StringRef(*FirstArg).starts_with("-cc1"));
  if (invokeCC1) {
    // If -cc1 came from a response file, remove the EOL sentinels.
    if (MarkEOLs) {
      auto newEnd = std::remove(ClangArgs.begin(), ClangArgs.end(), nullptr);
      ClangArgs.resize(newEnd - ClangArgs.begin());
    }

    std::unique_ptr<CompilerInstance> Clang(new CompilerInstance());

    Res = !CompilerInvocation::CreateFromArgs(Clang->getInvocation(),
                                              ArrayRef<const char *>(ClangArgs.data() + 2, ClangArgs.data() + ClangArgs.size()),
                                              Diags);

    // Infer the builtin include path if unspecified.
    void *GetExecutablePathVP = (void *)(intptr_t)GetExecutablePath;
    if (Clang->getHeaderSearchOpts().UseBuiltinIncludes &&
        Clang->getHeaderSearchOpts().ResourceDir.empty())
      Clang->getHeaderSearchOpts().ResourceDir =
          CompilerInvocation::GetResourcesPath(ClangArgs[0],
                                               GetExecutablePathVP);

    // Create the compilers actual diagnostics engine.
    Clang->createDiagnostics(*llvm::vfs::getRealFileSystem());
    if (!Clang->hasDiagnostics()) {
      return 1;
    }

    LocalOutputFilename = OutputFilename;

    // Execute the frontend actions.
    Res = ExecuteTemplightInvocation(Clang.get());

    // When running with -disable-free, don't do any destruction or shutdown.
    if (Clang->getFrontendOpts().DisableFree) {
      if (llvm::AreStatisticsEnabled() || Clang->getFrontendOpts().ShowStats)
        llvm::PrintStatistics();
      BuryPointer(std::move(Clang));
    }

  } else {

    Driver TheDriver(Path, llvm::sys::getDefaultTargetTriple(), Diags);
    TheDriver.setTitle("templight");

    TheDriver.setTargetAndMode(TargetAndMode);
    insertTargetAndModeArgs(TargetAndMode, ClangArgs, SavedStrings);

    if (!SetBackdoorDriverOutputsFromEnvVars(TheDriver)) {
      return 1;
    }

    std::unique_ptr<Compilation> C(TheDriver.BuildCompilation(ClangArgs));
    if (!C.get()) {
      return 1;
    }

    // If there were errors building the compilation, quit now.
    if (TheDriver.getDiags().hasErrorOccurred())
      return 1;

    SmallVector<std::pair<int, const Command *>, 4> FailingCommands;
    for (auto &J : C->getJobs())
      ExecuteTemplightCommand(TheDriver, Diags, *C, J, ClangArgs[0],
                              FailingCommands);

    // Merge all the temp files into a single output file:
    if (!TempOutputFiles.empty()) {
      if (OutputFilename.empty())
        OutputFilename = "a";
      std::string FinalOutputFilename = TemplightAction::CreateOutputFilename(
          nullptr, OutputFilename, InstProfiler, OutputToStdOut, MemoryProfile);
      if ((!FinalOutputFilename.empty()) && (FinalOutputFilename != "-")) {
        std::error_code error;
        llvm::raw_fd_ostream TraceOS(FinalOutputFilename, error,
                                     llvm::sys::fs::OF_None);
        if (error) {
          llvm::errs() << "Error: [Templight] Can not open file to write trace "
                          "of template instantiations: "
                       << FinalOutputFilename << " Error: " << error.message();
        } else {
          for (SmallVector<std::string, 32>::iterator
                   it = TempOutputFiles.begin(),
                   it_end = TempOutputFiles.end();
               it != it_end; ++it) {
            llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> file_epbuf =
                llvm::MemoryBuffer::getFile(llvm::Twine(*it));
            if (file_epbuf && file_epbuf.get()) {
              TraceOS << StringRef(file_epbuf.get()->getBufferStart(),
                                   file_epbuf.get()->getBufferEnd() -
                                       file_epbuf.get()->getBufferStart())
                      << '\n';
            }
          }
        }
      }
    }

    // Remove temp files.
    C->CleanupFileList(C->getTempFiles());

    // If the command succeeded, the number of failing commands should zero:
    Res = FailingCommands.size();

    // Otherwise, remove result files and print extra information about abnormal
    // failures.
    for (SmallVectorImpl<std::pair<int, const Command *>>::iterator
             it = FailingCommands.begin(),
             ie = FailingCommands.end();
         it != ie; ++it) {
      int FailRes = it->first;
      const Command *FailingCommand = it->second;

      // Remove result files if we're not saving temps.
      if (!C->getArgs().hasArg(options::OPT_save_temps)) {
        const JobAction *JA = cast<JobAction>(&FailingCommand->getSource());
        C->CleanupFileMap(C->getResultFiles(), JA, true);

        // Failure result files are valid unless we crashed.
        if (FailRes < 0)
          C->CleanupFileMap(C->getFailureResultFiles(), JA, true);
      }

      // Print extra information about abnormal failures, if possible.
      const Tool &FailingTool = FailingCommand->getCreator();

      if (!FailingCommand->getCreator().hasGoodDiagnostics() || FailRes != 1) {
        if (FailRes < 0)
          Diags.Report(clang::diag::err_drv_command_signalled)
              << FailingTool.getShortName();
        else
          Diags.Report(clang::diag::err_drv_command_failed)
              << FailingTool.getShortName() << FailRes;
      }
    }
  }

  // If any timers were active but haven't been destroyed yet, print their
  // results now.  This happens in -disable-free mode.
  llvm::TimerGroup::printAll(llvm::errs());

#ifdef LLVM_ON_WIN32
  // Exit status should not be negative on Win32, unless abnormal termination.
  // Once abnormal termiation was caught, negative status should not be
  // propagated.
  if (Res < 0)
    Res = 1;
#endif

  // If we have multiple failing commands, we return the result of the first
  // failing command.
  return Res;
}
