#pragma once

#include <common/userspace/syscall.h>

// NOLINTBEGIN

typedef enum {
    SC_I32,
    SC_U32,

    SC_U64,
    SC_I64,

    SC_VOID,
    SC_PTR,
} sc_kind_t;

typedef struct {
    const char* name;
    const char* type;
    sc_kind_t kind;
} syscall_arg_desc_t;

typedef struct {
    const char* name;
    bool no_return;
    uint64_t num;
    sc_kind_t ret_type;
    uint8_t nargs;
    const syscall_arg_desc_t* args;
    syscall_ret_t (*fn)(syscall_args_t*);
} syscall_meta_t;

#define SC_KIND(t) _Generic((t) 0, uint64_t: SC_U64, int64_t: SC_I64, uint32_t: SC_U32, int32_t: SC_I32, void*: SC_PTR, void: SC_VOID)

#define _SC_DESC(t, n) { #n, #t, SC_KIND(t) }
#define _SC_EXP(...) __VA_ARGS__

#define _SC_COMMON(name, num, no_return, ret, n, DESCS, PARAMS, CALL)                                                                                              \
    static syscall_ret_t name(_SC_EXP PARAMS);                                                                                                                     \
    static syscall_ret_t name##_wrap(syscall_args_t* a) {                                                                                                          \
        (void) a;                                                                                                                                                  \
        return name(_SC_EXP CALL);                                                                                                                                 \
    }                                                                                                                                                              \
    static const syscall_arg_desc_t name##_args[] = { _SC_EXP DESCS };                                                                                             \
    [[gnu::used, gnu::section("syscall_meta")]] static const syscall_meta_t name##_meta = { #name, no_return, (num), SC_KIND(ret), (n), name##_args, name##_wrap }

#define SYSCALL_DEFINE_0(name, num, no_return, ret) _SC_COMMON(name, num, no_return, ret, 0, ({ nullptr, nullptr, SC_U64 }), (syscall_args_t * argument_data), (a))
#define SYSCALL_DEFINE_1(name, num, no_return, ret, t1, n1) _SC_COMMON(name, num, no_return, ret, 1, (_SC_DESC(t1, n1)), (syscall_args_t * argument_data, t1 n1), (a, (t1) a->args[0]))
#define SYSCALL_DEFINE_2(name, num, no_return, ret, t1, n1, t2, n2) _SC_COMMON(name, num, no_return, ret, 2, (_SC_DESC(t1, n1), _SC_DESC(t2, n2)), (syscall_args_t * argument_data, t1 n1, t2 n2), (a, (t1) a->args[0], (t2) a->args[1]))
#define SYSCALL_DEFINE_3(name, num, no_return, ret, t1, n1, t2, n2, t3, n3)                                                                                                                                         \
    _SC_COMMON(name, num, no_return, ret, 3, (_SC_DESC(t1, n1), _SC_DESC(t2, n2), _SC_DESC(t3, n3)), (syscall_args_t * argument_data, t1 n1, t2 n2, t3 n3), (a, (t1) a->args[0], (t2) a->args[1], (t3) a->args[2]))
#define SYSCALL_DEFINE_4(name, num, no_return, ret, t1, n1, t2, n2, t3, n3, t4, n4) \
    _SC_COMMON(                                                                     \
        name,                                                                       \
        num,                                                                        \
        no_return,                                                                  \
        ret,                                                                        \
        4,                                                                          \
        (_SC_DESC(t1, n1), _SC_DESC(t2, n2), _SC_DESC(t3, n3), _SC_DESC(t4, n4)),   \
        (syscall_args_t * argument_data, t1 n1, t2 n2, t3 n3, t4 n4),               \
        (a, (t1) a->args[0], (t2) a->args[1], (t3) a->args[2], (t4) a->args[3])     \
    )
#define SYSCALL_DEFINE_5(name, num, no_return, ret, t1, n1, t2, n2, t3, n3, t4, n4, t5, n5)         \
    _SC_COMMON(                                                                                     \
        name,                                                                                       \
        num,                                                                                        \
        no_return,                                                                                  \
        ret,                                                                                        \
        5,                                                                                          \
        (_SC_DESC(t1, n1), _SC_DESC(t2, n2), _SC_DESC(t3, n3), _SC_DESC(t4, n4), _SC_DESC(t5, n5)), \
        (syscall_args_t * argument_data, t1 n1, t2 n2, t3 n3, t4 n4, t5 n5),                        \
        (a, (t1) a->args[0], (t2) a->args[1], (t3) a->args[2], (t4) a->args[3], (t5) a->args[4])    \
    )
#define SYSCALL_DEFINE_6(name, num, no_return, ret, t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6)                   \
    _SC_COMMON(                                                                                                       \
        name,                                                                                                         \
        num,                                                                                                          \
        no_return,                                                                                                    \
        ret,                                                                                                          \
        6,                                                                                                            \
        (_SC_DESC(t1, n1), _SC_DESC(t2, n2), _SC_DESC(t3, n3), _SC_DESC(t4, n4), _SC_DESC(t5, n5), _SC_DESC(t6, n6)), \
        (syscall_args_t * argument_data, t1 n1, t2 n2, t3 n3, t4 n4, t5 n5, t6 n6),                                   \
        (a, (t1) a->args[0], (t2) a->args[1], (t3) a->args[2], (t4) a->args[3], (t5) a->args[4], (t6) a->args[5])     \
    )

#define _SC_PAIRS(z, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, N, ...) N
#define _SC_NPAIRS(...) _SC_PAIRS(0, ##__VA_ARGS__, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0, 0)

#define _SC_CAT(a, b) _SC_CAT2(a, b)
#define _SC_CAT2(a, b) a##b

#define SYSCALL_DEFINE(name, num, ret, ...) _SC_CAT(SYSCALL_DEFINE_, _SC_NPAIRS(__VA_ARGS__))(name, num, false, ret, ##__VA_ARGS__)
#define SYSCALL_DEFINE_NO_RETURN(name, num, ...) _SC_CAT(SYSCALL_DEFINE_, _SC_NPAIRS(__VA_ARGS__))(name, num, true, void, ##__VA_ARGS__)

// NOLINTEND
