/*
 * kernel/include/nexs_journal.h — Write-Ahead Log (WAL)
 */
#ifndef NEXS_JOURNAL_H
#define NEXS_JOURNAL_H
#pragma once
#include <stdint.h>

int  journal_init(uint32_t dev, uint64_t journal_lba, uint32_t n_blocks);
int  journal_begin(void);
int  journal_log(uint32_t dev, uint64_t lba);
int  journal_commit(void);
int  journal_replay(void);
void journal_checkpoint(void);

#endif
