#include <core.p4>
#define V1MODEL_VERSION 20180101
#include <v1model.p4>

struct headers_t {
}

struct metadata_t {
}

parser ParserImpl(packet_in packet, out headers_t hdr, inout metadata_t meta, inout standard_metadata_t standard_metadata) {
    state start {
        transition accept;
    }
}

control DeparserImpl(packet_out packet, in headers_t hdr) {
    apply {
    }
}

control ingress(inout headers_t hdr, inout metadata_t meta, inout standard_metadata_t standard_metadata) {
    @name("ingress.standard_metadata") standard_metadata_t standard_metadata_0;
    @name(".send_to_cpu") action send_to_cpu_0() {
        standard_metadata_0 = standard_metadata;
        standard_metadata_0.egress_spec = 9w64;
        standard_metadata = standard_metadata_0;
    }
    @noWarn("unused") @name(".NoAction") action NoAction_1() {
    }
    @name("ingress.t0") table t0_0 {
        key = {
            standard_metadata.ingress_port: ternary @name("standard_metadata.ingress_port") ;
        }
        actions = {
            send_to_cpu_0();
            @defaultonly NoAction_1();
        }
        default_action = NoAction_1();
    }
    apply {
        t0_0.apply();
    }
}

control egress(inout headers_t hdr, inout metadata_t meta, inout standard_metadata_t standard_metadata) {
    apply {
    }
}

control verifyChecksum(inout headers_t hdr, inout metadata_t meta) {
    apply {
    }
}

control computeChecksum(inout headers_t hdr, inout metadata_t meta) {
    apply {
    }
}

V1Switch<headers_t, metadata_t>(ParserImpl(), verifyChecksum(), ingress(), egress(), computeChecksum(), DeparserImpl()) main;

