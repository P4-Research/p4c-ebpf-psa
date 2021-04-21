from common import *

import random

from scapy.layers.l2 import Ether
from scapy.layers.inet import IP, UDP
from scapy.packet import raw

import ptf.testutils as testutils

PORT0 = 0
PORT1 = 1
PORT2 = 2
ALL_PORTS = [PORT0, PORT1, PORT2]


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
            testutils.send_packet(self, PORT0, pkt)
            pkt[IP].ttl = pkt[IP].ttl - 2
            pkt[IP].src = '10.0.0.1'
            pkt[IP].chksum = None
            pkt[UDP].chksum = None
            testutils.verify_packet_any_port(self, pkt, ALL_PORTS)

            # test packet with invalid checksum
            # Checksum will never contain value 0xFFFF, see RFC 1624 sec. 3.
            pkt[IP].chksum = 0xFFFF
            testutils.send_packet(self, PORT0, pkt)
            testutils.verify_no_other_packets(self)


class HashCRC16PSATest(P4EbpfTest):
    p4_file_path ="samples/p4testdata/hash-crc16.p4"

    def runTest(self):
        pkt = Ether() / raw("123456789{}".format("0000".decode("hex")))
        exp_pkt = Ether() / raw("123456789{}".format("bb3d".decode("hex")))
        testutils.send_packet(self, PORT0, pkt)
        testutils.verify_packet_any_port(self, exp_pkt, ALL_PORTS)


class HashCRC32PSATest(P4EbpfTest):
    p4_file_path ="samples/p4testdata/hash-crc32.p4"

    def runTest(self):
        pkt = Ether() / raw("123456789{}".format("00000000".decode("hex")))
        exp_pkt = Ether() / raw("123456789{}".format("cbf43926".decode("hex")))
        testutils.send_packet(self, PORT0, pkt)
        testutils.verify_packet_any_port(self, exp_pkt, ALL_PORTS)


class HashRangePSATest(P4EbpfTest):
    p4_file_path = "samples/p4testdata/hash-range.p4"

    def runTest(self):
        res = 50 + (0xbb3d % 200)
        pkt = Ether() / raw("123456789{}".format("00".decode("hex")))
        exp_pkt = Ether() / raw("123456789{}".format(format(res, 'x').decode("hex")))
        testutils.send_packet(self, PORT0, pkt)
        testutils.verify_packet_any_port(self, exp_pkt, ALL_PORTS)
