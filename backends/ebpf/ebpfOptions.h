/*
Copyright 2013-present Barefoot Networks, Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef _BACKENDS_EBPF_EBPFOPTIONS_H_
#define _BACKENDS_EBPF_EBPFOPTIONS_H_

#include <getopt.h>
#include "frontends/common/options.h"

enum XDP2TC {
    XDP2TC_NONE,
    XDP2TC_META,
    XDP2TC_HEAD,
    XDP2TC_CPUMAP
};

class EbpfOptions : public CompilerOptions {
 public:
    // read from json
    bool loadIRFromJson = false;
    // Externs generation
    bool emitExterns = false;
    // Tracing eBPF code execution
    bool emitTraceMessages = false;
    // Enable table cache for LPM and ternary tables
    bool enableTableCache = false;
    // Generate headers and user metadata in/from CPUMAP
    bool generateHdrInMap = false;
    // maximum number of ternary masks
    unsigned int maxTernaryMasks = 128;

    bool generateToXDP = false;
    bool egressOptimization = false;
    enum XDP2TC xdp2tcMode = XDP2TC_NONE;
    EbpfOptions();

    void calculateXDP2TCMode() {
        if (arch != "psa") {
            return;
        }

        if (generateToXDP && xdp2tcMode == XDP2TC_META) {
            std::cerr << "XDP2TC 'meta' mode cannot be used if XDP is enabled. "
                         "Falling back to 'head' mode." << std::endl;
            xdp2tcMode = XDP2TC_HEAD;
        } else if (generateToXDP && xdp2tcMode == XDP2TC_NONE) {
            // use 'head' mode by default; it's the most safe option.
            xdp2tcMode = XDP2TC_HEAD;
        } else if (!generateToXDP && xdp2tcMode == XDP2TC_NONE) {
            // For TC, use 'meta' mode by default.
            xdp2tcMode = XDP2TC_META;
        }
        BUG_CHECK(xdp2tcMode != XDP2TC_NONE, "xdp2tc mode should not be set to NONE, bug?");
    }
};

using EbpfContext = P4CContextWithOptions<EbpfOptions>;

#endif /* _BACKENDS_EBPF_EBPFOPTIONS_H_ */
