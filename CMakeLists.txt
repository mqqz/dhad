cmake_minimum_required(VERSION 3.22)

# Prefer clang when no compiler was specified.
if(NOT CMAKE_C_COMPILER)
  find_program(_dhad_clang_c clang)
  if(_dhad_clang_c)
    set(CMAKE_C_COMPILER "${_dhad_clang_c}" CACHE STRING "C compiler" FORCE)
  endif()
endif()
if(NOT CMAKE_CXX_COMPILER)
  find_program(_dhad_clang_cxx clang++)
  if(_dhad_clang_cxx)
    set(CMAKE_CXX_COMPILER "${_dhad_clang_cxx}" CACHE STRING "C++ compiler" FORCE)
  endif()
endif()

project(dhad LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DHAD_FORCE_PARSER_REBUILD "Force regeneration of parser tables." OFF)
set(DHAD_PYTHON_VENV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.venv"
    CACHE PATH "Shared Python virtual environment directory.")

include(CTest)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(Python3 COMPONENTS Interpreter REQUIRED)
if(Python3_VERSION VERSION_LESS 3.10)
  message(FATAL_ERROR "Python ${Python3_VERSION} is too old; require >= 3.10")
endif()

if(NOT LLVM_DIR)
  set(_dhad_llvm_versions 21 20 19 18 17)
  foreach(ver IN LISTS _dhad_llvm_versions)
    set(_dhad_candidate "/usr/lib/llvm-${ver}/cmake")
    if(EXISTS "${_dhad_candidate}/LLVMConfig.cmake")
      set(LLVM_DIR "${_dhad_candidate}" CACHE PATH "Path to LLVMConfig.cmake" FORCE)
      break()
    endif()
    set(_dhad_candidate "/usr/lib/llvm/${ver}/lib/cmake/llvm")
    if(EXISTS "${_dhad_candidate}/LLVMConfig.cmake")
      set(LLVM_DIR "${_dhad_candidate}" CACHE PATH "Path to LLVMConfig.cmake" FORCE)
      break()
    endif()
  endforeach()
endif()

find_package(LLVM REQUIRED CONFIG)
if(LLVM_PACKAGE_VERSION VERSION_LESS 17)
  message(FATAL_ERROR "LLVM ${LLVM_PACKAGE_VERSION} is too old; require >= 17")
endif()
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
llvm_map_components_to_libnames(DHAD_LLVM_LIBS support core irreader)
if(NOT LLVM_TOOLS_BINARY_DIR)
  set(LLVM_TOOLS_BINARY_DIR "${LLVM_INSTALL_PREFIX}/bin" CACHE PATH
      "Path to LLVM command-line tools" FORCE)
endif()

# ------------------------------------------------------------------------------
# Parser table generation
# ------------------------------------------------------------------------------

set(PARSER_VENV_DIR "${DHAD_PYTHON_VENV_DIR}")
if(WIN32)
  set(DHAD_VENV_PYTHON "${PARSER_VENV_DIR}/Scripts/python.exe")
else()
  set(DHAD_VENV_PYTHON "${PARSER_VENV_DIR}/bin/python3")
endif()
set(DHAD_TEST_PYTHON "${DHAD_VENV_PYTHON}")
set(PARSER_REQUIREMENTS "${CMAKE_CURRENT_SOURCE_DIR}/requirements.txt")
set(PARSER_VENV_STAMP "${PARSER_VENV_DIR}/.stamp")

add_custom_command(
  OUTPUT "${PARSER_VENV_STAMP}"
  COMMAND "${Python3_EXECUTABLE}" -m venv "${PARSER_VENV_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PARSER_REQUIREMENTS}"
          "${PARSER_VENV_DIR}/requirements.lock"
  COMMAND "${CMAKE_COMMAND}" -E touch "${PARSER_VENV_STAMP}"
  DEPENDS "${PARSER_REQUIREMENTS}"
  COMMENT "Creating parser generator virtual environment"
  VERBATIM)
add_custom_target(parser_env DEPENDS "${PARSER_VENV_STAMP}")

set(PARSER_TABLE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/parser_tables.gen.hpp")
set(PARSER_GEN_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_parser_tables.py")
set(PARSER_GEN_FORCE_FLAG "")
if(DHAD_FORCE_PARSER_REBUILD)
  set(PARSER_GEN_FORCE_FLAG --force)
endif()

add_custom_command(
  OUTPUT "${PARSER_TABLE_OUTPUT}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
  COMMAND
    "${DHAD_VENV_PYTHON}" "${PARSER_GEN_SCRIPT}"
    --grammar-def "${CMAKE_CURRENT_SOURCE_DIR}/src/parser/grammar.def"
    --nonterminals "${CMAKE_CURRENT_SOURCE_DIR}/src/parser/nonterminals.def"
    --output "${PARSER_TABLE_OUTPUT}" ${PARSER_GEN_FORCE_FLAG}
  DEPENDS
    parser_env
    "${PARSER_GEN_SCRIPT}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/parser/grammar.def"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/parser/nonterminals.def"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/parser/rules.hpp"
  COMMENT "Generating parser tables"
  VERBATIM)
add_custom_target(parser_tables_gen DEPENDS "${PARSER_TABLE_OUTPUT}")
add_dependencies(parser_tables_gen parser_env)

add_compile_definitions(
  DHAD_GENERATED_PARSER_TABLE_HEADER="${PARSER_TABLE_OUTPUT}"
)

# ------------------------------------------------------------------------------
# Helper for strict warnings
# ------------------------------------------------------------------------------

function(dhad_strict_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

# ------------------------------------------------------------------------------
# Libraries
# ------------------------------------------------------------------------------

add_library(dhad_typing STATIC
  src/typing/types.cpp
  src/typing/scope.cpp
  src/typing/checker.cpp
  src/std/native.cpp
  src/std/registry.cpp
  src/std/runtime.cpp)
target_include_directories(dhad_typing PUBLIC src)
target_include_directories(dhad_typing SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
dhad_strict_warnings(dhad_typing)

add_library(dhad_ast STATIC src/ast/ast.cpp src/ast/builders.cpp)
target_include_directories(dhad_ast PUBLIC src)
target_include_directories(dhad_ast SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
target_link_libraries(dhad_ast PUBLIC ${DHAD_LLVM_LIBS})
dhad_strict_warnings(dhad_ast)

add_library(dhad_lexer STATIC src/lexer/lexer.cpp)
target_include_directories(dhad_lexer
  PUBLIC src/lexer
  PRIVATE src/parser "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(dhad_lexer PUBLIC dhad_ast)
dhad_strict_warnings(dhad_lexer)

add_library(dhad_parser STATIC src/parser/parser.cpp src/parser/rules.cpp
                              src/parser/actions.cpp)
target_link_libraries(dhad_parser PUBLIC dhad_lexer dhad_ast dhad_typing)
target_include_directories(dhad_parser
  PUBLIC src src/parser "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_dependencies(dhad_parser parser_tables_gen)
dhad_strict_warnings(dhad_parser)

add_library(dhad_codegen STATIC src/codegen/codegen.cpp)
target_link_libraries(dhad_codegen PUBLIC dhad_ast ${DHAD_LLVM_LIBS})
target_include_directories(dhad_codegen PUBLIC src)
target_include_directories(dhad_codegen SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
dhad_strict_warnings(dhad_codegen)

# ------------------------------------------------------------------------------
# Executables
# ------------------------------------------------------------------------------

add_executable(lang_driver tests/lang_driver.cpp)
target_link_libraries(lang_driver PRIVATE dhad_lexer dhad_parser)
target_include_directories(lang_driver PRIVATE src
                                             src/lexer
                                             src/parser
                                             "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_dependencies(lang_driver parser_tables_gen)
dhad_strict_warnings(lang_driver)

add_executable(codegen_ex1 EXCLUDE_FROM_ALL tests/codegen_ex1.cpp)
target_link_libraries(codegen_ex1 PRIVATE dhad_parser dhad_codegen dhad_typing)
add_dependencies(codegen_ex1 parser_tables_gen)
dhad_strict_warnings(codegen_ex1)

add_executable(dhad src/main.cpp)
target_link_libraries(dhad PRIVATE dhad_parser dhad_codegen dhad_typing)
target_include_directories(dhad PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_dependencies(dhad parser_tables_gen)
target_compile_definitions(dhad PRIVATE DHAD_DEFAULT_CLANG="${CMAKE_C_COMPILER}")
dhad_strict_warnings(dhad)

# ------------------------------------------------------------------------------
# Testing
# ------------------------------------------------------------------------------

if(BUILD_TESTING)
  add_test(
    NAME lexer_smoke
    COMMAND
      "${DHAD_TEST_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_lang_tests.py"
      --driver
      "$<TARGET_FILE:lang_driver>"
      --case
      lexer
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/lexer_smoke.dh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/expectations/lexer_smoke.txt")

  add_test(
    NAME parser_smoke
    COMMAND
      "${DHAD_TEST_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_lang_tests.py"
      --driver
      "$<TARGET_FILE:lang_driver>"
      --case
      parser
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/parser_smoke.dh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/expectations/parser_smoke.txt")

  add_test(
    NAME type_decl_parser
    COMMAND
      "${DHAD_TEST_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_lang_tests.py"
      --driver
      "$<TARGET_FILE:lang_driver>"
      --case
      parser
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/type_decl_smoke.dh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/expectations/type_decl_smoke.txt")

  add_test(
    NAME codegen_example
    COMMAND "${DHAD_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_codegen_tests.py"
            --codegen "$<TARGET_FILE:codegen_ex1>"
            --example "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/ex1.dh"
            --opt "${LLVM_TOOLS_BINARY_DIR}/opt"
            --lli "${LLVM_TOOLS_BINARY_DIR}/lli")

  add_test(
    NAME float_runtime
    COMMAND "${DHAD_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_codegen_tests.py"
            --codegen "$<TARGET_FILE:codegen_ex1>"
            --example "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/float_runtime.dh"
            --opt "${LLVM_TOOLS_BINARY_DIR}/opt"
            --lli "${LLVM_TOOLS_BINARY_DIR}/lli"
            --expected "ok\n")

  add_test(
    NAME struct_runtime
    COMMAND "${DHAD_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_codegen_tests.py"
            --codegen "$<TARGET_FILE:codegen_ex1>"
            --example "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/struct_runtime.dh"
            --opt "${LLVM_TOOLS_BINARY_DIR}/opt"
            --lli "${LLVM_TOOLS_BINARY_DIR}/lli"
            --expected "ok\n")

  add_test(
    NAME break_continue_outside_loop
    COMMAND
      "${DHAD_TEST_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_dhad_tests.py"
      --driver
      "$<TARGET_FILE:dhad>"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/break_outside_loop.dh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/expectations/break_outside_loop.txt"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/continue_outside_loop.dh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/expectations/continue_outside_loop.txt")

  add_test(
    NAME missing_return
    COMMAND
      "${DHAD_TEST_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_dhad_tests.py"
      --driver
      "$<TARGET_FILE:dhad>"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/missing_return.dh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/expectations/missing_return.txt")

  add_test(
    NAME import_from_file
    COMMAND
      "${DHAD_TEST_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_dhad_ir_tests.py"
      --driver
      "$<TARGET_FILE:dhad>"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/import_main.dh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/expectations/import_ir_contains.txt")

  add_test(
    NAME import_missing
    COMMAND
      "${DHAD_TEST_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_dhad_tests.py"
      --driver
      "$<TARGET_FILE:dhad>"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/import_missing.dh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/expectations/import_missing.txt")

  add_test(
    NAME float_ops_codegen
    COMMAND
      "${DHAD_TEST_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_dhad_ir_tests.py"
      --driver
      "$<TARGET_FILE:dhad>"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/float_ops.dh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/expectations/float_ir_contains.txt")

  add_test(
    NAME composite_type_mismatch
    COMMAND
      "${DHAD_TEST_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_dhad_tests.py"
      --driver
      "$<TARGET_FILE:dhad>"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/type_mismatch_sum.dh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/expectations/type_mismatch_sum.txt"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/type_mismatch_product.dh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/expectations/type_mismatch_product.txt"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/type_mismatch_array.dh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/expectations/type_mismatch_array.txt")

  add_test(
    NAME unknown_type
    COMMAND
      "${DHAD_TEST_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_dhad_tests.py"
      --driver
      "$<TARGET_FILE:dhad>"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/unknown_type.dh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/expectations/unknown_type.txt")

  add_test(
    NAME composite_codegen
    COMMAND
      "${DHAD_TEST_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_dhad_ir_tests.py"
      --driver
      "$<TARGET_FILE:dhad>"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/composite_codegen.dh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/expectations/composite_ir_contains.txt")

  add_test(
    NAME array_literal_codegen
    COMMAND
      "${DHAD_TEST_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_dhad_ir_tests.py"
      --driver
      "$<TARGET_FILE:dhad>"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/array_literal_codegen.dh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/expectations/array_literal_ir_contains.txt")

  add_test(
    NAME arabic_digits_codegen
    COMMAND
      "${DHAD_TEST_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_dhad_ir_tests.py"
      --driver
      "$<TARGET_FILE:dhad>"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/arabic_digits_codegen.dh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/expectations/arabic_digits_ir_contains.txt")

  add_test(
    NAME array_index_runtime_codegen
    COMMAND
      "${DHAD_TEST_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_dhad_ir_tests.py"
      --driver
      "$<TARGET_FILE:dhad>"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/array_index_runtime.dh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/expectations/array_index_ir_contains.txt")

  add_test(
    NAME array_index_write_codegen
    COMMAND
      "${DHAD_TEST_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_dhad_ir_tests.py"
      --driver
      "$<TARGET_FILE:dhad>"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sources/array_index_write_runtime.dh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/expectations/array_index_write_ir_contains.txt")
endif()
