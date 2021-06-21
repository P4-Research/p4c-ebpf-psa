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

#ifndef _BACKENDS_EBPF_TARGET_H_
#define _BACKENDS_EBPF_TARGET_H_

#include "lib/cstring.h"
#include "lib/sourceCodeBuilder.h"
#include "lib/exceptions.h"

// We are prepared to support code generation using multiple styles
// (e.g., using BCC or using CLANG).

namespace EBPF {

enum TableKind {
    TableHash,
    TableArray,
    TableLPMTrie  // longest prefix match trie
};

class Target {
 protected:
    explicit Target(cstring name) : name(name) {}
    Target() = delete;
    virtual ~Target() {}

 public:
    const cstring name;

    virtual void emitLicense(Util::SourceCodeBuilder* builder, cstring license) const = 0;
    virtual void emitCodeSection(Util::SourceCodeBuilder* builder, cstring sectionName) const = 0;
    virtual void emitIncludes(Util::SourceCodeBuilder* builder) const = 0;
    virtual void emitTableLookup(Util::SourceCodeBuilder* builder, cstring tblName,
                                 cstring key, cstring value) const = 0;
    virtual void emitTableUpdate(Util::SourceCodeBuilder* builder, cstring tblName,
                                 cstring key, cstring value) const = 0;
    virtual void emitUserTableUpdate(Util::SourceCodeBuilder* builder, cstring tblName,
                                     cstring key, cstring value) const = 0;
    virtual void emitTableDecl(Util::SourceCodeBuilder* builder,
                               cstring tblName, TableKind tableKind,
                               cstring keyType, cstring valueType,
                               unsigned size) const = 0;
    // map-in-map requires declaration of both inner and outer map,
    // thus we define them together in a single method.
    virtual void emitMapInMapDecl(Util::SourceCodeBuilder* builder,
                          cstring innerName, TableKind innerTableKind,
                          cstring innerKeyType, cstring innerValueType, unsigned innerSize,
                          cstring outerName, TableKind outerTableKind,
                          cstring outerKeyType, unsigned outerSize) const {
        (void) builder;
        (void) innerName;
        (void) innerTableKind;
        (void) innerKeyType;
        (void) innerValueType;
        (void) innerSize;
        (void) outerName;
        (void) outerTableKind;
        (void) outerKeyType;
        (void) outerSize;
    }
    virtual void emitMain(Util::SourceCodeBuilder* builder,
                          cstring functionName,
                          cstring argName) const = 0;
    virtual void emitPreamble(Util::SourceCodeBuilder* builder) const {
        (void) builder;
    }
    virtual void emitTraceMessage(Util::SourceCodeBuilder* builder, const char* format,
                                  int argc = 0, ...) const {
        (void) builder;
        (void) format;
        (void) argc;
    }
    virtual cstring dataOffset(cstring base) const = 0;
    virtual cstring dataEnd(cstring base) const = 0;
    virtual cstring forwardReturnCode() const = 0;
    virtual cstring dropReturnCode() const = 0;
    virtual cstring abortReturnCode() const = 0;
    // Path on /sys filesystem where maps are stored
    virtual cstring sysMapPath() const = 0;
};

// Represents a target that is compiled within the kernel
// source tree samples folder and which attaches to a socket
class KernelSamplesTarget : public Target {
 private:
    mutable unsigned int innerMapIndex;

    cstring getBPFMapType(TableKind kind) const {
        if (kind == TableHash) {
            return "BPF_MAP_TYPE_HASH";
        } else if (kind == TableArray) {
            return "BPF_MAP_TYPE_ARRAY";
        } else if (kind == TableLPMTrie) {
            return "BPF_MAP_TYPE_LPM_TRIE";
        }
        BUG("Unknown table kind");
    }

 protected:
    bool emitTraceMessages;

 public:
    explicit KernelSamplesTarget(bool emitTrace = false, cstring name = "Linux kernel")
        : Target(name) {
        emitTraceMessages = emitTrace;
        innerMapIndex = 0;
    }
    void emitLicense(Util::SourceCodeBuilder* builder, cstring license) const override;
    void emitCodeSection(Util::SourceCodeBuilder* builder, cstring sectionName) const override;
    void emitIncludes(Util::SourceCodeBuilder* builder) const override;
    void emitTableLookup(Util::SourceCodeBuilder* builder, cstring tblName,
                         cstring key, cstring value) const override;
    void emitTableUpdate(Util::SourceCodeBuilder* builder, cstring tblName,
                         cstring key, cstring value) const override;
    void emitUserTableUpdate(Util::SourceCodeBuilder* builder, cstring tblName,
                             cstring key, cstring value) const override;
    void emitTableDecl(Util::SourceCodeBuilder* builder,
                       cstring tblName, TableKind tableKind,
                       cstring keyType, cstring valueType, unsigned size) const override;
    void emitMapInMapDecl(Util::SourceCodeBuilder* builder,
                          cstring innerName, TableKind innerTableKind,
                          cstring innerKeyType, cstring innerValueType, unsigned innerSize,
                          cstring outerName, TableKind outerTableKind,
                          cstring outerKeyType, unsigned outerSize) const override;
    void emitMain(Util::SourceCodeBuilder* builder,
                  cstring functionName,
                  cstring argName) const override;
    void emitPreamble(Util::SourceCodeBuilder* builder) const override;
    void emitTraceMessage(Util::SourceCodeBuilder* builder, const char* format,
                          int argc = 0, ...) const override;
    cstring dataOffset(cstring base) const override
    { return cstring("((void*)(long)")+ base + "->data)"; }
    cstring dataEnd(cstring base) const override
    { return cstring("((void*)(long)")+ base + "->data_end)"; }
    cstring forwardReturnCode() const override { return "TC_ACT_OK"; }
    cstring dropReturnCode() const override { return "TC_ACT_SHOT"; }
    cstring abortReturnCode() const override { return "TC_ACT_SHOT"; }
    cstring sysMapPath() const override { return "/sys/fs/bpf/tc/globals"; }

    void annotateTableWithBTF(Util::SourceCodeBuilder* builder, cstring name,
                              cstring keyType, cstring valueType) const;
};

// Represents a target compiled by bcc that uses the TC
class BccTarget : public Target {
 public:
    BccTarget() : Target("BCC") {}
    void emitLicense(Util::SourceCodeBuilder*, cstring) const override {};
    void emitCodeSection(Util::SourceCodeBuilder*, cstring) const override {}
    void emitIncludes(Util::SourceCodeBuilder* builder) const override;
    void emitTableLookup(Util::SourceCodeBuilder* builder, cstring tblName,
                         cstring key, cstring value) const override;
    void emitTableUpdate(Util::SourceCodeBuilder* builder, cstring tblName,
                         cstring key, cstring value) const override;
    void emitUserTableUpdate(Util::SourceCodeBuilder* builder, cstring tblName,
                             cstring key, cstring value) const override;
    void emitTableDecl(Util::SourceCodeBuilder* builder,
                       cstring tblName, TableKind tableKind,
                       cstring keyType, cstring valueType, unsigned size) const override;
    void emitMain(Util::SourceCodeBuilder* builder,
                  cstring functionName,
                  cstring argName) const override;
    cstring dataOffset(cstring base) const override { return base; }
    cstring dataEnd(cstring base) const override
    { return cstring("(") + base + " + " + base + "->len)"; }
    cstring forwardReturnCode() const override { return "0"; }
    cstring dropReturnCode() const override { return "1"; }
    cstring abortReturnCode() const override { return "1"; }
    cstring sysMapPath() const override { return "/sys/fs/bpf"; }
};

// A userspace test version with functionality equivalent to the kernel
// Compiles with gcc
class TestTarget : public EBPF::KernelSamplesTarget {
 public:
    TestTarget() : KernelSamplesTarget(false, "Userspace Test") {}
    void emitIncludes(Util::SourceCodeBuilder* builder) const override;
    void emitTableDecl(Util::SourceCodeBuilder* builder,
                       cstring tblName, TableKind tableKind,
                       cstring keyType, cstring valueType, unsigned size) const override;
    cstring dataOffset(cstring base) const override
    { return cstring("((void*)(long)")+ base + "->data)"; }
    cstring dataEnd(cstring base) const override
    { return cstring("((void*)(long)(")+ base + "->data + "+ base +"->len))"; }
    cstring forwardReturnCode() const override { return "true"; }
    cstring dropReturnCode() const override { return "false"; }
    cstring abortReturnCode() const override { return "false"; }
    cstring sysMapPath() const override { return "/sys/fs/bpf"; }
};

}  // namespace EBPF

#endif /* _BACKENDS_EBPF_TARGET_H_ */
