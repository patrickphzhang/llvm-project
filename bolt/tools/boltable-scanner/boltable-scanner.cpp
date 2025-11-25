//===- bolt/tools/boltable-scanner/boltable-scanner.cpp - Bolt-able scanner ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a binary scanner that will check if a binary is bolt-able.
//
//===----------------------------------------------------------------------===//

#include "bolt/Profile/DataAggregator.h"
#include "bolt/Rewrite/MachORewriteInstance.h"
#include "bolt/Rewrite/RewriteInstance.h"
#include "bolt/Utils/CommandLineOpts.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/Binary.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"

#define DEBUG_TYPE "bolt"

using namespace llvm;
using namespace object;
using namespace bolt;

namespace opts {

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<executable>"),
                                          cl::Required, cl::cat(BoltCategory),
                                          cl::sub(cl::SubCommand::getAll()));

static cl::opt<std::string>
InputDataFilename("data",
  cl::desc("<data file>"),
  cl::Optional,
  cl::cat(BoltCategory));

static cl::alias
BoltProfile("b",
  cl::desc("alias for -data"),
  cl::aliasopt(InputDataFilename),
  cl::cat(BoltCategory));

cl::opt<std::string>
    LogFile("log-file",
            cl::desc("redirect journaling to a file instead of stdout/stderr"),
            cl::Hidden, cl::cat(BoltCategory));

static cl::opt<std::string>
InputDataFilename2("data2",
  cl::desc("<data file>"),
  cl::Optional,
  cl::cat(BoltCategory));

static cl::opt<std::string>
InputFilename2(
  cl::Positional,
  cl::desc("<executable>"),
  cl::Optional,
  cl::cat(BoltDiffCategory));

} // namespace opts

static StringRef ToolName;

static void report_error(StringRef Message, std::error_code EC) {
  assert(EC);
  errs() << ToolName << ": '" << Message << "': " << EC.message() << ".\n";
  exit(1);
}

static void report_error(StringRef Message, Error E) {
  assert(E);
  errs() << ToolName << ": '" << Message << "': " << toString(std::move(E))
         << ".\n";
  exit(1);
}

static void printBoltRevision(llvm::raw_ostream &OS) {
  OS << "BOLT revision " << BoltRevision << "\n";
}

static std::string GetExecutablePath(const char *Argv0) {
  SmallString<256> ExecutablePath(Argv0);
  // Do a PATH lookup if Argv0 isn't a valid path.
  if (!llvm::sys::fs::exists(ExecutablePath))
    if (llvm::ErrorOr<std::string> P =
            llvm::sys::findProgramByName(ExecutablePath))
      ExecutablePath = *P;
  return std::string(ExecutablePath);
}

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  PrettyStackTraceProgram X(argc, argv);

  llvm_shutdown_obj Y; // Call llvm_shutdown() on exit.

  std::string ToolPath = GetExecutablePath(argv[0]);

  // Initialize targets and assembly printers/parsers.
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();
  llvm::InitializeAllDisassemblers();

  llvm::InitializeAllTargets();
  llvm::InitializeAllAsmPrinters();

  cl::AddExtraVersionPrinter(printBoltRevision);
  cl::ParseCommandLineOptions(
      argc, argv,
      "boltable-scanner - BOLT-able binary scanner\n"
      "\nEXAMPLE: boltable-scanner executable\n");
  opts::ShowDensity = true;
  
  if (!sys::fs::exists(opts::InputFilename))
    report_error(opts::InputFilename, errc::no_such_file_or_directory);

  // Initialize journaling streams
  raw_ostream *BOLTJournalOut = &outs();
  raw_ostream *BOLTJournalErr = &errs();
  // RAII obj to keep log file open throughout execution
  std::unique_ptr<raw_fd_ostream> LogFileStream;
  if (!opts::LogFile.empty()) {
    std::error_code LogEC;
    LogFileStream = std::make_unique<raw_fd_ostream>(
        opts::LogFile, LogEC, sys::fs::OpenFlags::OF_None);
    if (LogEC) {
      errs() << "BOLT-ERROR: cannot open requested log file for writing: "
             << LogEC.message() << "\n";
      exit(1);
    }
    BOLTJournalOut = LogFileStream.get();
    BOLTJournalErr = LogFileStream.get();
  }

  // Attempt to open the binary.
  Expected<OwningBinary<Binary>> BinaryOrErr =
    createBinary(opts::InputFilename);
  if (Error E = BinaryOrErr.takeError())
    report_error(opts::InputFilename, std::move(E));
  Binary &Binary = *BinaryOrErr.get().getBinary();

  if (auto *e = dyn_cast<ELFObjectFileBase>(&Binary)) {
    auto RIOrErr = RewriteInstance::create(e, argc, argv, ToolPath,
                                            *BOLTJournalOut, *BOLTJournalErr);
    if (Error E = RIOrErr.takeError())
    report_error(opts::InputFilename, std::move(E));
    RewriteInstance &RI = *RIOrErr.get();
    if (Error E = RI.scan())
    report_error(opts::InputFilename, std::move(E));
  }
  return EXIT_SUCCESS;
}
