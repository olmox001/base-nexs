/*
 * hal/sel4/sel4_ipc_bridge.c — Inter-PD IPC Bridge Implementation
 * ================================================================
 * Serialises/deserialises NEXS Values into shared Memory Regions
 * using the same wire format as reg_ipc.c (LE binary, all types).
 *
 * ONLY compiled when NEXS_SEL4 is defined.
 * Hosted and baremetal builds never see this file.
 */

#ifdef NEXS_SEL4

#include "sel4_ipc_bridge.h"
#include "../../core/include/nexs_alloc.h"
#include "../../core/include/nexs_value.h"
#include <stdint.h>
#include <string.h>

/* =========================================================
   INTERNAL: LE helpers (identical to reg_ipc.c, inlined here
   to avoid a cross-module dependency in freestanding builds)
   ========================================================= */

static void le_w64(uint8_t *d, uint64_t v) {
    for (int i = 0; i < 8; i++) { d[i] = (uint8_t)(v & 0xFF); v >>= 8; }
}
static uint64_t le_r64(const uint8_t *s) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) { v <<= 8; v |= s[i]; }
    return v;
}
static void le_w32(uint8_t *d, uint32_t v) {
    for (int i = 0; i < 4; i++) { d[i] = (uint8_t)(v & 0xFF); v >>= 8; }
}
static uint32_t le_r32(const uint8_t *s) {
    uint32_t v = 0;
    for (int i = 3; i >= 0; i--) { v <<= 8; v |= s[i]; }
    return v;
}

/* =========================================================
   SERIALISER — writes a Value into buf[0..cap-1]
   Returns bytes written, or -1 if buf is too small.
   Wire format:
     [1]  type byte
     [8]  ival  (LE int64)
     [8]  fval  (raw bytes of double)
     [4]  str_len (LE uint32; 0 if no string)
     [*]  str_data (str_len bytes, no NUL in wire)
     [4]  arr_len (LE uint32; 0 if not TYPE_ARR)
     [*]  arr_len × recursive serialised elements
   ========================================================= */

static int ser_value(const Value *v, uint8_t *buf, int cap) {
    int pos = 0;

#define NEED(n) do { if (pos + (n) > cap) return -1; } while(0)
#define W1(b)   do { NEED(1); buf[pos++] = (uint8_t)(b); } while(0)

    /* type */
    W1(v->type);

    /* ival */
    NEED(8); le_w64(buf + pos, (uint64_t)v->ival); pos += 8;

    /* fval */
    NEED(8); memcpy(buf + pos, &v->fval, 8); pos += 8;

    /* str_len + str_data */
    const char *str_data = NULL;
    uint32_t    str_len  = 0;
    switch (v->type) {
    case TYPE_STR:
    case TYPE_REF:
    case TYPE_PTR:
        str_data = v->data ? (const char *)v->data : "";
        str_len  = (uint32_t)strlen(str_data);
        break;
    case TYPE_ERR:
        str_data = v->err_msg ? v->err_msg : "";
        str_len  = (uint32_t)strlen(str_data);
        break;
    default: break;
    }
    NEED(4); le_w32(buf + pos, str_len); pos += 4;
    if (str_len) { NEED((int)str_len); memcpy(buf + pos, str_data, str_len); pos += (int)str_len; }

    /* arr_len + elements */
    uint32_t arr_len = 0;
    DynArray *arr = NULL;
    if (v->type == TYPE_ARR && v->data) {
        arr     = (DynArray *)v->data;
        arr_len = (uint32_t)arr->size;
    }
    NEED(4); le_w32(buf + pos, arr_len); pos += 4;
    for (uint32_t i = 0; i < arr_len; i++) {
        Value elem = arr_get_at(arr, (size_t)i);
        int n = ser_value(&elem, buf + pos, cap - pos);
        val_free(&elem);
        if (n < 0) return -1;
        pos += n;
    }

#undef NEED
#undef W1
    return pos;
}

/* =========================================================
   DESERIALISER — reads one Value from buf[0..len-1]
   *consumed is set to bytes read on success.
   Returns val_err on truncation / unknown type.
   ========================================================= */

static Value deser_value(const uint8_t *buf, int len, int *consumed) {
    int pos = 0;

#define NEED(n) do { if (pos + (n) > len) return val_err(99, "bridge: truncated"); } while(0)

    NEED(1); uint8_t type_byte = buf[pos++];

    NEED(8); int64_t ival = (int64_t)le_r64(buf + pos); pos += 8;
    NEED(8); double  fval = 0.0; memcpy(&fval, buf + pos, 8); pos += 8;

    NEED(4); uint32_t str_len = le_r32(buf + pos); pos += 4;
    char *str_buf = NULL;
    if (str_len > 0) {
        NEED((int)str_len);
        str_buf = (char *)xmalloc(str_len + 1);
        memcpy(str_buf, buf + pos, str_len);
        str_buf[str_len] = '\0';
        pos += (int)str_len;
    }

    NEED(4); uint32_t arr_len = le_r32(buf + pos); pos += 4;

    Value result;
    switch ((ValueType)type_byte) {
    case TYPE_NIL:   result = val_nil();                                  break;
    case TYPE_INT:   result = val_int(ival);                              break;
    case TYPE_FLOAT: result = val_float(fval);                            break;
    case TYPE_BOOL:  result = val_bool((int)ival);                        break;
    case TYPE_FN:    result = val_fn_idx(ival);                           break;
    case TYPE_STR:   result = val_str(str_buf ? str_buf : "");            break;
    case TYPE_REF:   result = val_ref(str_buf ? str_buf : "/");           break;
    case TYPE_PTR:   result = val_ptr(str_buf ? str_buf : "/");           break;
    case TYPE_ERR:   result = val_err((int)ival, str_buf ? str_buf : ""); break;
    case TYPE_ARR: {
        DynArray *a = arr_create_anon();
        for (uint32_t i = 0; i < arr_len; i++) {
            int sub = 0;
            Value elem = deser_value(buf + pos, len - pos, &sub);
            arr_set(a, (size_t)i, elem);
            val_free(&elem);
            pos += sub;
        }
        result.type     = TYPE_ARR;
        result.data     = a;
        result.ival     = 0;
        result.fval     = 0.0;
        result.err_code = 0;
        result.err_msg  = NULL;
        break;
    }
    default:
        if (str_buf) xfree(str_buf);
        *consumed = pos;
        return val_err(99, "bridge: unknown type");
    }

    if (str_buf) xfree(str_buf);
    *consumed = pos;

#undef NEED
    return result;
}

/* =========================================================
   PUBLIC API — sel4_bridge_write
   ========================================================= */

int sel4_bridge_write(NexsMR *mr, const Value *val) {
    if (!mr || !val) return -1;

    int n = ser_value(val, mr->payload, (int)SEL4_MR_PAYLOAD);
    if (n < 0) return -1;   /* value too large for MR */

    mr->seq   += 1;
    mr->len    = (uint32_t)n;
    mr->magic  = SEL4_MR_MAGIC;
    return 0;
}

/* =========================================================
   PUBLIC API — sel4_bridge_read
   ========================================================= */

int sel4_bridge_read(NexsMR *mr, Value *out) {
    if (!mr || !out) return -1;
    if (mr->magic != SEL4_MR_MAGIC || mr->len == 0) return -1;

    int consumed = 0;
    *out = deser_value(mr->payload, (int)mr->len, &consumed);

    /* Recycle the slot so sender can tell the message was consumed */
    mr->len   = 0;
    mr->magic = 0;
    return 0;
}

/* =========================================================
   PUBLIC API — sel4_log_write / sel4_log_read
   ========================================================= */

void sel4_log_write(NexsLogMR *lmr, const char *s) {
    if (!lmr || !s) return;
    uint32_t n = 0;
    while (s[n] && n < (uint32_t)(SEL4_LOG_TEXT_MAX - 1)) n++;
    memcpy(lmr->text, s, n);
    lmr->text[n] = '\0';
    lmr->len   = n;
    lmr->magic = SEL4_MR_MAGIC;
}

int sel4_log_read(NexsLogMR *lmr, char *buf, uint32_t n) {
    if (!lmr || !buf || lmr->magic != SEL4_MR_MAGIC || lmr->len == 0)
        return -1;
    uint32_t copy = lmr->len < n - 1 ? lmr->len : n - 1;
    memcpy(buf, lmr->text, copy);
    buf[copy] = '\0';
    lmr->len   = 0;
    lmr->magic = 0;
    return 0;
}

#endif /* NEXS_SEL4 */
