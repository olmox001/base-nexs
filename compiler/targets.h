/*
 * compiler/targets.h — NEXS Compiler Target Configurations
 * ===========================================================
 * Internal header used by codegen.c and driver.c.
 */

#ifndef NEXS_TARGETS_H
#define NEXS_TARGETS_H
#pragma once

#include "include/nexs_compiler.h"

typedef struct {
  const char *name;
  const char *gcc_binary;
  const char *arch_flags;
  const char *os_flags;
  int         is_baremetal;
  const char *ld_script;   /* NULL for hosted targets */
} TargetConfig;

static const TargetConfig nexs_targets[TARGET_COUNT] = {
  [TARGET_LINUX_AMD64]     = {
    "linux-amd64",
    "gcc",
    "-march=x86-64",
    "-DNEXS_LINUX",
    0, NULL
  },
  [TARGET_LINUX_ARM64]     = {
    "linux-arm64",
    "aarch64-linux-gnu-gcc",
    "-march=armv8-a",
    "-DNEXS_LINUX",
    0, NULL
  },
  [TARGET_MACOS_AMD64]     = {
    "macos-amd64",
    "clang",
    "-arch x86_64",
    "-DNEXS_MACOS",
    0, NULL
  },
  [TARGET_MACOS_ARM64]     = {
    "macos-arm64",
    "clang",
    "-arch arm64",
    "-DNEXS_MACOS",
    0, NULL
  },
  [TARGET_PLAN9_AMD64]     = {
    "plan9-amd64",
    "gcc",
    "-march=x86-64",
    "-DNEXS_PLAN9",
    0, NULL
  },
  [TARGET_BAREMETAL_ARM64] = {
    "baremetal-arm64",
    "aarch64-none-elf-gcc",
    "-march=armv8-a",
    "-DNEXS_BAREMETAL",
    1, "hal/arm64/nexs.ld"
  },
  [TARGET_BAREMETAL_AMD64] = {
    "baremetal-amd64",
    "x86_64-elf-gcc",
    "-march=x86-64",
    "-DNEXS_BAREMETAL",
    1, "hal/amd64/nexs.ld"
  },
};

#endif /* NEXS_TARGETS_H */
