# NEXS Full Architecture Map

Generated at: 2026-05-03

## ⚠️ Redundancy Report
- `main` duplicated in: runtime/main.c, tools/nexsd/main.c
- `mmu_create_address_space` duplicated in: hal/arm64/mmu.c, hal/amd64/mmu.c
- `mmu_switch_address_space` duplicated in: hal/arm64/mmu.c, hal/amd64/mmu.c
- `mmu_destroy_address_space` duplicated in: hal/arm64/mmu.c, hal/amd64/mmu.c
- `mmu_init` duplicated in: hal/arm64/mmu.c, hal/amd64/mmu.c
- `mmu_map_page` duplicated in: hal/arm64/mmu.c, hal/amd64/mmu.c
- `mmu_unmap_page` duplicated in: hal/arm64/mmu.c, hal/amd64/mmu.c
- `mmu_flush_tlb` duplicated in: hal/arm64/mmu.c, hal/amd64/mmu.c
- `mmu_virt_to_phys` duplicated in: hal/arm64/mmu.c, hal/amd64/mmu.c
- `s_pid_roots` duplicated in: hal/arm64/mmu.c, hal/amd64/mmu.c
- `hal_timer_set_hz` duplicated in: hal/arm64/timer.c, hal/amd64/apic.c
- `hal_timer_init` duplicated in: hal/arm64/timer.c, hal/amd64/apic.c
- `stat` duplicated in: modules/stdlib.nx, kernel/libc_stub.c
- `sys_chdir` duplicated in: modules/stdlib.nx, kernel/syscall.c
- `s_dev` duplicated in: fs/fat.c, modules/journal/journal.c

---

## CORE

### core/dynarray.c
**Functions:** arr_ref, arr_unref, arr_ensure_cap, arr_set, arr_get_at, arr_delete, arr_print, arr_free
**Globals:** g_array_count

### core/utils.c
**Functions:** nexs_trim, nexs_path_join, nexs_path_dirname, nexs_fprintf
**Globals:** g_nexs_debug, g_nexs_lint_mode

### core/value.c
**Functions:** val_nil, val_bool, val_int, val_float, val_str, val_err, val_ref, val_ptr, val_fn_idx, val_is_error, val_is_truthy, val_equal, val_to_int, val_to_float, val_to_str, val_print, val_free, val_clone, val_add, val_div...

---

## HAL

### hal/amd64/acpi.c
**Functions:** acpi_checksum, parse_madt, parse_sdt, acpi_cpu_count, acpi_ioapic_base, acpi_init
**Globals:** s_cpu_count, s_ioapic_base

### hal/amd64/apic.c
**Functions:** lapic_read, lapic_write, ioapic_read, ioapic_write, __attribute__, pic_disable, lapic_calibrate_ticks_per_ms, apic_timer_isr, hal_timer_set_hz, apic_eoi, apic_set_ioapic_base, apic_init, hal_timer_init
**Globals:** s_cached_tpm

### hal/amd64/gdt.c
**Functions:** make_entry, gdt_init, gdt_set_rsp0
**Globals:** s_tss, s_gdtr

### hal/amd64/idt.c
**Functions:** idt_set_handler, idt_init, nexs_isr_register, nexs_isr_dispatch
**Globals:** s_idt, s_idtr, exc_names

### hal/amd64/mmu.c
**Functions:** phys_alloc_page, pml4_walk_readonly, pml4_walk_alloc, mmu_create_address_space, mmu_switch_address_space, mmu_destroy_address_space, mmu_init, mmu_map_page, mmu_unmap_page, mmu_flush_tlb, mmu_virt_to_phys, pf_isr, mm_alloc_page, mm_free_page, mm_map_range
**Globals:** s_phys_bump, s_pid_roots

### hal/amd64/uart.c
**Functions:** outb, outw, inb, amd64_hal_init, amd64_hal_putc, amd64_hal_getc, amd64_hal_memory_map, amd64_hal_irq_disable, amd64_hal_irq_enable, amd64_hal_halt
**Globals:** s_amd64_driver

### hal/arm64/exc_handler.c
**Functions:** print_hex, nexs_exc_handler

### hal/arm64/fdt.c
**Functions:** be32, be64, fdt_get_ram_base, fdt_get_ram_size, fdt_get_uart_base, fdt_get_gic_dist_base, fdt_get_gic_cpu_base, fdt_init
**Globals:** s_ram_base, s_ram_size, s_cmdline, s_uart_base, s_gic_dist, s_gic_cpu

### hal/arm64/gic.c
**Functions:** gic_rd32, gic_wr32, gic_init, gic_enable_irq, gic_disable_irq, gic_set_priority, gic_eoi, gic_register_handler, gic_handle_irq

### hal/arm64/mmu.c
**Functions:** memzero, mmu_init, mmu_create_address_space, mmu_switch_address_space, mmu_destroy_address_space, mmu_map_page, mmu_unmap_page, mmu_flush_tlb, mmu_virt_to_phys
**Globals:** s_pid_roots

### hal/arm64/timer.c
**Functions:** read_cntfrq, read_cntvct, write_cntv_tval, write_cntv_ctl, timer_irq, hal_timer_set_hz, hal_timer_init
**Globals:** s_reload

### hal/arm64/uart.c
**Functions:** arm64_hal_init, arm64_hal_putc, arm64_hal_getc, arm64_hal_memory_map, arm64_hal_irq_disable, arm64_hal_irq_enable, arm64_hal_halt
**Globals:** s_arm64_driver

### hal/common/console.c
**Functions:** nexs_hal_init, nexs_hal_putc, nexs_hal_getc, nexs_hal_print, nexs_hal_memory_map, nexs_hal_irq_disable, nexs_hal_irq_enable, nexs_hal_halt

### hal/common/timer.c
**Functions:** hal_timer_ticks, hal_timer_sleep_ms
**Globals:** g_hal_ticks, g_hal_tick_cb

### hal/hal_hosted.c
**Functions:** hosted_hal_init, hosted_hal_putc, hosted_hal_getc, hosted_hal_memory_map, hosted_hal_irq_disable, hosted_hal_irq_enable, hosted_hal_halt, hosted_register_hal
**Globals:** s_hosted_driver

### hal/module/nexs_hal_module.c
**Functions:** hal_module_register, hal_module_probe_all, hal_module_count, bi_hal_module_list, bi_hal_module_probe, hal_module_register_builtins
**Globals:** s_mods, s_count

---

## KERNEL

### kernel/blk.c
**Functions:** blk_register_dev, flush_buf, blk_init, blk_read, blk_write, blk_sync
**Globals:** s_cache, s_access_clock, s_devs, s_dev_count

### kernel/cap.c
**Functions:** cap_publish, cap_init, cap_insert, cap_remove

### kernel/ipc.c
**Functions:** enqueue_proc, nexs_ipc_send, nexs_ipc_recv, nexs_ipc_signal, nexs_ipc_wait, nexs_ipc_call, nexs_ipc_reply, ipc_endpoint_destroy, ipc_notification_destroy

### kernel/libc_stub.c
**Functions:** print_uint, print_int, vsnprintf, snprintf, sprintf, vfprintf, fprintf, printf, vprintf, fputc, fputs, fflush, fclose, fread, fwrite, fseek, ftell, remove, free, memset...
**Globals:** dummy_stdout, dummy_stderr, dummy_stdin, stdout, stderr, stdin, errno, line_buf, line_buf_pos, line_buf_len

### kernel/libcxx_stub.cpp
**Functions:** new

### kernel/mm/buddy.c
**Functions:** buddy_next_pow2, buddy_alloc_node, buddy_free_node, buddy_free, die, nexs_warn, buddy_get_node_size, xfree, buddy_dump_stats
**Globals:** memory_pool, buddy_tree

### kernel/mm/pmm.c
**Functions:** page_slot_find_free, page_free, is_page_ptr, nexs_free
**Globals:** page_table, large_brk

### kernel/msg.c
**Functions:** kmsg_encode, kmsg_decode, kmsg_dispatch_loop

### kernel/proc.c
**Functions:** proc_destroy, proc_block, proc_unblock_by_msg, proc_yield
**Globals:** s_next_pid

### kernel/sched.c
**Functions:** prio_level, publish_state, sched_init, sched_add, sched_remove, sched_tick, sched_yield, sched_unblock_waiting
**Globals:** s_counts, s_tick, s_total

### kernel/sys_brk.c
**Functions:** nexs_brk

### kernel/syscall.c
**Functions:** caller_has_admin, err_eperm, sys_open, sys_read, sys_write, sys_close, sys_stat, sys_mkdir, sys_mount, sys_fork, sys_exec, sys_getpid, sys_exit, sys_sleep, sys_bind, sys_chdir, sys_create, sys_remove, sys_pipe, sys_dup...
**Globals:** s_table

### kernel/vfs.c
**Functions:** ino_alloc, ino_publish, vfs_init, vfs_fd_alloc, vfs_dup, vfs_seek, path_to_ino, vfs_open, vfs_read, vfs_write, vfs_close, vfs_stat, vfs_mkdir, vfs_mount
**Globals:** s_next_ino, g_fd_table, s_mounts, s_mount_count

### kernel/vfs_server.c
**Functions:** vfs_server_main, vfs_server_init

---

## LANG

### lang/builtins.c
**Functions:** register_builtin_sig, builtin_str, builtin_int_, builtin_float_, builtin_len, builtin_type, builtin_buddy_stats, builtin_errstr, builtin_substr, builtin_contains, builtin_trim, builtin_upper, builtin_lower, builtin_split, builtin_replace, builtin_abs, builtin_min, builtin_max, builtin_lines_of, builtin_keys...

### lang/eval.c
**Functions:** eval_ctx_init, ok, ctrl_break, ctrl_cont, ctrl_ret, err_result, eval, eval_block, eval_node, eval_str, eval_str_ex, eval_file, nexs_check_syntax_file

### lang/fn_table.c
**Functions:** fn_print_hook, fn_table_init, fn_table_free, fn_register, fn_register_builtin, fn_register_builtin_sig, fn_ref, fn_unref
**Globals:** g_fn_table, g_fn_count

### lang/lexer.c
**Functions:** lexer_init, lexer_skip_ws, make_tok, lexer_next, lexer_peek

### lang/parser.c
**Functions:** ast_free, ast_free_safe, ast_print, parser_advance, parser_skip_newlines, parser_expect, binop_prec, parser_init

---

## REGISTRY

### registry/reg_ipc.c
**Functions:** pipe_write_all, pipe_read_all, le_write64, le_read64, le_write32, le_read32, pipe_serialize_value, pipe_deserialize_value, walk_and_enable_pipes, reg_ipc_enable_pipes, reg_ipc_init_queue, reg_ipc_send, reg_ipc_recv, reg_ipc_pending

### registry/registry.c
**Functions:** regkey_add_child, regkey_add_child_tail, regkey_detach_child, regkey_free_recursive, regkey_update_paths, reg_set, reg_get, reg_delete, reg_set_ptr, reg_get_deref, reg_ls_node, reg_ls, reg_ls_recursive, reg_key_print, reg_move, reg_mount, reg_unmount, reg_bind, reg_pop_scope, reg_init...
**Globals:** g_registry, g_scope_counter

---

## RUNTIME

### runtime/main.c
**Functions:** nexs_repl, nexs_main_baremetal, main
**Globals:** g_ast_debug

### runtime/nexs_line.c
**Functions:** nexs_line_raw_on, nexs_line_raw_off, read_byte, read_byte_timeout, utf8_seq_len, utf8_col_width, utf8_decode, visual_cols, utf8_next, utf8_prev, nexs_key_read, nexs_line_init, write_str, write_bytes, cursor_left, cursor_right, nexs_line_render, nexs_line_clear_screen, nexs_line_add_history, history_nav...
**Globals:** s_orig_termios, s_raw_active

### runtime/runtime.c
**Functions:** nexs_print_version, nexs_runtime_init

---

## SYS

### sys/include/nexs_keymap.h
**Functions:** translate_key_layout

### sys/sysio.c
**Functions:** sysio_init, set_errstr, nexs_open, nexs_create, nexs_close, nexs_pread, nexs_pwrite, nexs_seek, nexs_dup, nexs_fd2path, nexs_remove, nexs_pipe, nexs_stat, nexs_chdir, nexs_errstr, nexs_read_byte, bi_open, bi_create, bi_close, bi_read...
**Globals:** g_errstr, s_key_lookahead, s_key_layout

### sys/sysproc.c
**Functions:** nexs_sleep, nexs_exec, nexs_exits, nexs_alarm, nexs_rfork, nexs_await, bi_sleep, bi_exec, bi_exits, bi_alarm, bi_rfork, bi_await, bi_getpid, bi_getwd, bi_ipc_send, bi_ipc_recv, bi_brk, bi_ipc_signal, bi_ipc_wait, bi_ipc_create...

---

## FS

### fs/9p.c
**Functions:** read_u16, read_u32, write_u16, write_u32, p9_server_init, p9_server_handle
**Globals:** s_fids, s_msize

### fs/fat.c
**Functions:** fat_entry, fat_init, handle_alloc, fat_open, fat_read, fat_readdir, fat_close
**Globals:** s_dev, s_fat_start_lba, s_root_start_lba, s_data_start_lba, s_cluster_size, s_root_entries, s_mounted, s_handles

### fs/regfs.c
**Functions:** crc32_byte, save_key, regfs_save, regfs_load, regfs_sync, regfs_watch

---

## COMPILER

### compiler/codegen.c
**Functions:** write_c_string, emit_forward_decls, emit_dep_table, nexs_codegen_ex, nexs_codegen

### compiler/dep_scan.c
**Functions:** dep_already_seen, scan_source, nexs_scan_deps, nexs_free_deps

### compiler/driver.c
**Functions:** nexs_print_baremetal_info, nexs_compile_file_ex, nexs_compile_file

### compiler/targets.h
**Globals:** nexs_targets

---

## LIBRARY

### library/auth/init.nx
**Functions:** auth_set_ring, auth_get_ring, auth_get_uid, auth_cap_grant, auth_cap_revoke, auth_cap_check, auth_require_ring, auth_user_add, auth_get_home

### library/fs/init.nx
**Functions:** fs_mount, fs_umount, fs_sync, fs_stat, vfs_import, vfs_cp, vfs_mv

### library/fs/p9_mnt.nx
**Functions:** p9_mnt_init, p9_file_manager

### library/hw_dt.nx
**Functions:** hw_dt_init, hw_dt_node_set, hw_dt_node_get, hw_dt_dump

### library/logos/init.nx
**Functions:** logo_register

### library/pm/pm.nx
**Functions:** set_fg_pid, _pm_is_system_fn, pm_mangle, pm_alloc_pid, pm_alloc_window, pm_register_proc, pm_unregister_proc, pm_set_focus, pm_get_focus_pid

### library/textbuf.nx
**Functions:** tb_create, tb_get_line, tb_set_line, tb_insert_char, tb_delete_char, tb_split_line, tb_merge_line, tb_load, tb_to_str

### library/tty/init.nx
**Functions:** tty_open, tty_close, tty_write, tty_read, tty_set_raw, tty_resize

### library/ui/img.nx
**Functions:** ui_draw_image, ui_draw_logo

### library/ui/init.nx
**Functions:** ui_init, ui_at, ui_cls, ui_flip

---

## EXAMPLE

### example/ipc_echo.nx
**Functions:** server

### example/minios/logo.nx
**Functions:** load_logos

### example/minios/logos_config.nx
**Functions:** configure_logos, show_main_logo

### example/minios/nxed_editor.nx
**Functions:** ed_clamp_cursor, ed_render_text, ed_prompt, ed_render_menu, ed_handle_edit_key, ed_handle_menu_key, nxed_start, _entry

### example/minios/shell.nx
**Functions:** shell_help, shell_cwd_display, shell_loop

### example/minios/termimg.nx
**Functions:** termimg_usage, termimg_main

### example/minios/wasm_vm.nx
**Functions:** vm_run

---

## OTHER

### experimental/hal_bc/nexs_hal_bc.c
**Functions:** halbc_memcpy, halbc_strlen, halbc_memcmp, halbc_register_devfn, halbc_find_devfn, read_str, read_int, stack_push_int, stack_push_str, stack_pop, halbc_init, hal_dev_write, hal_dev_read, hal_dev_ctl, halbc_step, halbc_run, halbc_encode_str, halbc_encode_int, halbc_asm_init, asm_emit...
**Globals:** g_hal_devfns, g_hal_devfn_count

### include/nexs.h
**Functions:** nexs_eval_cstr, nexs_reg_int

### include/utf8.h
**Functions:** utf8casecmp, utf8cmp, utf8cspn, utf8len, utf8nlen, utf8ncasecmp, utf8ncmp, utf8size, utf8size_lazy, utf8nsize_lazy, utf8spn, utf8makevalid, utf8codepoint, utf8codepointcalcsize, utf8codepointsize, utf8islower, utf8isupper, utf8lwr, utf8upr, utf8lwrcodepoint...
**Globals:** utf8_int8_t

### modules/journal/journal.c
**Functions:** journal_lba_hdr, journal_lba_data, write_record, journal_init, journal_begin, journal_log, journal_commit, journal_replay, journal_checkpoint
**Globals:** s_dev, s_base_lba, s_n_blocks, s_write_pos, s_seq, s_in_txn, s_commit_count

### modules/stdlib.nx
**Functions:** readkey, term_at, term_cls, term_flush, term_cursor_move, term_cursor_show, term_size, input, get_proc_pid, get_proc_uid, get_proc_base, pm_is_fg, posix_read, posix_write, stat, basename, dirname, wc, mkdir, getcwd...

### tools/nexsd/main.c
**Functions:** add_symbol, scan_c_file_deep, scan_project_c, scan_nx_file, scan_project_nx, count_nx_files, handle_client, nexs_runtime_reset_to_baseline, register_fns_in_fn_table, scan_symbols_in_ast, main
**Globals:** c_builtins_count, baseline_fn_count, symbol_table, symbol_count

---

