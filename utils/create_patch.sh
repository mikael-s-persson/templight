#!/bin/bash

# This script can be used for re-creating the patch. It assumes, that the source
# code has been checked out as part of the llvm/clang source tree according to
# the tutorial found in README.md of this repository.

export LANG=C
export LC_MESSAGES=C
export LC_COLLATE=C

SCRIPTPATH="$( cd "$(dirname "$0")" ; pwd -P )"
cd "${SCRIPTPATH}/../../../../../"

VERSIONFILE="${SCRIPTPATH}/../templight_clang_version.txt"

LLVMGIT="$(git log -1 | grep "commit .*" | awk -v N=2 '{print $N}')"
LLVMSVN="$(git log -1 | grep "git-svn-id: .*" | awk -v N=2 '{print $N}')"
cd tools/clang/
CLANGGIT="$(git log -1 | grep "commit .*" | awk -v N=2 '{print $N}')"
CLANGSVN="$(git log -1 | grep "git-svn-id: .*" | awk -v N=2 '{print $N}')"

echo "'templight_clang_patch*.diff' was created using the following LLVM/Clang versions:\n" > $VERSIONFILE

echo "LLVM git: $LLVMGIT" >> $VERSIONFILE
echo "LLVM svn: $LLVMSVN" >> $VERSIONFILE

echo "Clang git: $CLANGGIT" >> $VERSIONFILE
echo "Clang svn: $CLANGSVN" >> $VERSIONFILE

git diff > $SCRIPTPATH/../templight_clang_patch.diff
git diff -U999999 > $SCRIPTPATH/../templight_clang_patch_with_context.diff


