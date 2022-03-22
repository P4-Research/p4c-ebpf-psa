import os
import logging
import json
import shlex
import subprocess
import ptf
import ptf.testutils as testutils

from ptf.base_tests import BaseTest

logger = logging.getLogger('eBPFTest')
if not len(logger.handlers):
    logger.addHandler(logging.StreamHandler())

TEST_PIPELINE_ID = 999
TEST_PIPELINE_MOUNT_PATH = "/sys/fs/bpf/pipeline{}".format(TEST_PIPELINE_ID)
PIPELINE_MAPS_MOUNT_PATH = "{}/maps".format(TEST_PIPELINE_MOUNT_PATH)


def tc_only(cls):
    if cls.is_xdp_test(cls):
        cls.skip = True
        cls.skip_reason = "not supported by XDP"
    return cls


def xdp2tc_head_not_supported(cls):
    if cls.xdp2tc_mode(cls) == 'head':
        cls.skip = True
        cls.skip_reason = "not supported for xdp2tc=head"
    return cls


def table_caching_only(cls):
    if not cls.is_table_caching_test(cls):
        cls.skip = True
        cls.skip_reason = "table caching test"
    return cls


class EbpfTest(BaseTest):
    skip = False
    skip_reason = ''
    switch_ns = 'test'
    test_prog_image = 'generic.o'  # default, if test case not specify program

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
            logger.info("STDOUT: %s", stdout_data.decode("utf-8"))
            logger.info("STDERR: %s", stderr_data.decode("utf-8"))
            if do_fail:
                self.fail("Command failed (see above for details): {}".format(str(do_fail)))
        return process.returncode, stdout_data, stderr_data

    def add_port(self, dev):
        self.exec_ns_cmd("psabpf-ctl add-port pipe {} dev {}".format(TEST_PIPELINE_ID, dev))
        if dev.startswith("eth") and self.is_xdp_test():
            self.exec_cmd("ip link set dev s1-{} xdp pinned {}/{}".format(dev, TEST_PIPELINE_MOUNT_PATH, "xdp_redirect_dummy_sec"))

    def del_port(self, dev):
        self.exec_ns_cmd("psabpf-ctl del-port pipe {} dev {}".format(TEST_PIPELINE_ID, dev))
        if dev.startswith("eth"):
            self.exec_cmd("psabpf-ctl del-port pipe {} dev s1-{}".format(TEST_PIPELINE_ID, dev))

    def remove_map(self, name):
        self.exec_ns_cmd("rm {}/maps/{}".format(TEST_PIPELINE_MOUNT_PATH, name))

    def remove_maps(self, maps):
        for map in maps:
            self.remove_map(map)

    def update_map(self, name, key, value, map_in_map=False):
        if map_in_map:
            value = "pinned {}/{} any".format(PIPELINE_MAPS_MOUNT_PATH, value)
        self.exec_ns_cmd("bpftool map update pinned {}/{} key {} value {}".format(
            PIPELINE_MAPS_MOUNT_PATH, name, key, value))

    def read_map(self, name, key):
        cmd = "bpftool -j map lookup pinned {}/{} key {}".format(PIPELINE_MAPS_MOUNT_PATH, name, key)
        _, stdout, _ = self.exec_ns_cmd(cmd, "Failed to read map {}".format(name))
        value = [format(int(v, 0), '02x') for v in json.loads(stdout)['value']]
        return ' '.join(value)

    def verify_map_entry(self, name, key, expected_value, mask=None):
        value = self.read_map(name, key)

        if "hex" in expected_value:
            expected_value = expected_value.replace("hex ", "")

        expected_value = "0x" + expected_value
        expected_value = expected_value.replace(" ", "")
        value = "0x" + value
        value = value.replace(" ", "")

        if mask:
            expected_value = int(expected_value, 0) & mask
            value = int(value, 0) & mask

        if expected_value != value:
            self.fail("Map {} key {} does not have correct value. Expected {}; got {}"
                      .format(name, key, expected_value, value))

    def xdp2tc_mode(self):
        return testutils.test_param_get('xdp2tc')

    def is_xdp_test(self):
        return testutils.test_param_get('xdp') == 'True'

    def is_table_caching_test(self):
        return testutils.test_param_get('table_caching') == 'True'

    def is_pipeline_opt_enabled(self):
        return testutils.test_param_get('pipeline_optimization') == 'True'

    def is_trace_logs_enabled(self):
        return testutils.test_param_get('trace') == 'True'

    def setUp(self):
        super(EbpfTest, self).setUp()
        self.dataplane = ptf.dataplane_instance
        self.dataplane.flush()
        logger.info("\nUsing test params: %s", testutils.test_params_get())
        if "namespace" in testutils.test_params_get():
            self.switch_ns = testutils.test_param_get("namespace")
        self.interfaces = testutils.test_param_get("interfaces").split(",")

        self.exec_ns_cmd("psabpf-ctl pipeline load id {} {}".format(TEST_PIPELINE_ID, self.test_prog_image), "Can't load programs into eBPF subsystem")

        for intf in self.interfaces:
            if intf == "psa_recirc" and self.is_xdp_test():
                continue
            self.add_port(dev=intf)

    def tearDown(self):
        for intf in self.interfaces:
            self.del_port(intf)
        self.exec_ns_cmd("psabpf-ctl pipeline unload id {}".format(TEST_PIPELINE_ID))
        super(EbpfTest, self).tearDown()


class P4EbpfTest(EbpfTest):
    """
    Similar to EbpfTest, but generates BPF bytecode from a P4 program.
    """

    p4_file_path = ""

    def setUp(self):
        if self.skip:
            self.skipTest(self.skip_reason)

        if not os.path.exists(self.p4_file_path):
            self.fail("P4 program not found, no such file.")

        if not os.path.exists("ptf_out"):
            os.makedirs("ptf_out")

        head, tail = os.path.split(self.p4_file_path)
        filename = tail.split(".")[0]
        self.test_prog_image = os.path.join("ptf_out", filename + ".o")

        p4args = "--Wdisable=unused --max-ternary-masks 3"
        if "xdp2tc" in testutils.test_params_get():
            p4args += " --xdp2tc=" + testutils.test_param_get("xdp2tc")
        if self.is_xdp_test():
            p4args += " --xdp"
        if self.is_pipeline_opt_enabled():
            p4args += " --pipeline-opt"
        if self.is_table_caching_test():
            p4args += " --table-caching"
        if self.is_trace_logs_enabled():
            p4args += " --trace"

        logger.info("P4ARGS=" + p4args)
        self.exec_cmd("make -f ../runtime/kernel.mk BPFOBJ={output} P4FILE={p4file} "
                      "ARGS=\"{cargs}\" P4C=p4c-ebpf P4ARGS=\"{p4args}\" psa".format(
                            output=self.test_prog_image,
                            p4file=self.p4_file_path,
                            cargs="-DPSA_PORT_RECIRCULATE=2",
                            p4args=p4args),
                      "Compilation error")
        super(P4EbpfTest, self).setUp()

    def tearDown(self):
        super(P4EbpfTest, self).tearDown()

    def clone_session_create(self, id):
        self.exec_ns_cmd("psabpf-ctl clone-session create pipe {} id {}".format(TEST_PIPELINE_ID, id))

    def clone_session_add_member(self, clone_session, egress_port, instance=1, cos=0):
        self.exec_ns_cmd("psabpf-ctl clone-session add-member pipe {} id {} egress-port {} instance {} cos {}".format(
            TEST_PIPELINE_ID, clone_session, egress_port, instance, cos))

    def clone_session_delete(self, id):
        self.exec_ns_cmd("psabpf-ctl clone-session delete pipe {} id {}".format(TEST_PIPELINE_ID, id))

    def multicast_group_create(self, group):
        self.exec_ns_cmd("psabpf-ctl multicast-group create pipe {} id {}".format(TEST_PIPELINE_ID, group))

    def multicast_group_add_member(self, group, egress_port, instance=1):
        self.exec_ns_cmd("psabpf-ctl multicast-group add-member pipe {} id {} egress-port {} instance {}".format(
            TEST_PIPELINE_ID, group, egress_port, instance))

    def multicast_group_delete(self, group):
        self.exec_ns_cmd("psabpf-ctl multicast-group delete pipe {} id {}".format(TEST_PIPELINE_ID, group))

    def _table_create_str_from_data(self, data, counters, meters):
        s = ""
        if data or counters or meters:
            s = s + "data "
            if data:
                for d in data:
                    s = s + "{} ".format(d)
            if counters:
                for k, v in counters.items():
                    s = s + "counter {} {} ".format(k, v)
            if meters:
                for k, v in meters.items():
                    s = s + "meter {} {} ".format(k, v)
        return s

    def _table_create_str_from_key(self, keys):
        s = ""
        if keys:
            s = "key "
            for k in keys:
                s = s + "{} ".format(k)
        return s

    def table_write(self, method, table, keys, action=0, data=None, priority=None, references=None,
                    counters=None, meters=None):
        """
        Use table_add or table_update instead of this method
        """
        cmd = "psabpf-ctl table {} pipe {} {} ".format(method, TEST_PIPELINE_ID, table)
        if references:
            data = references
            cmd = cmd + "ref "
        else:
            cmd = cmd + "id {} ".format(action)
        cmd = cmd + self._table_create_str_from_key(keys=keys)
        cmd = cmd + self._table_create_str_from_data(data=data, counters=counters, meters=meters)
        if priority:
            cmd = cmd + "priority {}".format(priority)
        self.exec_ns_cmd(cmd, "Table {} failed".format(method))

    def table_add(self, table, keys, action=0, data=None, priority=None, references=None,
                  counters=None, meters=None):
        self.table_write(method="add", table=table, keys=keys, action=action, data=data,
                         priority=priority, references=references, counters=counters, meters=meters)

    def table_update(self, table, keys, action=0, data=None, priority=None, references=None,
                     counters=None, meters=None):
        self.table_write(method="update", table=table, keys=keys, action=action, data=data,
                         priority=priority, references=references, counters=counters, meters=meters)

    def table_delete(self, table, keys=None):
        cmd = "psabpf-ctl table delete pipe {} {} ".format(TEST_PIPELINE_ID, table)
        if keys:
            cmd = cmd + "key "
            for k in keys:
                cmd = cmd + "{} ".format(k)
        self.exec_ns_cmd(cmd, "Table delete failed")

    def table_set_default(self, table, action=0, data=None, counters=None, meters=None):
        cmd = "psabpf-ctl table default set pipe {} {} id {} ".format(TEST_PIPELINE_ID, table, action)
        cmd = cmd + self._table_create_str_from_data(data=data, counters=counters, meters=meters)
        self.exec_ns_cmd(cmd, "Table set default entry failed")

    def table_get(self, table, keys, indirect=False):
        cmd = "psabpf-ctl table get pipe {} {} ".format(TEST_PIPELINE_ID, table)
        if indirect:
            cmd = cmd + "ref "
        cmd = cmd + self._table_create_str_from_key(keys=keys)
        _, stdout, _ = self.exec_ns_cmd(cmd, "Table set default entry failed")
        return json.loads(stdout)[table]

    def table_verify(self, table, keys, action=0, priority=None, data=None, references=None,
                     counters=None, meters=None):
        json_data = self.table_get(table=table, keys=keys, indirect=references)
        entries = json_data["entries"]
        if len(entries) != 1:
            self.fail("Expected 1 table entry to verify")
        entry = entries[0]

        if action is not None:
            if action != entry["action"]["id"]:
                self.fail("Invalid action ID: expected {}, got {}".format(action, entry["action"]["id"]))
        if priority is not None:
            if priority != entry["priority"]:
                self.fail("Invalid priority: expected {}, got {}".format(priority, entry["priority"]))
        if data:
            action_params = entry["action"]["parameters"]
            if len(action_params) != len(data):
                self.fail("Invalid number of action parameters: expected {}, got {}".format(len(data), len(action_params)))
            for k, v in enumerate(data):
                if v != int(action_params[k]["value"], 0):
                    self.fail("Invalid action parameter {} (id {}): expected {}, got {}".
                              format(action_params[k]["name"], k, v, int(action_params[k]["value"], 0)))
        if references:
            pass  # TODO
        if counters:
            for k, v in counters.items():
                type = json_data["DirectCounter"][k]["type"]
                entry_value = entry["DirectCounter"][k]
                self._do_counter_verify(bytes=v.get("bytes", None), packets=v.get("packets", None),
                                        entry_value=entry_value, counter_type=type)
        if meters:
            pass  # TODO

    def meter_update(self, name, index, pir, pbs, cir, cbs):
        cmd = "psabpf-ctl meter update pipe {} {} " \
              "index {} {}:{} {}:{}".format(TEST_PIPELINE_ID, name,
                                            index, pir, pbs, cir, cbs)
        self.exec_ns_cmd(cmd, "Meter update failed")

    def action_selector_add_action(self, selector, action, data=None):
        cmd = "psabpf-ctl action-selector add_member pipe {} {} id {}".format(TEST_PIPELINE_ID, selector, action)
        if data:
            cmd = cmd + " data"
            for d in data:
                cmd = cmd + " {}".format(d)
        _, stdout, _ = self.exec_ns_cmd(cmd, "ActionSelector add_member failed")
        return int(stdout)

    def action_selector_create_empty_group(self, selector):
        cmd = "psabpf-ctl action-selector create_group pipe {} {}".format(TEST_PIPELINE_ID, selector)
        _, stdout, _ = self.exec_ns_cmd(cmd, "ActionSelector create_group failed")
        return int(stdout)

    def action_selector_add_member_to_group(self, selector, group_ref, member_ref):
        cmd = "psabpf-ctl action-selector add_to_group pipe {} {} {} to {}"\
            .format(TEST_PIPELINE_ID, selector, member_ref, group_ref)
        self.exec_ns_cmd(cmd, "ActionSelector add_to_group failed")

    def digest_get(self, name):
        cmd = "psabpf-ctl digest get pipe {} {}".format(TEST_PIPELINE_ID, name)
        _, stdout, _ = self.exec_ns_cmd(cmd, "Digest get failed")
        return json.loads(stdout)['Digest'][name]['digests']

    def counter_get(self, name, keys=None):
        key_str = self._table_create_str_from_key(keys=keys)
        cmd = "psabpf-ctl counter get pipe {} {} {}".format(TEST_PIPELINE_ID, name, key_str)
        _, stdout, _ = self.exec_ns_cmd(cmd, "Counter get failed")
        return json.loads(stdout)['Counter'][name]

    def _do_counter_verify(self, bytes, packets, entry_value, counter_type):
        expected_type = ""
        if packets is not None:
            expected_type = "PACKETS"
        if bytes is not None:
            if packets is not None:
                expected_type = expected_type + "_AND_"
            expected_type = expected_type + "BYTES"
        if expected_type != counter_type:
            self.fail("Invalid counter type, expected: \"{}\", got \"{}\"".format(expected_type, counter_type))
        if bytes is not None:
            counter_bytes = int(entry_value["bytes"], 0)
            if counter_bytes != bytes:
                self.fail("Invalid counter bytes, expected {}, got {}".format(bytes, counter_bytes))
        if packets is not None:
            counter_packets = int(entry_value["packets"], 0)
            if counter_packets != packets:
                self.fail("Invalid counter packets, expected {}, got {}".format(packets, counter_packets))

    def counter_verify(self, name, keys, bytes=None, packets=None):
        counter = self.counter_get(name, keys=keys)
        entries = counter["entries"]
        if len(entries) != 1:
            self.fail("expected one Counter entry")
        entry = entries[0]
        self._do_counter_verify(bytes=bytes, packets=packets, entry_value=entry["value"], counter_type=counter["type"])
