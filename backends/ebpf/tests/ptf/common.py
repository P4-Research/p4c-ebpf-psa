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
            logger.info("STDOUT: %s", stdout_data.decode("utf-8"))
            logger.info("STDERR: %s", stderr_data.decode("utf-8"))
            if do_fail:
                self.fail("Command failed (see above for details): {}".format(str(do_fail)))
        return process.returncode, stdout_data, stderr_data

    def add_port(self, dev):
        self.exec_ns_cmd("psabpf-ctl pipeline add-port id {} {}".format(TEST_PIPELINE_ID, dev))

    def del_port(self, dev):
        self.exec_ns_cmd("psabpf-ctl pipeline del-port id {} {}".format(TEST_PIPELINE_ID, dev))

    def remove_map(self, name):
        self.exec_ns_cmd("rm {}/maps/{}".format(TEST_PIPELINE_MOUNT_PATH, name))

    def remove_maps(self, maps):
        for map in maps:
            self.remove_map(map)

    def create_map(self, name, type, key_size, value_size, max_entries):
        self.exec_ns_cmd("bpftool map create {}/{} type "
                         "{} key {} value {} entries {} name {}".format(
            PIPELINE_MAPS_MOUNT_PATH, name, type, key_size, value_size, max_entries, name))

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

    def verify_map_entry(self, name, key, expected_value):
        value = self.read_map(name, key)
        if expected_value != value:
            self.fail("Map {} key {} does not have correct value. Expected {}; got {}"
                      .format(name, key, expected_value, value))

    def setUp(self):
        super(EbpfTest, self).setUp()
        self.dataplane = ptf.dataplane_instance
        self.dataplane.flush()

        if "namespace" in testutils.test_params_get():
            self.switch_ns = testutils.test_param_get("namespace")
        logger.info("Using namespace: %s", self.switch_ns)
        self.interfaces = testutils.test_param_get("interfaces").split(",")
        logger.info("Using interfaces: %s", str(self.interfaces))

        self.exec_ns_cmd("psabpf-ctl pipeline load id {} {}".format(TEST_PIPELINE_ID, self.test_prog_image), "Can't load programs into eBPF subsystem")

        for intf in self.interfaces:
            self.add_port(dev=intf)

        if self.ctool_file_path:
            head, tail = os.path.split(self.ctool_file_path)
            filename = tail.split(".")[0]
            so_file_path = head + "/" + filename + ".so"
            cmd = ["clang", "-I../runtime/usr/include", "-L../runtime/usr/lib64",
                   "-fPIC", "-l", "bpf", "-shared", "-o", so_file_path, self.ctool_file_path]
            self.exec_cmd(cmd, "Ctool compilation error")
            self.so_file_path = so_file_path

    def tearDown(self):
        for intf in self.interfaces:
            self.del_port(intf)
        for filename in os.listdir("{}".format(PIPELINE_MAPS_MOUNT_PATH)):
            if not os.path.isdir(filename):
                self.remove_map(filename)
        self.exec_ns_cmd("rm -rf {}".format(TEST_PIPELINE_MOUNT_PATH))
        super(EbpfTest, self).tearDown()


class P4EbpfTest(EbpfTest):
    """
    Similar to EbpfTest, but generates BPF bytecode from a P4 program.
    """

    p4_file_path = ""

    def table_add(self, table, keys, action, data=None):
        cmd = "psabpf-ctl table add {} id {} key ".format(table, action)
        for k in keys:
            cmd = cmd + "{} ".format(k)
        if data:
            cmd = cmd + "data "
            for d in data:
                cmd = cmd + "{} ".format(d)
        self.exec_ns_cmd(cmd, "Table add failed")

    def setUp(self):
        if not os.path.exists(self.p4_file_path):
            self.fail("P4 program not found, no such file.")

        if not os.path.exists("ptf_out"):
            os.makedirs("ptf_out")

        head, tail = os.path.split(self.p4_file_path)
        filename = tail.split(".")[0]
        self.test_prog_image = os.path.join("ptf_out", filename + ".o")
        self.exec_cmd("make -f ../runtime/kernel.mk BPFOBJ={output} P4FILE={p4file} "
                      "ARGS=\"{cargs}\" P4C=p4c-ebpf P4ARGS=\"{p4args}\" psa".format(
                            output=self.test_prog_image,
                            p4file=self.p4_file_path,
                            cargs="-DPSA_PORT_RECIRCULATE=2",
                            p4args="--trace"),
                      "Compilation error")

        super(P4EbpfTest, self).setUp()

    def tearDown(self):
        self.remove_map("clone_session_tbl")
        self.remove_map("multicast_grp_tbl")
        super(P4EbpfTest, self).tearDown()

    def clone_session_create(self, id):
        self.exec_ns_cmd("psabpf-ctl clone-session create pipe {} id {}".format(TEST_PIPELINE_ID, id))\

    def clone_session_add_member(self, clone_session, egress_port, instance=1, cos=0):
        self.exec_ns_cmd("psabpf-ctl clone-session add-member pipe {} id {} egress-port {} instance {} cos {}".format(
            TEST_PIPELINE_ID, clone_session, egress_port, instance, cos))

    def clone_session_delete(self, id):
        self.exec_ns_cmd("psabpf-ctl clone-session delete pipe {} id {}".format(TEST_PIPELINE_ID, id))