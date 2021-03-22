#!/usr/bin/env python
import os
import logging
import subprocess
import copy
import shlex
import random

import ptf
# from ptf import config
# from ptf.mask import Mask
import ptf.testutils as testutils
from scapy.packet import Packet
from scapy.fields import (
    ByteField,
    IntField,
    ShortField,
)
from ptf.base_tests import BaseTest
import ctypes as c
import struct

from scapy.layers.l2 import Ether
from scapy.layers.inet import IP, UDP
from ptf.packet import MPLS

logger = logging.getLogger('eBPFTest')
if not len(logger.handlers):
    logger.addHandler(logging.StreamHandler())

PORT0 = 0
PORT1 = 1
PORT2 = 2
ALL_PORTS = [PORT0, PORT1, PORT2]


class EbpfTest(BaseTest):
    switch_ns = 'test'
    test_prog_image = 'generic.o'  # default, if test case not specify program
    ctool_file_path = ""

    def exec_ns_cmd(self, command='echo me', do_fail=None):
        command = "nsenter --net=/var/run/netns/" + self.switch_ns + " " + command
        return self.exec_cmd(command, do_fail)

    def exec_cmd(self, command, do_fail=None):
        if isinstance(command, str):
            command = shlex.split(command)
        process = subprocess.Popen(command,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        stdout_data, stderr_data = process.communicate()
        if stderr_data is None:
            stderr_data = ""
        if stdout_data is None:
            stdout_data = ""
        if process.returncode != 0:
            logger.info("Command failed: %s", command)
            logger.info("Return code: %d", process.returncode)
            logger.info("STDOUT: %s", stdout_data)
            logger.info("STDERR: %s", stderr_data)
            if do_fail:
                self.fail("Command failed (see above for details): {}".format(str(do_fail)))
        return process.returncode, stdout_data, stderr_data

    def add_port(self, dev):
        self.exec_ns_cmd("bpftool net attach xdp pinned /sys/fs/bpf/prog/xdp_xdp-ingress dev {} overwrite".format(dev))
        self.exec_ns_cmd("tc qdisc add dev {} clsact".format(dev))
        self.exec_ns_cmd("tc filter add dev {} ingress bpf da fd /sys/fs/bpf/prog/classifier_tc-ingress".format(dev))
        self.exec_ns_cmd("tc filter add dev {} egress bpf da fd /sys/fs/bpf/prog/classifier_tc-egress".format(dev))

    def del_port(self, dev):
        self.exec_ns_cmd("ip link set dev {} xdp off".format(dev))
        self.exec_ns_cmd("tc qdisc del dev {} clsact".format(dev))

    def remove_map(self, name):
        self.exec_ns_cmd("rm /sys/fs/bpf/{}".format(name))

    def remove_maps(self, maps):
        for map in maps:
            self.remove_map(map)

    def create_map(self, name, type, key_size, value_size, max_entries):
        self.exec_ns_cmd("bpftool map create /sys/fs/bpf/{} type "
                         "{} key {} value {} entries {} name {}".format(
                         name, type, key_size, value_size, max_entries, name))

    def update_map(self, name, key, value, map_in_map=False):
        if map_in_map:
            value = "pinned /sys/fs/bpf/{} any".format(value)
        self.exec_ns_cmd("bpftool map update pinned /sys/fs/bpf/{} key {} value {}".format(name, key, value))

    def setUp(self):
        super(EbpfTest, self).setUp()
        self.dataplane = ptf.dataplane_instance
        self.dataplane.flush()

        if "namespace" in testutils.test_params_get():
            self.switch_ns = testutils.test_param_get("namespace")
        logger.info("Using namespace: %s", self.switch_ns)
        self.interfaces = testutils.test_param_get("interfaces").split(",")
        logger.info("Using interfaces: %s", str(self.interfaces))

        self.exec_ns_cmd("bpftool prog loadall {} /sys/fs/bpf/prog".format(self.test_prog_image))

        for intf in self.interfaces:
            self.add_port(dev=intf)

        if self.ctool_file_path:
            head, tail = os.path.split(self.ctool_file_path)
            filename = tail.split(".")[0]
            so_file_path = head + "/" + filename + ".so"
            cmd = ["clang", "-fPIC", "-l", "bpf", "-shared", "-o", so_file_path, self.ctool_file_path]
            self.exec_cmd(cmd, "Ctool compilation error")
            self.so_file_path = so_file_path

    def tearDown(self):
        for intf in self.interfaces:
            self.del_port(intf)
        self.exec_ns_cmd("rm -rf /sys/fs/bpf/prog")
        super(EbpfTest, self).tearDown()


class P4EbpfTest(EbpfTest):
    """
    Similar to EbpfTest, but generates BPF bytecode from a P4 program.
    """

    p4_file_path = ""

    def setUp(self):
        if not os.path.exists(self.p4_file_path):
            self.fail("P4 program not found, no such file.")

        if not os.path.exists("ptf_out"):
            os.makedirs("ptf_out")

        head, tail = os.path.split(self.p4_file_path)
        filename = tail.split(".")[0]
        c_file_path = os.path.join("ptf_out", filename + ".c")
        cmd = ["p4c-ebpf", "--trace", "--arch", "psa", "-o", c_file_path, self.p4_file_path]
        self.exec_cmd(cmd, "P4 compilation error")
        output_file_path = os.path.join("ptf_out", filename + ".o")

        cmd = ["clang", "-O2", "-target", "bpf", "-Werror", "-DBTF", "-DPSA_PORT_RECIRCULATE=2", "-g", "-c", c_file_path, "-o", output_file_path, "-I../runtime", "-I../runtime/contrib/libbpf/include/uapi/", "-I../runtime/contrib/libbpf/src/" ]
        self.exec_cmd(cmd, "Clang compilation error")
        self.test_prog_image = output_file_path

        super(P4EbpfTest, self).setUp()

    def tearDown(self):
        self.remove_map("clone_session_tbl")
        self.remove_map("multicast_grp_tbl")
        super(P4EbpfTest, self).tearDown()


class SimpleForwardingPSATest(P4EbpfTest):

    p4_file_path = "samples/p4testdata/simple-fwd.p4"

    def runTest(self):
        pkt = testutils.simple_ip_packet()
        # initialize default action
        # TODO: we need to come up with a better solution to initialize default action.
        self.update_map(name="ingress_tbl_fwd_defaultAction", key="00 00 00 00", value="01 00 00 00 05 00 00 00")
        testutils.send_packet(self, PORT0, str(pkt))
        testutils.verify_packet(self, str(pkt), PORT1)


class PSAResubmitTest(P4EbpfTest):

    p4_file_path = "samples/p4testdata/resubmit.p4"

    def runTest(self):
        pkt = testutils.simple_eth_packet()
        testutils.send_packet(self, PORT0, pkt)
        pkt[Ether].dst = "11:22:33:44:55:66"
        testutils.verify_packet(self, pkt, PORT1)


class SimpleTunnelingPSATest(P4EbpfTest):

    p4_file_path = "samples/p4testdata/psa-tunneling.p4"

    def runTest(self):
        pkt = Ether(dst="11:11:11:11:11:11") / testutils.simple_ip_only_packet(ip_dst="192.168.1.1")

        exp_pkt = Ether(dst="11:11:11:11:11:11") / MPLS(label=20, cos=5, s=1, ttl=64) / testutils.simple_ip_only_packet(
            ip_dst="192.168.1.1")

        testutils.send_packet(self, PORT0, str(pkt))
        testutils.verify_packet(self, str(exp_pkt), PORT1)


class PSACloneI2E(P4EbpfTest):

    p4_file_path = "../../../testdata/p4_16_samples/psa-i2e-cloning-basic-bmv2.p4"

    def runTest(self):
        # create clone session table
        self.exec_ns_cmd("prectl clone-session create id 8")
        # add egress_port=6 (PORT2), instance=1 as clone session member, cos = 0
        self.exec_ns_cmd("prectl clone-session add-member id 8 egress-port 6 instance 1 cos 0")
        # add egress_port=6 (PORT2), instance=2 as clone session member, cos = 1
        self.exec_ns_cmd("prectl clone-session add-member id 8 egress-port 6 instance 2 cos 1")

        pkt = testutils.simple_eth_packet(eth_dst='00:00:00:00:00:05')
        testutils.send_packet(self, PORT0, pkt)
        cloned_pkt = copy.deepcopy(pkt)
        cloned_pkt[Ether].type = 0xface
        testutils.verify_packet(self, cloned_pkt, PORT2)
        testutils.verify_packet(self, cloned_pkt, PORT2)
        pkt[Ether].src = "00:00:00:00:ca:fe"
        testutils.verify_packet(self, pkt, PORT1)

        pkt = testutils.simple_eth_packet(eth_dst='00:00:00:00:00:09')
        testutils.send_packet(self, PORT0, pkt)
        testutils.verify_no_packet(self, pkt, PORT1)

    def tearDown(self):
        self.exec_ns_cmd("prectl clone-session delete id 8")
        super(P4EbpfTest, self).tearDown()


class EgressTrafficManagerDropPSATest(P4EbpfTest):
    p4_file_path = "samples/p4testdata/etm-drop.p4"

    def runTest(self):
        pkt = testutils.simple_ip_packet(eth_dst='00:11:22:33:44:55', eth_src='55:44:33:22:11:00')
        testutils.send_packet(self, PORT0, str(pkt))
        testutils.verify_packet_any_port(self, str(pkt), ALL_PORTS)
        pkt[Ether].src = '00:44:33:22:FF:FF'
        testutils.send_packet(self, PORT0, str(pkt))
        testutils.verify_no_other_packets(self)


class EgressTrafficManagerClonePSATest(P4EbpfTest):
    """
    1. Send packet to interface PORT1 (bpf ifindex = 5) with destination MAC address equals to aa:bb:cc:dd:ee:ff.
    2. Observe that:
      2.1. Original packet was sent back through interface PORT1 (bpf ifindex = 5).
           The packet should have destination MAC address set to '00:00:00:00:00:12'.
      2.2. Packet was cloned at egress and processed by egress pipeline at interface PORT2 (bpf ifindex = 6).
           The cloned packet should have destination MAC address set to '00:00:00:00:00:11'.
    """
    p4_file_path = "samples/p4testdata/etm-clone-e2e.p4"

    def runTest(self):
        # create clone session table
        self.exec_ns_cmd("prectl clone-session create id 8")
        # add egress_port=6 (PORT2), instance=1 as clone session member, cos = 0
        self.exec_ns_cmd("prectl clone-session add-member id 8 egress-port 6 instance 1 cos 0")

        pkt = testutils.simple_ip_packet(eth_dst='aa:bb:cc:dd:ee:ff', eth_src='55:44:33:22:11:00')
        testutils.send_packet(self, PORT1, str(pkt))
        pkt[Ether].dst = '00:00:00:00:00:11'
        testutils.verify_packet(self, str(pkt), PORT2)
        pkt[Ether].dst = '00:00:00:00:00:12'
        testutils.verify_packet(self, str(pkt), PORT1)

    def tearDown(self):
        self.exec_ns_cmd("prectl clone-session delete id 8")
        super(EgressTrafficManagerClonePSATest, self).tearDown()


class EgressTrafficManagerRecirculatePSATest(P4EbpfTest):
    """
    Test resubmit packet path. eBPF program should do following operation:
    1. In NORMAL path: In all packet set source MAC to starts with '00:44'.
        Test if destination MAC address ends with 'FE:F0' - in this case recirculate.
    2. In RECIRCULATE path destination MAC set to zero.
    Any packet modification should be done on egress.
    Open question: how to verify here that the eBPF program did above operations?
    """
    p4_file_path = "samples/p4testdata/etm-recirc.p4"

    def runTest(self):
        pkt = testutils.simple_ip_packet(eth_dst='00:11:22:33:44:55', eth_src='55:44:33:22:11:00')
        testutils.send_packet(self, PORT0, str(pkt))
        pkt[Ether].src = '00:44:33:22:11:00'
        testutils.verify_packet_any_port(self, str(pkt), ALL_PORTS)

        pkt = testutils.simple_ip_packet(eth_dst='00:11:22:33:FE:F0', eth_src='55:44:33:22:11:00')
        testutils.send_packet(self, PORT0, str(pkt))
        pkt[Ether].dst = '00:00:00:00:00:00'
        pkt[Ether].src = '00:44:33:22:11:00'
        testutils.verify_packet_any_port(self, str(pkt), ALL_PORTS)


class MulticastPSATest(P4EbpfTest):
    p4_file_path = "../../../testdata/p4_16_samples/psa-multicast-basic-bmv2.p4"

    def runTest(self):
        # TODO: replace bpftool with prectl
        self.create_map(name="mcast_grp_8", type="hash", key_size=8, value_size=20, max_entries=64)
        self.update_map(name="mcast_grp_8", key="02 00 00 00 01 00 00 00",
                        value="06 00 00 00 00 00 05 00 00 00 00 00 00 00 00 00 00 00 00 00")
        self.update_map(name="mcast_grp_8", key="01 00 00 00 01 00 00 00",
                        value="05 00 00 00 00 00 05 00 00 00 00 00 02 00 00 00 01 00 00 00")
        self.update_map(name="mcast_grp_8", key="00 00 00 00 00 00 00 00",
                        value="00 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 01 00 00 00")
        self.update_map(name="multicast_grp_tbl", key="8 0 0 0", value="mcast_grp_8", map_in_map=True)

        pkt = testutils.simple_eth_packet(eth_dst='00:00:00:00:00:05')
        testutils.send_packet(self, PORT0, pkt)
        testutils.verify_no_other_packets(self)

        pkt = testutils.simple_eth_packet(eth_dst='00:00:00:00:00:08')
        testutils.send_packet(self, PORT0, pkt)
        testutils.verify_packet(self, pkt, PORT1)
        testutils.verify_packet(self, pkt, PORT2)
        testutils.verify_no_other_packets(self)

    def tearDown(self):
        self.remove_map("mcast_grp_8")
        super(MulticastPSATest, self).tearDown()


class SimpleLpmP4PSATest(P4EbpfTest):

    p4_file_path = "samples/p4testdata/psa-lpm.p4"

    def runTest(self):
        pkt = testutils.simple_ip_packet(ip_src='1.1.1.1', ip_dst='10.10.11.11')
        # This command adds LPM entry 10.10.0.0/16 with action forwarding on port 6 (PORT2 in ptf)
        self.update_map(name="ingress_tbl_fwd_lpm", key="hex 10 00 00 00 0a 0a 00 00",
                        value="hex 01 00 00 00 06 00 00 00")
        # This command adds 10.10.10.10/8 entry with not existing port number (0)
        self.update_map(name="ingress_tbl_fwd_lpm", key="hex 08 00 00 00 0a 0a 0a 0a",
                        value="hex 01 00 00 00 00 00 00 00")

        testutils.send_packet(self, PORT0, str(pkt))
        testutils.verify_packet(self, str(pkt), PORT2)

        pkt = testutils.simple_ip_packet(ip_src='1.1.1.1', ip_dst='192.168.2.1')
        # This command adds LPM entry 192.168.2.1/24 with action forwarding on port 5 (PORT1 in ptf)
        self.update_map(name="ingress_tbl_fwd_lpm", key="hex 18 00 00 00 c0 a8 02 00",
                        value="hex 01 00 00 00 05 00 00 00")
        testutils.send_packet(self, PORT0, str(pkt))
        testutils.verify_packet(self, str(pkt), PORT1)

    def tearDown(self):
        self.remove_map("ingress_tbl_fwd_lpm")
        self.remove_map("ingress_tbl_fwd_lpm_defaultAction")
        super(SimpleLpmP4PSATest, self).tearDown()


class SimpleLpmP4TwoKeysPSATest(P4EbpfTest):

    p4_file_path = "samples/p4testdata/psa-lpm-two-keys.p4"

    def runTest(self):
        pkt = testutils.simple_ip_packet(ip_src='1.2.3.4', ip_dst='10.10.11.11')
        # This command adds LPM entry 10.10.11.0/24 with action forwarding on port 6 (PORT2 in ptf)
        # Note that prefix value has to be a sum of exact fields size and lpm prefix
        self.update_map(name="ingress_tbl_fwd_exact_lpm", key="hex 38 00 00 00 01 02 03 04 0a 0a 0b 00",
                        value="hex 01 00 00 00 06 00 00 00")
        testutils.send_packet(self, PORT0, str(pkt))
        testutils.verify_packet(self, str(pkt), PORT2)

        pkt = testutils.simple_ip_packet(ip_src='1.2.3.4', ip_dst='192.168.2.1')
        # This command adds LPM entry 192.168.2.1/24 with action forwarding on port 5 (PORT1 in ptf)
        # Note that prefix value has to be a sum of exact fields size and lpm prefix
        self.update_map(name="ingress_tbl_fwd_exact_lpm", key="hex 38 00 00 00 01 02 03 04 c0 a8 02 00",
                        value="hex 01 00 00 00 05 00 00 00")
        testutils.send_packet(self, PORT0, str(pkt))
        testutils.verify_packet(self, str(pkt), PORT1)

    def tearDown(self):
        self.remove_map("ingress_tbl_fwd_exact_lpm")
        self.remove_map("ingress_tbl_fwd_exact_lpm_defaultAction")
        super(SimpleLpmP4TwoKeysPSATest, self).tearDown()


class CountersPSATest(P4EbpfTest):
    import json

    p4_file_path = "samples/p4testdata/counters.p4"

    def get_counter_value(self, name, cid):
        # convert number into hex stream and compose separate bytes as decimal values
        cid = ['{}{}'.format(a, b) for a, b in zip(*[iter('{:08x}'.format(cid))]*2)]
        cid = [format(int(v, 16), 'd') for v in cid]
        cid.reverse()
        cid = ' '.join(cid)
        cmd = "bpftool -j map lookup pinned /sys/fs/bpf/{} key {}".format(name, cid)
        _, stdout, _ = self.exec_ns_cmd(cmd, "Failed to get counter")
        # create hex string from value
        value = [format(int(v, 0), '02x') for v in self.json.loads(stdout)['value']]
        value.reverse()
        return ''.join(value)

    def verify_counter(self, name, cid, expected_value):
        value = self.get_counter_value(name, cid)
        if expected_value != value:
            self.fail("Counter {}.{} does not have correct value. Expected {}; got {}"
                      .format(name, cid, expected_value, value))

    def runTest(self):
        pkt = testutils.simple_ip_packet(eth_dst='00:11:22:33:44:55',
                                         eth_src='00:AA:00:00:00:01',
                                         pktlen=100)
        testutils.send_packet(self, PORT0, str(pkt))
        testutils.verify_packet_any_port(self, str(pkt), ALL_PORTS)

        self.verify_counter("ingress_test1_cnt", 1, "0000000000000064")
        self.verify_counter("ingress_test2_cnt", 1, "00000001")
        self.verify_counter("ingress_test3_cnt", 1, "0000000100000064")

        pkt = testutils.simple_ip_packet(eth_dst='00:11:22:33:44:55',
                                         eth_src='00:AA:00:00:01:FE',
                                         pktlen=199)
        testutils.send_packet(self, PORT0, str(pkt))
        testutils.verify_packet_any_port(self, str(pkt), ALL_PORTS)

        self.verify_counter("ingress_test1_cnt", 510, "00000000000000c7")
        self.verify_counter("ingress_test2_cnt", 510, "00000001")
        self.verify_counter("ingress_test3_cnt", 510, "00000001000000c7")

    def tearDown(self):
        self.remove_map("ingress_test1_cnt")
        self.remove_map("ingress_test2_cnt")
        self.remove_map("ingress_test3_cnt")
        super(CountersPSATest, self).tearDown()


class DigestPSATest(P4EbpfTest):

    p4_file_path = "samples/p4testdata/digest.p4"
    ctool_file_path = "ptf/tools/read_digest.c"

    def double_to_hex(self, f):
        return hex(struct.unpack('<Q', struct.pack('<d', f))[0])

    def get_digest_value(self):
        class Digest(c.Structure):
            pass
        Digest._fields_ = [("mac", c.c_long), ("port", c.c_int)]
        my_functions = c.CDLL(self.so_file_path)
        my_functions.pop_value.restype = Digest

        return my_functions.pop_value()

    def runTest(self):
        pkt = testutils.simple_ip_packet(eth_src="ff:ff:ff:ff:ff:ff")
        testutils.send_packet(self, PORT0, str(pkt))
        testutils.send_packet(self, PORT0, str(pkt))
        testutils.send_packet(self, PORT0, str(pkt))

        for i in range(0, 3):
            value = self.get_digest_value()
            if hex(value.mac) != "0xffffffffffff" or value.port != 4:
                self.fail("Digest map stored wrong values: mac->%s, port->%s" %
                          (hex(value.mac), value.port))

    def tearDown(self):
        self.remove_map("mac_learn_digest_0")
        super(DigestPSATest, self).tearDown()


class PSATernaryTest(P4EbpfTest):

    p4_file_path = "samples/p4testdata/psa-ternary.p4"

    def runTest(self):
        # flow rules for 'tbl_ternary_0'
        # 1. hdr.ipv4.srcAddr=0x01020304/0xffffff00 => action 0 priority 1
        # 2. hdr.ipv4.srcAddr=0x01020304/0xffff00ff => action 1 priority 10
        self.update_map(name="ingress_tbl_ternary_0_prefixes", key="00 00 00 00",
                        value="01 00 00 00 00 0xff 0xff 0xff 01 00 00 00")
        self.update_map(name="ingress_tbl_ternary_0_prefixes", key="00 0xff 0xff 0xff",
                        value="01 00 00 00 0xff 00 0xff 0xff 01 00 00 00")
        self.update_map(name="ingress_tbl_ternary_0_prefixes", key="0xff 00 0xff 0xff",
                        value="02 00 00 00 00 00 00 00 00 00 00 00")
        self.create_map(name="ingress_tbl_ternary_0_tuple_1", type="hash", key_size=4, value_size=8,
                        max_entries=100)
        self.create_map(name="ingress_tbl_ternary_0_tuple_2", type="hash", key_size=4, value_size=8,
                        max_entries=100)
        self.update_map(name="ingress_tbl_ternary_0_tuple_1", key="00 0x03 0x02 0x01",
                        value="00 00 00 00 01 00 00 00")
        self.update_map(name="ingress_tbl_ternary_0_tuple_2", key="0x04 00 0x02 0x01",
                        value="01 00 00 00 10 00 00 00")
        self.update_map(name="ingress_tbl_ternary_0_tuples_map", key="01 0 0 0",
                        value="ingress_tbl_ternary_0_tuple_1", map_in_map=True)
        self.update_map(name="ingress_tbl_ternary_0_tuples_map", key="02 0 0 0",
                        value="ingress_tbl_ternary_0_tuple_2", map_in_map=True)

        # flow rules for 'tbl_ternary_1'
        # 1. hdr.ipv4.diffserv=0x00/0x00, hdr.ipv4.dstAddr=0xc0a80201/0xffffff00 => action 0 priority 1
        # 2. hdr.ipv4.diffserv=0x00/0xff, hdr.ipv4.dstAddr=0xc0a80201/0xffffff00 => action 1 priority 10
        self.update_map(name="ingress_tbl_ternary_1_prefixes", key="00 00 00 00 00 00 00 00",
                        value="01 00 00 00 00 0xff 0xff 0xff 0xff 00 00 00 01 00 00 00")
        self.update_map(name="ingress_tbl_ternary_1_prefixes", key="00 0xff 0xff 0xff 0xff 00 00 00",
                        value="06 00 00 00 00 0xff 0xff 0xff 00 00 00 00 01 00 00 00")
        self.update_map(name="ingress_tbl_ternary_1_prefixes", key="00 0xff 0xff 0xff 00 00 00 00",
                        value="07 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00")
        self.create_map(name="ingress_tbl_ternary_1_tuple_6", type="hash", key_size=8, value_size=8,
                        max_entries=100)
        self.create_map(name="ingress_tbl_ternary_1_tuple_7", type="hash", key_size=8, value_size=8,
                        max_entries=100)
        self.update_map(name="ingress_tbl_ternary_1_tuple_7", key="00 0x02 0xa8 0xc0 00 00 00 00",
                        value="00 00 00 00 01 00 00 00")
        self.update_map(name="ingress_tbl_ternary_1_tuple_6", key="00 0x02 0xa8 0xc0 00 00 00 00",
                        value="01 00 00 00 10 00 00 00")
        self.update_map(name="ingress_tbl_ternary_1_tuples_map", key="06 00 00 00",
                        value="ingress_tbl_ternary_1_tuple_6", map_in_map=True)
        self.update_map(name="ingress_tbl_ternary_1_tuples_map", key="07 00 00 00",
                        value="ingress_tbl_ternary_1_tuple_7", map_in_map=True)

        # flow rules 'tbl_ternary_2':
        # 1. hdr.ipv4.protocol=0x11, hdr.ipv4.diffserv=0x00/0x00, hdr.ipv4.dstAddr=0xc0a80201/0xffff0000 => action 0 priority 1
        # 2. hdr.ipv4.protocol=0x11, hdr.ipv4.diffserv=0x00/0xff, hdr.ipv4.dstAddr=0xc0a80201/0xffff0000 => action 1 priority 10
        self.update_map(name="ingress_tbl_ternary_2_prefixes", key="00 00 00 00 00 00 00 00",
                        value="01 00 00 00 00 00 0xff 0xff 0xff 00 00 00 01 00 00 00")
        self.update_map(name="ingress_tbl_ternary_2_prefixes", key="00 00 0xff 0xff 0xff 00 00 00",
                        value="03 00 00 00 00 00 0xff 0xff 00 00 00 00 01 00 00 00")
        self.update_map(name="ingress_tbl_ternary_2_prefixes", key="00 00 0xff 0xff 00 00 00 00",
                        value="05 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00")
        self.create_map(name="ingress_tbl_ternary_2_tuple_3", type="hash", key_size=8, value_size=8,
                        max_entries=100)
        self.create_map(name="ingress_tbl_ternary_2_tuple_5", type="hash", key_size=8, value_size=8,
                        max_entries=100)
        self.update_map(name="ingress_tbl_ternary_2_tuple_3", key="00 00 0xa8 0xc0 0x11 00 00 00",
                        value="00 00 00 00 01 00 00 00")
        self.update_map(name="ingress_tbl_ternary_2_tuple_5", key="00 00 0xa8 0xc0 00 00 00 00",
                        value="01 00 00 00 10 00 00 00")
        self.update_map(name="ingress_tbl_ternary_2_tuples_map", key="03 00 00 00",
                        value="ingress_tbl_ternary_2_tuple_3", map_in_map=True)
        self.update_map(name="ingress_tbl_ternary_2_tuples_map", key="05 00 00 00",
                        value="ingress_tbl_ternary_2_tuple_5", map_in_map=True)


        pkt = testutils.simple_udp_packet(ip_src='1.2.3.4', ip_dst='192.168.2.1')
        testutils.send_packet(self, PORT0, str(pkt))
        pkt[IP].proto = 0x7
        pkt[IP].chksum = 0xb3e7
        pkt[IP].src = '17.17.17.17'
        pkt[IP].dst = '255.255.255.255'
        pkt[UDP].chksum = 0x044D
        testutils.verify_packet(self, str(pkt), PORT1)

    def tearDown(self):
        self.remove_maps(
            ["ingress_tbl_ternary_0_prefixes",
             "ingress_tbl_ternary_0_tuples_map",
             "ingress_tbl_ternary_0_tuple_1",
             "ingress_tbl_ternary_0_tuple_2",
             "ingress_tbl_ternary_0_defaultAction",
             "ingress_tbl_ternary_1_prefixes",
             "ingress_tbl_ternary_1_tuples_map",
             "ingress_tbl_ternary_1_tuple_6",
             "ingress_tbl_ternary_1_tuple_7",
             "ingress_tbl_ternary_1_defaultAction",
             "ingress_tbl_ternary_2_prefixes",
             "ingress_tbl_ternary_2_tuples_map",
             "ingress_tbl_ternary_2_tuple_3",
             "ingress_tbl_ternary_2_tuple_5",
             "ingress_tbl_ternary_2_defaultAction"]
        )

        super(PSATernaryTest, self).tearDown()
        
    
class InternetChecksumPSATest(P4EbpfTest):
    """
    Test if checksum in IP header (or any other using Ones Complement algorithm)
    is computed correctly.
    1. Generate IP packet with random values in header.
    2. Verify that packet is forwarded. Data plane will decrement TTL twice and change
     source IP address.
    3. Send the same packet with bad checksum.
    4. Verify that packet is dropped.
    5. Repeat 1-4 a few times with a different packet.
    """

    p4_file_path = "samples/p4testdata/internet-checksum.p4"

    def random_ip(self):
        return ".".join(str(random.randint(0, 255)) for _ in range(4))

    def runTest(self):
        for _ in range(10):
            # test checksum computation
            pkt = testutils.simple_udp_packet(pktlen=random.randint(100, 512),
                                              ip_src=self.random_ip(),
                                              ip_dst=self.random_ip(),
                                              ip_ttl=random.randint(3, 255),
                                              ip_id=random.randint(0, 0xFFFF))
            pkt[IP].flags = random.randint(0, 7)
            pkt[IP].frag = random.randint(0, 0x1FFF)
            testutils.send_packet(self, PORT0, str(pkt))
            pkt[IP].ttl = pkt[IP].ttl - 2
            pkt[IP].src = '10.0.0.1'
            pkt[IP].chksum = None
            pkt[UDP].chksum = None
            testutils.verify_packet_any_port(self, str(pkt), ALL_PORTS)

            # test packet with invalid checksum
            # Checksum will never contain value 0xFFFF, see RFC 1624 sec. 3.
            pkt[IP].chksum = 0xFFFF
            testutils.send_packet(self, PORT0, str(pkt))
            testutils.verify_no_other_packets(self)


class ParserValueSetPSATest(P4EbpfTest):
    """
    Test value_set implementation. P4 application will pass packet, which IP destination
    address contains value_set and destination port 80.
    1. Send UDP packet. Should be dropped.
    2. Configure value_set with other IP address.
    3. Send UDP packet. Should be dropped.
    4. Change IP destination address to the same as in value_set.
    5. Send UDP packet. Should be passed.
    """
    p4_file_path = "samples/p4testdata/pvs.p4"

    def runTest(self):
        pkt = testutils.simple_udp_packet(ip_dst='8.8.8.8', udp_dport=80)

        testutils.send_packet(self, PORT0, str(pkt))
        testutils.verify_no_other_packets(self)

        self.update_map("IngressParserImpl_pvs", '1 0 0 10', '0 0 0 0')

        testutils.send_packet(self, PORT0, str(pkt))
        testutils.verify_no_other_packets(self)

        pkt[IP].dst = '10.0.0.1'
        testutils.send_packet(self, PORT0, str(pkt))
        testutils.verify_packet_any_port(self, str(pkt), ALL_PORTS)

    def tearDown(self):
        self.remove_map("IngressParserImpl_pvs")
        super(ParserValueSetPSATest, self).tearDown()
