// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 Covalent IO, Inc. http://covalent.io
 */
#include <linux/bpf.h>
#include <linux/if_link.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <net/if.h>
#include <unistd.h>
#include <libgen.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

static int ifindex_in;
static int ifindex_out;
static bool ifindex_out_xdp_dummy_attached = true;
static bool xdp_devmap_attached;
static __u32 prog_id;
static __u32 dummy_prog_id;

static __u32 xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST;

static void int_exit(int sig)
{
    __u32 curr_prog_id = 0;

    if (bpf_get_link_xdp_id(ifindex_in, &curr_prog_id, xdp_flags)) {
        printf("bpf_get_link_xdp_id failed\n");
        exit(1);
    }
    if (prog_id == curr_prog_id)
        bpf_set_link_xdp_fd(ifindex_in, -1, xdp_flags);
    else if (!curr_prog_id)
        printf("couldn't find a prog id on iface IN\n");
    else
        printf("program on iface IN changed, not removing\n");

    if (ifindex_out_xdp_dummy_attached) {
        curr_prog_id = 0;
        if (bpf_get_link_xdp_id(ifindex_out, &curr_prog_id,
                                xdp_flags)) {
            printf("bpf_get_link_xdp_id failed\n");
            exit(1);
        }
        if (dummy_prog_id == curr_prog_id)
            bpf_set_link_xdp_fd(ifindex_out, -1, xdp_flags);
        else if (!curr_prog_id)
            printf("couldn't find a prog id on iface OUT\n");
        else
            printf("program on iface OUT changed, not removing\n");
    }
    exit(0);
}

static void poll_stats()
{
    printf("Running!\n");
    while (1) {
        sleep(1);
    }
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [OPTS] <IFNAME|IFINDEX>_IN <IFNAME|IFINDEX>_OUT\n\n"
            "OPTS:\n"
            "    -S    use skb-mode\n"
            "    -N    enforce native mode\n"
            "    -F    force loading prog\n"
            "    -X    load xdp program on egress\n",
            prog);
}

int main(int argc, char **argv)
{
    struct bpf_prog_load_attr prog_load_attr = {
            .prog_type	= BPF_PROG_TYPE_UNSPEC,
    };
    struct bpf_program *prog, *dummy_prog, *devmap_prog;
    int prog_fd, dummy_prog_fd, devmap_prog_fd = 0;
    int tx_port_map_fd;
    struct bpf_devmap_val devmap_val;
    struct bpf_prog_info info = {};
    __u32 info_len = sizeof(info);
    const char *optstr = "FSNX";
    struct bpf_object *obj;
    int ret, opt, key = 0;
    char filename[256];

    while ((opt = getopt(argc, argv, optstr)) != -1) {
        switch (opt) {
            case 'S':
                xdp_flags |= XDP_FLAGS_SKB_MODE;
                break;
            case 'N':
                /* default, set below */
                break;
            case 'F':
                xdp_flags &= ~XDP_FLAGS_UPDATE_IF_NOEXIST;
                break;
            case 'X':
                xdp_devmap_attached = true;
                break;
            default:
                usage(basename(argv[0]));
                return 1;
        }
    }

    if (!(xdp_flags & XDP_FLAGS_SKB_MODE)) {
        xdp_flags |= XDP_FLAGS_DRV_MODE;
    } else if (xdp_devmap_attached) {
        printf("Load xdp program on egress with SKB mode not supported yet\n");
        return 1;
    }

    if (optind == argc) {
        printf("usage: %s <IFNAME|IFINDEX>_IN <IFNAME|IFINDEX>_OUT\n", argv[0]);
        return 1;
    }

    ifindex_in = if_nametoindex(argv[optind]);
    if (!ifindex_in)
        ifindex_in = strtoul(argv[optind], NULL, 0);

    ifindex_out = if_nametoindex(argv[optind + 1]);
    if (!ifindex_out)
        ifindex_out = strtoul(argv[optind + 1], NULL, 0);

    printf("input: %d output: %d\n", ifindex_in, ifindex_out);

    snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
    prog_load_attr.file = filename;

    if (bpf_prog_load_xattr(&prog_load_attr, &obj, &prog_fd))
        return 1;

    if (xdp_flags & XDP_FLAGS_SKB_MODE) {
        prog = bpf_object__find_program_by_name(obj, "xdp_redirect_map_general");
        tx_port_map_fd = bpf_object__find_map_fd_by_name(obj, "tx_port_general");
    } else {
        prog = bpf_object__find_program_by_name(obj, "xdp_redirect_map_native");
        tx_port_map_fd = bpf_object__find_map_fd_by_name(obj, "tx_port_native");
    }
    dummy_prog = bpf_object__find_program_by_name(obj, "xdp_redirect_dummy_prog");
    if (!prog || dummy_prog < 0 || tx_port_map_fd < 0) {
        printf("finding prog/dummy_prog/tx_port_map in obj file failed\n");
        goto out;
    }
    prog_fd = bpf_program__fd(prog);
    dummy_prog_fd = bpf_program__fd(dummy_prog);
    if (prog_fd < 0 || dummy_prog_fd < 0 || tx_port_map_fd < 0) {
        printf("bpf_prog_load_xattr: %s\n", strerror(errno));
        return 1;
    }

    if (bpf_set_link_xdp_fd(ifindex_in, prog_fd, xdp_flags) < 0) {
        printf("ERROR: link set xdp fd failed on %d\n", ifindex_in);
        return 1;
    }

    ret = bpf_obj_get_info_by_fd(prog_fd, &info, &info_len);
    if (ret) {
        printf("can't get prog info - %s\n", strerror(errno));
        return ret;
    }
    prog_id = info.id;

    /* Loading dummy XDP prog on out-device */
    if (bpf_set_link_xdp_fd(ifindex_out, dummy_prog_fd,
                            (xdp_flags | XDP_FLAGS_UPDATE_IF_NOEXIST)) < 0) {
        printf("WARN: link set xdp fd failed on %d\n", ifindex_out);
        ifindex_out_xdp_dummy_attached = false;
    }

    memset(&info, 0, sizeof(info));
    ret = bpf_obj_get_info_by_fd(dummy_prog_fd, &info, &info_len);
    if (ret) {
        printf("can't get prog info - %s\n", strerror(errno));
        return ret;
    }
    dummy_prog_id = info.id;

    /* Load 2nd xdp prog on egress. */
    if (xdp_devmap_attached) {
        devmap_prog = bpf_object__find_program_by_name(obj, "xdp_redirect_map_egress");
        if (!devmap_prog) {
            printf("finding devmap_prog in obj file failed\n");
            goto out;
        }
        devmap_prog_fd = bpf_program__fd(devmap_prog);
        if (devmap_prog_fd < 0) {
            printf("finding devmap_prog fd failed\n");
            goto out;
        }
    }

    signal(SIGINT, int_exit);
    signal(SIGTERM, int_exit);

    devmap_val.ifindex = ifindex_out;
    devmap_val.bpf_prog.fd = devmap_prog_fd;
    ret = bpf_map_update_elem(tx_port_map_fd, &key, &devmap_val, 0);
    if (ret) {
        perror("bpf_update_elem");
        goto out;
    }

    poll_stats();

    out:
    return 0;
}