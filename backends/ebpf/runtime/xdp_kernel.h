#pragma once

#include "ebpf_common.h"

#include "bpf_endian.h" // definitions for bpf_ntohs etc...

#undef htonl
#undef htons
#define htons(d) bpf_htons(d)
#define htonl(d) bpf_htonl(d)
#define htonll(d) bpf_cpu_to_be64(d)
#define ntohll(x) bpf_be64_to_cpu(x)

#define load_byte(data, b) (*(((u8*)(data)) + (b)))
#define load_half(data, b) bpf_ntohs(*(u16 *)((u8*)(data) + (b)))
#define load_word(data, b) bpf_ntohl(*(u32 *)((u8*)(data) + (b)))
#define load_dword(data, b) bpf_be64_to_cpu(*(u64 *)((u8*)(data) + (b)))


/* If we operate in user space we only need to include bpf.h and
 * define the userspace API macros.
 * For kernel programs we need to specify a list of kernel helpers. These are
 * taken from here: https://github.com/torvalds/linux/blob/master/tools/testing/selftests/bpf/bpf_helpers.h
 */
#ifdef CONTROL_PLANE // BEGIN EBPF USER SPACE DEFINITIONS

#include "bpf.h" // bpf_obj_get/pin, bpf_map_update_elem

#define BPF_USER_MAP_UPDATE_ELEM(index, key, value, flags)\
    bpf_map_update_elem(index, key, value, flags)
#define BPF_OBJ_PIN(table, name) bpf_obj_pin(table, name)
#define BPF_OBJ_GET(name) bpf_obj_get(name)

#else // BEGIN EBPF KERNEL DEFINITIONS

#include <linux/pkt_cls.h>  // TC_ACT_OK, TC_ACT_SHOT
#include "linux/bpf.h"  // types, and general bpf definitions
// This file contains the definitions of all the kernel bpf essentials
#include "bpf_helpers.h"

/* simple descriptor which replaces the kernel sk_buff structure */
#define SK_BUFF struct __sk_buff

#define REGISTER_START()
#ifndef BTF

#define REGISTER_TABLE(NAME, TYPE, KEY_SIZE, VALUE_SIZE, MAX_ENTRIES) \
struct bpf_map_def SEC("maps") NAME = {          \
    .type           = TYPE,             \
    .key_size       = KEY_SIZE,         \
    .value_size     = VALUE_SIZE,       \
    .max_entries    = MAX_ENTRIES,      \
    .map_flags      = 0,                \
};

#else
#define REGISTER_TABLE(NAME, TYPE, KEY_SIZE, VALUE_SIZE, MAX_ENTRIES) \
struct {                                 \
	__uint(type, TYPE);                  \
	__uint(key, KEY_SIZE);          \
	__uint(value, VALUE_SIZE);      \
	__uint(max_entries, MAX_ENTRIES);    \
} NAME SEC(".maps");

#endif
#define REGISTER_END()

#define BPF_MAP_LOOKUP_ELEM(table, key) \
    bpf_map_lookup_elem(&table, key)
#define BPF_MAP_UPDATE_ELEM(table, key, value, flags) \
    bpf_map_update_elem(&table, key, value, flags)
#define BPF_MAP_DELETE_ELEM(table, key) \
    bpf_map_delete_elem(&table, key)
#define BPF_USER_MAP_UPDATE_ELEM(index, key, value, flags)\
    bpf_update_elem(index, key, value, flags)
#define BPF_OBJ_PIN(table, name) bpf_obj_pin(table, name)
#define BPF_OBJ_GET(name) bpf_obj_get(name)

#endif // END EBPF KERNEL DEFINITIONS