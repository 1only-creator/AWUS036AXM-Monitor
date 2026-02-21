/**
 * compat.h  –  "Fake kernel" shim for userspace MT7921 driver
 *
 * Provides userspace replacements for Linux kernel types/macros.
 *
 * Key decision: we do NOT define __le16/__le32/__le64/__be16/__be32 here.
 * Those annotated types live in linux/types.h which the NDK sysroot may
 * pull in via libusb headers, and redefining them causes a typedef clash
 * because on aarch64 uint64_t=unsigned long while __u64=unsigned long long.
 * Our shim code uses plain u8/u16/u32/u64 and the cpu_to_le*() macros –
 * the annotated types are only needed by the kernel sparse checker.
 */
#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <android/log.h>

/* ── basic integer types ──────────────────────────────────────────── */
typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef int8_t    s8;
typedef int16_t   s16;
typedef int32_t   s32;
typedef int64_t   s64;

/* ── endian helpers (ARM is little-endian, so le ops are no-ops) ──── */
#define cpu_to_le16(x) ((u16)(x))
#define cpu_to_le32(x) ((u32)(x))
#define cpu_to_le64(x) ((u64)(x))
#define le16_to_cpu(x) ((u16)(x))
#define le32_to_cpu(x) ((u32)(x))
#define le64_to_cpu(x) ((u64)(x))
#define cpu_to_be16(x) ((u16)__builtin_bswap16(x))
#define cpu_to_be32(x) ((u32)__builtin_bswap32(x))
#define be16_to_cpu(x) ((u16)__builtin_bswap16(x))
#define be32_to_cpu(x) ((u32)__builtin_bswap32(x))

/* ── logging ──────────────────────────────────────────────────────── */
#define LOG_TAG "MT7921"
#define dev_info(dev, fmt, ...)  __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, fmt, ##__VA_ARGS__)
#define dev_err(dev,  fmt, ...)  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, fmt, ##__VA_ARGS__)
#define dev_warn(dev, fmt, ...)  __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, fmt, ##__VA_ARGS__)
#define dev_dbg(dev,  fmt, ...)  __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, fmt, ##__VA_ARGS__)
#define pr_info(fmt,  ...)       __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, fmt, ##__VA_ARGS__)
#define pr_err(fmt,   ...)       __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, fmt, ##__VA_ARGS__)
#define printk(fmt,   ...)       __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, fmt, ##__VA_ARGS__)
#define WARN_ON(c)               ((void)(c))
#define BUG_ON(c)                do { if (c) abort(); } while (0)

/* ── memory ───────────────────────────────────────────────────────── */
#define GFP_KERNEL   0
#define GFP_ATOMIC   0
#define kmalloc(s,f) malloc(s)
#define kzalloc(s,f) calloc(1, (s))
#define kfree(p)     free(p)
#define vmalloc(s)   malloc(s)
#define vfree(p)     free(p)
#define krealloc(p,s,f) realloc((p),(s))

/* ── bit operations ───────────────────────────────────────────────── */
#define BIT(n)            (1UL << (n))
#define GENMASK(h,l)      (((~0UL) << (l)) & (~0UL >> (sizeof(unsigned long)*8 - 1 - (h))))
#define FIELD_PREP(mask, val) (((u32)(val) << __builtin_ctz(mask)) & (u32)(mask))
#define FIELD_GET(mask, val)  (((u32)(val) & (u32)(mask)) >> __builtin_ctz(mask))
#define test_bit(n,p)     ((*((unsigned long*)(p)) >> (n)) & 1)
#define set_bit(n,p)      (*((unsigned long*)(p)) |= BIT(n))
#define clear_bit(n,p)    (*((unsigned long*)(p)) &= ~BIT(n))

/* ── minimal sk_buff ──────────────────────────────────────────────── */
struct sk_buff {
    u8  *data;
    u32  len;
};
static inline struct sk_buff *alloc_skb(u32 size, int gfp) {
    (void)gfp;
    struct sk_buff *skb = (struct sk_buff *)calloc(1, sizeof(*skb));
    if (!skb) return NULL;
    skb->data = (u8 *)malloc(size);
    if (!skb->data) { free(skb); return NULL; }
    skb->len = size;
    return skb;
}
static inline void dev_kfree_skb(struct sk_buff *skb) {
    if (skb) { free(skb->data); free(skb); }
}
#define dev_kfree_skb_any(s) dev_kfree_skb(s)

/* ── mutex / spinlock stubs (single-threaded shim) ───────────────── */
typedef struct { int _d; } spinlock_t;
typedef struct { int _d; } my_mutex_t;
#define mutex_init(m)               ((void)0)
#define mutex_lock(m)               ((void)0)
#define mutex_unlock(m)             ((void)0)
#define mutex_trylock(m)            1
#define spin_lock_init(l)           ((void)0)
#define spin_lock(l)                ((void)0)
#define spin_unlock(l)              ((void)0)
#define spin_lock_irqsave(l,f)      ((void)0)
#define spin_unlock_irqrestore(l,f) ((void)0)

/* ── misc helpers ─────────────────────────────────────────────────── */
#define ARRAY_SIZE(a)      (sizeof(a)/sizeof((a)[0]))
#define min_t(t,a,b)       ((t)(a) < (t)(b) ? (t)(a) : (t)(b))
#define max_t(t,a,b)       ((t)(a) > (t)(b) ? (t)(a) : (t)(b))
#define DIV_ROUND_UP(n,d)  (((n)+(d)-1)/(d))
#define round_up(x,y)      ((((x)+(y)-1)/(y))*(y))
#define IS_ERR(p)          ((unsigned long)(p) > (unsigned long)(-4096))
#define PTR_ERR(p)         ((long)(p))
#define ERR_PTR(e)         ((void*)(long)(e))
#define likely(x)          __builtin_expect(!!(x), 1)
#define unlikely(x)        __builtin_expect(!!(x), 0)
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#define msleep(ms)         usleep((unsigned int)(ms) * 1000u)
#define udelay(us)         usleep((unsigned int)(us))
#define usleep_range(a,b)  usleep((unsigned int)(a))
#define jiffies            0UL
#define HZ                 100
#define time_after(a,b)    0
#define schedule()         ((void)0)
#define cond_resched()     ((void)0)

/* ── completion stubs ─────────────────────────────────────────────── */
struct completion { volatile int done; };
#define DECLARE_COMPLETION_ONSTACK(x) struct completion x = {0}
#define init_completion(c)            ((c)->done = 0)
#define complete(c)                   ((c)->done = 1)
#define wait_for_completion(c)        do { while(!(c)->done) usleep(1000); } while(0)
#define wait_for_completion_timeout(c,t) (wait_for_completion(c), 1)
