/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_x86_LIR_x86_h
#define jit_x86_LIR_x86_h

namespace js {
namespace jit {

class LBoxFloatingPoint : public LInstructionHelper<2, 1, 1>
{
    MIRType type_;

  public:
    LIR_HEADER(BoxFloatingPoint);

    LBoxFloatingPoint(const LAllocation& in, const LDefinition& temp, MIRType type)
      : type_(type)
    {
        MOZ_ASSERT(IsFloatingPointType(type));
        setOperand(0, in);
        setTemp(0, temp);
    }

    MIRType type() const {
        return type_;
    }
    const char* extraName() const {
        return StringFromMIRType(type_);
    }
};

class LUnbox : public LInstructionHelper<1, 2, 0>
{
  public:
    LIR_HEADER(Unbox);

    MUnbox* mir() const {
        return mir_->toUnbox();
    }
    const LAllocation* payload() {
        return getOperand(0);
    }
    const LAllocation* type() {
        return getOperand(1);
    }
    const char* extraName() const {
        return StringFromMIRType(mir()->type());
    }
};

class LUnboxFloatingPoint : public LInstructionHelper<1, 2, 0>
{
    MIRType type_;

  public:
    LIR_HEADER(UnboxFloatingPoint);

    static const size_t Input = 0;

    LUnboxFloatingPoint(const LBoxAllocation& input, MIRType type)
      : type_(type)
    {
        setBoxOperand(Input, input);
    }

    MUnbox* mir() const {
        return mir_->toUnbox();
    }

    MIRType type() const {
        return type_;
    }
    const char* extraName() const {
        return StringFromMIRType(type_);
    }
};

// Convert a 32-bit unsigned integer to a double.
class LWasmUint32ToDouble : public LInstructionHelper<1, 1, 1>
{
  public:
    LIR_HEADER(WasmUint32ToDouble)

    LWasmUint32ToDouble(const LAllocation& input, const LDefinition& temp) {
        setOperand(0, input);
        setTemp(0, temp);
    }
    const LDefinition* temp() {
        return getTemp(0);
    }
};

// Convert a 32-bit unsigned integer to a float32.
class LWasmUint32ToFloat32: public LInstructionHelper<1, 1, 1>
{
  public:
    LIR_HEADER(WasmUint32ToFloat32)

    LWasmUint32ToFloat32(const LAllocation& input, const LDefinition& temp) {
        setOperand(0, input);
        setTemp(0, temp);
    }
    const LDefinition* temp() {
        return getTemp(0);
    }
};

class LDivOrModI64 : public LCallInstructionHelper<INT64_PIECES, INT64_PIECES*2 + 1, 0>
{
  public:
    LIR_HEADER(DivOrModI64)

    static const size_t Lhs = 0;
    static const size_t Rhs = INT64_PIECES;

    LDivOrModI64(const LInt64Allocation& lhs, const LInt64Allocation& rhs,
                 const LAllocation& instance = LAllocation())
    {
        setInt64Operand(Lhs, lhs);
        setInt64Operand(Rhs, rhs);
        setOperand(2 * INT64_PIECES, instance);
    }

    bool hasInstance() {
        return !getOperand(2 * INT64_PIECES)->isBogus();
    }
    const LAllocation* instance() {
        return getOperand(2 * INT64_PIECES);
    }
    bool isModulo() const {
        return mir_->isMod() || mir_->isWasmBuiltinModI64();
    }
    bool canBeDivideByZero() const {
        if (mir_->isWasmBuiltinDivI64())
            return mir_->toWasmBuiltinDivI64()->canBeDivideByZero();
        if (mir_->isWasmBuiltinModI64())
            return mir_->toWasmBuiltinModI64()->canBeDivideByZero();
        if (mir_->isMod())
            return mir_->toMod()->canBeDivideByZero();
        return mir_->toDiv()->canBeDivideByZero();
    }
    bool canBeNegativeOverflow() const {
        if (mir_->isWasmBuiltinDivI64())
            return mir_->toWasmBuiltinDivI64()->canBeNegativeOverflow();
        if (mir_->isWasmBuiltinModI64())
            return mir_->toWasmBuiltinModI64()->canBeNegativeDividend();
        if (mir_->isMod())
            return mir_->toMod()->canBeNegativeDividend();
        return mir_->toDiv()->canBeNegativeOverflow();
    }
    wasm::TrapOffset trapOffset() const {
        if (mir_->isWasmBuiltinDivI64())
            return mir_->toWasmBuiltinDivI64()->trapOffset();
        if (mir_->isWasmBuiltinModI64())
            return mir_->toWasmBuiltinModI64()->trapOffset();
        MOZ_ASSERT(mir_->isDiv() || mir_->isMod());
        if (mir_->isMod())
            return mir_->toMod()->trapOffset();
        return mir_->toDiv()->trapOffset();
    }
};

class LUDivOrModI64 : public LCallInstructionHelper<INT64_PIECES, INT64_PIECES*2 + 1, 0>
{
  public:
    LIR_HEADER(UDivOrModI64)

    static const size_t Lhs = 0;
    static const size_t Rhs = INT64_PIECES;

    LUDivOrModI64(const LInt64Allocation& lhs, const LInt64Allocation& rhs,
                 const LAllocation& instance = LAllocation())
    {
        setInt64Operand(Lhs, lhs);
        setInt64Operand(Rhs, rhs);
        setOperand(2 * INT64_PIECES, instance);
    }

    bool hasInstance() {
        return !getOperand(2 * INT64_PIECES)->isBogus();
    }
    const LAllocation* instance() {
        return getOperand(2 * INT64_PIECES);
    }
    bool isModulo() const {
        return mir_->isMod() || mir_->isWasmBuiltinModI64();
    }
    bool canBeDivideByZero() const {
        if (mir_->isWasmBuiltinDivI64())
            return mir_->toWasmBuiltinDivI64()->canBeDivideByZero();
        if (mir_->isWasmBuiltinModI64())
            return mir_->toWasmBuiltinModI64()->canBeDivideByZero();
        if (mir_->isMod())
            return mir_->toMod()->canBeDivideByZero();
        return mir_->toDiv()->canBeDivideByZero();
    }
    bool canBeNegativeOverflow() const {
        if (mir_->isWasmBuiltinDivI64())
            return mir_->toWasmBuiltinDivI64()->canBeNegativeOverflow();
        if (mir_->isWasmBuiltinModI64())
            return mir_->toWasmBuiltinModI64()->canBeNegativeDividend();
        if (mir_->isMod())
            return mir_->toMod()->canBeNegativeDividend();
        return mir_->toDiv()->canBeNegativeOverflow();
    }
    wasm::TrapOffset trapOffset() const {
        if (mir_->isWasmBuiltinDivI64())
            return mir_->toWasmBuiltinDivI64()->trapOffset();
        if (mir_->isWasmBuiltinModI64())
            return mir_->toWasmBuiltinModI64()->trapOffset();
        MOZ_ASSERT(mir_->isDiv() || mir_->isMod());
        if (mir_->isMod())
            return mir_->toMod()->trapOffset();
        return mir_->toDiv()->trapOffset();
    }
};

class LWasmTruncateToInt64 : public LInstructionHelper<INT64_PIECES, 1, 1>
{
  public:
    LIR_HEADER(WasmTruncateToInt64);

    LWasmTruncateToInt64(const LAllocation& in, const LDefinition& temp)
    {
        setOperand(0, in);
        setTemp(0, temp);
    }

    MWasmTruncateToInt64* mir() const {
        return mir_->toWasmTruncateToInt64();
    }

    const LDefinition* temp() {
        return getTemp(0);
    }
};

} // namespace jit
} // namespace js

#endif /* jit_x86_LIR_x86_h */
