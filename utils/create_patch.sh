#!/bin/bash

# This script can be used for re-creating the patch. It assumes, that the source
# code has been checked out as part of the llvm/clang source tree according to
# the tutorial found in README.md of this repository.

export LANG=C
export LC_MESSAGES=C
export LC_COLLATE=C

SCRIPTPATH="$( cd "$(dirname "$0")" ; pwd -P )"
cd "${SCRIPTPATH}/../../.."

svn diff --diff-cmd=diff > tools/templight/templight_clang_patch.diff
svn diff --diff-cmd=diff -x -U999999 > tools/templight/templight_clang_patch_with_context.diff

