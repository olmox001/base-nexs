#include <stddef.h>

extern "C" {
    void* xmalloc(size_t);
    void xfree(void*);
    void __gxx_personality_v0() { while(1); }
}

/* Basic C++ runtime stubs for baremetal */

void* operator new(size_t size) {
    return xmalloc(size);
}

void* operator new[](size_t size) {
    return xmalloc(size);
}

void operator delete(void* p) noexcept {
    if (p) xfree(p);
}

void operator delete[](void* p) noexcept {
    if (p) xfree(p);
}

/* Sized delete (C++14) */
void operator delete(void* p, size_t sz) noexcept {
    (void)sz;
    if (p) xfree(p);
}

void operator delete[](void* p, size_t sz) noexcept {
    (void)sz;
    if (p) xfree(p);
}

extern "C" void __cxa_pure_virtual() {
    /* Handle pure virtual call error */
    while (1);
}
