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

#include "ebpfType.h"

namespace EBPF {

EBPFTypeFactory* EBPFTypeFactory::instance;

EBPFType* EBPFTypeFactory::create(const IR::Type* type) {
    CHECK_NULL(type);
    CHECK_NULL(typeMap);
    EBPFType* result = nullptr;
    if (type->is<IR::Type_Boolean>()) {
        result = new EBPFBoolType();
    } else if (auto bt = type->to<IR::Type_Bits>()) {
        result = new EBPFScalarType(bt);
    } else if (auto st = type->to<IR::Type_StructLike>()) {
        result = new EBPFStructType(st);
    } else if (auto tt = type->to<IR::Type_Typedef>()) {
        auto canon = typeMap->getTypeType(type, true);
        result = create(canon);
        auto path = new IR::Path(tt->name);
        result = new EBPFTypeName(new IR::Type_Name(path), result);
    } else if (auto tn = type->to<IR::Type_Name>()) {
        auto canon = typeMap->getTypeType(type, true);
        result = create(canon);
        result = new EBPFTypeName(tn, result);
    } else if (auto te = type->to<IR::Type_Enum>()) {
        result = new EBPFEnumType(te);
    } else if (auto ts = type->to<IR::Type_Stack>()) {
        auto et = create(ts->elementType);
        if (et == nullptr)
            return nullptr;
        result = new EBPFStackType(ts, et);
    } else if (auto terr = type->to<IR::Type_Error>()) {
        // EBPF target implements error type as scalar of witdh 8 bits
        result = new EBPFScalarType(new IR::Type_Bits(8, false));
    } else {
        ::error(ErrorType::ERR_UNSUPPORTED_ON_TARGET,
                "Type %1% not supported", type);
    }

    return result;
}

void
EBPFBoolType::declare(CodeBuilder* builder, cstring id, bool asPointer) {
    emit(builder);
    if (asPointer)
        builder->append("*");
    builder->appendFormat(" %s", id.c_str());
}

void
EBPFBoolType::declareInit(CodeBuilder* builder, cstring id, bool asPointer) {
    declare(builder, id, asPointer);
}

/////////////////////////////////////////////////////////////

void EBPFStackType::declare(CodeBuilder* builder, cstring id, bool) {
    elementType->declareArray(builder, id, size);
}

void EBPFStackType::declareInit(CodeBuilder* builder, cstring id, bool) {
    elementType->declareArray(builder, id, size);
}

void EBPFStackType::emitInitializer(CodeBuilder* builder) {
    builder->append("{");
    for (unsigned i = 0; i < size; i++) {
        if (i > 0)
            builder->append(", ");
        elementType->emitInitializer(builder);
    }
    builder->append(" }");
}

unsigned EBPFStackType::widthInBits() {
    return size * elementType->to<IHasWidth>()->widthInBits();
}

unsigned EBPFStackType::implementationWidthInBits() {
    return size * elementType->to<IHasWidth>()->implementationWidthInBits();
}

/////////////////////////////////////////////////////////////

unsigned EBPFScalarType::alignment() const {
    if (width <= 8)
        return 1;
    else if (width <= 16)
        return 2;
    else if (width <= 32)
        return 4;
    else if (width <= 64)
        return 8;
    else
        // compiled as u8*
        return 1;
}

void EBPFScalarType::emit(CodeBuilder* builder) {
    auto prefix = isSigned ? "i" : "u";

    if (width <= 8)
        builder->appendFormat("%s8", prefix);
    else if (width <= 16)
        builder->appendFormat("%s16", prefix);
    else if (width <= 32)
        builder->appendFormat("%s32", prefix);
    else if (width <= 64)
        builder->appendFormat("%s64", prefix);
    else
        builder->appendFormat("u8*");
}

void
EBPFScalarType::declare(CodeBuilder* builder, cstring id, bool asPointer) {
    if (EBPFScalarType::generatesScalar(width)) {
        emit(builder);
        if (asPointer)
            builder->append("*");
        builder->spc();
        builder->append(id);
    } else {
        if (asPointer)
            builder->append("u8*");
        else
            builder->appendFormat("u8 %s[%d]", id.c_str(), bytesRequired());
    }
}

void
EBPFScalarType::declareInit(CodeBuilder* builder, cstring id, bool asPointer) {
    if (EBPFScalarType::generatesScalar(width)) {
        emit(builder);
        if (asPointer)
            builder->append("*");
        builder->spc();
        id = id + cstring(" = 0");
        builder->append(id);
    } else {
        if (asPointer)
            builder->append("u8*");
        else
            builder->appendFormat("uint8_t %s[%d]", id.c_str(), bytesRequired());
    }
}

//////////////////////////////////////////////////////////

EBPFStructType::EBPFStructType(const IR::Type_StructLike* strct) :
        EBPFType(strct) {
    if (strct->is<IR::Type_Struct>())
        kind = "struct";
    else if (strct->is<IR::Type_Header>())
        kind = "struct";
    else if (strct->is<IR::Type_HeaderUnion>())
        kind = "union";
    else
        BUG("Unexpected struct type %1%", strct);
    name = strct->name.name;
    width = 0;
    implWidth = 0;

    for (auto f : strct->fields) {
        auto type = EBPFTypeFactory::instance->create(f->type);
        auto wt = dynamic_cast<IHasWidth*>(type);
        if (wt == nullptr) {
            ::error(ErrorType::ERR_UNSUPPORTED_ON_TARGET,
                    "EBPF: Unsupported type in struct: %s", f->type);
        } else {
            width += wt->widthInBits();
            implWidth += wt->implementationWidthInBits();
        }
        fields.push_back(new EBPFField(type, f));
    }
}

void
EBPFStructType::declare(CodeBuilder* builder, cstring id, bool asPointer) {
    builder->append(kind);
    builder->appendFormat(" %s ", name.c_str());
    if (asPointer)
        builder->append("*");
    builder->appendFormat("%s", id.c_str());
}

void EBPFStructType::declareInit(CodeBuilder* builder, cstring id, bool asPointer) {
    declare(builder, id, asPointer);
}

void EBPFStructType::emitInitializer(CodeBuilder* builder) {
    builder->blockStart();
    if (type->is<IR::Type_Struct>() || type->is<IR::Type_HeaderUnion>()) {
        for (auto f : fields) {
            builder->emitIndent();
            builder->appendFormat(".%s = ", f->field->name.name);
            f->type->emitInitializer(builder);
            builder->append(",");
            builder->newline();
        }
    } else if (type->is<IR::Type_Header>()) {
        builder->emitIndent();
        builder->appendLine(".ebpf_valid = 0");
    } else {
        BUG("Unexpected type %1%", type);
    }
    builder->blockEnd(false);
}

void EBPFStructType::emit(CodeBuilder* builder) {
    builder->emitIndent();
    builder->append(kind);
    builder->spc();
    builder->append(name);
    builder->spc();
    builder->blockStart();

    bool emitHelperVariableForHeaders = false;
    for (auto f : fields) {
        auto type = f->type;
        builder->emitIndent();

        if (type->is<EBPFTypeName>() &&
            type->to<EBPFTypeName>()->getCanonicalType()->type->is<IR::Type_Header>()) {
            emitHelperVariableForHeaders = true;
        }

        type->declare(builder, f->field->name, false);
        builder->append("; ");
        builder->append("/* ");
        builder->append(type->type->toString());
        if (f->comment != nullptr) {
            builder->append(" ");
            builder->append(f->comment);
        }
        builder->append(" */");
        builder->newline();
    }

    if (emitHelperVariableForHeaders) {
        // this is a struct storing headers
        // append helper variable that will be used by
        // pipeline-aware optimization to transfer egress_port from
        // ingress eBPF program to tail-called egress eBPF program.
        // This variable has intentionally generic name as it might be
        // used for another purpose in future.
        builder->appendLine("__u32 __helper_variable;");
    }

    if (type->is<IR::Type_Header>()) {
        builder->emitIndent();
        auto type = EBPFTypeFactory::instance->create(IR::Type_Boolean::get());
        if (type != nullptr) {
            type->declare(builder, "ebpf_valid", false);
            builder->endOfStatement(true);
        }
    }

    builder->blockEnd(false);
    builder->endOfStatement(true);
}

void
EBPFStructType::declareArray(CodeBuilder* builder, cstring id, unsigned size) {
    builder->appendFormat("%s %s[%d]", name.c_str(), id.c_str(), size);
}

///////////////////////////////////////////////////////////////

void EBPFTypeName::declare(CodeBuilder* builder, cstring id, bool asPointer) {
    if (canonical != nullptr)
        canonical->declare(builder, id, asPointer);
}

void EBPFTypeName::declareInit(CodeBuilder* builder, cstring id, bool asPointer) {
    declare(builder, id, asPointer);
}

void EBPFTypeName::emitInitializer(CodeBuilder* builder) {
    if (canonical != nullptr)
        canonical->emitInitializer(builder);
}

unsigned EBPFTypeName::widthInBits() {
    auto wt = dynamic_cast<IHasWidth*>(canonical);
    if (wt == nullptr) {
        ::error(ErrorType::ERR_UNSUPPORTED_ON_TARGET,
                "Type %1% does not have a fixed witdh", type);
        return 0;
    }
    return wt->widthInBits();
}

unsigned EBPFTypeName::implementationWidthInBits() {
    auto wt = dynamic_cast<IHasWidth*>(canonical);
    if (wt == nullptr) {
        ::error(ErrorType::ERR_UNSUPPORTED_ON_TARGET,
                "Type %1% does not have a fixed witdh", type);
        return 0;
    }
    return wt->implementationWidthInBits();
}

void
EBPFTypeName::declareArray(CodeBuilder* builder, cstring id, unsigned size) {
    declare(builder, id, false);
    builder->appendFormat("[%d]", size);
}

////////////////////////////////////////////////////////////////

void EBPFEnumType::declare(EBPF::CodeBuilder* builder, cstring id, bool asPointer) {
    builder->append("enum ");
    builder->append(getType()->name);
    if (asPointer)
        builder->append("*");
    builder->append(" ");
    builder->append(id);
}

void EBPFEnumType::declareInit(CodeBuilder* builder, cstring id, bool asPointer) {
    declare(builder, id, asPointer);
}

void EBPFEnumType::emit(EBPF::CodeBuilder* builder) {
    builder->append("enum ");
    auto et = getType();
    builder->append(et->name);
    builder->spc();
    builder->blockStart();
    for (auto m : et->members) {
        builder->append(m->name);
        builder->appendLine(",");
    }
    builder->blockEnd(false);
    builder->endOfStatement(true);
}

////////////////////////////////////////////////////////////////

void EBPFErrorTypePSA::emit(CodeBuilder* builder) {
    auto terr = this->getType();
    int id = -1;
    for (auto decl : terr->members) {
        ++id;
        auto sourceFile = decl->srcInfo.getSourceFile();
        // all the error codes are located in core.p4 file, they are defined in psa.h
        if (sourceFile.endsWith("core.p4"))
            continue;
        // for future, also exclude definitions in psa.p4 file
        if (sourceFile.endsWith("/psa.p4"))
            continue;

        builder->emitIndent();
        builder->append("static const ParserError_t ");
        builder->appendFormat("%s = %d", decl->name.name, id);
        builder->endOfStatement(true);

        // type u8 can have values from 0 to 255
        if (id > 255) {
            ::warning(ErrorType::ERR_OVERLIMIT,
                      "%1%: Reached maximum number of possible errors", decl);
        }
    }

    builder->newline();
}

void EBPFErrorTypePSA::declareInit(CodeBuilder* builder, cstring id, bool asPointer) {
    declare(builder, id, asPointer);
}

void EBPFErrorTypePSA::declare(CodeBuilder* builder, cstring id, bool asPointer) {
    (void) builder; (void) id; (void) asPointer;
    BUG("Error type is not declarable");
}

void EBPFErrorTypePSA::emitInitializer(CodeBuilder* builder) {
    (void) builder;
    BUG("Error type cannot be initialized");
}

}  // namespace EBPF
