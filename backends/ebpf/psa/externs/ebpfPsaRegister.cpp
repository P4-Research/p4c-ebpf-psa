#include "ebpfPsaRegister.h"

namespace EBPF {

EBPFRegisterPSA::EBPFRegisterPSA(const EBPFProgram *program,
                                 cstring instanceName, const IR::Declaration_Instance* di,
                                 CodeGenInspector *codeGen) : EBPFTableBase(program,
                                                                            instanceName,
                                                                            codeGen) {

    auto ts = di->type->to<IR::Type_Specialized>();

    auto keyArg = ts->arguments->at(0);
    auto valueArg = ts->arguments->at(1);
    this->keyType = EBPFTypeFactory::instance->create(keyArg);
    this->valueType = EBPFTypeFactory::instance->create(valueArg);

    if (keyArg->is<IR::Type_Bits>()) {
        unsigned keyWidth = keyArg->width_bits();
        // For keys <= 32 bit register is based on array map,
        // otherwise we use hash map
        arrayMapBased = (keyWidth <= 32);
    }

    auto declaredSize = di->arguments->at(0)->expression->to<IR::Constant>();
    if (!declaredSize->fitsInt()) {
        ::error(ErrorType::ERR_OVERLIMIT, "%1%: size too large", declaredSize);
        return;
    }
    size = declaredSize->asUnsigned();

    if (di->arguments->size() == 2) {
        this->initialValue = di->arguments->at(1)->expression->to<IR::Constant>();
    }
}

void EBPFRegisterPSA::emitTypes(CodeBuilder* builder) {
    emitKeyType(builder);
    emitValueType(builder);
}

void EBPFRegisterPSA::emitKeyType(CodeBuilder* builder) {
    builder->emitIndent();
    builder->append("typedef ");
    this->keyType->emit(builder);
    builder->appendFormat(" %s", keyTypeName.c_str());
    builder->endOfStatement(true);
}

void EBPFRegisterPSA::emitValueType(CodeBuilder* builder) {
    builder->emitIndent();
    builder->append("typedef ");
    this->valueType->emit(builder);
    builder->appendFormat(" %s", valueTypeName.c_str());
    builder->endOfStatement(true);
}

void EBPFRegisterPSA::emitInitializer(CodeBuilder* builder) {
    if (arrayMapBased && this->initialValue != nullptr) {
        auto ret = program->refMap->newName("ret");
        cstring keyName = program->refMap->newName("key");
        cstring valueName = program->refMap->newName("value");

        builder->emitIndent();
        builder->appendFormat("%s %s", keyTypeName.c_str(), keyName.c_str());
        builder->endOfStatement(true);

        builder->emitIndent();
        builder->appendFormat("%s %s = ", valueTypeName.c_str(), valueName.c_str());
        builder->append(this->initialValue->value.str());
        builder->endOfStatement(true);

        builder->emitIndent();
        builder->appendFormat("for (size_t index = 0; index < %u; index++) ", this->size);
        builder->blockStart();
        builder->emitIndent();
        builder->appendFormat("%s = index", keyName.c_str());
        builder->endOfStatement(true);
        builder->emitIndent();
        builder->appendFormat("int %s = ", ret.c_str());
        builder->target->emitTableUpdate(builder, instanceName,
                                         keyName, valueName);
        builder->newline();

        builder->emitIndent();
        builder->appendFormat("if (%s) ", ret.c_str());
        builder->blockStart();
        cstring msgStr = Util::printf_format("Map initializer: Error while map (%s) update, code: %s",
                                             instanceName, "%d");
        builder->target->emitTraceMessage(builder,
                                          msgStr, 1, ret.c_str());

        builder->blockEnd(true);

        builder->blockEnd(true);
    }
}

void EBPFRegisterPSA::emitInstance(CodeBuilder *builder) {
    builder->target->emitTableDecl(builder, instanceName, TableHash,
                                   this->keyTypeName,
                                   this->valueTypeName, size);
}

void EBPFRegisterPSA::emitRegisterRead(CodeBuilder* builder, const P4::ExternMethod* method,
                      cstring indexParamStr, const IR::Expression* leftExpression) {
    BUG_CHECK(!indexParamStr.isNullOrEmpty(), "Index param must be provided");
    BUG_CHECK(leftExpression != nullptr, "Register read must be with left assigment");

    cstring keyName = program->refMap->newName("key");
    cstring valueName = program->refMap->newName("value");
    cstring msgStr, varStr;

    auto pipeline = dynamic_cast<const EBPFPipeline *>(program);
    if (pipeline == nullptr) {
        ::error(ErrorType::ERR_UNSUPPORTED, "Register used outside of pipeline %1%", method->expr);
        return;
    }

    builder->emitIndent();
    builder->appendFormat("%s *%s = NULL", valueTypeName.c_str(), valueName.c_str());
    builder->endOfStatement(true);

    builder->emitIndent();
    builder->appendFormat("%s %s = %s", keyTypeName.c_str(), keyName.c_str(), indexParamStr.c_str());
    builder->endOfStatement(true);

    msgStr = Util::printf_format("Register: reading %s, id=%%u, packets=1, bytes=%%u",
                                 instanceName.c_str());
    varStr = Util::printf_format("%s->len", pipeline->contextVar.c_str());
    builder->target->emitTraceMessage(builder, msgStr.c_str(), 2, keyName.c_str(), varStr.c_str());

    builder->emitIndent();
    builder->target->emitTableLookup(builder, dataMapName, keyName, valueName);
    builder->endOfStatement(true);

    builder->emitIndent();
    builder->appendFormat("if (%s != NULL) ", valueName.c_str());
    builder->blockStart();
    builder->emitIndent();
    codeGen->visit(leftExpression);
    builder->appendFormat(" = *%s", valueName);
    builder->endOfStatement(true);
    builder->target->emitTraceMessage(builder,
                                      "Register: Entry found! (index: 0x%%llx, value: 0x%%llx)",
                                      2, keyName.c_str(), Util::printf_format("*%s", valueName.c_str()));
    builder->blockEnd(false);
    builder->appendFormat(" else ");
    builder->blockStart();
    builder->emitIndent();

    codeGen->visit(leftExpression);
    builder->append(" = ");
    if (this->initialValue != nullptr) {
        builder->append(this->initialValue->value.str());
    } else {
        valueType->emitInitializer(builder);
    }
    builder->endOfStatement(true);

    builder->target->emitTraceMessage(builder, "Register: Entry not found, using default value");
    builder->blockEnd(true);
}

void EBPFRegisterPSA::emitRegisterWrite(CodeBuilder* builder, const P4::ExternMethod* method,
                                        cstring indexParamStr, cstring valueParamStr) {
    BUG_CHECK(!indexParamStr.isNullOrEmpty(), "Index param must be provided");
    BUG_CHECK(!valueParamStr.isNullOrEmpty(), "Value param must be provided");

    cstring keyName = program->refMap->newName("key");
    cstring msgStr, varStr;

    auto pipeline = dynamic_cast<const EBPFPipeline *>(program);
    if (pipeline == nullptr) {
        ::error(ErrorType::ERR_UNSUPPORTED, "Register used outside of pipeline %1%", method->expr);
        return;
    }

    builder->emitIndent();
    builder->appendFormat("%s %s = ", keyTypeName.c_str(), keyName.c_str());

    builder->append(indexParamStr);
    builder->endOfStatement(true);

    msgStr = Util::printf_format("Register: writing %s, id=%%u, packets=1, bytes=%%u",
                                 instanceName.c_str());
    varStr = Util::printf_format("%s->len", pipeline->contextVar.c_str());
    builder->target->emitTraceMessage(builder, msgStr.c_str(), 2, keyName.c_str(), varStr.c_str());

    builder->emitIndent();
    builder->target->emitTableUpdate(builder, instanceName, keyName, valueParamStr);
}

} // namespace EBPF