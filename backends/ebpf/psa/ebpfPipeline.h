#ifndef BACKENDS_EBPF_PSA_EBPFPIPELINE_H_
#define BACKENDS_EBPF_PSA_EBPFPIPELINE_H_

#include "ebpfPsaControl.h"
#include "backends/ebpf/ebpfProgram.h"
#include "ebpfPsaDeparser.h"

namespace EBPF {

/*
 * EBPFPipeline represents a single eBPF program in the TC/XDP hook.
 */
class EBPFPipeline : public EBPFProgram {
 public:
    const cstring name;
    cstring sectionName;
    cstring contextVar;
    cstring timestampVar, ifindexVar;
    cstring priorityVar, packetPathVar, pktInstanceVar;
    cstring compilerGlobalMetadata;

    EBPFControlPSA* control;
    EBPFDeparserPSA* deparser;

    EBPFPipeline(cstring name, const EbpfOptions& options, P4::ReferenceMap* refMap,
                 P4::TypeMap* typeMap) :
                 EBPFProgram(options, nullptr, refMap, typeMap, nullptr),
                             name(name) {
        sectionName = "classifier/" + name;
        functionName = name.replace("-", "_") + "_func";
        errorType = "ParserError_t";
        packetStartVar = cstring("pkt");
        contextVar = cstring("skb");
        lengthVar = cstring("pkt_len");
        endLabel = cstring("deparser");
        timestampVar = cstring("tstamp");
        ifindexVar = cstring("skb->ifindex");
        compilerGlobalMetadata = cstring("compiler_meta__");
        packetPathVar = compilerGlobalMetadata + cstring("->packet_path");
        pktInstanceVar = compilerGlobalMetadata + cstring("->instance");
        priorityVar = cstring("skb->priority");
    }

    virtual cstring dropReturnCode() {
        if (sectionName.startsWith("xdp")) {
            return "XDP_DROP";
        }

        // TC is the default hookpoint
        return "TC_ACT_SHOT";
    }
    virtual cstring forwardReturnCode() {
        if (sectionName.startsWith("xdp")) {
            return "XDP_PASS";
        }

        // TC is the default hookpoint
        return "TC_ACT_OK";
    }

    virtual void emit(CodeBuilder* builder) = 0;
    virtual void emitTrafficManager(CodeBuilder *builder) = 0;
    virtual void emitPSAControlInputMetadata(CodeBuilder* builder) = 0;
    virtual void emitPSAControlOutputMetadata(CodeBuilder* builder) = 0;

    /* Generates an instance of struct Headers_t and allocates it on the BPF program's stack. */
    void emitLocalHeaderInstances(CodeBuilder *builder);
    /* Generates a pointer to struct Headers_t and puts it on the BPF program's stack. */
    void emitLocalHeaderInstancesAsPointers(CodeBuilder *builder);
    /* Generates a pointer to struct hdr_md. The pointer is used to access data from per-CPU map. */
    void emitCPUMAPHeadersInitializers(CodeBuilder *builder);
    void emitCPUMAPHeaderInstances(CodeBuilder *builder);
    /* Generates an instance of struct Headers_t,
     * allocated either on stack or in the per-CPU map. */
    void emitHeaderInstances(CodeBuilder *builder) override;
    /* Generates a set of helper variables that are used during packet processing. */
    void emitLocalVariables(CodeBuilder* builder) override;
    /* Generates a pointer to skb->cb and maps it to
     * psa_global_metadata to access global metadata shared between pipelines. */
    void emitGlobalMetadataInitializer(CodeBuilder *builder);

    void emitLocalUserMetadataInstances(CodeBuilder *builder);
    void emitCPUMapUserMetadataInstance(CodeBuilder *builder);
    /* Generates and instance of user metadata for a pipeline,
     * allocated either on stack or in the per-CPU map. */
    void emitUserMetadataInstance(CodeBuilder *builder);
    void emitCPUMAPInitializers(CodeBuilder *builder);
    void emitHeadersFromCPUMAP(CodeBuilder* builder);
    void emitMetadataFromCPUMAP(CodeBuilder *builder);

    virtual void emitPacketLength(CodeBuilder *builder);
    virtual void emitTimestamp(CodeBuilder *builder);
    virtual bool shouldEmitTimestamp() {
        auto directMeter = std::find_if(control->tables.begin(),
                                        control->tables.end(),
                                        [](std::pair<const cstring, EBPFTable*> elem) {
                                            return !elem.second->to<EBPFTablePSA>()->meters.empty();
                                        });
        bool anyDirectMeter = directMeter != control->tables.end();
        if (!control->meters.empty() || anyDirectMeter || control->timestampIsUsed) {
            return true;
        }

        return false;
    }
};

/*
 * EBPFIngressPipeline represents a hook-independent EBPF-based ingress pipeline.
 * It includes common definitions for TC and XDP.
 */
class EBPFIngressPipeline : public EBPFPipeline {
 public:
    EBPFIngressPipeline(cstring name, const EbpfOptions& options, P4::ReferenceMap* refMap,
                        P4::TypeMap* typeMap) : EBPFPipeline(name, options, refMap, typeMap) { }

    void emitPSAControlInputMetadata(CodeBuilder* builder) override;
    void emitPSAControlOutputMetadata(CodeBuilder* builder) override;
};

class EBPFEgressPipeline : public EBPFPipeline {
 public:
    EBPFEgressPipeline(cstring name, const EbpfOptions& options, P4::ReferenceMap* refMap,
                       P4::TypeMap* typeMap) : EBPFPipeline(name, options, refMap, typeMap) {

    }

    void emitPSAControlInputMetadata(CodeBuilder* builder) override;
    void emitPSAControlOutputMetadata(CodeBuilder* builder) override;
};

class TCIngressPipeline : public EBPFIngressPipeline {
 public:
    cstring processFunctionName;
    unsigned int maxResubmitDepth;

    TCIngressPipeline(cstring name, const EbpfOptions& options, P4::ReferenceMap* refMap,
                        P4::TypeMap* typeMap) :
            EBPFIngressPipeline(name, options, refMap, typeMap) {
        processFunctionName = "process";
        // FIXME: hardcded
        maxResubmitDepth = 4;
    }

    void emitTrafficManager(CodeBuilder *builder) override;
    void emit(CodeBuilder *builder) override;
 private:
    void emitTCWorkaroundUsingMeta(CodeBuilder *builder);
    void emitTCWorkaroundUsingHead(CodeBuilder *builder);
    void emitTCWorkaroundUsingCPUMAP(CodeBuilder *builder);
};

class TCEgressPipeline : public EBPFEgressPipeline {
 public:
    TCEgressPipeline(cstring name, const EbpfOptions& options, P4::ReferenceMap* refMap,
                       P4::TypeMap* typeMap) :
            EBPFEgressPipeline(name, options, refMap, typeMap) { }

    void emit(CodeBuilder *builder) override;
    void emitTrafficManager(CodeBuilder *builder) override;
};

class XDPIngressPipeline : public EBPFIngressPipeline {
 public:
    XDPIngressPipeline(cstring name, const EbpfOptions& options, P4::ReferenceMap* refMap,
                    P4::TypeMap* typeMap) :
            EBPFIngressPipeline(name, options, refMap, typeMap) {
        sectionName = "xdp_ingress/" + name;
        ifindexVar = cstring("skb->ingress_ifindex");
        packetPathVar = cstring("0");
    }

    void emit(CodeBuilder *builder) override;
    void emitTrafficManager(CodeBuilder *builder) override;
    void emitWithEgress(CodeBuilder *builder, EBPFPipeline* egress);
};

class XDPEgressPipeline : public EBPFEgressPipeline {
 public:
    XDPEgressPipeline(cstring name, const EbpfOptions& options, P4::ReferenceMap* refMap,
                        P4::TypeMap* typeMap):
            EBPFEgressPipeline(name, options, refMap, typeMap) {
        sectionName = "xdp_devmap/" + name;
        ifindexVar = cstring("skb->egress_ifindex");
        // we do not support packet path, instance & priority in the XDP egress.
        packetPathVar = cstring("0");
        pktInstanceVar = cstring("0");
        priorityVar = cstring("0");
    }

    void emit(CodeBuilder *builder) override;
    void emitTrafficManager(CodeBuilder *builder) override;
};

class TCTrafficManagerForXDP : public TCIngressPipeline {
    void emitReadXDP2TCMetadataFromHead(CodeBuilder *builder);
    void emitReadXDP2TCMetadataFromCPUMAP(CodeBuilder *builder);

 public:
    TCTrafficManagerForXDP(cstring name, const EbpfOptions& options, P4::ReferenceMap* refMap,
                           P4::TypeMap* typeMap) :
            TCIngressPipeline(name, options, refMap, typeMap) {
    }

    void emit(CodeBuilder *builder) override;
};

}  // namespace EBPF

#endif /* BACKENDS_EBPF_PSA_EBPFPIPELINE_H_ */
