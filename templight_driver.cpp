//===-- templight_driver.cpp - Clang GCC-Compatible Driver --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the entry point to the templight driver; it is a thin wrapper
// for functionality in the Driver clang library with modifications to invoke Templight.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/CharInfo.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Driver/Action.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/Tool.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/ChainedDiagnosticConsumer.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendDiagnostic.h"  // IWYU pragma: keep
#include "clang/Frontend/SerializedDiagnosticPrinter.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "clang/FrontendTool/Utils.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistration.h"
#include "clang/StaticAnalyzer/Frontend/FrontendActions.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"

#include "TemplightAction.h"

#include <set>
#include <memory>
#include <system_error>

using namespace clang;
using namespace clang::driver;
using namespace llvm::opt;
using namespace llvm;



// Mark all Templight options with this category, everything else will be handled as clang driver options.
static cl::OptionCategory ClangTemplightCategory("Templight options (USAGE: templight [[-Xtemplight [templight option]]|[options]] <inputs>)");

static cl::opt<bool> OutputToStdOut("stdout",
  cl::desc("Output template instantiation traces to standard output."),
  cl::cat(ClangTemplightCategory));

static cl::opt<bool> MemoryProfile("memory",
  cl::desc("Profile the memory usage during template instantiations."),
  cl::cat(ClangTemplightCategory));

static cl::opt<bool> OutputInSafeMode("safe-mode",
  cl::desc("Output Templight traces without buffering, \n"
           "not to lose them at failure (note: this will \n"
           "distort the timing profiles due to file I/O latency)."),
  cl::cat(ClangTemplightCategory));

static cl::opt<bool> IgnoreSystemInst("ignore-system",
  cl::desc("Ignore any template instantiation coming from \n"
           "system-includes (-isystem)."),
  cl::cat(ClangTemplightCategory));

static cl::opt<bool> InstProfiler("profiler",
  cl::desc("Start an interactive Templight debugging session."),
  cl::cat(ClangTemplightCategory));

static cl::opt<bool> InteractiveDebug("debugger",
  cl::desc("Start an interactive Templight debugging session."),
  cl::cat(ClangTemplightCategory));

static cl::opt<std::string> OutputFilename("output",
  cl::desc("Write Templight profiling traces to <file>."),
  cl::cat(ClangTemplightCategory));

static std::string LocalOutputFilename;
static SmallVector<std::string, 32> TempOutputFiles;

static cl::opt<std::string> BlackListFilename("blacklist",
  cl::desc("Use regex expressions in <file> to filter out undesirable traces."),
  cl::cat(ClangTemplightCategory));


static cl::Option* TemplightOptions[] = {
    &OutputToStdOut,
    &MemoryProfile,
    &OutputInSafeMode,
    &IgnoreSystemInst,
    &InstProfiler,
    &InteractiveDebug,
    &OutputFilename,
    &BlackListFilename};

void PrintTemplightHelp() {
  // Compute the maximum argument length...
  const std::size_t TemplightOptNum = sizeof(TemplightOptions) / sizeof(cl::Option*);
  std::size_t MaxArgLen = 0;
  for (std::size_t i = 0, e = TemplightOptNum; i != e; ++i)
    MaxArgLen = std::max(MaxArgLen, TemplightOptions[i]->getOptionWidth());

  llvm::outs() << '\n' << ClangTemplightCategory.getName() << "\n\n";

  for (std::size_t i = 0, e = TemplightOptNum; i != e; ++i)
    TemplightOptions[i]->printOptionInfo(MaxArgLen);
  llvm::outs() << '\n';
}

std::string GetExecutablePath(const char *Argv0, bool CanonicalPrefixes) {
  if (!CanonicalPrefixes)
    return Argv0;

  // This just needs to be some symbol in the binary; C++ doesn't
  // allow taking the address of ::main however.
  void *P = (void*) (intptr_t) GetExecutablePath;
  return llvm::sys::fs::getMainExecutable(Argv0, P);
}

#ifdef LINK_POLLY_INTO_TOOLS
namespace polly {
void initializePollyPasses(llvm::PassRegistry &Registry);
}
#endif

static const char *GetStableCStr(std::set<std::string> &SavedStrings,
                                 StringRef S) {
  return SavedStrings.insert(S).first->c_str();
}

struct DriverSuffix {
  const char *Suffix;
  const char *ModeFlag;
};

static const DriverSuffix *FindDriverSuffix(StringRef ProgName) {
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

  for (size_t i = 0; i < llvm::array_lengthof(DriverSuffixes); ++i)
    if (ProgName.endswith(DriverSuffixes[i].Suffix))
      return &DriverSuffixes[i];
  return nullptr;
}

/// Normalize the program name from argv[0] by stripping the file extension if
/// present and lower-casing the string on Windows.
static std::string normalizeProgramName(const char *Argv0) {
  std::string ProgName = llvm::sys::path::stem(Argv0);
#ifdef LLVM_ON_WIN32
  // Transform to lowercase for case insensitive file systems.
  std::transform(ProgName.begin(), ProgName.end(), ProgName.begin(), ::tolower);
#endif
  return ProgName;
}

static const DriverSuffix *parseDriverSuffix(StringRef ProgName) {
  // Try to infer frontend type and default target from the program name by
  // comparing it against DriverSuffixes in order.

  // If there is a match, the function tries to identify a target as prefix.
  // E.g. "x86_64-linux-clang" as interpreted as suffix "clang" with target
  // prefix "x86_64-linux". If such a target prefix is found, is gets added via
  // -target as implicit first argument.
  const DriverSuffix *DS = FindDriverSuffix(ProgName);

  if (!DS) {
    // Try again after stripping any trailing version number:
    // clang++3.5 -> clang++
    ProgName = ProgName.rtrim("0123456789.");
    DS = FindDriverSuffix(ProgName);
  }

  if (!DS) {
    // Try again after stripping trailing -component.
    // clang++-tot -> clang++
    ProgName = ProgName.slice(0, ProgName.rfind('-'));
    DS = FindDriverSuffix(ProgName);
  }
   return DS;
}

static void insertArgsFromProgramName(StringRef ProgName,
                                      const DriverSuffix *DS,
                                      SmallVectorImpl<const char *> &ArgVector,
                                      std::set<std::string> &SavedStrings) {
  if (!DS)
    return;

  if (const char *Flag = DS->ModeFlag) {
    // Add Flag to the arguments.
    auto it = ArgVector.begin();
    if (it != ArgVector.end())
      ++it;
    ArgVector.insert(it, Flag);
  }

  StringRef::size_type LastComponent = ProgName.rfind(
      '-', ProgName.size() - strlen(DS->Suffix));
  if (LastComponent == StringRef::npos)
    return;

  // Infer target from the prefix.
  StringRef Prefix = ProgName.slice(0, LastComponent);
  std::string IgnoredError;
  if (llvm::TargetRegistry::lookupTarget(Prefix, IgnoredError)) {
    auto it = ArgVector.begin();
    if (it != ArgVector.end())
      ++it;
    const char *arr[] = { "-target", GetStableCStr(SavedStrings, Prefix) };
    ArgVector.insert(it, std::begin(arr), std::end(arr));
  }
}

static void SetBackdoorDriverOutputsFromEnvVars(Driver &TheDriver) {
  // Handle CC_PRINT_OPTIONS and CC_PRINT_OPTIONS_FILE.
  TheDriver.CCPrintOptions = !!::getenv("CC_PRINT_OPTIONS");
  if (TheDriver.CCPrintOptions)
    TheDriver.CCPrintOptionsFilename = ::getenv("CC_PRINT_OPTIONS_FILE");

  // Handle CC_PRINT_HEADERS and CC_PRINT_HEADERS_FILE.
  TheDriver.CCPrintHeaders = !!::getenv("CC_PRINT_HEADERS");
  if (TheDriver.CCPrintHeaders)
    TheDriver.CCPrintHeadersFilename = ::getenv("CC_PRINT_HEADERS_FILE");

  // Handle CC_LOG_DIAGNOSTICS and CC_LOG_DIAGNOSTICS_FILE.
  TheDriver.CCLogDiagnostics = !!::getenv("CC_LOG_DIAGNOSTICS");
  if (TheDriver.CCLogDiagnostics)
    TheDriver.CCLogDiagnosticsFilename = ::getenv("CC_LOG_DIAGNOSTICS_FILE");
}

static void FixupDiagPrefixExeName(TextDiagnosticPrinter *DiagClient,
                                   const std::string &Path) {
  // If the templight binary happens to be named cl.exe for compatibility reasons,
  // use templight-cl.exe as the prefix to avoid confusion between templight and MSVC.
  StringRef ExeBasename(llvm::sys::path::filename(Path));
  if (ExeBasename.equals_lower("cl.exe"))
    ExeBasename = "templight-cl.exe";
  DiagClient->setPrefix(ExeBasename);
}

// This lets us create the DiagnosticsEngine with a properly-filled-out
// DiagnosticOptions instance.
static DiagnosticOptions *
CreateAndPopulateDiagOpts(SmallVectorImpl<const char *> &argv) {
  auto *DiagOpts = new DiagnosticOptions;
  std::unique_ptr<OptTable> Opts(createDriverOptTable());
  unsigned MissingArgIndex, MissingArgCount;
  InputArgList Args(Opts->ParseArgs(
      llvm::ArrayRef<const char*>(argv.begin() + 1, argv.end()), MissingArgIndex, MissingArgCount));
  // We ignore MissingArgCount and the return value of ParseDiagnosticArgs.
  // Any errors that would be diagnosed here will also be diagnosed later,
  // when the DiagnosticsEngine actually exists.
  (void) ParseDiagnosticArgs(*DiagOpts, Args);
  return DiagOpts;
}

static void SetInstallDir(SmallVectorImpl<const char *> &argv,
                          Driver &TheDriver) {
  // Attempt to find the original path used to invoke the driver, to determine
  // the installed path. We do this manually, because we want to support that
  // path being a symlink.
  SmallString<128> InstalledPath(argv[0]);

  // Do a PATH lookup, if there are no directory components.
  if (llvm::sys::path::filename(InstalledPath) == InstalledPath) {
    std::string Tmp = llvm::sys::findProgramByName(
      llvm::sys::path::filename(InstalledPath.str())).get();
    if (!Tmp.empty())
      InstalledPath = Tmp;
  }
  llvm::sys::fs::make_absolute(InstalledPath);
  InstalledPath = llvm::sys::path::parent_path(InstalledPath);
  if (llvm::sys::fs::exists(InstalledPath.c_str()))
    TheDriver.setInstalledDir(InstalledPath);
}


static
int ExecuteTemplightInvocation(CompilerInstance *Clang) {
  // Honor -help.
  if (Clang->getFrontendOpts().ShowHelp) {

    // Print the help for the general clang options:
    std::unique_ptr<OptTable> Opts(driver::createDriverOptTable());
    Opts->PrintHelp(llvm::outs(), "templight",
                    "Template Profiler and Debugger based on LLVM 'Clang' Compiler: http://clang.llvm.org",
                    /*Include=*/ driver::options::CC1Option, /*Exclude=*/ 0);

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
  for (unsigned i = 0,
         e = Clang->getFrontendOpts().Plugins.size(); i != e; ++i) {
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
    auto Args = llvm::make_unique<const char*[]>(NumArgs + 2);
    Args[0] = "clang (LLVM option parsing)";
    for (unsigned i = 0; i != NumArgs; ++i)
      Args[i + 1] = Clang->getFrontendOpts().LLVMArgs[i].c_str();
    Args[NumArgs + 1] = nullptr;
    llvm::cl::ParseCommandLineOptions(NumArgs + 1, Args.get());
  }

#ifdef CLANG_ENABLE_STATIC_ANALYZER
  // Honor -analyzer-checker-help.
  // This should happen AFTER plugins have been loaded!
  if (Clang->getAnalyzerOpts()->ShowCheckerHelp) {
    ento::printCheckerHelp(llvm::outs(), Clang->getFrontendOpts().Plugins);
    return 0;
  }
#endif
  // If there were errors in processing arguments, don't do anything else.
  if (Clang->getDiagnostics().hasErrorOccurred())
    return 1;

  // Create and execute the frontend action.
  std::unique_ptr<TemplightAction> Act(new TemplightAction(CreateFrontendAction(*Clang)));
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
    Clang, LocalOutputFilename,
    InstProfiler, OutputToStdOut, MemoryProfile);

  // Executing the templight action...
  bool Success = Clang->ExecuteAction(*Act);
  if (Clang->getFrontendOpts().DisableFree)
    BuryPointer(Act.release());
  return !Success;
}



static
void ExecuteTemplightCommand(Driver &TheDriver, DiagnosticsEngine &Diags,
    Compilation &C, Command &J, const char* Argv0,
    SmallVector<std::pair<int, const Command *>, 4>& FailingCommands) {

  // Since commandLineFitsWithinSystemLimits() may underestimate system's capacity
  // if the tool does not support response files, there is a chance/ that things
  // will just work without a response file, so we silently just skip it.
  if ( J.getCreator().getResponseFilesSupport() != Tool::RF_None &&
       llvm::sys::commandLineFitsWithinSystemLimits(J.getExecutable(), J.getArguments()) ) {
    std::string TmpName = TheDriver.GetTemporaryPath("response", "txt");
    J.setResponseFile(C.addTempFile(C.getArgs().MakeArgString(TmpName.c_str())));
  }

  if ( StringRef(J.getCreator().getName()) == "clang" ) {
    // Initialize a compiler invocation object from the clang (-cc1) arguments.
    const ArgStringList &cc_arguments = J.getArguments();
    const char** args_start = const_cast<const char**>(cc_arguments.data());
    const char** args_end = args_start + cc_arguments.size();

    std::unique_ptr<CompilerInstance> Clang(new CompilerInstance());

    int Res = !CompilerInvocation::CreateFromArgs(
        Clang->getInvocation(), args_start, args_end, Diags);
    if(Res)
      FailingCommands.push_back(std::make_pair(Res, &J));

    Clang->getFrontendOpts().DisableFree = false;

    // Infer the builtin include path if unspecified.
    void *GetExecutablePathVP = (void *)(intptr_t) GetExecutablePath;
    if (Clang->getHeaderSearchOpts().UseBuiltinIncludes &&
        Clang->getHeaderSearchOpts().ResourceDir.empty())
      Clang->getHeaderSearchOpts().ResourceDir =
        CompilerInvocation::GetResourcesPath(Argv0, GetExecutablePathVP);

    // Create the compilers actual diagnostics engine.
    Clang->createDiagnostics();
    if (!Clang->hasDiagnostics()) {
      FailingCommands.push_back(std::make_pair(1, &J));
      return;
    }

    LocalOutputFilename = ""; // Let the filename be created from options or output file name.
    std::string TemplightOutFile = TemplightAction::CreateOutputFilename(
      Clang.get(), "", InstProfiler, OutputToStdOut, MemoryProfile);
    // Check if templight filename is in a temporary path:
    llvm::SmallString<128> TDir;
    llvm::sys::path::system_temp_directory(true, TDir);
    if ( TDir.equals(llvm::sys::path::parent_path(llvm::StringRef(TemplightOutFile))) ) {
      C.addTempFile(TemplightOutFile.c_str());
      TempOutputFiles.push_back(TemplightOutFile);
    }

    // Execute the frontend actions.
    Res = ExecuteTemplightInvocation(Clang.get());
    if(Res)
      FailingCommands.push_back(std::make_pair(Res, &J));

  } else {

    const Command *FailingCommand = nullptr;
    if (int Res = C.ExecuteCommand(J, FailingCommand))
      FailingCommands.push_back(std::make_pair(Res, FailingCommand));

  }

}



int main(int argc_, const char **argv_) {
  llvm::sys::PrintStackTraceOnErrorSignal(argv_[0]);
  llvm::PrettyStackTraceProgram X(argc_, argv_);

  if (llvm::sys::Process::FixupStandardFileDescriptors())
    return 1;

  SmallVector<const char *, 256> argv;
  llvm::SpecificBumpPtrAllocator<char> ArgAllocator;
  std::error_code EC = llvm::sys::Process::GetArgumentVector(
      argv, llvm::makeArrayRef(argv_, argc_), ArgAllocator);
  if (EC) {
    llvm::errs() << "error: couldn't get arguments: " << EC.message() << '\n';
    return 1;
  }

  std::string ProgName = normalizeProgramName(argv[0]);
  const DriverSuffix *DS = parseDriverSuffix(ProgName);

  llvm::BumpPtrAllocator A;
  llvm::StringSaver Saver(A);

  // Parse response files using the GNU syntax, unless we're in CL mode. There
  // are two ways to put clang in CL compatibility mode: argv[0] is either
  // clang-cl or cl, or --driver-mode=cl is on the command line. The normal
  // command line parsing can't happen until after response file parsing, so we
  // have to manually search for a --driver-mode=cl argument the hard way.
  // Finally, our -cc1 tools don't care which tokenization mode we use because
  // response files written by clang will tokenize the same way in either mode.
  llvm::cl::TokenizerCallback Tokenizer = &llvm::cl::TokenizeGNUCommandLine;
  if ((DS && DS->ModeFlag && strcmp(DS->ModeFlag, "--driver-mode=cl") == 0) ||
      std::find_if(argv.begin(), argv.end(), [](const char *F) {
        return F && strcmp(F, "--driver-mode=cl") == 0;
      }) != argv.end()) {
    Tokenizer = &llvm::cl::TokenizeWindowsCommandLine;
  }

  // Determines whether we want nullptr markers in argv to indicate response
  // files end-of-lines. We only use this for the /LINK driver argument.
  bool MarkEOLs = true;
  if (argv.size() > 1 && StringRef(argv[1]).startswith("-cc1"))
    MarkEOLs = false;
  llvm::cl::ExpandResponseFiles(Saver, Tokenizer, argv, MarkEOLs);

  // Separate out templight and clang flags.  templight flags are "-Xtemplight <templight_flag>"
  SmallVector<const char *, 256> templight_argv, clang_argv;
  templight_argv.push_back(argv[0]);
  clang_argv.push_back(argv[0]);
  for (int i = 1, size = argv.size(); i < size; /* in loop */ ) {
    if ((argv[i] != nullptr) &&
        (strcmp(argv[i], "-Xtemplight") == 0)) {
      while( i < size - 1 && argv[++i] == nullptr ) /* skip EOLs */ ;
      templight_argv.push_back(argv[i]);   // the word after -Xtemplight
      if( i == size - 1 ) // was this the last argument?
        break;
      while( i < size - 1 && argv[++i] == nullptr ) /* skip EOLs */ ;
    } else {
      if ((argv[i] != nullptr) &&
          ((strcmp(argv[i], "-help") == 0) ||
           (strcmp(argv[i], "--help") == 0))) {
        // Print the help for the templight options:
        PrintTemplightHelp();
      }
      clang_argv.push_back(argv[i++]);  // also leave -help to driver (to print its help info too)
    }
  }

  cl::ParseCommandLineOptions(
      templight_argv.size(), &templight_argv[0],
      "A tool to profile template instantiations in C++ code.\n");

  bool CanonicalPrefixes = true;
  for (int i = 1, size = clang_argv.size(); i < size; ++i) {
    // Skip end-of-line response file markers
    if (clang_argv[i] == nullptr)
      continue;
    if (StringRef(clang_argv[i]) == "-no-canonical-prefixes") {
      CanonicalPrefixes = false;
      break;
    }
  }

  std::string Path = GetExecutablePath(clang_argv[0], CanonicalPrefixes);

  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts =
      CreateAndPopulateDiagOpts(clang_argv);

  TextDiagnosticPrinter *DiagClient
    = new TextDiagnosticPrinter(llvm::errs(), &*DiagOpts);
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

  ProcessWarningOptions(Diags, *DiagOpts, /*ReportDiags=*/false);

  // Prepare a variable for the return value:
  int Res = 0;

  void *GetExecutablePathVP = (void *)(intptr_t) GetExecutablePath;

  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllAsmParsers();

#ifdef LINK_POLLY_INTO_TOOLS
  llvm::PassRegistry &Registry = *llvm::PassRegistry::getPassRegistry();
  polly::initializePollyPasses(Registry);
#endif

  // Handle -cc1 integrated tools, even if -cc1 was expanded from a response
  // file.
  auto FirstArg = std::find_if(clang_argv.begin() + 1, clang_argv.end(),
                               [](const char *A) { return A != nullptr; });
  bool invokeCC1 = (FirstArg != clang_argv.end() && StringRef(*FirstArg).startswith("-cc1"));
  if (invokeCC1) {
    // If -cc1 came from a response file, remove the EOL sentinels.
    if (MarkEOLs) {
      auto newEnd = std::remove(clang_argv.begin(), clang_argv.end(), nullptr);
      clang_argv.resize(newEnd - clang_argv.begin());
    }

    std::unique_ptr<CompilerInstance> Clang(new CompilerInstance());

    Res = !CompilerInvocation::CreateFromArgs(
        Clang->getInvocation(), clang_argv.begin() + 2, clang_argv.end(), Diags);

    // Infer the builtin include path if unspecified.
    if (Clang->getHeaderSearchOpts().UseBuiltinIncludes &&
        Clang->getHeaderSearchOpts().ResourceDir.empty())
      Clang->getHeaderSearchOpts().ResourceDir =
        CompilerInvocation::GetResourcesPath(clang_argv[0], GetExecutablePathVP);

    // Create the compilers actual diagnostics engine.
    Clang->createDiagnostics();
    if (!Clang->hasDiagnostics()) {
      Res = 1;
      goto cleanup;
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
    SetInstallDir(clang_argv, TheDriver);

    std::set<std::string> SavedStrings;
    insertArgsFromProgramName(ProgName, DS, clang_argv, SavedStrings);

    SetBackdoorDriverOutputsFromEnvVars(TheDriver);

    std::unique_ptr<Compilation> C(TheDriver.BuildCompilation(clang_argv));
    if(!C.get()) {
      Res = 1;
      goto cleanup;
    }

    SmallVector<std::pair<int, const Command *>, 4> FailingCommands;
    for (auto &J : C->getJobs())
      ExecuteTemplightCommand(TheDriver, Diags, *C, J, clang_argv[0], FailingCommands);

    // Merge all the temp files into a single output file:
    if ( ! TempOutputFiles.empty() ) {
      if ( OutputFilename.empty() )
        OutputFilename = "a";
      std::string FinalOutputFilename = TemplightAction::CreateOutputFilename(
        nullptr, OutputFilename,
        InstProfiler, OutputToStdOut, MemoryProfile);
      if ( ( !FinalOutputFilename.empty() ) && ( FinalOutputFilename != "-" ) ) {
        std::error_code error;
        llvm::raw_fd_ostream TraceOS(FinalOutputFilename, error, llvm::sys::fs::F_None);
        if ( error ) {
          llvm::errs() <<
            "Error: [Templight] Can not open file to write trace of template instantiations: "
            << FinalOutputFilename << " Error: " << error.message();
        } else {
          for ( SmallVector< std::string, 32 >::iterator it = TempOutputFiles.begin(),
                it_end = TempOutputFiles.end(); it != it_end; ++it) {
            llvm::ErrorOr< std::unique_ptr<llvm::MemoryBuffer> >
              file_epbuf = llvm::MemoryBuffer::getFile(llvm::Twine(*it));
            if(file_epbuf && file_epbuf.get()) {
              TraceOS << StringRef(file_epbuf.get()->getBufferStart(),
                file_epbuf.get()->getBufferEnd() - file_epbuf.get()->getBufferStart())
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
    for (SmallVectorImpl< std::pair<int, const Command *> >::iterator it =
          FailingCommands.begin(), ie = FailingCommands.end(); it != ie; ++it) {
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

cleanup:

  // If any timers were active but haven't been destroyed yet, print their
  // results now.  This happens in -disable-free mode.
  llvm::TimerGroup::printAll(llvm::errs());

  llvm::llvm_shutdown();

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
