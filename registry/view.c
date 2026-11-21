/*
 * registry/view.c — display actual NXOLMO registry object metadata.
 * This is a reader of ONE StateStore, not another namespace or VFS.
 * Payloads are accessed only by explicit reg_get(), never by enumeration.
 */
#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_common.h"
#include "../core/include/nexs_utils.h"
#include "../core/include/nexs_value.h"
#include "include/nexs_registry.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The NXOLMO store is the single authority for identity, revision and
 * child relationships. A RegKey carries only the current name projection. */
static int reg_view_object(RegKey *key, FILE *out, int depth) {
  nx_state_node node;
  nx_result result;
  uint32_t children = 0U;
  if (reg_object_read(key->path, &node, &result) != NX_STATUS_OK ||
      nx_state_store_child_count(&g_registry.state, node.ref, &children,
                                 &result) != NX_STATUS_OK) {
    nexs_fprintf(out, "[REGISTRY] unavailable object metadata\n");
    return -1;
  }
  for (int i = 0; i < depth; ++i)
    nexs_fprintf(out, "  ");
  nexs_fprintf(out,
      "%-28s  %-6s  ref=%llu:%llu gen=%llu rev=%llu children=%u",
      key->name, val_type_name(key->val.type),
      (unsigned long long)node.ref.id.high,
      (unsigned long long)node.ref.id.low,
      (unsigned long long)node.ref.generation,
      (unsigned long long)node.revision, (unsigned)children);
  if (key->queue != NULL) {
    if (key->queue->use_pipe != 0)
      nexs_fprintf(out, " queue=pipe");
    else
      nexs_fprintf(out, " queue=%d/%d", key->queue->count,
                   key->queue->max_count);
  }
  nexs_fprintf(out, "\n");
  return 0;
}

/* Iterative heapsort of temporary pointers. Never reorder registry links.
 * O(n log n) comparisons instead of the previous O(n^2) bubble sort. */
static void reg_view_sift(RegKey **items, size_t root, size_t end) {
  while (root < end / 2U) {
    size_t child = root * 2U + 1U;
    if (child >= end)
      return;
    if (child + 1U < end &&
        strcmp(items[child]->name, items[child + 1U]->name) < 0)
      ++child;
    if (strcmp(items[root]->name, items[child]->name) >= 0)
      return;
    RegKey *swap = items[root];
    items[root] = items[child];
    items[child] = swap;
    root = child;
  }
}

static void reg_view_sort(RegKey **items, size_t count) {
  for (size_t i = count / 2U; i > 0U; --i)
    reg_view_sift(items, i - 1U, count);
  for (size_t i = count; i > 1U; --i) {
    RegKey *swap = items[0];
    items[0] = items[i - 1U];
    items[i - 1U] = swap;
    reg_view_sift(items, 0U, i - 1U);
  }
}

static int reg_view_child_count(RegKey *key, size_t *out) {
  size_t count = 0U;
  RegKey *cursor = key->children;
  while (cursor != NULL && count < g_registry.total_keys) {
    ++count;
    cursor = cursor->next;
  }
  if (cursor != NULL || count > SIZE_MAX / sizeof(RegKey *))
    return -1;
  *out = count;
  return 0;
}

void reg_ls(const char *path, FILE *out) {
  RegKey *parent = reg_lookup(path);
  if (parent == NULL) {
    nexs_fprintf(out, "Path '%s' not found\n", path == NULL ? "(null)" : path);
    return;
  }
  nexs_fprintf(out, "\n[REGISTRY] %s\n", path);
  nexs_fprintf(out,
      "OBJECT                        VALUE_KIND  NXOLMO ID / GENERATION / REVISION / RELATIONS\n");
  nexs_fprintf(out,
      "---------------------------------------------------\n");
  size_t count = 0U;
  if (reg_view_child_count(parent, &count) != 0) {
    nexs_fprintf(out, "[REGISTRY] invalid child traversal\n");
    return;
  }
  if (count == 0U) {
    nexs_fprintf(out, "\n");
    return;
  }
  const size_t bytes = count * sizeof(RegKey *);
  RegKey **items = nexs_alloc(bytes);
  if (items == NULL) {
    nexs_fprintf(out, "[REGISTRY] listing unavailable: provider exhausted\n");
    return;
  }
  RegKey *cursor = parent->children;
  for (size_t i = 0U; i < count; ++i) {
    items[i] = cursor;
    cursor = cursor->next;
  }
  reg_view_sort(items, count);
  for (size_t i = 0U; i < count; ++i) {
    if (reg_view_object(items[i], out, 0) != 0) {
      nexs_fprintf(out, "[REGISTRY] listing interrupted\n");
      nexs_free(items, bytes);
      return;
    }
  }
  nexs_free(items, bytes);
  nexs_fprintf(out, "\n");
}

void reg_ls_recursive(const char *path, FILE *out, int depth) {
  RegKey *root = reg_lookup(path);
  if (root == NULL || depth < 0)
    return;
  RegKey *node = root;
  size_t emitted = 0U;
  int level = depth;
  while (node != NULL && emitted < g_registry.total_keys) {
    if (reg_view_object(node, out, level) != 0)
      return;
    ++emitted;
    if (node->children != NULL) {
      if (level == INT_MAX) {
        nexs_fprintf(out, "[REGISTRY] depth overflow\n");
        return;
      }
      node = node->children;
      ++level;
    } else {
      while (node != root && node->next == NULL) {
        node = node->parent;
        --level;
      }
      node = node == root ? NULL : node->next;
    }
  }
  if (node != NULL)
    nexs_fprintf(out, "[REGISTRY] invalid recursive traversal\n");
}

void reg_key_print(RegKey *key, FILE *out) {
  if (key != NULL)
    (void)reg_view_object(key, out, 0);
}
