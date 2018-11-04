add_custom_target(TemplightUnitTests)
set_target_properties(TemplightUnitTests PROPERTIES FOLDER "Templight Unit Tests")

function(add_templight_unittest test_dirname)
  add_unittest(TemplightUnitTests ${test_dirname} ${ARGN})
endfunction()

set(LLVM_LINK_COMPONENTS
  Support
  )

add_templight_unittest(TemplightTests
  TemplightActionTest.cpp
  )

target_link_libraries(TemplightTests
  PRIVATE
  clangBasic
  clangFrontend
  clangSerialization
  clangTemplight
  clangTooling
  )
