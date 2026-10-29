# SPDX-License-Identifier: Apache-2.0
# RunRTOS - configure-time conformance checks
#
# Layer: build
# Purpose: Reject missing file/function contracts, banned headers, relative
#          public includes, and upward dependencies.
# Constraint: The scanner covers include/runrtos, src, and host tests; exceptions
#             require a reviewed standards change rather than weakening this script.

set(runProjectRoot "${CMAKE_CURRENT_LIST_DIR}/..")
set(runLayerOrder core runtime kernel arch drivers bsp components applications)
set(runBannedHeaders
    stdint.h stddef.h stdbool.h stdio.h stdlib.h string.h assert.h
    inttypes.h limits.h stdarg.h stdatomic.h setjmp.h signal.h
    time.h math.h ctype.h errno.h
)
set(runCModifiers
    "((RUN_STATIC|RUN_INLINE|RUN_FORCE_INLINE|RUN_NOINLINE|RUN_CONST|RUN_ATTR_[A-Z0-9_]+|WINAPI)[ \t\r\n]+)*")
set(runCReturnType
    "(RUN_[A-Z0-9_]+|Run[A-Za-z0-9_]+|unsigned|int|LPVOID|DWORD)")
set(runCFunctionStart
    "[ \t]*${runCModifiers}${runCReturnType}[ \t\r\n]*\\*?[ \t\r\n]+[A-Za-z_][A-Za-z0-9_]*[ \t\r\n]*\\([^;{}]*\\)[ \t\r\n]*\\{")
set(runCComment
    "(^|\n)[ \t]*(//[^\n]*|/\\*[^*\n]*\\*/)[ \t\r\n]+")

file(GLOB_RECURSE runCImplementationFiles
    "${runProjectRoot}/include/runrtos/*.h"
    "${runProjectRoot}/src/*.c"
    "${runProjectRoot}/tests/host/*.c"
)
foreach(runFile IN LISTS runCImplementationFiles)
    file(READ "${runFile}" runContents)
    string(REGEX REPLACE "${runCComment}${runCFunctionStart}" ""
           runRemaining "${runContents}")
    if(runRemaining MATCHES "${runCFunctionStart}")
        message(FATAL_ERROR
            "RunRTOS function comment violation: ${runFile} has a function "
            "definition without an adjacent single-line purpose comment")
    endif()
endforeach()

file(GLOB_RECURSE runAssemblyFiles "${runProjectRoot}/src/*.S")
foreach(runFile IN LISTS runAssemblyFiles)
    file(READ "${runFile}" runContents)
    string(REGEX REPLACE
           "(^|\n)[ \t]*@[^\n]*\n[ \t]*[A-Za-z_][A-Za-z0-9_]*:[ \t]*"
           "" runRemaining "${runContents}")
    if(runRemaining MATCHES
       "(^|\n)[ \t]*[A-Za-z_][A-Za-z0-9_]*:[ \t]*")
        message(FATAL_ERROR
            "RunRTOS function comment violation: ${runFile} has an assembly "
            "entry label without an adjacent @ purpose comment")
    endif()
endforeach()

file(GLOB_RECURSE runImplementationFiles
    "${runProjectRoot}/include/runrtos/*.h"
    "${runProjectRoot}/include/runrtos/*.h.in"
    "${runProjectRoot}/src/*.c"
    "${runProjectRoot}/src/*.S"
)

foreach(runFile IN LISTS runImplementationFiles)
    file(READ "${runFile}" runContents)
    file(STRINGS "${runFile}" runFileHeader LIMIT_COUNT 12)
    string(JOIN "\n" runFileHeaderText ${runFileHeader})
    if(NOT runFileHeaderText MATCHES "SPDX-License-Identifier: Apache-2\\.0")
        message(FATAL_ERROR
            "RunRTOS header violation: ${runFile} lacks the Apache-2.0 SPDX tag")
    endif()
    if(NOT runFileHeaderText MATCHES "(Purpose|职责):")
        message(FATAL_ERROR
            "RunRTOS header violation: ${runFile} lacks a Purpose/职责 field")
    endif()
    if(NOT runFileHeaderText MATCHES "(Constraint|约束):")
        message(FATAL_ERROR
            "RunRTOS header violation: ${runFile} lacks a Constraint/约束 field")
    endif()
    foreach(runHeader IN LISTS runBannedHeaders)
        if(runContents MATCHES "#[ \t]*include[ \t]*[<\\\"]${runHeader}[>\\\"]")
            message(FATAL_ERROR
                "RunRTOS standard violation: ${runFile} includes banned ${runHeader}")
        endif()
    endforeach()
endforeach()

file(GLOB_RECURSE runPublicHeaders "${runProjectRoot}/include/runrtos/*.h")
foreach(runFile IN LISTS runPublicHeaders)
    file(STRINGS "${runFile}" runLines)
    foreach(runLine IN LISTS runLines)
        if(runLine MATCHES "^[ \t]*#[ \t]*include[ \t]*\\\"([^\\\"]+)\\\"")
            set(runInclude "${CMAKE_MATCH_1}")
            if(NOT runInclude MATCHES "^runrtos/")
                message(FATAL_ERROR
                    "RunRTOS public include violation: ${runFile} uses relative "
                    "include ${runInclude}; use a runrtos/... path")
            endif()
        endif()
    endforeach()
endforeach()

file(GLOB_RECURSE runSourceFiles
    "${runProjectRoot}/src/*.c"
    "${runProjectRoot}/src/*.S"
)
foreach(runFile IN LISTS runSourceFiles)
    file(RELATIVE_PATH runRelative "${runProjectRoot}/src" "${runFile}")
    string(REGEX REPLACE "[/\\\\].*$" "" runSourceLayer "${runRelative}")
    set(runSourceLayerIndex -1)
    set(runIndex 0)
    foreach(runLayer IN LISTS runLayerOrder)
        if(runSourceLayer STREQUAL runLayer)
            set(runSourceLayerIndex ${runIndex})
        endif()
        math(EXPR runIndex "${runIndex} + 1")
    endforeach()
    if(runSourceLayerIndex LESS 0)
        message(FATAL_ERROR
            "RunRTOS layering violation: ${runFile} is outside a defined layer")
    endif()

    file(STRINGS "${runFile}" runLines)
    foreach(runLine IN LISTS runLines)
        if(runLine MATCHES "^[ \t]*#[ \t]*include[ \t]*\\\"runrtos/([^/\\\"]+)/")
            set(runTargetLayer "${CMAKE_MATCH_1}")
            set(runTargetLayerIndex -1)
            set(runIndex 0)
            foreach(runLayer IN LISTS runLayerOrder)
                if(runTargetLayer STREQUAL runLayer)
                    set(runTargetLayerIndex ${runIndex})
                endif()
                math(EXPR runIndex "${runIndex} + 1")
            endforeach()
            if((runTargetLayerIndex LESS 0) OR
               (runTargetLayerIndex GREATER runSourceLayerIndex))
                message(FATAL_ERROR
                    "RunRTOS layering violation: ${runFile} includes a higher "
                    "layer header from ${runTargetLayer}")
            endif()
        endif()
    endforeach()
endforeach()

