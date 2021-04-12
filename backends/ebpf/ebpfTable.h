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

#ifndef _BACKENDS_EBPF_EBPFTABLE_H_
#define _BACKENDS_EBPF_EBPFTABLE_H_

#include "ebpfObject.h"
#include "ebpfProgram.h"
#include "frontends/p4/methodInstance.h"

namespace EBPF {

class ActionTranslationVisitor : public CodeGenInspector {
 protected:
    const EBPFProgram*  program;
    const IR::P4Action* action;
    cstring             valueName;

 public:
    ActionTranslationVisitor(cstring valueName, const EBPFProgram* program):
            CodeGenInspector(program->refMap, program->typeMap), program(program),
            action(nullptr), valueName(valueName)
    { CHECK_NULL(program); }

    bool preorder(const IR::PathExpression* expression);

    bool preorder(const IR::P4Action* act);
};  // ActionTranslationVisitor

// Also used to represent counters
class EBPFTableBase : public EBPFObject {
 public:
    const EBPFProgram* program;

    cstring instanceName;
    cstring keyTypeName;
    cstring valueTypeName;
    cstring dataMapName;
    CodeGenInspector* codeGen;

 protected:
    EBPFTableBase(const EBPFProgram* program, cstring instanceName,
                  CodeGenInspector* codeGen) :
            program(program), instanceName(instanceName), codeGen(codeGen) {
        CHECK_NULL(codeGen); CHECK_NULL(program);
        keyTypeName = program->refMap->newName(instanceName + "_key");
        valueTypeName = program->refMap->newName(instanceName + "_value");
        dataMapName = instanceName;
    }
};

class EBPFTable : public EBPFTableBase {
    const int prefixLenFieldWidth = 32;

 protected:
    const cstring prefixFieldName = "prefixlen";

    bool isLPMTable();
    bool isTernaryTable();
    virtual ActionTranslationVisitor*
        createActionTranslationVisitor(cstring valueName, const EBPFProgram* program) const {
        return new ActionTranslationVisitor(valueName, program);
    }

 public:
    const IR::Key*            keyGenerator;
    const IR::ActionList*     actionList;
    const IR::TableBlock*    table;
    cstring               defaultActionMapName;
    cstring               actionEnumName;
    std::map<const IR::KeyElement*, cstring> keyFieldNames;
    std::map<const IR::KeyElement*, EBPFType*> keyTypes;

    EBPFTable(const EBPFProgram* program, const IR::TableBlock* table, CodeGenInspector* codeGen);
    virtual void emitTypes(CodeBuilder* builder);
    virtual void emitInstance(CodeBuilder* builder);
    void emitActionArguments(CodeBuilder* builder, const IR::P4Action* action, cstring name);
    virtual void emitKeyType(CodeBuilder* builder);
    void emitValueType(CodeBuilder* builder);
    void emitKey(CodeBuilder* builder, cstring keyName);
    void emitAction(CodeBuilder* builder, cstring valueName);
    virtual void emitInitializer(CodeBuilder* builder);
    virtual void emitLookup(CodeBuilder* builder, cstring key, cstring value) {
            builder->target->emitTableLookup(builder, dataMapName, key, value);
            builder->endOfStatement(true);
    }
    virtual bool isMatchTypeSupported(const IR::Declaration_ID* matchType) {
        return matchType->name.name == P4::P4CoreLibrary::instance.exactMatch.name ||
               matchType->name.name == P4::P4CoreLibrary::instance.lpmMatch.name;
    }
    virtual void emitDirectTypes(CodeBuilder* builder) { (void) builder; }
    cstring actionToActionIDName(const IR::P4Action * action) const;

 private:
    cstring getByteSwapMethod(unsigned int width) const;
    void declareTmpLpmKey(CodeBuilder *builder, const IR::KeyElement *c, std::string &tmpVar);
    void emitLpmKeyField(CodeBuilder *builder,
                         const cstring &swap,
                         const std::string &tmpVar) const;
    void validateKeys(const EBPFProgram *program) const;
};

class EBPFCounterTable : public EBPFTableBase {
 protected:
    size_t    size;
    bool      isHash;

 public:
    EBPFCounterTable(const EBPFProgram* program, const IR::ExternBlock* block,
                     cstring name, CodeGenInspector* codeGen);
    EBPFCounterTable(const EBPFProgram* program, cstring name, CodeGenInspector* codeGen,
                     size_t size, bool isHash) :
            EBPFTableBase(program, name, codeGen), size(size), isHash(isHash) { }
    virtual void emitTypes(CodeBuilder*);
    virtual void emitInstance(CodeBuilder* builder);
    virtual void emitCounterIncrement(CodeBuilder* builder,
                                      const IR::MethodCallExpression* expression);
    virtual void emitCounterAdd(CodeBuilder* builder, const IR::MethodCallExpression* expression);
    virtual void emitMethodInvocation(CodeBuilder* builder, const P4::ExternMethod* method);
};

}  // namespace EBPF

#endif /* _BACKENDS_EBPF_EBPFTABLE_H_ */
