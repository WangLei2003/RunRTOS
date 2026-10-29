# SPDX-License-Identifier: Apache-2.0
# RunRTOS - configure-time conformance checks
#
# Layer: build
# Purpose: Reject missing file contracts, banned headers, relative public
#          includes, and upward dependencies.
# Constraint: The scanner covers include/runrtos and src; exceptions require a
#             separate reviewed standards change rather than weakening this script.

set(runProjectRoot "${CMAKE_CURRENT_LIST_DIR}/..")
set(runLayerOrder core runtime kernel arch drivers bsp components applications)
set(runBannedHeaders
    stdint.h stddef.h stdbool.h stdio.h stdlib.h string.h assert.h
    inttypes.h limits.h stdarg.h stdatomic.h setjmp.h signal.h
    time.h math.h ctype.h errno.h
)

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

