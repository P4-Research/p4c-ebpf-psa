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

#include "ebpfModel.h"
#include "ebpfParser.h"
#include "ebpfType.h"
#include "frontends/p4/coreLibrary.h"
#include "frontends/p4/methodInstance.h"

namespace EBPF {

void
StateTranslationVisitor::compileLookahead(const IR::Expression* destination) {
    cstring msgStr = Util::printf_format("Parser: lookahead for %s %s",
         state->parser->typeMap->getType(destination)->toString(),
         destination->toString());
    builder->target->emitTraceMessage(builder, msgStr.c_str());

    builder->emitIndent();
    builder->blockStart();
    builder->emitIndent();
    builder->appendFormat("%s_save = %s",
                          state->parser->program->offsetVar.c_str(),
                          state->parser->program->offsetVar.c_str());
    builder->endOfStatement(true);
    compileExtract(destination);
    builder->emitIndent();
    builder->appendFormat("%s = %s_save",
                          state->parser->program->offsetVar.c_str(),
                          state->parser->program->offsetVar.c_str());
    builder->endOfStatement(true);
    builder->blockEnd(true);
}

bool StateTranslationVisitor::preorder(const IR::AssignmentStatement* statement) {
    if (auto mce = statement->right->to<IR::MethodCallExpression>()) {
        auto mi = P4::MethodInstance::resolve(mce,
                                              state->parser->program->refMap,
                                              state->parser->program->typeMap);
        auto extMethod = mi->to<P4::ExternMethod>();
        if (extMethod == nullptr)
            BUG("Unhandled method %1%", mce);

        auto decl = extMethod->object;
        if (decl == state->parser->packet) {
            if (extMethod->method->name.name == p4lib.packetIn.lookahead.name) {
                compileLookahead(statement->left);
                return false;
            }
        }
    }

    return CodeGenInspector::preorder(statement);
}

bool StateTranslationVisitor::preorder(const IR::ParserState* parserState) {
    if (parserState->isBuiltin()) return false;

    builder->emitIndent();
    builder->append(parserState->name.name);
    builder->append(":");
    builder->spc();
    builder->blockStart();

    cstring msgStr = Util::printf_format("Parser: state %s (curr_offset=%%u)",
                                         parserState->name.name);
    builder->target->emitTraceMessage(builder, msgStr.c_str(), 1,
                                      state->parser->program->offsetVar);

    visit(parserState->components, "components");
    if (parserState->selectExpression == nullptr) {
        builder->emitIndent();
        builder->append("goto ");
        builder->append(IR::ParserState::reject);
        builder->endOfStatement(true);
    } else if (parserState->selectExpression->is<IR::SelectExpression>()) {
        visit(parserState->selectExpression);
    } else {
        // must be a PathExpression which is a state name
        if (!parserState->selectExpression->is<IR::PathExpression>())
            BUG("Expected a PathExpression, got a %1%", parserState->selectExpression);
        builder->emitIndent();
        builder->append(" goto ");
        visit(parserState->selectExpression);
        builder->endOfStatement(true);
    }

    builder->blockEnd(true);
    return false;
}

bool StateTranslationVisitor::preorder(const IR::SelectExpression* expression) {
    BUG_CHECK(expression->select->components.size() == 1,
              "%1%: tuple not eliminated in select",
              expression->select);
    selectValue = state->parser->program->refMap->newName("select");
    auto type = state->parser->program->typeMap->getType(expression->select, true);
    if (auto list = type->to<IR::Type_List>()) {
        BUG_CHECK(list->components.size() == 1, "%1% list type with more than 1 element", list);
        type = list->components.at(0);
    }
    auto etype = EBPFTypeFactory::instance->create(type);
    builder->emitIndent();
    etype->declare(builder, selectValue, false);
    builder->endOfStatement(true);
    builder->emitIndent();
    builder->appendFormat("%s = ", selectValue);
    visit(expression->select);
    builder->endOfStatement(true);
    for (auto e : expression->selectCases)
        visit(e);

    builder->emitIndent();
    builder->appendFormat("else goto %s;", IR::ParserState::reject.c_str());
    builder->newline();
    return false;
}

bool StateTranslationVisitor::preorder(const IR::SelectCase* selectCase) {
    builder->emitIndent();
    if (auto mask = selectCase->keyset->to<IR::Mask>()) {
        builder->appendFormat("if ((%s", selectValue);
        builder->append(" & ");
        visit(mask->right);
        builder->append(") == (");
        visit(mask->left);
        builder->append(" & ");
        visit(mask->right);
        builder->append("))");
    } else {
        builder->appendFormat("if (%s", selectValue);
        builder->append(" == ");
        visit(selectCase->keyset);
        builder->append(")");
    }
    builder->append("goto ");
    visit(selectCase->state);
    builder->endOfStatement(true);
    return false;
}

void
StateTranslationVisitor::compileExtractField(
    const IR::Expression* expr, cstring field, unsigned alignment, EBPFType* type) {
    unsigned widthToExtract = dynamic_cast<IHasWidth*>(type)->widthInBits();
    auto program = state->parser->program;
    cstring msgStr;

    msgStr = Util::printf_format("Parser: extracting field %s", field);
    builder->target->emitTraceMessage(builder, msgStr.c_str());

    if (widthToExtract <= 64) {
        unsigned lastBitIndex = widthToExtract + alignment - 1;
        unsigned lastWordIndex = lastBitIndex / 8;
        unsigned wordsToRead = lastWordIndex + 1;
        unsigned loadSize;

        const char* helper = nullptr;
        if (wordsToRead <= 1) {
            helper = "load_byte";
            loadSize = 8;
        } else if (wordsToRead <= 2)  {
            helper = "load_half";
            loadSize = 16;
        } else if (wordsToRead <= 4) {
            helper = "load_word";
            loadSize = 32;
        } else {
            // TODO: this is wrong, since a 60-bit unaligned read may require 9 words.
            if (wordsToRead > 64) BUG("Unexpected width %d", widthToExtract);
            helper = "load_dword";
            loadSize = 64;
        }

        unsigned shift = loadSize - alignment - widthToExtract;
        builder->emitIndent();
        visit(expr);
        builder->appendFormat(".%s = (", field.c_str());
        type->emit(builder);
        builder->appendFormat(")((%s(%s, BYTES(%s))",
                              helper,
                              program->packetStartVar.c_str(),
                              program->offsetVar.c_str());
        if (shift != 0)
            builder->appendFormat(" >> %d", shift);
        builder->append(")");

        if (widthToExtract != loadSize) {
            builder->append(" & EBPF_MASK(");
            type->emit(builder);
            builder->appendFormat(", %d)", widthToExtract);
        }

        builder->append(")");
        builder->endOfStatement(true);
    } else {
        // wide values; read all bytes one by one.
        unsigned shift;
        if (alignment == 0)
            shift = 0;
        else
            shift = 8 - alignment;

        const char* helper;
        if (shift == 0)
            helper = "load_byte";
        else
            helper = "load_half";
        auto bt = EBPFTypeFactory::instance->create(IR::Type_Bits::get(8));
        unsigned bytes = ROUNDUP(widthToExtract, 8);
        for (unsigned i=0; i < bytes; i++) {
            builder->emitIndent();
            visit(expr);
            builder->appendFormat(".%s[%d] = (", field.c_str(), i);
            bt->emit(builder);
            builder->appendFormat(")((%s(%s, BYTES(%s) + %d) >> %d)",
                                  helper,
                                  program->packetStartVar.c_str(),
                                  program->offsetVar.c_str(), i, shift);

            if ((i == bytes - 1) && (widthToExtract % 8 != 0)) {
                builder->append(" & EBPF_MASK(");
                bt->emit(builder);
                builder->appendFormat(", %d)", widthToExtract % 8);
            }

            builder->append(")");
            builder->endOfStatement(true);
        }
    }

    builder->emitIndent();
    builder->appendFormat("%s += %d", program->offsetVar.c_str(), widthToExtract);
    builder->endOfStatement(true);

    // eBPF can pass 64 bits of data as one argument, so value of the field is
    // printed only when its fits into register
    if (widthToExtract <= 64) {
        cstring exprStr = expr->is<IR::PathExpression>() ?
                expr->to<IR::PathExpression>()->path->name.name : expr->toString();

        if (expr->is<IR::Member>() && expr->to<IR::Member>()->expr->is<IR::PathExpression>() &&
            asPointerVariables.count(
                    expr->to<IR::Member>()->expr->to<IR::PathExpression>()->path->name.name) > 0) {
            exprStr = exprStr.replace(".", "->");
        }
        cstring tmp = Util::printf_format("(unsigned long long) %s.%s", exprStr, field);

        msgStr = Util::printf_format("Parser: extracted %s=0x%%llx (%u bits)",
                                     field, widthToExtract);
        builder->target->emitTraceMessage(builder, msgStr.c_str(), 1, tmp.c_str());
    } else {
        msgStr = Util::printf_format("Parser: extracted %s (%u bits)", field, widthToExtract);
        builder->target->emitTraceMessage(builder, msgStr.c_str());
    }

    builder->newline();
}

void
StateTranslationVisitor::compileExtract(const IR::Expression* destination) {
    cstring msgStr;
    auto type = state->parser->typeMap->getType(destination);
    auto ht = type->to<IR::Type_StructLike>();
    if (ht == nullptr) {
        ::error(ErrorType::ERR_UNSUPPORTED_ON_TARGET,
                "Cannot extract to a non-struct type %1%", destination);
        return;
    }

    unsigned width = ht->width_bits();
    auto program = state->parser->program;

    cstring offsetStr = Util::printf_format("BYTES(%s + %s)",
                                            program->offsetVar, cstring::to_cstring(width));

    // FIXME: program->lengthVariable should be used instead of difference of end and start
    builder->target->emitTraceMessage(builder, "Parser: check pkt_len=%%d < last_read_byte=%%d", 2,
                              (program->packetEndVar + " - " + program->packetStartVar).c_str(),
                              offsetStr.c_str());

    // to load some fields the compiler will use larger words
    // than actual width of a field (e.g. 48-bit field loaded using load_dword())
    // we must ensure that the larger word is not outside of packet buffer.
    // FIXME: this can fail if a packet does not contain additional payload after header.
    //  However, we don't have better solution in case of using load_X functions to parse packet.
    unsigned curr_padding = 0;
    for (auto f : ht->fields) {
        auto ftype = state->parser->typeMap->getType(f);
        auto etype = EBPFTypeFactory::instance->create(ftype);
        if (etype->is<EBPFScalarType>()) {
            auto scalarType = etype->to<EBPFScalarType>();
            unsigned padding = scalarType->alignment() * 8 - scalarType->widthInBits();
            if (scalarType->widthInBits() + padding >= curr_padding) {
                curr_padding = padding;
            }
        }
    }

    builder->emitIndent();
    builder->appendFormat("if (%s < %s + BYTES(%s + %d + %u)) ",
                          program->packetEndVar.c_str(),
                          program->packetStartVar.c_str(),
                          program->offsetVar.c_str(), width, curr_padding);
    builder->blockStart();

    builder->target->emitTraceMessage(builder, "Parser: invalid packet (packet too short)");

    builder->emitIndent();
    builder->appendFormat("%s = %s;", program->errorVar.c_str(),
                          p4lib.packetTooShort.str());
    builder->newline();

    builder->emitIndent();
    builder->appendFormat("goto %s;", IR::ParserState::reject.c_str());
    builder->newline();
    builder->blockEnd(true);

    msgStr = Util::printf_format("Parser: extracting header %s", destination->toString());
    builder->target->emitTraceMessage(builder, msgStr.c_str());
    builder->newline();

    unsigned alignment = 0;
    for (auto f : ht->fields) {
        auto ftype = state->parser->typeMap->getType(f);
        auto etype = EBPFTypeFactory::instance->create(ftype);
        auto et = dynamic_cast<IHasWidth*>(etype);
        if (et == nullptr) {
            ::error(ErrorType::ERR_UNSUPPORTED_ON_TARGET,
                    "Only headers with fixed widths supported %1%", f);
            return;
        }
        compileExtractField(destination, f->name, alignment, etype);
        alignment += et->widthInBits();
        alignment %= 8;
    }

    if (ht->is<IR::Type_Header>()) {
        builder->emitIndent();
        visit(destination);
        builder->appendLine(".ebpf_valid = 1;");
    }

    msgStr = Util::printf_format("Parser: extracted %s", destination->toString());
    builder->target->emitTraceMessage(builder, msgStr.c_str());

    builder->newline();
}

void StateTranslationVisitor::compileAdvance(const P4::ExternMethod *ext) {
    auto argExpr = ext->expr->arguments->at(0)->expression;
    cstring argStr;
    if (auto cnst = argExpr->to<IR::Constant>()) {
        argStr = cstring::to_cstring(cnst->asUnsigned());
    } else {
        argStr = argExpr->toString();
    }
    cstring offsetStr = Util::printf_format("BYTES(%s + %s)",
                                            state->parser->program->offsetVar, argStr);
    builder->target->emitTraceMessage(builder, "Parser (advance): check pkt_len=%%d < "
                                               "last_read_byte=%%d", 2,
                                      (state->parser->program->packetEndVar + " - " +
                                              state->parser->program->packetStartVar).c_str(),
                                      offsetStr.c_str());
    builder->emitIndent();
    builder->appendFormat("if (%s < %s + BYTES(%s + ",
                          state->parser->program->packetEndVar.c_str(),
                          state->parser->program->packetStartVar.c_str(),
                          state->parser->program->offsetVar.c_str());
    visit(argExpr);
    builder->appendFormat(")) ");
    builder->blockStart();

    builder->target->emitTraceMessage(builder, "Parser: invalid packet (packet too short)");

    builder->emitIndent();
    builder->appendFormat("%s = %s;", state->parser->program->errorVar.c_str(),
                          p4lib.packetTooShort.str());
    builder->newline();

    builder->emitIndent();
    builder->appendFormat("goto %s;", IR::ParserState::reject.c_str());
    builder->newline();
    builder->blockEnd(true);

    builder->emitIndent();
    builder->appendFormat("%s = %s + ",
                          state->parser->program->offsetVar.c_str(),
                          state->parser->program->offsetVar.c_str());
    visit(argExpr);
    builder->endOfStatement(true);
}

void StateTranslationVisitor::processFunction(const P4::ExternFunction* function) {
    ::error(ErrorType::ERR_UNEXPECTED,
            "Unexpected extern function call in parser %1%", function->expr);
}

void StateTranslationVisitor::processMethod(const P4::ExternMethod* method) {
    auto expression = method->expr;

    auto decl = method->object;
    if (decl == state->parser->packet) {
        if (method->method->name.name == p4lib.packetIn.extract.name) {
            if (expression->arguments->size() != 1) {
                ::error(ErrorType::ERR_UNSUPPORTED_ON_TARGET,
                        "Variable-sized header fields not yet supported %1%", expression);
                return;
            }
            compileExtract(expression->arguments->at(0)->expression);
            return;
        } else if (method->method->name.name == p4lib.packetIn.length.name) {
            builder->append(state->parser->program->lengthVar);
            return;
        } else if (method->method->name.name == p4lib.packetIn.advance.name) {
            compileAdvance(method);
            return;
        }
        BUG("Unhandled packet method %1%", expression->method);
        return;
    }

    ::error(ErrorType::ERR_UNEXPECTED,
            "Unexpected extern method call in parser %1%", expression);
}

bool StateTranslationVisitor::preorder(const IR::MethodCallExpression* expression) {
    if (commentDescriptionDepth == 0)
        builder->append("/* ");
    commentDescriptionDepth++;
    visit(expression->method);
    builder->append("(");
    bool first = true;
    for (auto a  : *expression->arguments) {
        if (!first)
            builder->append(", ");
        first = false;
        visit(a);
    }
    builder->append(")");
    if (commentDescriptionDepth == 1) {
        builder->append(" */");
        builder->newline();
    }
    commentDescriptionDepth--;

    // do not process extern when comment is generated
    if (commentDescriptionDepth != 0)
        return false;

    auto mi = P4::MethodInstance::resolve(expression,
                                          state->parser->program->refMap,
                                          state->parser->program->typeMap);

    auto ef = mi->to<P4::ExternFunction>();
    if (ef != nullptr) {
        processFunction(ef);
        return false;
    }

    auto extMethod = mi->to<P4::ExternMethod>();
    if (extMethod != nullptr) {
        processMethod(extMethod);
        return false;
    }

    if (auto bim = mi->to<P4::BuiltInMethod>()) {
        builder->emitIndent();
        if (bim->name == IR::Type_Header::isValid) {
            visit(bim->appliedTo);
            builder->append(".ebpf_valid");
            return false;
        } else if (bim->name == IR::Type_Header::setValid) {
            visit(bim->appliedTo);
            builder->append(".ebpf_valid = true");
            return false;
        } else if (bim->name == IR::Type_Header::setInvalid) {
            visit(bim->appliedTo);
            builder->append(".ebpf_valid = false");
            return false;
        }
    }

    ::error(ErrorType::ERR_UNEXPECTED,
            "Unexpected method call in parser %1%", expression);
    return false;
}

bool StateTranslationVisitor::preorder(const IR::StructExpression *expr) {
    if (commentDescriptionDepth == 0)
        return CodeGenInspector::preorder(expr);

    // Dump structure for helper comment
    builder->append("{");
    bool first = true;
    for (auto c : expr->components) {
        if (!first)
            builder->append(", ");
        visit(c->expression);
        first = false;
    }
    builder->append("}");

    return false;
}

bool StateTranslationVisitor::preorder(const IR::Member* expression) {
    if (expression->expr->is<IR::PathExpression>()) {
        auto pe = expression->expr->to<IR::PathExpression>();
        auto decl = state->parser->program->refMap->getDeclaration(pe->path, true);
        if (decl == state->parser->packet) {
            builder->append(expression->member);
            return false;
        }
    }

    return CodeGenInspector::preorder(expression);
}

//////////////////////////////////////////////////////////////////

EBPFParser::EBPFParser(const EBPFProgram* program, const IR::ParserBlock* block,
                       const P4::TypeMap* typeMap) :
        program(program), typeMap(typeMap), parserBlock(block),
        packet(nullptr), headers(nullptr), headerType(nullptr) {
    visitor = new StateTranslationVisitor(program->refMap, program->typeMap);
}

void EBPFParser::emitDeclaration(CodeBuilder* builder, const IR::Declaration* decl) {
    if (decl->is<IR::Declaration_Variable>()) {
        auto vd = decl->to<IR::Declaration_Variable>();
        auto etype = EBPFTypeFactory::instance->create(vd->type);
        builder->emitIndent();
        etype->declare(builder, vd->name, false);
        builder->endOfStatement(true);
        BUG_CHECK(vd->initializer == nullptr,
                  "%1%: declarations with initializers not supported", decl);
        return;
    }
    BUG("%1%: not yet handled", decl);
}


void EBPFParser::emit(CodeBuilder* builder) {
    for (auto l : parserBlock->container->parserLocals)
        emitDeclaration(builder, l);

    builder->emitIndent();
    builder->appendFormat("goto %s;", IR::ParserState::start.c_str());
    builder->newline();

    visitor->setBuilder(builder);
    for (auto s : states) {
        visitor->setState(s);
        s->state->apply(*visitor);
    }

    builder->newline();

    // Create a synthetic reject state
    builder->emitIndent();
    builder->appendFormat("%s:", IR::ParserState::reject.c_str());
    builder->spc();
    builder->blockStart();

    // This state may be called from deparser, so do not explicitly tell source of this event.
    builder->target->emitTraceMessage(builder, "Packet rejected");

    emitRejectState(builder);

    builder->blockEnd(true);
    builder->newline();
}

bool EBPFParser::build() {
    auto pl = parserBlock->container->type->applyParams;
    if (pl->size() != 2) {
        ::error(ErrorType::ERR_EXPECTED,
                "Expected parser to have exactly 2 parameters");
        return false;
    }

    auto it = pl->parameters.begin();
    packet = *it; ++it;
    headers = *it;
    for (auto state : parserBlock->container->states) {
        auto ps = new EBPFParserState(state, this);
        states.push_back(ps);
    }

    auto ht = typeMap->getType(headers);
    if (ht == nullptr)
        return false;
    headerType = EBPFTypeFactory::instance->create(ht);
    return true;
}

void EBPFParser::emitRejectState(CodeBuilder* builder) {
    builder->emitIndent();
    builder->appendFormat("return %s;", builder->target->abortReturnCode().c_str());
    builder->newline();
}

}  // namespace EBPF
