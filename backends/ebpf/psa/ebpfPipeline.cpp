#include "ebpfPipeline.h"
#include "backends/ebpf/ebpfParser.h"

namespace EBPF {

void EBPFPipeline::emit(CodeBuilder* builder) {
    cstring msgStr, varStr;
    // Ingress and egress has different variables which are pointers,
    // clearing is needed to not preserving them between pipelines
    control->codeGen->asPointerVariables.clear();
    builder->target->emitCodeSection(builder, sectionName);
    builder->emitIndent();
    builder->target->emitMain(builder, functionName, model.CPacketName.str());
    builder->spc();
    builder->blockStart();
    emitGlobalMetadataInitializer(builder);
    emitHeaderInstances(builder);
    emitLocalVariables(builder);
    emitPSAControlDataTypes(builder);
    msgStr = Util::printf_format("%s parser: parsing new packet, path=%%d", sectionName);
    varStr = Util::printf_format("%s.packet_path", control->inputStandardMetadata->name.name);
    builder->target->emitTraceMessage(builder, msgStr.c_str(), 1, varStr.c_str());
    parser->emit(builder);
    builder->emitIndent();
    builder->append(IR::ParserState::accept);
    builder->append(":");
    builder->newline();
    builder->emitIndent();
    builder->blockStart();
    // TODO: add more info: packet length, ingress port
    msgStr = Util::printf_format("%s control: packet processing started", sectionName);
    builder->target->emitTraceMessage(builder, msgStr.c_str());
    control->emit(builder);
    builder->blockEnd(true);
    msgStr = Util::printf_format("%s control: packet processing finished", sectionName);
    builder->target->emitTraceMessage(builder, msgStr.c_str());
    builder->emitIndent();
    builder->blockStart();
    msgStr = Util::printf_format("%s deparser: packet deparsing started", sectionName);
    builder->target->emitTraceMessage(builder, msgStr.c_str());
    deparser->emit(builder);
    builder->blockEnd(true);
    msgStr = Util::printf_format("%s deparser: packet deparsing finished", sectionName);
    builder->target->emitTraceMessage(builder, msgStr.c_str());
    this->emitTrafficManager(builder);
    builder->blockEnd(true);
}

void EBPFPipeline::emitLocalVariables(CodeBuilder* builder) {
    builder->emitIndent();
    builder->appendFormat("unsigned %s = 0;", offsetVar.c_str());
    builder->appendFormat("unsigned %s_save = 0;", offsetVar.c_str());
    builder->newline();
    builder->emitIndent();
    builder->appendFormat("%s %s = %s;", errorType.c_str(), errorVar.c_str(),
                          P4::P4CoreLibrary::instance.noError.str());
    builder->newline();
    builder->emitIndent();
    builder->appendFormat("void* %s = %s;",
                          packetStartVar.c_str(),
                          builder->target->dataOffset(model.CPacketName.str()).c_str());
    builder->newline();
    builder->emitIndent();
    builder->appendFormat("void* %s = %s;",
                          packetEndVar.c_str(),
                          builder->target->dataEnd(model.CPacketName.str()).c_str());
    builder->newline();
    builder->emitIndent();
    builder->appendFormat("u32 %s = 0;", zeroKey.c_str());
    builder->newline();
    builder->emitIndent();
    builder->appendFormat("unsigned char %s;", byteVar.c_str());
    builder->newline();
}

void EBPFPipeline::emitHeaderInstances(CodeBuilder* builder) {
    builder->emitIndent();
    parser->headerType->declare(builder, parser->headers->name.name, false);
    builder->append(" = ");
    parser->headerType->emitInitializer(builder);
    builder->endOfStatement(true);
}

void EBPFPipeline::emitGlobalMetadataInitializer(CodeBuilder *builder) {
    builder->emitIndent();
    builder->appendLine(
            "struct psa_global_metadata *meta = (struct psa_global_metadata *) skb->cb;");
}

// =====================EBPFIngressPipeline=============================
void EBPFIngressPipeline::emit(CodeBuilder *builder) {
    cstring msgStr;
    // firstly emit process() in-lined function and then the actual BPF section.
    builder->append("static __always_inline");
    builder->spc();
    builder->appendFormat(
            "int %s(SK_BUFF *%s, struct psa_ingress_output_metadata_t *%s, ",
            processFunctionName, model.CPacketName.str(),
            control->outputStandardMetadata->name.name);
    auto type = EBPFTypeFactory::instance->create(
            deparser->to<EBPFIngressDeparserPSA>()->resubmit_meta->type);
    type->declare(builder,
            deparser->to<EBPFIngressDeparserPSA>()->resubmit_meta->name.name,
            true);
    builder->append(")");
    builder->newline();
    builder->blockStart();
    emitGlobalMetadataInitializer(builder);

    // workaround to make TC protocol-independent, DO NOT REMOVE
    builder->emitIndent();
    // replace ether_type only if a packet comes from XDP
    builder->append("if (meta->packet_path == NORMAL) ");
    builder->blockStart();
    builder->emitIndent();
    builder->append("struct internal_metadata *md = "
                        "(struct internal_metadata *)(unsigned long)skb->data_meta;\n");
    builder->emitIndent();
    builder->append("if ((void *) ((struct internal_metadata *) md + 1) > "
                        "(void *)(long)skb->data) {\n"
                        "           return TC_ACT_SHOT;\n"
                        "       }\n");
    builder->append("    __u16 *ether_type = (__u16 *) ((void *) (long)skb->data + 12);\n"
                        "    if ((void *) ((__u16 *) ether_type + 1) > "
                        "    (void *) (long) skb->data_end) {\n"
                        "        return TC_ACT_SHOT;\n"
                        "    }\n"
                        "    *ether_type = md->pkt_ether_type;\n");
    builder->blockEnd(true);

    emitHeaderInstances(builder);

    builder->emitIndent();
    auto user_md_type = typeMap->getType(control->user_metadata);
    if (user_md_type == nullptr) {
        ::error("cannot emit user metadata");
    }
    auto userMetadataType = EBPFTypeFactory::instance->create(user_md_type);
    userMetadataType->declare(builder, control->user_metadata->name.name, false);
    builder->append(" = ");
    userMetadataType->emitInitializer(builder);
    builder->endOfStatement(true);

    emitLocalVariables(builder);
    msgStr = Util::printf_format("%s parser: parsing new packet, path=%%d", sectionName);
    builder->target->emitTraceMessage(builder, msgStr.c_str(), 1, "meta->packet_path");
    parser->emit(builder);
    builder->emitIndent();
    builder->append(IR::ParserState::accept);
    builder->append(":");
    builder->newline();
    builder->emitIndent();
    builder->blockStart();
    emitPSAControlDataTypes(builder);
    // TODO: add more info: packet length, ingress port
    msgStr = Util::printf_format("%s control: packet processing started", sectionName);
    builder->target->emitTraceMessage(builder, msgStr.c_str());
    control->emit(builder);
    builder->blockEnd(true);
    msgStr = Util::printf_format("%s control: packet processing finished", sectionName);
    builder->target->emitTraceMessage(builder, msgStr.c_str());
    builder->emitIndent();
    builder->blockStart();

    deparser->emit(builder);
    builder->blockEnd(true);
    builder->emitIndent();
    builder->appendLine("return TC_ACT_UNSPEC;");
    builder->blockEnd(true);

    builder->target->emitCodeSection(builder, sectionName);
    builder->emitIndent();
    builder->target->emitMain(builder, functionName, model.CPacketName.str());
    builder->spc();
    builder->blockStart();
    builder->emitIndent();

    builder->appendLine("struct psa_ingress_output_metadata_t ostd = {\n"
                        "            .drop = true,\n"
                        "    };");
    builder->newline();

    builder->emitIndent();
    deparser->to<EBPFIngressDeparserPSA>()->emitSharedMetadataInitializer(builder);

    builder->appendFormat("int i = 0;\n"
                        "    int ret = TC_ACT_UNSPEC;\n"
                        "    #pragma clang loop unroll(disable)\n"
                        "    for (i = 0; i < %d; i++) {\n"
                        "        ostd.resubmit = 0;\n"
                        "        ret = %s(skb, &ostd, &%s);\n"
                        "        if (ostd.drop == 1 || ostd.resubmit == 0) {\n"
                        "            break;\n"
                        "        }\n"
                        "    }", maxResubmitDepth, processFunctionName,
                        deparser->to<EBPFIngressDeparserPSA>()->resubmit_meta->name.name);
    builder->newline();

    builder->emitIndent();
    builder->appendLine("if (ret != TC_ACT_UNSPEC) {\n"
                        "        return ret;\n"
                        "    }");

    this->emitTrafficManager(builder);
    builder->blockEnd(true);
}

void EBPFIngressPipeline::emitPSAControlDataTypes(CodeBuilder *builder) {
    builder->emitIndent();
    builder->appendFormat("struct psa_ingress_input_metadata_t %s = {\n"
                        "            .ingress_port = skb->ifindex,\n"
                        "            .packet_path = meta->packet_path,\n"
                        "            .ingress_timestamp = skb->tstamp,\n"
                        "            .parser_error = %s,\n"
                        "    };", control->inputStandardMetadata->name.name, errorVar.c_str());
    builder->newline();
}

/*
 * The Traffic Manager for Ingress pipeline implements:
 * - Multicast handling
 * - send to port
 */
void EBPFIngressPipeline::emitTrafficManager(CodeBuilder *builder) {
    cstring mcast_grp = Util::printf_format("ostd.multicast_group");
    builder->emitIndent();
    builder->appendFormat("if (%s != 0) ", mcast_grp.c_str());
    builder->blockStart();
    builder->target->emitTraceMessage(builder,
        "IngressTM: Performing multicast, multicast_group=%u", 1, mcast_grp.c_str());
    builder->emitIndent();
    builder->appendFormat("do_packet_clones(%s, &multicast_grp_tbl, %s, NORMAL_MULTICAST, 2)",
                          contextVar.c_str(), mcast_grp.c_str());
    builder->endOfStatement(true);
    // In multicast mode, unicast packet is not send
    builder->target->emitTraceMessage(builder, "IngressTM: Multicast done, dropping source packet");
    builder->emitIndent();
    builder->appendFormat("return %s", builder->target->dropReturnCode());
    builder->endOfStatement(true);
    builder->blockEnd(true);

    builder->target->emitTraceMessage(builder,
            "IngressTM: Sending packet out of port %d", 1, "ostd.egress_port");
    builder->emitIndent();
    builder->appendLine("return bpf_redirect(ostd.egress_port, 0);");
}

// =====================EBPFEgressPipeline=============================
void EBPFEgressPipeline::emitPSAControlDataTypes(CodeBuilder* builder) {
    cstring outputMdVar, inputMdVar;
    outputMdVar = control->outputStandardMetadata->name.name;
    inputMdVar = control->inputStandardMetadata->name.name;

    builder->emitIndent();
    builder->appendFormat("struct psa_egress_input_metadata_t %s = {\n"
                          "        .class_of_service = meta->class_of_service,\n"
                          "        .egress_port = skb->ifindex,\n"
                          "        .packet_path = meta->packet_path,\n"
                          "        .instance = meta->instance,\n"
                          "        .egress_timestamp = skb->tstamp,\n"
                          "        .parser_error = %s,\n"
                          "    };", inputMdVar.c_str(), errorVar.c_str());
    builder->newline();
    builder->emitIndent();
    builder->appendFormat("if (%s.egress_port == PSA_PORT_RECIRCULATE) ", inputMdVar.c_str());
    builder->blockStart();
    builder->emitIndent();
    // To be conformant with psa.p4, where PSA_PORT_RECIRCULATE is constant
    builder->appendFormat("%s.egress_port = P4C_PSA_PORT_RECIRCULATE", inputMdVar.c_str());
    builder->endOfStatement(true);
    builder->blockEnd(true);

    builder->emitIndent();
    builder->appendFormat("struct psa_egress_output_metadata_t %s = {\n", outputMdVar.c_str());
    builder->appendLine("        .clone = false,\n"
                        "        .drop = false,\n"
                        "    };");

    builder->newline();
}

void EBPFEgressPipeline::emitTrafficManager(CodeBuilder *builder) {
    cstring varStr, outputMdVar, inputMdVar;
    outputMdVar = control->outputStandardMetadata->name.name;
    inputMdVar = control->inputStandardMetadata->name.name;

    // clone support
    builder->emitIndent();
    builder->appendFormat("if (%s.clone) ", outputMdVar.c_str());
    builder->blockStart();

    builder->emitIndent();
    builder->appendFormat("do_packet_clones(%s, &clone_session_tbl, %s.clone_session_id, "
                          "CLONE_E2E, 3)",
                          contextVar.c_str(), outputMdVar.c_str());
    builder->endOfStatement(true);
    builder->blockEnd(true);

    builder->newline();

    // drop support
    builder->emitIndent();
    builder->appendFormat("if (%s.drop) ", outputMdVar.c_str());
    builder->blockStart();
    builder->target->emitTraceMessage(builder, "EgressTM: Packet dropped due to metadata");
    builder->emitIndent();
    builder->appendFormat("return %s", builder->target->dropReturnCode().c_str());
    builder->endOfStatement(true);
    builder->blockEnd(true);

    builder->newline();

    // recirculation support
    // TODO: pass recirculation metadata
    // TODO: there is parameter type `psa_egress_deparser_input_metadata_t` to the deparser,
    //  maybe it should be used instead of `istd`?
    builder->emitIndent();
    builder->appendFormat("if (%s.egress_port == P4C_PSA_PORT_RECIRCULATE) ", inputMdVar.c_str());
    builder->blockStart();
    builder->target->emitTraceMessage(builder, "EgressTM: recirculating packet");
    builder->emitIndent();
    builder->appendFormat("meta->packet_path = RECIRCULATE");
    builder->endOfStatement(true);
    builder->emitIndent();
    builder->appendFormat("return bpf_redirect(PSA_PORT_RECIRCULATE, BPF_F_INGRESS)",
                          contextVar.c_str());
    builder->endOfStatement(true);
    builder->blockEnd(true);

    builder->newline();

    // normal packet to port
    varStr = Util::printf_format("%s->ifindex", contextVar);
    builder->target->emitTraceMessage(builder, "EgressTM: output packet to port %d",
                                      1, varStr.c_str());
    builder->emitIndent();
    builder->appendFormat("return %s", builder->target->forwardReturnCode());
    builder->endOfStatement(true);
}

}  // namespace EBPF