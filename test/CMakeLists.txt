# Test runner infrastructure for Templight. This configures the Clang
# test trees for use by Lit, and delegates to LLVM's lit test handlers.

set(TEMPLIGHT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(TEMPLIGHT_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/..")

if (CMAKE_CFG_INTDIR STREQUAL ".")
  set(LLVM_BUILD_MODE ".")
else ()
  set(LLVM_BUILD_MODE "%(build_mode)s")
endif ()

string(REPLACE ${CMAKE_CFG_INTDIR} ${LLVM_BUILD_MODE} TEMPLIGHT_DIR ${LLVM_RUNTIME_OUTPUT_INTDIR})

set(TEMPLIGHT_TEST_DEPS
  ${CLANG_TEST_DEPS}
  TemplightUnitTests
  templight
  llvm-config
  FileCheck
  count
  not
)

set(TEMPLIGHT_TEST_EXTRA_ARGS "")

configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg
  )

configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/Unit/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/Unit/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/Unit/lit.cfg.py
  )

add_lit_testsuite(check-templight "Running Templight regression tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${TEMPLIGHT_TEST_DEPS}
  ARGS ${TEMPLIGHT_TEST_EXTRA_ARGS}
  )

set_target_properties(check-templight PROPERTIES FOLDER "Templight tests")
