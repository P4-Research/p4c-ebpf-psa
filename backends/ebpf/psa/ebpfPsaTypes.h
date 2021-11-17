#ifndef BACKENDS_EBPF_PSA_EBPFPSATYPES_H_
#define BACKENDS_EBPF_PSA_EBPFPSATYPES_H_

#include "backends/ebpf/ebpfType.h"

namespace EBPF {

// represents an error type for PSA
class EBPFErrorTypePSA : public EBPFType {
 public:
    explicit EBPFErrorTypePSA(const IR::Type_Error * type) : EBPFType(type) {}

    void emit(CodeBuilder* builder) override;
    void declare(CodeBuilder* builder, cstring id, bool asPointer) override;
    void emitInitializer(CodeBuilder* builder) override;

    const IR::Type_Error* getType() const { return type->to<IR::Type_Error>(); }
};

class EBPFHeaderTypePSA : public EBPFStructType {
 protected:
    class FieldsGroup {
     public:
        std::vector<EBPFField*> fields;
        unsigned int groupWidth = 0;
        unsigned int groupOffset = 0;
//        bool byteSwapRequired = false;
    };

    void createFieldsGroups();
    void emitField(CodeBuilder* builder, EBPFField* field);

 public:
    std::vector<FieldsGroup*> groupedFields;

    explicit EBPFHeaderTypePSA(const IR::Type_Header* header);

//    void declare(CodeBuilder* builder, cstring id, bool asPointer) override;
//    void emitInitializer(CodeBuilder* builder) override;
//    unsigned widthInBits() override { return width; }
//    unsigned implementationWidthInBits() override { return implWidth; }
    void emit(CodeBuilder* builder) override;
//    void declareArray(CodeBuilder* builder, cstring id, unsigned size) override;
};

}  // namespace EBPF

#endif  /* BACKENDS_EBPF_PSA_EBPFPSATYPES_H_ */
