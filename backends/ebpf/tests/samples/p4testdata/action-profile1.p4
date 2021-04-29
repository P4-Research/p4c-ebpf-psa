#include <core.p4>
#include <psa.p4>

struct EMPTY { };

typedef bit<48>  EthernetAddress;

header ethernet_t {
    EthernetAddress dstAddr;
    EthernetAddress srcAddr;
    bit<16>         etherType;
}

struct headers {
    ethernet_t eth;
}

parser MyIP(packet_in buffer, out headers hdr, inout EMPTY bp,
            in psa_ingress_parser_input_metadata_t c, in EMPTY d, in EMPTY e) {
    state start {
        buffer.extract(hdr.eth);
        transition accept;
    }
}

parser MyEP(packet_in buffer, out EMPTY a, inout EMPTY b,
            in psa_egress_parser_input_metadata_t c, in EMPTY d, in EMPTY e, in EMPTY f) {
    state start {
        transition accept;
    }
}

control MyIC(inout headers a, inout EMPTY bc,
             in psa_ingress_input_metadata_t c, inout psa_ingress_output_metadata_t ostd) {

    ActionProfile(1024) ap;
    action a1(bit<48> param) { a.eth.dstAddr = param; }
    action a2(bit<16> param) { a.eth.etherType = param; }
    table tbl {
        key = {
            a.eth.srcAddr : exact;
        }
        actions = { NoAction; a1; a2; }
        psa_implementation = ap;
    }

    apply {
        tbl.apply();
        send_to_port(ostd, (PortId_t) 5);
    }
}

control MyEC(inout EMPTY a, inout EMPTY b,
    in psa_egress_input_metadata_t c, inout psa_egress_output_metadata_t d) {
    apply { }
}

control MyID(packet_out buffer, out EMPTY a, out EMPTY b, out EMPTY c,
    inout headers d, in EMPTY e, in psa_ingress_output_metadata_t f) {
    apply {
        buffer.emit(d.eth);
    }
}

control MyED(packet_out buffer, out EMPTY a, out EMPTY b, inout EMPTY c, in EMPTY d,
    in psa_egress_output_metadata_t e, in psa_egress_deparser_input_metadata_t f) {
    apply { }
}

IngressPipeline(MyIP(), MyIC(), MyID()) ip;
EgressPipeline(MyEP(), MyEC(), MyED()) ep;

PSA_Switch(ip, PacketReplicationEngine(), ep, BufferingQueueingEngine()) main;
