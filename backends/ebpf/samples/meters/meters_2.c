#include <stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>
#include <linux/pkt_cls.h>
#include <linux/ip.h>

#include "ebpf_kernel.h"

#include <stdbool.h>
//#include <linux/if_ether.h>
#include "psa.h"

#include <inttypes.h>
#include <stdio.h>
#include <math.h>

#define bpf_trace_message(fmt, ...)
//#define bpf_trace_message(fmt, ...)                                \
//    do {                                                           \
//        char ____fmt[] = fmt;                                      \
//        bpf_trace_printk(____fmt, sizeof(____fmt), ##__VA_ARGS__); \
//    } while(0)

#ifndef RTE_METER_TB_PERIOD_MIN
#define RTE_METER_TB_PERIOD_MIN      100
#endif

struct rte_meter_trtcm_params {
    u64 cir; /**< Committed Information Rate (CIR). Measured in bytes per second. */
    u64 pir; /**< Peak Information Rate (PIR). Measured in bytes per second. */
    u64 cbs; /**< Committed Burst Size (CBS). Measured in bytes. */
    u64 pbs; /**< Peak Burst Size (PBS). Measured in bytes. */
};

struct rte_meter_trtcm_profile {
    u64 cbs;
    /**< Upper limit for C token bucket */
    u64 pbs;
    /**< Upper limit for P token bucket */
    u64 cir_period;
    /**< Number of CPU cycles for one update of C token bucket */
    u64 cir_bytes_per_period;
    /**< Number of bytes to add to C token bucket on each update */
    u64 pir_period;
    /**< Number of CPU cycles for one update of P token bucket */
    u64 pir_bytes_per_period;
    /**< Number of bytes to add to P token bucket on each update */


    u64 time_tc;
    /**< Time of latest update of C token bucket */
    u64 time_tp;
    /**< Time of latest update of P token bucket */
    u64 tc;
    /**< Number of bytes currently available in committed(C) token bucket */
    u64 tp;
    /**< Number of bytes currently available in the peak(P) token bucket */
};

//typedef struct rte_meter_trtcm_profile meter_value;

/**
 * Internal data structure storing the trTCM run-time context per metered
 * traffic flow.
 */
struct rte_meter_trtcm {
    u64 time_tc;
    /**< Time of latest update of C token bucket */
    u64 time_tp;
    /**< Time of latest update of P token bucket */
    u64 tc;
    /**< Number of bytes currently available in committed(C) token bucket */
    u64 tp;
    /**< Number of bytes currently available in the peak(P) token bucket */
};

struct key {
    u32 index;
};
typedef struct key ingress_meter1_key;

struct value_meter {
    u32 pir;
    u32 cir;
    u32 pbs;
    u32 cbs;
    u64 timestamp;
    u32 pbs_left;
    u32 cbs_left;
    struct bpf_spin_lock lock;
};
typedef struct value_meter meter_value;

//REGISTER_START()
//REGISTER_TABLE(ingress_meter1, BPF_MAP_TYPE_HASH, sizeof(ingress_meter1_key), sizeof(meter_value), 1)
//BPF_ANNOTATE_KV_PAIR(ingress_meter1, struct key, struct value_meter);
//REGISTER_END()



struct bpf_map_def SEC("maps") ingress_meter1 = {
        .type = BPF_MAP_TYPE_HASH,
        .key_size = sizeof(ingress_meter1_key),
        .value_size = sizeof(meter_value),
        .max_entries = 1,
};

BPF_ANNOTATE_KV_PAIR(ingress_meter1, ingress_meter1_key, meter_value);


//static __always_inline
//int enough_tokens(u32 *tokens, u32 *packet_len, u32 *bs, u32 *bs_left, u32 *ir, u64 *delta_t, u32 *factor) {
//
//    // B = B + ns * kb/s / 8000000
//    *tokens = *bs_left + ((*delta_t * *ir) / *factor);
//
//    if (*tokens > *bs) {
//        *tokens = *bs;
//    }
//
//    if (*packet_len > *tokens) {
//        bpf_trace_message("Meter: No enough tokens");
//        return 0; // No
//    }
//
//    bpf_trace_message("Meter: Enough tokens");
//    return 1; // Yes, enough tokens
//}

static __always_inline
enum PSA_MeterColor_t rte_meter_trtcm_color_blind_check(
//        struct rte_meter_trtcm *m,
        struct rte_meter_trtcm_profile *p,
        u64 time,
        u32 pkt_len)
{
    u64 time_diff_tc, time_diff_tp, n_periods_tc, n_periods_tp, tc, tp;

    /* Bucket update */
    time_diff_tc = time - p->time_tc;
    time_diff_tp = time - p->time_tp;
    bpf_trace_message("Time diff tc: %lu\n", time_diff_tc);
    bpf_trace_message("Time diff tp: %lu\n", time_diff_tp);
    bpf_trace_message("Cir period: %lu\n", p->cir_period);
    n_periods_tc = time_diff_tc / p->cir_period;
    bpf_trace_message("Number of periods: %lu\n", n_periods_tc);
    n_periods_tp = time_diff_tp / p->pir_period;
    p->time_tc += n_periods_tc * p->cir_period;
    p->time_tp += n_periods_tp * p->pir_period;

    bpf_trace_message("Tc: %lu\n", p->tc);
    bpf_trace_message("Tp: %lu\n", p->tp);

    bpf_trace_message("Bytes per period: %lu\n", p->cir_bytes_per_period);
    tc = p->tc + n_periods_tc * p->cir_bytes_per_period;
    if (tc > p->cbs)
        tc = p->cbs;

    tp = p->tp + n_periods_tp * p->pir_bytes_per_period;
    if (tp > p->pbs)
        tp = p->pbs;

    /* Color logic */
    if (tp < pkt_len) {
        p->tc = tc;
        p->tp = tp;
        return RED;
    }

    if (tc < pkt_len) {
        p->tc = tc;
        p->tp = tp - pkt_len;
        return YELLOW;
    }

    p->tc = tc - pkt_len;
    p->tp = tp - pkt_len;
    return GREEN;
}

//static __always_inline
//enum PSA_MeterColor_t meter_execute(meter_value *value, u32 *packet_len, u32 *factor) {
//    bpf_trace_message("Meter: execute");
//    u64 time_ns = bpf_ktime_get_ns();
//    u64 delta_t = time_ns - value->timestamp;
//    u32 tokens_pbs = 0;
//    __sync_fetch_and_add(&(value->timestamp), delta_t);
//    if (enough_tokens(&tokens_pbs, packet_len, &value->pbs, &value->pbs_left, &value->pir, &delta_t, factor)) {
//        u32 tokens_cbs = 0;
//        if (enough_tokens(&tokens_cbs, packet_len, &value->cbs, &value->cbs_left, &value->cir, &delta_t, factor)) {
////            value->timestamp = value->timestamp + delta_t;
////            __sync_fetch_and_add(&(value->timestamp), delta_t);
//            value->pbs_left = tokens_pbs - *packet_len;
//            value->cbs_left = tokens_cbs - *packet_len;
//            bpf_trace_message("Meter: GREEN");
//            return GREEN;
//        } else {
////            value->timestamp = value->timestamp + delta_t;
////            __sync_fetch_and_add(&(value->timestamp), delta_t);
//            value->pbs_left = tokens_pbs - *packet_len;
//            value->cbs_left = tokens_cbs;
//            bpf_trace_message("Meter: YELLOW");
//            return YELLOW;
//        }
//    } else {
//        value->pbs_left = tokens_pbs;
//        value->cbs_left = tokens_cbs;
//        bpf_trace_message("Meter: RED");
//        return RED;
//    }
//}

static __always_inline
enum PSA_MeterColor_t meter_execute(meter_value *value, u32 *packet_len, u32 *factor) {
    bpf_trace_message("Meter: execute");
    u64 time_ns = bpf_ktime_get_ns();
    u64 delta_t = time_ns - value->timestamp;
    u32 tokens_pbs = 0;
    u32 tokens_cbs = 0;
//    __sync_fetch_and_add(&(value->timestamp), delta_t);
    value->timestamp = value->timestamp + delta_t;
    tokens_pbs = value->pbs_left + ((delta_t * value->pir) / *factor);
    if (tokens_pbs > value->pbs) {
        tokens_pbs = value->pbs;
    }
    tokens_cbs = value->cbs_left + ((delta_t * value->cir) / *factor);
    if (tokens_cbs > value->cbs) {
        tokens_cbs = value->cbs;
    }

    if (*packet_len > tokens_pbs) {
        value->pbs_left = tokens_pbs;
        value->cbs_left = tokens_cbs;
        bpf_trace_message("Meter: RED");
        return RED;
    }

    if (*packet_len > tokens_cbs) {
        value->pbs_left = tokens_pbs - *packet_len;
        value->cbs_left = tokens_cbs;
        bpf_trace_message("Meter: YELLOW");
        return YELLOW;
    }

    value->pbs_left = tokens_pbs - *packet_len;
    value->cbs_left = tokens_cbs - *packet_len;
    bpf_trace_message("Meter: GREEN");
    return GREEN;
}

//static __always_inline
//enum PSA_MeterColor_t meter_execute(meter_value *value, u32 *packet_len, u32 *factor) {
//    bpf_trace_message("Meter: execute\n");
//    u64 time_ns = bpf_ktime_get_ns();
//
//    time_ns = time_ns + 2000000;
//
//
//    return rte_meter_trtcm_color_blind_check(value, time_ns, *packet_len);
//}

static __always_inline
enum PSA_MeterColor_t meter_execute_bytes_value(meter_value *value, u32 *packet_len) {
    u32 factor = 8000000;
    if (value != NULL) {
        return meter_execute(value, packet_len, &factor);
    } else {
        bpf_trace_message("Meter: No meter value!\n");
        return RED;
    }
}

static __always_inline
enum PSA_MeterColor_t meter_execute_bytes(void *map, u32 *packet_len, void *key) {
    meter_value *value = BPF_MAP_LOOKUP_ELEM(*map, key);
    enum PSA_MeterColor_t col;
    u32 factor = 8000000;
    u64 time_ns = bpf_ktime_get_ns();
    if (value != NULL) {
//        bpf_spin_lock(&value->lock);
//        return meter_execute(value, packet_len, &factor);
        u64 delta_t = time_ns - value->timestamp;
        u32 tokens_pbs = 0;
        u32 tokens_cbs = 0;
//    __sync_fetch_and_add(&(value->timestamp), delta_t);
        value->timestamp = value->timestamp + delta_t;
        tokens_pbs = value->pbs_left + ((delta_t * value->pir) / factor);
        if (tokens_pbs > value->pbs) {
            tokens_pbs = value->pbs;
        }
        tokens_cbs = value->cbs_left + ((delta_t * value->cir) / factor);
        if (tokens_cbs > value->cbs) {
            tokens_cbs = value->cbs;
        }

        if (*packet_len > tokens_pbs) {
            value->pbs_left = tokens_pbs;
            value->cbs_left = tokens_cbs;
            bpf_trace_message("Meter: RED");
            col = RED;
        }

        if (*packet_len > tokens_cbs) {
            value->pbs_left = tokens_pbs - *packet_len;
            value->cbs_left = tokens_cbs;
            bpf_trace_message("Meter: YELLOW");
            col = YELLOW;
        }

        value->pbs_left = tokens_pbs - *packet_len;
        value->cbs_left = tokens_cbs - *packet_len;
        bpf_trace_message("Meter: GREEN");
        col = GREEN;
//        bpf_spin_unlock(&value->lock);
        return col;
    } else {
        bpf_trace_message("Meter: No meter value!\n");
        return RED;
    }
//    enum PSA_MeterColor_t col = meter_execute_bytes_value(value, packet_len);
//
//    return col;
}

static __always_inline
enum PSA_MeterColor_t meter_execute_packets_value(meter_value *value) {
    u32 len = 1;
    u32 factor = 1000000000;
    if (value != NULL) {
        bpf_spin_lock(&value->lock);
        enum PSA_MeterColor_t col = meter_execute(value, &len, &factor);
        bpf_spin_unlock(&value->lock);
        return col;
    } else {
        bpf_trace_message("Meter: No meter value!\n");
        return RED;
    }
}

static __always_inline
enum PSA_MeterColor_t meter_execute_packets(void *map, void *key) {
    meter_value *value = BPF_MAP_LOOKUP_ELEM(*map, key);
    return meter_execute_packets_value(value);
}

SEC("classifier/tc-ingress")
int tc_l2fwd(struct __sk_buff *ctx)
{
//    u32 cpu = bpf_get_smp_processor_id();
//    bpf_printk("[XDP       ] cpu=%d\n", cpu);

    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;
    struct ethhdr *eth = data;

    if (eth + 1 > data_end) {
        return TC_ACT_SHOT;
    }

    struct iphdr *iph = data + sizeof(*eth);
    if (iph + 1 > data_end) {
        return TC_ACT_SHOT;
    }
    u32 color1_0;
    u32 idx_0 = 0;

    if (ctx->ifindex == 2) {
        color1_0 = meter_execute_bytes(&ingress_meter1, &ctx->len, &idx_0);

        if (color1_0 == 1) {
            return bpf_redirect(3, 0);
        }

        return TC_ACT_SHOT;
    } else if (ctx->ifindex == 3) {
        return bpf_redirect(2, 0);
    }
    return bpf_redirect(0, 0);

}

SEC("classifier/tc-egress")
int tc_l2fwd_egress(struct __sk_buff *ctx)
{
//    bpf_printk("ifindex=%d, skb->priority = %d\n", ctx->ifindex, ctx->priority);
    return TC_ACT_OK;
}

//SEC("xdp/xdp-ingress")
//int xdp_func(struct xdp_md *skb)
//{
//    u32 cpu = bpf_get_smp_processor_id();
//    bpf_printk("[XDP       ] cpu=%d\n", cpu);
//
//    return XDP_PASS;
//}

char _license[] SEC("license") = "GPL";