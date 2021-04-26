#ifndef __PSABPF_H
#define __PSABPF_H

#include <stdint.h>

#define PSABPF_MAX_CLONE_SESSION_MEMBERS 64


/**
 * \brief          Global PSABPF context. Should be maintained between calls to the PSABPF API.
 */
typedef struct psabpf_context {

} psabpf_context_t;

/**
 * Initialize the PSABPF context.
 *
 * @param ctx
 */
void psabpf_init(psabpf_context_t *ctx);

/**
 * Clear the PSABPF context.
 *
 * @param ctx
 */
void psabpf_free(psabpf_context_t *ctx);

/*
 * PRE - Clone Sessions
 */
typedef uint32_t psabpf_clone_session_id_t;

typedef struct psabpf_clone_session_entry {
    uint32_t egress_port;
    uint16_t instance;
    uint8_t  class_of_service;
    uint8_t  truncate;
    uint8_t  packet_length_bytes;
} psabpf_clone_session_entry_t;

typedef struct psabpf_clone_session_ctx {
    psabpf_clone_session_id_t id;
    size_t curr_idx;
    psabpf_clone_session_entry_t *prev;
} psabpf_clone_session_ctx_t;

/*
 * We do we need clone session context? It is mainly useful for iteration over clone session members.
 */
void psabpf_clone_session_context_init(psabpf_clone_session_ctx_t *ctx);
void psabpf_clone_session_context_free(psabpf_clone_session_ctx_t *ctx);

void psabpf_clone_session_id(psabpf_clone_session_ctx_t *ctx, psabpf_clone_session_id_t id);
int psabpf_clone_session_create(psabpf_clone_session_ctx_t *ctx);
int psabpf_clone_session_exists(psabpf_clone_session_ctx_t *ctx);
int psabpf_clone_session_delete(psabpf_clone_session_ctx_t *ctx);

int psabpf_clone_session_member_init(psabpf_clone_session_entry_t *entry);
int psabpf_clone_session_member_free(psabpf_clone_session_entry_t *entry);
int psabpf_clone_session_member_port(psabpf_clone_session_entry_t *entry, uint32_t egress_port);
int psabpf_clone_session_member_instance(psabpf_clone_session_entry_t *entry, uint16_t instance);
int psabpf_clone_session_member_cos(psabpf_clone_session_entry_t *entry, uint8_t class_of_service);
int psabpf_clone_session_member_truncate(psabpf_clone_session_entry_t *entry, uint8_t packet_length_bytes);

int psabpf_clone_session_member_add(psabpf_clone_session_ctx_t *ctx, psabpf_clone_session_entry_t *entry);
int psabpf_clone_session_member_delete(psabpf_clone_session_ctx_t *ctx, psabpf_clone_session_entry_t *entry);
int psabpf_clone_session_member_exists(psabpf_clone_session_ctx_t *ctx, psabpf_clone_session_entry_t *entry);
int psabpf_clone_session_member_get(psabpf_clone_session_ctx_t *ctx, psabpf_clone_session_entry_t *entry);

/*
 * Example:
 * psabpf_clone_session_ctx_t ctx;
 * psabpf_clone_session_context_init(&ctx);
 *
 * psabpf_clone_session_entry_t entry;
 * psabpf_clone_session_member_init(&entry);
 *
 * while(psabpf_clone_session_member_getnext(&ctx, &entry)) {
 *     // print entry fields
 * }
 *
 * psabpf_clone_session_member_free(&entry);
 * psabpf_clone_session_context_free(&ctx);
 *
 */
int psabpf_clone_session_member_getnext(psabpf_clone_session_ctx_t *ctx, psabpf_clone_session_entry_t **entry);

/*
 * PRE - Multicast Groups
 */
typedef uint32_t psabpf_mcast_grp_id_t;

typedef struct psabpf_mcast_grp_context {
    psabpf_mcast_grp_id_t id;
} psabpf_mcast_grp_ctx_t;

typedef struct psabpf_mcast_grp_member {
    uint32_t egress_port;
    uint16_t instance;
} psabpf_mcast_grp_member_t;

void psabpf_mcast_grp_context_init(psabpf_mcast_grp_ctx_t *ctx);
void psabpf_mcast_grp_context_free(psabpf_mcast_grp_ctx_t *ctx);

void psabpf_mcast_grp_id(psabpf_mcast_grp_ctx_t *ctx, psabpf_mcast_grp_id_t mcast_grp_id);

int psabpf_mcast_grp_create(psabpf_mcast_grp_ctx_t *ctx);
int psabpf_mcast_grp_exists(psabpf_mcast_grp_ctx_t *ctx);
int psabpf_mcast_grp_delete(psabpf_mcast_grp_ctx_t *ctx);

int psabpf_mcast_grp_member_init(psabpf_mcast_grp_member_t *member);
int psabpf_mcast_grp_member_free(psabpf_mcast_grp_member_t *member);

int psabpf_mcast_grp_member_port(psabpf_mcast_grp_member_t *member, uint32_t egress_port);
int psabpf_mcast_grp_member_instance(psabpf_mcast_grp_member_t *member, uint16_t instance);

int psabpf_mcast_grp_member_add(psabpf_mcast_grp_ctx_t *ctx, psabpf_mcast_grp_member_t *member);
int psabpf_mcast_grp_member_exists(psabpf_mcast_grp_ctx_t *ctx, psabpf_mcast_grp_member_t *member);
int psabpf_mcast_grp_member_delete(psabpf_mcast_grp_ctx_t *ctx, psabpf_mcast_grp_member_t *member);
// psabpf_mcast_grp_member_get does not make sense as mcast grp member does not have additional parameters

////// ForwardingConfig
typedef struct psabpf_prog {
    const char *obj;
    int prog_id;
} psabpf_prog_t;

int psabpf_prog_init(psabpf_prog_t *prog);
int psabpf_prog_free(psabpf_prog_t *prog);

int psabpf_prog_setobj(psabpf_prog_t *prog, char *obj);
int psabpf_prog_setid(psabpf_prog_t *prog, int prog_id);
int psabpf_prog_getid(psabpf_prog_t *prog, int *prog_id);

/* This function should load BPF program and initialize default maps (call map initializer program) */
int psabpf_prog_load(psabpf_prog_t *prog);
int psabpf_prog_unload(psabpf_prog_t *prog);

////// TableEntry
enum psabpf_matchkind_t {
    PSABPF_EXACT,
    PSABPF_LPM,
    PSABPF_TERNARY,
    PSBPF_RANGE
};

// TODO: this struct may not be well-designed yet; we need feedback from implementation; to be adjusted
typedef struct psabpf_match_key {
    enum psabpf_matchkind_t type;
    const char *data;
    const size_t key_size;  // key_size determines size of val and mask
    union {
        struct {
            // used only for 'ternary'
            const void *mask;
        } ternary;
        struct {
            // used only for 'lpm'
            const size_t prefix_len;
        } lpm;
        struct {
            // used only for 'range'
            const uint64_t start;
            const uint64_t end;
        } range;
    } u;
} psabpf_match_key_t;

typedef struct psabpf_action_param {
    const char *data;
    const size_t len;
} psabpf_action_param_t;

typedef struct psabpf_action {
    uint32_t action_id;

    size_t n_params;
    psabpf_action_param_t *params;
} psabpf_action_t;

typedef struct psabpf_table_entry {
    const char tbl_name[256];

    size_t n_keys;
    psabpf_match_key_t *match_keys;

    psabpf_action_t *action;

    const uint32_t priority;
} psabpf_table_entry_t;

/*
 * TODO: specific fields of table entry context are still to be added.
 * The table entry context may store information about a table itself (e.g. key size, num of entries, etc.).
 * It may be filled in based on the P4Info file.
 */
typedef struct psabpf_table_entry_context {

    // below fields might be useful when iterating
    size_t curr_idx;
    psabpf_table_entry_t *prev;
} psabpf_table_entry_ctx_t;

void psabpf_table_entry_ctx_init(psabpf_table_entry_ctx_t *ctx);
void psabpf_table_entry_ctx_free(psabpf_table_entry_ctx_t *ctx);

void psabpf_table_entry_init(psabpf_table_entry_t *entry);
void psabpf_table_entry_free(psabpf_table_entry_t *entry);
void psabpf_table_entry_tblname(psabpf_table_entry_t *entry, const char *name);
int psabpf_table_entry_matchkey(psabpf_table_entry_t *entry, psabpf_match_key_t *mk);
void psabpf_table_entry_action(psabpf_table_entry_t *entry, psabpf_action_t *act);
void psabpf_table_entry_priority(psabpf_table_entry_t *entry, const uint32_t priority);

void psabpf_matchkey_init(psabpf_match_key_t *mk);
void psabpf_matchkey_free(psabpf_match_key_t *mk);
void psabpf_matchkey_type(psabpf_match_key_t *mk, enum psabpf_matchkind_t type);
int psabpf_matchkey_data(psabpf_match_key_t *mk, const char *data, size_t size);
int psabpf_matchkey_prefix(psabpf_match_key_t *mk, uint32_t prefix);

// only for ternary
int psabpf_matchkey_mask(psabpf_match_key_t *mk, const char *mask, size_t size);

// only for 'range' match
int psabpf_matchkey_start(psabpf_match_key_t *mk, uint64_t start);
int psabpf_matchkey_end(psabpf_match_key_t *mk, uint64_t end);

int psabpf_action_param_create(psabpf_action_param_t *param, const char *data, size_t size);

void psabpf_action_init(psabpf_action_t *action);
void psabpf_action_free(psabpf_action_t *action);
void psabpf_action_param(psabpf_action_t *action, psabpf_action_param_t *param);

int psabpf_table_entry_add(psabpf_table_entry_ctx_t *ctx, psabpf_table_entry_t *entry);
int psabpf_table_entry_del(psabpf_table_entry_ctx_t *ctx, psabpf_table_entry_t *entry);
int psabpf_table_entry_get(psabpf_table_entry_ctx_t *ctx, psabpf_table_entry_t **entry);
int psabpf_table_entry_getnext(psabpf_table_entry_ctx_t *ctx, psabpf_table_entry_t **entry);

/**
 * Sets a default entry.
 *
 * Example code:
 *  psabpf_table_entry_t entry;
 *  if (!psabpf_table_entry_init(&entry))
 *      return;
 *  psabpf_table_entry_tblname(&entry, "xyz");
 *
 *  psabpf_action_t action;
 *  psabpf_action_init(&action);
 *  psabpf_action_setid(&action, 1);
 *  for (action params)
 *      psabpf_action_param_set(&action, "dsada", 12);
 *
 *  if (!psabpf_table_entry_setdefault(&entry))
 *      psabpf_table_entry_free(&entry);
 *      return EINVAL;
 *
 *  psabpf_table_entry_free(&entry);
 *
 * @param entry
 * @return
 */
int psabpf_table_entry_setdefault(psabpf_table_entry_t *entry);
int psabpf_table_entry_getdefault(psabpf_table_entry_t *entry);


/*
 * P4 Counters
 */

typedef uint64_t psabpf_counter_value_t;

// TODO: psabpf API for counters is not done yet.
typedef struct {
    //! member validity: packets, bytes or both?
    int valid;
    psabpf_counter_value_t bytes;
    psabpf_counter_value_t packets;
} psabpf_counter_data_t;

int psabpf_counter_read(const char *name, size_t index, psabpf_counter_data_t *data);
int psabpf_counter_reset(const char *name, size_t index);

////// P4 Registers
// TODO: to be implemented

////// P4 Digests
/* Used to read a next Digest message. */
typedef struct psabpf_digest {
    size_t size;  // size of Digest message.
} psabpf_digest_t;

int psabpf_digest_init(psabpf_digest_t *digest, size_t size);
int psabpf_digest_free(psabpf_digest_t *digest);
int psabpf_digest_setname(psabpf_digest_t *digest, const char *name);
int psabpf_digest_get_next(psabpf_digest_t *digest, void **data);

////// PacketIn / PacketOut
// TODO: to be implemented
//  - to listen on the specified PSA_PORT_CPU interfaces
//  - to send packet out of the specified PSA_PORT_CPU interface

////// MISC
// TODO: to be implemented
//  /* Use to retrieve report about packet processing from the data plane. */
//  int psabpf_report_get_next();

#endif //__PSABPF_H
