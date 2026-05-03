CC = gcc
CFLAGS = -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter \
         -DPOOL_16MB -DHOST_OS_MACOS \
         -I../../core/include -I../../registry/include -I../../lang/include -I../../sys/include

# Path to original project objects
BASE_DIR = ../..
CORE_OBJS = $(BASE_DIR)/core/buddy.o $(BASE_DIR)/core/pager.o $(BASE_DIR)/core/value.o $(BASE_DIR)/core/utils.o $(BASE_DIR)/core/dynarray.o
LANG_OBJS = $(BASE_DIR)/lang/lexer.o $(BASE_DIR)/lang/parser.o $(BASE_DIR)/lang/fn_table.o $(BASE_DIR)/lang/eval.o $(BASE_DIR)/lang/builtins.o
REG_OBJS  = $(BASE_DIR)/registry/registry.o $(BASE_DIR)/registry/reg_ipc.o
SYS_OBJS  = $(BASE_DIR)/sys/sysio.o $(BASE_DIR)/sys/sysproc.o $(BASE_DIR)/kernel/vfs.o
HAL_OBJS  = $(BASE_DIR)/hal/module/nexs_hal_module.o $(BASE_DIR)/hal/hal_hosted.o $(BASE_DIR)/hal/bc/nexs_hal_bc.o $(BASE_DIR)/hal/common/console.o $(BASE_DIR)/hal/common/timer.o
RUNTIME_OBJS = $(BASE_DIR)/runtime/runtime.o $(BASE_DIR)/runtime/nexs_line.o
COMPILER_OBJS = $(BASE_DIR)/compiler/dep_scan.o

OBJS = main.o $(CORE_OBJS) $(LANG_OBJS) $(REG_OBJS) $(SYS_OBJS) $(HAL_OBJS) $(RUNTIME_OBJS) $(COMPILER_OBJS)

all: nexsd

nexsd: $(OBJS)
	$(CC) $(CFLAGS) -o nexsd $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f nexsd main.o
