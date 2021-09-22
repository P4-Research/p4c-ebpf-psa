#!/usr/bin/env python
from common import *

PORT0 = 0
PORT1 = 1
PORT2 = 2
ALL_PORTS = [PORT0, PORT1, PORT2]

meter_value_mask = 0xff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00
two_meters_value_mask = 0xff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_ff_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00


class MeterPSATest(P4EbpfTest):
    """
    Test Meter used in control block. Type BYTES.
    Send 100 B packet and verify if there is 100 tokens less left.
    """

    p4_file_path = "samples/p4testdata/meters.p4"

    def runTest(self):
        pkt = testutils.simple_ip_packet()
        # cir, pir -> 2 Mb/s -> 250000 byte/s, cbs, pbs -> bs (10 ms) -> 2500 B -> 09 C4
        # period 4000 ns -> 0F A0, 1 B per period -> 01
        self.meter_update(name="ingress_meter1", index=0,
                          pir=250000, pbs=2500, cir=250000, cbs=2500)
        testutils.send_packet(self, PORT0, pkt)
        testutils.verify_packet(self, pkt, PORT1)
        # Expecting pbs_left, cbs_left 2500 B - 100 B = 2400 B -> 09 60
        self.verify_map_entry(name="ingress_meter1", key="hex 00",
                              expected_value="hex "
                                             "A0 0F 00 00 00 00 00 00 "  # pir_period
                                             "01 00 00 00 00 00 00 00 "  # pir_unit_per_period
                                             "A0 0F 00 00 00 00 00 00 "  # cir_period
                                             "01 00 00 00 00 00 00 00 "  # cir_unit_per_period
                                             "C4 09 00 00 00 00 00 00 "  # pbs
                                             "C4 09 00 00 00 00 00 00 "  # cbs
                                             "60 09 00 00 00 00 00 00 "  # pbs_left
                                             "60 09 00 00 00 00 00 00 "  # cbs_left
                                             "00 00 00 00 00 00 00 00 "  # time_p
                                             "00 00 00 00 00 00 00 00 "  # time_c
                                             "00 00 00 00 00 00 00 00",  # Spin lock
                              mask=meter_value_mask)

    def tearDown(self):
        self.remove_maps(["ingress_meter1"])
        super(MeterPSATest, self).tearDown()


class MeterActionPSATest(P4EbpfTest):
    """
    Test Meter used in action. Type BYTES.
    Send 100 B packet and verify if there is 100 tokens less left.
    """

    p4_file_path = "samples/p4testdata/meters-action.p4"

    def runTest(self):
        pkt = testutils.simple_ip_packet()

        # cir, pir -> 10 Mb/s -> 1,25 MB/s, cbs, pbs -> bs (10 ms) -> 6250 B -> 18 6A
        # period 800 ns -> 03 20,  1 B per period -> 01
        self.meter_update(name="ingress_meter1", index=0,
                          pir=1250000, pbs=6250, cir=1250000, cbs=6250)
        self.update_map(name="ingress_tbl_fwd", key="hex 04 00 00 00", value="hex 01 00 00 00 05 00 00 00")

        testutils.send_packet(self, PORT0, pkt)
        testutils.verify_packet(self, pkt, PORT1)
        # Expecting pbs_left, cbs_left 6250 B - 100 B = 6150 B -> 18 06
        self.verify_map_entry(name="ingress_meter1", key="hex 00",
                              expected_value="hex "
                                             "20 03 00 00 00 00 00 00 "  # pir_period
                                             "01 00 00 00 00 00 00 00 "  # pir_unit_per_period
                                             "20 03 00 00 00 00 00 00 "  # cir_period
                                             "01 00 00 00 00 00 00 00 "  # cir_unit_per_period
                                             "6A 18 00 00 00 00 00 00 "  # pbs
                                             "6A 18 00 00 00 00 00 00 "  # cbs
                                             "06 18 00 00 00 00 00 00 "  # pbs_left
                                             "06 18 00 00 00 00 00 00 "  # cbs_left
                                             "00 00 00 00 00 00 00 00 "  # time_p
                                             "00 00 00 00 00 00 00 00 "  # time_c
                                             "00 00 00 00 00 00 00 00",  # Spin lock
                              mask=meter_value_mask)

    def tearDown(self):
        self.remove_maps(["ingress_meter1"])
        super(MeterActionPSATest, self).tearDown()


class MeterPacketsPSATest(P4EbpfTest):
    """
    Test Meter used in control block. Type PACKETS.
    Send 1 packet and verify if there is 9 tokens left.
    """

    p4_file_path = "samples/p4testdata/meters-packets.p4"

    def runTest(self):
        pkt = testutils.simple_ip_packet()
        # cir, pir -> 100 packets/s, period 10M -> 98 96 80, bs -> 10 -> 0A
        self.meter_update(name="ingress_meter1", index=0,
                          pir=100, pbs=10, cir=100, cbs=10)
        testutils.send_packet(self, PORT0, pkt)
        testutils.verify_packet(self, pkt, PORT1)
        # Expecting pbs_left, cbs_left 10 - 1 = 9
        self.verify_map_entry(name="ingress_meter1", key="hex 00",
                              expected_value="hex "
                                             "80 96 98 00 00 00 00 00 "  # pir_period
                                             "01 00 00 00 00 00 00 00 "  # pir_unit_per_period
                                             "80 96 98 00 00 00 00 00 "  # cir_period
                                             "01 00 00 00 00 00 00 00 "  # cir_unit_per_period
                                             "0A 00 00 00 00 00 00 00 "  # pbs
                                             "0A 00 00 00 00 00 00 00 "  # cbs
                                             "09 00 00 00 00 00 00 00 "  # pbs_left
                                             "09 00 00 00 00 00 00 00 "  # cbs_left
                                             "00 00 00 00 00 00 00 00 "  # time_p
                                             "00 00 00 00 00 00 00 00 "  # time_c
                                             "00 00 00 00 00 00 00 00",  # Spin lock
                              mask=meter_value_mask)

    def tearDown(self):
        self.remove_maps(["ingress_meter1"])
        super(MeterPacketsPSATest, self).tearDown()


class DirectMeterPSATest(P4EbpfTest):
    """
    Test Direct Meter. Type BYTES.
    Send 100 B packet and verify if there is 100 tokens less left.
    """

    p4_file_path = "samples/p4testdata/meters-direct.p4"

    def runTest(self):
        pkt = testutils.simple_ip_packet()

        # cir, pir -> 10 Mb/s, cbs, pbs -> bs (10 ms) -> 6250 B -> 18 6A
        # period 1ms -> 1250 B per period, 1ms -> 1e6 ns -> 0F 42 40, 1250 -> 04 E2
        self.update_map(name="ingress_tbl_fwd", key="hex 04 00 00 00",
                        value="hex "
                              "01 00 00 00 05 00 00 00 "  # action id | egress port
                              "40 42 0F 00 00 00 00 00 "  # pir_period
                              "E2 04 00 00 00 00 00 00 "  # pir_unit_per_period
                              "40 42 0F 00 00 00 00 00 "  # cir_period
                              "E2 04 00 00 00 00 00 00 "  # cir_unit_per_period
                              "6A 18 00 00 00 00 00 00 "  # pbs
                              "6A 18 00 00 00 00 00 00 "  # cbs
                              "6A 18 00 00 00 00 00 00 "  # pbs_left
                              "6A 18 00 00 00 00 00 00 "  # cbs_left
                              "00 00 00 00 00 00 00 00 "  # time_p
                              "00 00 00 00 00 00 00 00 "  # time_c
                              "00 00 00 00 00 00 00 00")  # Spin lock

        testutils.send_packet(self, PORT0, pkt)
        testutils.verify_packet(self, pkt, PORT1)
        # Expecting pbs_left, cbs_left 6250 B - 100 B = 6150 B -> 18 06
        self.verify_map_entry(name="ingress_tbl_fwd", key="hex 04 00 00 00",
                              expected_value="hex "
                                             "01 00 00 00 05 00 00 00 "
                                             "40 42 0F 00 00 00 00 00 "
                                             "E2 04 00 00 00 00 00 00 "
                                             "40 42 0F 00 00 00 00 00 "
                                             "E2 04 00 00 00 00 00 00 "
                                             "6A 18 00 00 00 00 00 00 "
                                             "6A 18 00 00 00 00 00 00 "
                                             "06 18 00 00 00 00 00 00 "  # pbs_left
                                             "06 18 00 00 00 00 00 00 "  # cbs_left
                                             "00 00 00 00 00 00 00 00 "
                                             "00 00 00 00 00 00 00 00 "
                                             "00 00 00 00 00 00 00 00",
                              mask=meter_value_mask)

    def tearDown(self):
        self.remove_maps(["ingress_tbl_fwd"])
        super(DirectMeterPSATest, self).tearDown()


class DirectAndIndirectMeterPSATest(P4EbpfTest):
    """
    Test Direct Meter and Indirect Meter together. Type BYTES.
    Send 100 B packet and verify if there is 100 tokens less left.
    """

    p4_file_path = "samples/p4testdata/meters-direct-and-indirect.p4"

    def runTest(self):
        pkt = testutils.simple_ip_packet()

        # cir, pir -> 10 Mb/s, cbs, pbs -> bs (10 ms) -> 6250 B -> 18 6A
        # period 1ms -> 1250 B per period, 1ms -> 1e6 ns -> 0F 42 40, 1250 -> 04 E2
        self.update_map(name="ingress_tbl_fwd", key="hex 04 00 00 00",
                        value="hex "
                              "01 00 00 00 05 00 00 00 "  # action id | egress port
                              "40 42 0F 00 00 00 00 00 "  # pir_period
                              "E2 04 00 00 00 00 00 00 "  # pir_unit_per_period
                              "40 42 0F 00 00 00 00 00 "  # cir_period
                              "E2 04 00 00 00 00 00 00 "  # cir_unit_per_period
                              "6A 18 00 00 00 00 00 00 "  # pbs
                              "6A 18 00 00 00 00 00 00 "  # cbs
                              "6A 18 00 00 00 00 00 00 "  # pbs_left
                              "6A 18 00 00 00 00 00 00 "  # cbs_left
                              "00 00 00 00 00 00 00 00 "  # time_p
                              "00 00 00 00 00 00 00 00 "  # time_c
                              "00 00 00 00 00 00 00 00")  # Spin lock

        # cir, pir -> 10 Mb/s, cbs, pbs -> bs (10 ms) -> 6250 B -> 18 6A
        # period 1ms -> 1250 B per period, 1ms -> 1e6 ns -> 0F 42 40, 1250 -> 04 E2
        self.update_map(name="ingress_indirect_meter", key="hex 00",
                        value="hex "
                              "40 42 0F 00 00 00 00 00 "  # pir_period
                              "E2 04 00 00 00 00 00 00 "  # pir_unit_per_period
                              "40 42 0F 00 00 00 00 00 "  # cir_period
                              "E2 04 00 00 00 00 00 00 "  # cir_unit_per_period
                              "6A 18 00 00 00 00 00 00 "  # pbs
                              "6A 18 00 00 00 00 00 00 "  # cbs
                              "6A 18 00 00 00 00 00 00 "  # pbs_left
                              "6A 18 00 00 00 00 00 00 "  # cbs_left
                              "00 00 00 00 00 00 00 00 "  # time_p
                              "00 00 00 00 00 00 00 00 "  # time_c
                              "00 00 00 00 00 00 00 00")  # Spin lock

        testutils.send_packet(self, PORT0, pkt)
        testutils.verify_packet(self, pkt, PORT1)
        # Expecting pbs_left, cbs_left 6250 B - 100 B = 6150 B -> 18 06
        self.verify_map_entry(name="ingress_tbl_fwd", key="hex 04 00 00 00",
                              expected_value="hex "
                                             "01 00 00 00 05 00 00 00 "
                                             "40 42 0F 00 00 00 00 00 "
                                             "E2 04 00 00 00 00 00 00 "
                                             "40 42 0F 00 00 00 00 00 "
                                             "E2 04 00 00 00 00 00 00 "
                                             "6A 18 00 00 00 00 00 00 "
                                             "6A 18 00 00 00 00 00 00 "
                                             "06 18 00 00 00 00 00 00 "  # pbs_left
                                             "06 18 00 00 00 00 00 00 "  # cbs_left
                                             "00 00 00 00 00 00 00 00 "
                                             "00 00 00 00 00 00 00 00 "
                                             "00 00 00 00 00 00 00 00",
                              mask=meter_value_mask)

        # Expecting pbs_left, cbs_left 6250 B - 100 B = 6150 B -> 18 06
        self.verify_map_entry(name="ingress_indirect_meter", key="hex 00",
                              expected_value="hex "
                                             "40 42 0F 00 00 00 00 00 "
                                             "E2 04 00 00 00 00 00 00 "
                                             "40 42 0F 00 00 00 00 00 "
                                             "E2 04 00 00 00 00 00 00 "
                                             "6A 18 00 00 00 00 00 00 "
                                             "6A 18 00 00 00 00 00 00 "
                                             "06 18 00 00 00 00 00 00 "  # pbs_left
                                             "06 18 00 00 00 00 00 00 "  # cbs_left
                                             "00 00 00 00 00 00 00 00 "
                                             "00 00 00 00 00 00 00 00 "
                                             "00 00 00 00 00 00 00 00",
                              mask=meter_value_mask)

    def tearDown(self):
        self.remove_maps(["ingress_tbl_fwd", "ingress_indirect_meter"])
        super(DirectAndIndirectMeterPSATest, self).tearDown()


class DirectAndIndirectActionMeterPSATest(DirectAndIndirectMeterPSATest):
    """
    Test Direct Meter and Indirect Meter together
    and execute Indirect Meter in single action.
    """

    p4_file_path = "samples/p4testdata/meters-direct-and-indirect-single-action.p4"


class DirectTwoMetersPSATest(P4EbpfTest):
    """
    Test two Direct Meters in one table. Type BYTES.
    Send 100 B packet and verify if there is 100 tokens less left.
    """

    p4_file_path = "samples/p4testdata/meters-two-direct.p4"

    def runTest(self):
        pkt = testutils.simple_ip_packet()

        # cir, pir -> 10 Mb/s, cbs, pbs -> bs (10 ms) -> 6250 B -> 18 6A
        # period 1ms -> 1250 B per period, 1ms -> 1e6 ns -> 0F 42 40, 1250 -> 04 E2
        self.update_map(name="ingress_tbl_fwd", key="hex 04 00 00 00",
                        value="hex "
                              "01 00 00 00 05 00 00 00 "  # action id | egress port
                              "40 42 0F 00 00 00 00 00 "  # pir_period
                              "E2 04 00 00 00 00 00 00 "  # pir_unit_per_period
                              "40 42 0F 00 00 00 00 00 "  # cir_period
                              "E2 04 00 00 00 00 00 00 "  # cir_unit_per_period
                              "6A 18 00 00 00 00 00 00 "  # pbs
                              "6A 18 00 00 00 00 00 00 "  # cbs
                              "6A 18 00 00 00 00 00 00 "  # pbs_left
                              "6A 18 00 00 00 00 00 00 "  # cbs_left
                              "00 00 00 00 00 00 00 00 "  # time_p
                              "00 00 00 00 00 00 00 00 "  # time_c
                              "40 42 0F 00 00 00 00 00 "  # pir_period -- second meter
                              "E2 04 00 00 00 00 00 00 "  # pir_unit_per_period
                              "40 42 0F 00 00 00 00 00 "  # cir_period
                              "E2 04 00 00 00 00 00 00 "  # cir_unit_per_period
                              "6A 18 00 00 00 00 00 00 "  # pbs
                              "6A 18 00 00 00 00 00 00 "  # cbs
                              "6A 18 00 00 00 00 00 00 "  # pbs_left
                              "6A 18 00 00 00 00 00 00 "  # cbs_left
                              "00 00 00 00 00 00 00 00 "  # time_p
                              "00 00 00 00 00 00 00 00 "  # time_c
                              "00 00 00 00 00 00 00 00")  # Spin lock

        testutils.send_packet(self, PORT0, pkt)
        testutils.verify_packet(self, pkt, PORT1)
        # Expecting pbs_left, cbs_left 6250 B - 100 B = 6150 B -> 18 06
        self.verify_map_entry(name="ingress_tbl_fwd", key="hex 04 00 00 00",
                              expected_value="hex "
                                             "01 00 00 00 05 00 00 00 "
                                             "40 42 0F 00 00 00 00 00 "
                                             "E2 04 00 00 00 00 00 00 "
                                             "40 42 0F 00 00 00 00 00 "
                                             "E2 04 00 00 00 00 00 00 "
                                             "6A 18 00 00 00 00 00 00 "
                                             "6A 18 00 00 00 00 00 00 "
                                             "06 18 00 00 00 00 00 00 "  # pbs_left
                                             "06 18 00 00 00 00 00 00 "  # cbs_left
                                             "00 00 00 00 00 00 00 00 "
                                             "00 00 00 00 00 00 00 00 "
                                             "40 42 0F 00 00 00 00 00 "  # second meter
                                             "E2 04 00 00 00 00 00 00 "
                                             "40 42 0F 00 00 00 00 00 "
                                             "E2 04 00 00 00 00 00 00 "
                                             "6A 18 00 00 00 00 00 00 "
                                             "6A 18 00 00 00 00 00 00 "
                                             "06 18 00 00 00 00 00 00 "  # pbs_left
                                             "06 18 00 00 00 00 00 00 "  # cbs_left
                                             "00 00 00 00 00 00 00 00 "
                                             "00 00 00 00 00 00 00 00 "
                                             "00 00 00 00 00 00 00 00",
                              mask=two_meters_value_mask)

    def tearDown(self):
        self.remove_maps(["ingress_tbl_fwd"])
        super(DirectTwoMetersPSATest, self).tearDown()


class DirectAndCounterMeterPSATest(P4EbpfTest):
    """
    Test Direct Meter with Direct Counter.
    Send 100 B packet and verify if there is 100 tokens less left.
    """

    p4_file_path = "samples/p4testdata/meters-direct-and-counter.p4"

    def runTest(self):
        pkt = testutils.simple_ip_packet()

        # cir, pir -> 10 Mb/s, cbs, pbs -> bs (10 ms) -> 6250 B -> 18 6A
        # period 1ms -> 1250 B per period, 1ms -> 1e6 ns -> 0F 42 40, 1250 -> 04 E2
        self.update_map(name="ingress_tbl_fwd", key="hex 04 00 00 00",
                        value="hex "
                              "01 00 00 00 05 00 00 00 "  # action id | egress port
                              "00 00 00 00 00 00 00 00 "  # counter packets and padding
                              "40 42 0F 00 00 00 00 00 "  # pir_period
                              "E2 04 00 00 00 00 00 00 "  # pir_unit_per_period
                              "40 42 0F 00 00 00 00 00 "  # cir_period
                              "E2 04 00 00 00 00 00 00 "  # cir_unit_per_period
                              "6A 18 00 00 00 00 00 00 "  # pbs
                              "6A 18 00 00 00 00 00 00 "  # cbs
                              "6A 18 00 00 00 00 00 00 "  # pbs_left
                              "6A 18 00 00 00 00 00 00 "  # cbs_left
                              "00 00 00 00 00 00 00 00 "  # time_p
                              "00 00 00 00 00 00 00 00 "  # time_c
                              "00 00 00 00 00 00 00 00")  # Spin lock

        testutils.send_packet(self, PORT0, pkt)
        testutils.verify_packet(self, pkt, PORT1)
        # Expecting pbs_left, cbs_left 6250 B - 100 B = 6150 B -> 18 06
        self.verify_map_entry(name="ingress_tbl_fwd", key="hex 04 00 00 00",
                              expected_value="hex "
                                             "01 00 00 00 05 00 00 00 "
                                             "01 00 00 00 00 00 00 00 "  # counter packets and padding
                                             "40 42 0F 00 00 00 00 00 "
                                             "E2 04 00 00 00 00 00 00 "
                                             "40 42 0F 00 00 00 00 00 "
                                             "E2 04 00 00 00 00 00 00 "
                                             "6A 18 00 00 00 00 00 00 "
                                             "6A 18 00 00 00 00 00 00 "
                                             "06 18 00 00 00 00 00 00 "  # pbs_left
                                             "06 18 00 00 00 00 00 00 "  # cbs_left
                                             "00 00 00 00 00 00 00 00 "
                                             "00 00 00 00 00 00 00 00 "
                                             "00 00 00 00 00 00 00 00",
                              mask=meter_value_mask)

    def tearDown(self):
        self.remove_maps(["ingress_tbl_fwd"])
        super(DirectAndCounterMeterPSATest, self).tearDown()