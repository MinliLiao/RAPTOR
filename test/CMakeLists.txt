umbrella_lit_testsuite_begin(check-all)

configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
)

set(RAPTOR_TEST_DEPS LLVMRaptor-${LLVM_VERSION_MAJOR} Raptor-RT-${LLVM_VERSION_MAJOR})

add_subdirectory(Unit)
if (${Clang_FOUND})
  if (MPFR_LIB_PATH)
    add_subdirectory(Integration)
  else ()
    message(WARNING "mpfr lib required for integration test but not found. Skipping integration test.")
  endif()
endif()

umbrella_lit_testsuite_end(check-all)
