set(LLVM_LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  MC
  Object
  Support
  )

if (BOLT_ENABLE_RUNTIME)
  set(BOLT_DRIVER_DEPS "bolt_rt")
else()
  set(BOLT_DRIVER_DEPS "")
endif()

add_bolt_tool(boltable-scanner
  boltable-scanner.cpp

  DISABLE_LLVM_LINK_LLVM_DYLIB

  DEPENDS
  ${BOLT_DRIVER_DEPS}
  )

target_link_libraries(boltable-scanner
  PRIVATE
  LLVMBOLTProfile
  LLVMBOLTRewrite
  LLVMBOLTUtils
  )

add_dependencies(bolt boltable-scanner)

include_directories(${BOLT_SOURCE_DIR}/lib)
