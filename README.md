# NEXS

NEXS is a dynamically-typed, Plan 9-inspired scripting language with a tree-walking interpreter, hierarchical registry, IPC message queues, and a cross-compilation pipeline.

## Building

```sh
make          # host build (macOS Intel: clang -arch x86_64)
./nexs        # interactive REPL
./nexs file.nx
```

Cross-compile targets:

```sh
make linux-amd64     # GCC x86_64 Linux ELF
make linux-arm64     # aarch64-linux-gnu-gcc
make macos-arm64     # clang -arch arm64 (Apple Silicon)
make macos-amd64 
make plan9-amd64     # Plan 9 style (GCC)
make baremetal-arm64 # aarch64-none-elf-gcc, QEMU virt
make baremetal-amd64 # x86_64-elf-gcc, Multiboot2
```

Compile a `.nx` file to a native binary:

```sh
./nexs --compile program.nx --target linux-amd64 -o out/program
./nexs --compile program.nx --target baremetal-arm64 -o out/kernel.elf
```

## Language Reference

### Variables

```nexs
x = 42
y = 3.14
s = "hello"
```

### Arrays

```nexs
arr[0] = 10
arr[1] = 20
out arr[0]
del arr[1]
```

### Output

```nexs
out x
out "hello world"
out arr[0]
```

### Arithmetic

```nexs
z = x + y
z = x * 2 - 1
z = x / y
z = x % 3
```

### Comparison and Logic

```nexs
if x > 0 { out "positive" }
if x == 0 { out "zero" }
if x != y { out "different" }
if x > 0 && y > 0 { out "both" }
if x == 0 || y == 0 { out "one zero" }
```

### Conditionals

```nexs
if x > 10 {
  out "big"
} else {
  out "small"
}
```

### Loops

```nexs
i = 0
loop {
  if i >= 5 { break }
  out i
  i = i + 1
}
```

### Functions

```nexs
fn add(a b) {
  ret a + b
}

out add(3 4)
```

Functions can be recursive:

```nexs
fn fact(n) {
  if n <= 1 { ret 1 }
  ret n * fact(n - 1)
}

out fact(10)
```

### Strings

```nexs
s = "hello"
t = s + " world"
out t
out len(s)
out substr(s 1 3)
```

---

## Registry

The registry is a hierarchical key-value store with Plan 9-style paths.

### Write and Read

```nexs
reg /env/x = 99
out reg /env/x
```

### List

```nexs
ls /env
```

Recursive listing (REPL):

```
:reg /env
```

### Delete

```nexs
del /env/x
```

### Registry vs Variables

Variables (`x = 42`) are local to the current eval context. Registry keys (`reg /env/x = 42`) persist for the lifetime of the runtime and are accessible from any context, including spawned processes.

---

## Registry Pointers

A registry pointer stores a path string. When you read through a pointer, NEXS follows the chain automatically.

### Create a pointer

```nexs
# ptr /alias = /target
ptr /alias = /env/x
```

`/alias` now points to `/env/x`.

### Read through a pointer

```nexs
reg /env/x = 42
ptr /alias = /env/x
out deref /alias
```

Output: `42`

### Pointer chains

Pointers can chain:

```nexs
reg /env/x = 100
ptr /a = /env/x
ptr /b = /a
ptr /c = /b
out deref /c
```

Output: `100`

NEXS follows up to 32 hops and detects cycles.

### Inspect a pointer chain (REPL)

```
:ptr /c
```

Output:

```
[PTR CHAIN] /c
  /c [ptr] -> /b
  /b [ptr] -> /a
  /a [ptr] -> /env/x
  /env/x [int] = 100
```

### Update through an alias

A pointer does **not** automatically write through. To update the target, write directly:

```nexs
reg /env/x = 200
out deref /alias
```

Output: `200` (pointer still points to `/env/x`, value updated)

### Use case: configuration aliasing

```nexs
reg /config/db/host = "localhost"
ptr /db/host = /config/db/host

# Application reads from /db/host
out deref /db/host

# Ops changes the backing value
reg /config/db/host = "db.prod.internal"
out deref /db/host
```

---

## IPC Message Queues

Every registry key can have an attached FIFO message queue. Queues are created automatically on first send.

### Send a message

```nexs
sendmessage /my/queue 42
sendmessage /my/queue "hello"
```

### Receive a message

```nexs
v = receivemessage /my/queue
out v
```

`receivemessage` returns `nil` if the queue is empty (non-blocking).

### Check pending count

```nexs
n = msgpending /my/queue
out n
```

### Simple producer / consumer

```nexs
# Producer
sendmessage /jobs 1
sendmessage /jobs 2
sendmessage /jobs 3

# Consumer
loop {
  n = msgpending /jobs
  if n == 0 { break }
  v = receivemessage /jobs
  out v
}
```

Output:

```
1
2
3
```

### Queue status (REPL)

```
:ipc /my/queue
```

Output:

```
[IPC] /my/queue: count=3 max=64
```

---

## Combining Pointers and IPC

Because pointers resolve to registry keys, you can send to a key through a pointer path.

```nexs
ptr /inbox = /proc/worker/inbox
sendmessage /proc/worker/inbox "start"
v = receivemessage /proc/worker/inbox
out v
```

### Semaphore pattern

```nexs
# Initialize semaphore with N tokens
sendmessage /sem/lock 1
sendmessage /sem/lock 1
sendmessage /sem/lock 1

fn acquire() {
  loop {
    n = msgpending /sem/lock
    if n > 0 {
      receivemessage /sem/lock
      ret 1
    }
  }
}

fn release() {
  sendmessage /sem/lock 1
}

acquire()
out "critical section"
release()
```

### Request-reply (seL4-style)

```nexs
# "Worker" writes result to its own outbox
fn worker_handle(req) {
  result = req * 2
  sendmessage /proc/worker/outbox result
}

# Caller sends request and waits for reply
sendmessage /proc/worker/inbox 21
req = receivemessage /proc/worker/inbox
worker_handle(req)
reply = receivemessage /proc/worker/outbox
out reply
```

Output: `42`

### Pub/sub fan-out

```nexs
fn publish(topic val) {
  sendmessage topic val
}

publish("/events/click" 1)
publish("/events/click" 2)
publish("/events/click" 3)

fn consume(topic) {
  loop {
    n = msgpending topic
    if n == 0 { break }
    v = receivemessage topic
    out v
  }
}

consume("/events/click")
```

---

## Plan 9 Syscalls

```nexs
fd = open("file.txt" 0)    # 0=read 1=write 2=rdwr
s  = read(fd 256)
write(fd "data")
close(fd)
pid = rfork(1)
sleep(100)
```

---

## REPL Commands

| Command | Description |
|---------|-------------|
| `:exit` / `:q` | Quit |
| `:help` | Show syntax reference |
| `:version` | Print version |
| `:debug` | Toggle debug trace |
| `:ast` | Toggle AST dump |
| `:fn` | List all registered functions |
| `:ls [path]` | List registry at path (default `/`) |
| `:reg [path]` | Recursive registry dump |
| `:ptr /path` | Follow and print pointer chain |
| `:ipc /path` | Show IPC queue status |

---

## Examples

Run bundled examples:

```sh
./nexs examples/hello.nx
./nexs examples/fib.nx
./nexs examples/registry.nx
```

---

## References

- utf8.h: https://github.com/sheredom/utf8.h
