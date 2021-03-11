#include <core.p4>
#include <psa.p4>

typedef bit<48>  EthernetAddress;

header ethernet_t {
    EthernetAddress dstAddr;
    EthernetAddress srcAddr;
    bit<16>         etherType;
}

header ipv4_t {
    bit<4>  version;
    bit<4>  ihl;
    bit<8>  diffserv;
    bit<16> totalLen;
    bit<16> identification;
    bit<3>  flags;
    bit<13> fragOffset;
    bit<8>  ttl;
    bit<8>  protocol;
    bit<16> hdrChecksum;
    bit<32> srcAddr;
    bit<32> dstAddr;
}

header clone_i2e_metadata_t {
}

struct empty_metadata_t {
}

struct metadata {
    bit<16> checksum;
}

struct headers {
    ethernet_t ethernet;
    ipv4_t     ipv4;
}

parser IngressParserImpl(
    packet_in buffer,
    out headers parsed_hdr,
    inout metadata user_meta,
    in psa_ingress_parser_input_metadata_t istd,
    in empty_metadata_t resubmit_meta,
    in empty_metadata_t recirculate_meta)
{
    InternetChecksum() ck;
    
    state start {
        transition parse_ethernet;
    }
    state parse_ethernet {
        buffer.extract(parsed_hdr.ethernet);
        transition select(parsed_hdr.ethernet.etherType) {
            0x0800: parse_ipv4;
            default: accept;
        }
    }
    state parse_ipv4 {
        buffer.extract(parsed_hdr.ipv4);
        ck.clear();
        ck.add({
            /* 16-bit word 0 */     parsed_hdr.ipv4.version, parsed_hdr.ipv4.ihl, parsed_hdr.ipv4.diffserv,
            /* 16-bit word 1 */     parsed_hdr.ipv4.totalLen,
            /* 16-bit word 2 */     parsed_hdr.ipv4.identification,
            /* 16-bit word 3 */     parsed_hdr.ipv4.flags, parsed_hdr.ipv4.fragOffset,
            /* 16-bit word 4 */     parsed_hdr.ipv4.ttl, parsed_hdr.ipv4.protocol,
            /* 16-bit word 5 skip parsed_hdr.ipv4.hdrChecksum, */
            /* 16-bit words 6-7 */  parsed_hdr.ipv4.srcAddr,
            /* 16-bit words 8-9 */  parsed_hdr.ipv4.dstAddr
            });
        user_meta.checksum = ck.get();
        transition accept;
    }
}


control ingress(inout headers hdr,
                inout metadata user_meta,
                in  psa_ingress_input_metadata_t  istd,
                inout psa_ingress_output_metadata_t ostd)
{
    apply {
        if (hdr.ipv4.isValid()){
            if (user_meta.checksum == hdr.ipv4.hdrChecksum) {
                ostd.drop = false;
                ostd.egress_port = (PortId_t) 5;
            }
        }
    }
}

control IngressDeparserImpl(
    packet_out packet,
    out clone_i2e_metadata_t clone_i2e_meta,
    out empty_metadata_t resubmit_meta,
    out metadata normal_meta,
    inout headers parsed_hdr,
    in metadata meta,
    in psa_ingress_output_metadata_t istd)
{
    InternetChecksum() ck;

    apply {
        ck.set_state(parsed_hdr.ipv4.hdrChecksum);
        ck.subtract({/* 16-bit word 4 */ parsed_hdr.ipv4.ttl, parsed_hdr.ipv4.protocol});
        parsed_hdr.ipv4.hdrChecksum = ck.get();

        packet.emit(parsed_hdr.ethernet);
        packet.emit(parsed_hdr.ipv4);
    }
}

parser EgressParserImpl(
    packet_in buffer,
    out headers parsed_hdr,
    inout metadata user_meta,
    in psa_egress_parser_input_metadata_t istd,
    in metadata normal_meta,
    in clone_i2e_metadata_t clone_i2e_meta,
    in empty_metadata_t clone_e2e_meta)
{
    state start {
        transition parse_ethernet;
    }
    state parse_ethernet {
        buffer.extract(parsed_hdr.ethernet);
        transition parse_ipv4;
    }
    state parse_ipv4 {
        buffer.extract(parsed_hdr.ipv4);
        transition accept;
    }
}

control egress(inout headers hdr,
               inout metadata user_meta,
               in  psa_egress_input_metadata_t  istd,
               inout psa_egress_output_metadata_t ostd)
{
    apply {
        ostd.drop = false;
        hdr.ipv4.ttl = hdr.ipv4.ttl - 2;
    }
}

control EgressDeparserImpl(
    packet_out packet,
    out empty_metadata_t clone_e2e_meta,
    out empty_metadata_t recirculate_meta,
    inout headers parsed_hdr,
    in metadata meta,
    in psa_egress_output_metadata_t istd,
    in psa_egress_deparser_input_metadata_t edstd)
{
    InternetChecksum() ck;
    apply {
        ck.set_state(parsed_hdr.ipv4.hdrChecksum);
        ck.add({/* 16-bit word 4 */ parsed_hdr.ipv4.ttl, parsed_hdr.ipv4.protocol});
        parsed_hdr.ipv4.hdrChecksum = ck.get();

        packet.emit(parsed_hdr.ethernet);
        packet.emit(parsed_hdr.ipv4);
    }
}

IngressPipeline(IngressParserImpl(),
                ingress(),
                IngressDeparserImpl()) ip;

EgressPipeline(EgressParserImpl(),
               egress(),
               EgressDeparserImpl()) ep;

PSA_Switch(ip, PacketReplicationEngine(), ep, BufferingQueueingEngine()) main;
