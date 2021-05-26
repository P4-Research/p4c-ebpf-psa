#ifndef BACKENDS_EBPF_PSA_XDPTARGET_H_
#define BACKENDS_EBPF_PSA_XDPTARGET_H_

#include "backends/ebpf/target.h"

namespace EBPF {

// Target XDP
class XdpTarget : public KernelSamplesTarget {
 public:
    explicit XdpTarget(bool emitTrace) : KernelSamplesTarget(emitTrace, "XDP") {}

    void emitIncludes(Util::SourceCodeBuilder* builder) const override;
    cstring forwardReturnCode() const override { return "XDP_PASS"; }
    cstring dropReturnCode() const override { return "XDP_DROP"; }
    cstring abortReturnCode() const override { return "XDP_ABORTED"; }
    cstring redirectReturnCode() const { return "XDP_REDIRECT"; }
    cstring sysMapPath() const override { return "/sys/fs/bpf/xdp/globals"; }
};

}  // namespace EBPF

#endif  // BACKENDS_EBPF_PSA_XDPTARGET_H_