// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the
// distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2012 the V8 project authors. All rights reserved.

//
// Copyright IBM Corp. 2012, 2013. All rights reserved.
//

#include "v8.h"

#if defined(V8_TARGET_ARCH_S390)

#include "s390/assembler-s390-inl.h"
#include "serialize.h"

namespace v8 {
namespace internal {

#ifdef DEBUG
bool CpuFeatures::initialized_ = false;
#endif
unsigned CpuFeatures::supported_ = 0;
unsigned CpuFeatures::found_by_runtime_probing_ = 0;

// Get the CPU features enabled by the build.
static unsigned CpuFeaturesImpliedByCompiler() {
  unsigned answer = 0;
  return answer;
}

#if !defined(_AIX)
// This function uses types in elf.h
static bool is_processor(const char* p) {
  static bool read_tried = false;
  static char *auxv_cpu_type = NULL;

  if (!read_tried) {
    // Open the AUXV (auxilliary vector) psuedo-file
    int fd = open("/proc/self/auxv", O_RDONLY);

    read_tried = true;
    if (fd != -1) {
#if V8_TARGET_ARCH_S390X
      static Elf64_auxv_t buffer[16];
      Elf64_auxv_t *auxv_element;
#else
      static Elf32_auxv_t buffer[16];
      Elf32_auxv_t *auxv_element;
#endif
      int bytes_read = 0;
      while (bytes_read >= 0) {
        // Read a chunk of the AUXV
        bytes_read = read(fd, buffer, sizeof(buffer));
        // Locate and read the platform field of AUXV if it is in the chunk
        for (auxv_element = buffer;
             auxv_element+sizeof(auxv_element) <= buffer+bytes_read &&
             auxv_element->a_type != AT_NULL;
             auxv_element++) {
          if (auxv_element->a_type == AT_PLATFORM) {
            /* Note: Both auxv_cpu_type and buffer are static */
            auxv_cpu_type = reinterpret_cast<char*>(auxv_element->a_un.a_val);
            goto done_reading;
          }
        }
      }
      done_reading:
      close(fd);
    }
  }

  if (auxv_cpu_type == NULL) {
    return false;
  }
  return (strcmp(auxv_cpu_type, p) == 0);
}
#endif

void CpuFeatures::Probe() {
  unsigned standard_features = static_cast<unsigned>(
      OS::CpuFeaturesImpliedByPlatform()) | CpuFeaturesImpliedByCompiler();
  ASSERT(supported_ == 0 || supported_ == standard_features);
#ifdef DEBUG
  initialized_ = true;
#endif

  // Get the features implied by the OS and the compiler settings. This is the
  // minimal set of features which is also alowed for generated code in the
  // snapshot.
  supported_ |= standard_features;

  if (Serializer::enabled()) {
    // No probing for features if we might serialize (generate snapshot).
    return;
  }

  // Detect whether frim instruction is supported (POWER5+)
  // For now we will just check for processors we know do not
  // support it
#if !defined(_AIX)
  if (!is_processor("ppc970") /* G5 */ && !is_processor("ppc7450") /* G4 */) {
    // Assume support
    supported_ |= (1u << FPU);
  }
#else
  // Fallback: assume frim is supported -- will implement processor
  // detection for other PPC platforms in is_processor() if required
  supported_ |= (1u << FPU);
#endif
}

Register ToRegister(int num) {
  ASSERT(num >= 0 && num < kNumRegisters);
  const Register kRegisters[] = {
    r0,
    r1,
    r2, r3, r4, r5, r6, r7, r8, r9, r10,
    fp, ip, r13, r14, sp
  };
  return kRegisters[num];
}


// -----------------------------------------------------------------------------
// Implementation of RelocInfo

const int RelocInfo::kApplyMask = 1 << RelocInfo::INTERNAL_REFERENCE;


bool RelocInfo::IsCodedSpecially() {
  // The deserializer needs to know whether a pointer is specially
  // coded.  Being specially coded on PPC means that it is a lis/ori
  // instruction sequence, and that is always the case inside code
  // objects.
  return true;
}


void RelocInfo::PatchCode(byte* instructions, int num_bytes) {
  // Patch the code at the current address with the supplied instructions.
  byte* pc = reinterpret_cast<byte*>(pc_);
  byte *instr = instructions;

  // We patch byte to byte as instructions have to be stored in big endian
  // regardless of host's endianness
  for (int i = 0; i < num_bytes; i++) {
    *(pc++) = *(instr++);
  }

  // Indicate that code has changed.
  CPU::FlushICache(pc_, num_bytes);
}


// Patch the code at the current PC with a call to the target address.
// Additional guard instructions can be added if required.
void RelocInfo::PatchCodeWithCall(Address target, int guard_bytes) {
  // Patch the code at the current address with a call to the target.
  UNIMPLEMENTED();
}


// -----------------------------------------------------------------------------
// Implementation of Operand and MemOperand
// See assembler-s390-inl.h for inlined constructors

Operand::Operand(Handle<Object> handle) {
  rm_ = no_reg;
  // Verify all Objects referred by code are NOT in new space.
  Object* obj = *handle;
  ASSERT(!HEAP->InNewSpace(obj));
  if (obj->IsHeapObject()) {
    imm_ = reinterpret_cast<intptr_t>(handle.location());
    rmode_ = RelocInfo::EMBEDDED_OBJECT;
  } else {
    // no relocation needed
    imm_ =  reinterpret_cast<intptr_t>(obj);
    rmode_ = RelocInfo::NONE;
  }
}

MemOperand::MemOperand(Register rn, int32_t offset) {
  baseRegister = rn;
  indexRegister = r0;
  offset_ = offset;
}

MemOperand::MemOperand(Register rx, Register rb, int32_t offset) {
  baseRegister = rb;
  indexRegister = rx;
  offset_ = offset;
}

// -----------------------------------------------------------------------------
// Specific instructions, constants, and masks.

// Spare buffer.
static const int kMinimalBufferSize = 4*KB;


Assembler::Assembler(Isolate* arg_isolate, void* buffer, int buffer_size)
    : AssemblerBase(arg_isolate),
      recorded_ast_id_(TypeFeedbackId::None()),
      positions_recorder_(this),
      emit_debug_code_(FLAG_debug_code),
      predictable_code_size_(false) {
  if (buffer == NULL) {
    // Do our own buffer management.
    if (buffer_size <= kMinimalBufferSize) {
      buffer_size = kMinimalBufferSize;

      if (isolate()->assembler_spare_buffer() != NULL) {
        buffer = isolate()->assembler_spare_buffer();
        isolate()->set_assembler_spare_buffer(NULL);
      }
    }
    if (buffer == NULL) {
      buffer_ = NewArray<byte>(buffer_size);
    } else {
      buffer_ = static_cast<byte*>(buffer);
    }
    buffer_size_ = buffer_size;
    own_buffer_ = true;

  } else {
    // Use externally provided buffer instead.
    ASSERT(buffer_size > 0);
    buffer_ = static_cast<byte*>(buffer);
    buffer_size_ = buffer_size;
    own_buffer_ = false;
  }

  // Set up buffer pointers.
  ASSERT(buffer_ != NULL);
  pc_ = buffer_;
  reloc_info_writer.Reposition(buffer_ + buffer_size, pc_);

  no_trampoline_pool_before_ = 0;
  trampoline_pool_blocked_nesting_ = 0;
  // We leave space (kMaxBlockTrampolineSectionSize)
  // for BlockTrampolinePoolScope buffer.
  next_buffer_check_ = kMaxCondBranchReach - kMaxBlockTrampolineSectionSize;
  internal_trampoline_exception_ = false;
  last_bound_pos_ = 0;

  trampoline_emitted_ = false;
  unbound_labels_count_ = 0;

  ClearRecordedAstId();
}


Assembler::~Assembler() {
  if (own_buffer_) {
    if (isolate()->assembler_spare_buffer() == NULL &&
        buffer_size_ == kMinimalBufferSize) {
      isolate()->set_assembler_spare_buffer(buffer_);
    } else {
      DeleteArray(buffer_);
    }
  }
}


void Assembler::GetCode(CodeDesc* desc) {
  // Set up code descriptor.
  desc->buffer = buffer_;
  desc->buffer_size = buffer_size_;
  desc->instr_size = pc_offset();
  desc->reloc_size = (buffer_ + buffer_size_) - reloc_info_writer.pos();
}


void Assembler::Align(int m) {
  ASSERT(m >= 4 && IsPowerOf2(m));
  while ((pc_offset() & (m - 1)) != 0) {
    nop();
  }
}


void Assembler::CodeTargetAlign() {
  Align(8);
}


Condition Assembler::GetCondition(Instr instr) {
  switch (instr & kCondMask) {
    case BT:
      return eq;
    case BF:
      return ne;
    default:
      UNIMPLEMENTED();
    }
  return al;
}

// PowerPC
bool Assembler::IsBranch(Instr instr) {
  return ((instr & kOpcodeMask) == BCX);
}

// end PowerPC

Register Assembler::GetRA(Instr instr) {
  Register reg;
  reg.code_ = Instruction::RAValue(instr);
  return reg;
}

Register Assembler::GetRB(Instr instr) {
  Register reg;
  reg.code_ = Instruction::RBValue(instr);
  return reg;
}

#if V8_TARGET_ARCH_S390X
// This code assumes a FIXED_SEQUENCE for 64bit loads (iihf/iilf)
bool Assembler::Is64BitLoadIntoIP(SixByteInstr instr1, SixByteInstr instr2) {
  // Check the instructions are the iihf/iilf load into ip
  return (((instr1 >> 32) == 0xC0C8) && ((instr2 >> 32) == 0xC0C9));
}
#else
// This code assumes a FIXED_SEQUENCE for 32bit loads (iilf)
bool Assembler::Is32BitLoadIntoIP(SixByteInstr instr) {
  // Check the instruction is an iilf load into ip/r12.
  return ((instr >> 32) == 0xC0C9);
}
#endif

bool Assembler::IsCmpRegister(Instr instr) {
  ASSERT(false);
  return Instruction::S390OpcodeValue(reinterpret_cast<byte*>(&instr)) == CR;
}

bool Assembler::IsRlwinm(Instr instr) {
  return ((instr & kOpcodeMask) == RLWINMX);
}

// Labels refer to positions in the (to be) generated code.
// There are bound, linked, and unused labels.
//
// Bound labels refer to known positions in the already
// generated code. pos() is the position the label refers to.
//
// Linked labels refer to unknown positions in the code
// to be generated; pos() is the position of the last
// instruction using the label.


// The link chain is terminated by a negative code position (must be aligned)
const int kEndOfChain = -4;


int Assembler::target_at(int pos)  {
  SixByteInstr instr = instr_at(pos);
  // check which type of branch this is 16 or 26 bit offset
  Opcode opcode = Instruction::S390OpcodeValue(buffer_ + pos);

  if (BRC == opcode || BRCT == opcode || BRCTG == opcode) {
    int16_t imm16 = SIGN_EXT_IMM16((instr & kImm16Mask));
    imm16 <<= 1;   // BRC immediate is in # of halfwords
    if (imm16 == 0)
      return kEndOfChain;
    return pos + imm16;
  } else if (BRCL == opcode || LARL == opcode || BRASL == opcode) {
    int32_t imm32 = static_cast<int32_t>(
        instr & (static_cast<uint64_t>(0xffffffff)));
    imm32 <<= 1;   // BRCL immediate is in # of halfwords
    if (imm32 == 0)
      return kEndOfChain;
    return pos + imm32;
  }

  // Unknown condition
  ASSERT(false);
  return -1;
}

void Assembler::target_at_put(int pos, int target_pos) {
  SixByteInstr instr = instr_at(pos);
  Opcode opcode = Instruction::S390OpcodeValue(buffer_ + pos);

  if (BRC == opcode || BRCT == opcode || BRCTG == opcode) {
    int16_t imm16 = target_pos - pos;
    instr &= (~0xffff);
    ASSERT(is_int16(imm16));
    instr_at_put<FourByteInstr>(pos, instr | (imm16 >> 1));
    return;
  } else if (BRCL == opcode || LARL == opcode || BRASL == opcode) {
    // BRCL / LARL
    int32_t imm32 = target_pos - pos;
    instr &= (~static_cast<uint64_t>(0xffffffff));
    instr_at_put<SixByteInstr>(pos, instr | (imm32 >> 1));
    return;
  }

  ASSERT(false);
}

int Assembler::max_reach_from(int pos) {
  Opcode opcode = Instruction::S390OpcodeValue(buffer_ + pos);

  // Check which type of instr.  In theory, we can return
  // the values below + 1, given offset is # of halfwords
  if (BRC == opcode || BRCT == opcode || BRCTG == opcode) {
    return 16;
  } else if (BRCL == opcode || LARL == opcode || BRASL == opcode) {
    return 31;  // Using 31 as workaround instead of 32 as
                // is_intn(x,32) doesn't work on 32-bit platforms.
  }

  ASSERT(false);
  return 16;
}

void Assembler::bind_to(Label* L, int pos) {
  ASSERT(0 <= pos && pos <= pc_offset());  // must have a valid binding position
  // int32_t trampoline_pos = kInvalidSlotPos;
  if (L->is_linked() && !trampoline_emitted_) {
    unbound_labels_count_--;
    next_buffer_check_ += kTrampolineSlotsSize;
  }

  while (L->is_linked()) {
    int fixup_pos = L->pos();
#ifdef DEBUG
    int32_t offset = pos - fixup_pos;
    int maxReach = max_reach_from(fixup_pos);
#endif
    next(L);  // call next before overwriting link with target at fixup_pos
    // if (is_intn(offset, maxReach) == false) {
      // if (trampoline_pos == kInvalidSlotPos) {
        // trampoline_pos = get_trampoline_entry();
        // CHECK(trampoline_pos != kInvalidSlotPos);
        // target_at_put(trampoline_pos, pos);
      // }
      // target_at_put(fixup_pos, trampoline_pos);
    // } else {
      ASSERT(is_intn(offset, maxReach));
      target_at_put(fixup_pos, pos);
    // }
  }
  L->bind_to(pos);

  // Keep track of the last bound label so we don't eliminate any instructions
  // before a bound label.
  if (pos > last_bound_pos_)
    last_bound_pos_ = pos;
}

void Assembler::bind(Label* L) {
  ASSERT(!L->is_bound());  // label can only be bound once
  bind_to(L, pc_offset());
}


void Assembler::next(Label* L) {
  ASSERT(L->is_linked());
  int link = target_at(L->pos());
  if (link == kEndOfChain) {
    L->Unuse();
  } else {
    ASSERT(link >= 0);
    L->link_to(link);
  }
}

bool Assembler::is_near(Label* L, Condition cond) {
  ASSERT(L->is_bound());
  if (L->is_bound() == false)
    return false;

  int maxReach = ((cond == al) ? 26 : 16);
  int offset = L->pos() - pc_offset();

  return is_intn(offset, maxReach);
}

void Assembler::a_form(Instr instr,
                       DoubleRegister frt,
                       DoubleRegister fra,
                       DoubleRegister frb,
                       RCBit r) {
  emit(instr | frt.code()*B21 | fra.code()*B16 | frb.code()*B11 | r);
}

void Assembler::d_form(Instr instr,
                        Register rt,
                        Register rb,
                        const intptr_t val,
                        bool signed_disp) {
  if (signed_disp) {
    if (!is_int16(val)) {
      PrintF("val = %" V8PRIdPTR ", 0x%" V8PRIxPTR "\n", val, val);
    }
    ASSERT(is_int16(val));
  } else {
    if (!is_uint16(val)) {
      PrintF("val = %" V8PRIdPTR ", 0x%" V8PRIxPTR
             ", is_unsigned_imm16(val)=%d, kImm16Mask=0x%x\n",
             val, val, is_uint16(val), kImm16Mask);
    }
    ASSERT(is_uint16(val));
  }
  emit(instr | rt.code()*B21 | rb.code()*B16 | (kImm16Mask & val));
}

void Assembler::x_form(Instr instr,
                         Register rb,
                         Register rs,
                         Register rx,
                         RCBit r) {
  emit(instr | rs.code()*B21 | rb.code()*B16 | rx.code()*B11 | r);
}

void Assembler::xo_form(Instr instr,
                         Register rt,
                         Register rb,
                         Register rx,
                         OEBit o,
                         RCBit r) {
  emit(instr | rt.code()*B21 | rb.code()*B16 | rx.code()*B11 | o | r);
}

void Assembler::md_form(Instr instr,
                        Register rb,
                        Register rs,
                        int shift,
                        int maskbit,
                        RCBit r) {
  int sh0_4 = shift & 0x1f;
  int sh5   = (shift >> 5) & 0x1;
  int m0_4  = maskbit & 0x1f;
  int m5    = (maskbit >> 5) & 0x1;

  emit(instr | rs.code()*B21 | rb.code()*B16 |
       sh0_4*B11 | m0_4*B6 | m5*B5 | sh5*B1 | r);
}

// Returns the next free trampoline entry.
int32_t Assembler::get_trampoline_entry() {
  int32_t trampoline_entry = kInvalidSlotPos;

  if (!internal_trampoline_exception_) {
    trampoline_entry = trampoline_.take_slot();

    if (kInvalidSlotPos == trampoline_entry) {
      internal_trampoline_exception_ = true;
    }
  }
  return trampoline_entry;
}

int Assembler::branch_offset(Label* L, bool jump_elimination_allowed) {
  int target_pos;
  if (L->is_bound()) {
    target_pos = L->pos();
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();  // L's link
    } else {
      // was: target_pos = kEndOfChain;
      // However, using branch to self to mark the first reference
      // should avoid most instances of branch offset overflow.  See
      // target_at() for where this is converted back to kEndOfChain.
      target_pos = pc_offset();
      if (!trampoline_emitted_) {
        unbound_labels_count_++;
        next_buffer_check_ -= kTrampolineSlotsSize;
      }
    }
    L->link_to(pc_offset());
  }

  return target_pos - pc_offset();
}


void Assembler::label_at_put(Label* L, int at_offset) {
  int target_pos;
  if (L->is_bound()) {
    target_pos = L->pos();
    instr_at_put(at_offset, target_pos + (Code::kHeaderSize - kHeapObjectTag));
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();  // L's link
    } else {
      // was: target_pos = kEndOfChain;
      // However, using branch to self to mark the first reference
      // should avoid most instances of branch offset overflow.  See
      // target_at() for where this is converted back to kEndOfChain.
      target_pos = at_offset;
      if (!trampoline_emitted_) {
        unbound_labels_count_++;
        next_buffer_check_ -= kTrampolineSlotsSize;
      }
    }
    L->link_to(at_offset);

    Instr constant = target_pos - at_offset;
    ASSERT(is_int16(constant));
    instr_at_put(at_offset, constant);
  }
}


// Branch instructions.

// PowerPC
void Assembler::bclr(BOfield bo, LKBit lk) {
  positions_recorder()->WriteRecordedPositions();
  emit(EXT1 | bo | BCLRX | lk);
}

void Assembler::bcctr(BOfield bo, LKBit lk) {
  positions_recorder()->WriteRecordedPositions();
  emit(EXT1 | bo | BCCTRX | lk);
}

// Pseudo op - branch to count register -- used for "jump"
void Assembler::bcr() {
  bcctr(BA, LeaveLK);
}

// Pseudo op - branch on condition
void Assembler::branchOnCond(Condition c, int branch_offset, bool is_bound) {
  positions_recorder()->WriteRecordedPositions();
  int offset = branch_offset;
  if (is_bound && is_int16(offset)) {
    brc(c, Operand(offset & 0xFFFF));  // short jump
  } else {
    brcl(c, Operand(offset));          // long jump
  }
}

// Branch On Count (32)
void Assembler::brct(Register r1, const Operand& imm) {
  // BRCT actually encodes # of halfwords, so divide by 2.
  int16_t numHalfwords = static_cast<int16_t>(imm.immediate()) / 2;
  Operand halfwordOp = Operand(numHalfwords);
  halfwordOp.setBits(16);
  ri_form(BRCT, r1, halfwordOp);
}

// Branch On Count (32)
void Assembler::brctg(Register r1, const Operand& imm) {
  // BRCTG actually encodes # of halfwords, so divide by 2.
  int16_t numHalfwords = static_cast<int16_t>(imm.immediate()) / 2;
  Operand halfwordOp = Operand(numHalfwords);
  halfwordOp.setBits(16);
  ri_form(BRCTG, r1, halfwordOp);
}

// Indirect Conditional Branch via register
void Assembler::bcr(Condition m, Register target) {
  rr_form(BCR, m, target);
}

void Assembler::cntlzw_(Register rb, Register rs, RCBit rc) {
  x_form(EXT2 | CNTLZWX, rb, rs, r0, rc);
}

void Assembler::rlwimi(Register rb, Register rs,
                       int sh, int mb, int me, RCBit rc) {
  sh &= 0x1f;
  mb &= 0x1f;
  me &= 0x1f;
  emit(RLWIMIX | rs.code()*B21 | rb.code()*B16 | sh*B11 | mb*B6 | me << 1 | rc);
}

void Assembler::subfc(Register dst, Register src1, Register src2,
                    OEBit o, RCBit r) {
  xo_form(EXT2 | SUBFCX, dst, src2, src1, o, r);
}

void Assembler::subfic(Register dst, Register src, const Operand& imm) {
  d_form(SUBFIC, dst, src, imm.imm_, true);
}

// Multiply low word
void Assembler::mullw(Register dst, Register src1, Register src2,
                    OEBit o, RCBit r) {
  xo_form(EXT2 | MULLW, dst, src1, src2, o, r);
}

// Multiply hi word
void Assembler::mulhw(Register dst, Register src1, Register src2,
                    OEBit o, RCBit r) {
  xo_form(EXT2 | MULHWX, dst, src1, src2, o, r);
}

// Divide word
void Assembler::divw(Register dst, Register src1, Register src2,
                     OEBit o, RCBit r) {
  xo_form(EXT2 | DIVW, dst, src1, src2, o, r);
}

void Assembler::addic(Register dst, Register src, const Operand& imm) {
  d_form(ADDIC, dst, src, imm.imm_, true);
}

void Assembler::andis(Register rb, Register rs, const Operand& imm) {
  d_form(ANDISx, rs, rb, imm.imm_, false);
}

void Assembler::nor(Register dst, Register src1, Register src2, RCBit r) {
  x_form(EXT2 | NORX, dst, src1, src2, r);
}

void  Assembler::lis(Register dst, const Operand& imm) {
  d_form(ADDIS, dst, r0, imm.imm_, true);
}

// 32-bit Store Multiple - short displacement (12-bits unsigned)
void Assembler::stm(Register r1, Register r2, const MemOperand& src) {
  rs_form(STM, r1, r2, src.rb(), src.offset());
}
// 32-bit Store Multiple - long displacement (20-bits signed)
void Assembler::stmy(Register r1, Register r2, const MemOperand& src) {
  rsy_form(STMY, r1, r2, src.rb(), src.offset());
}
// 64-bit Store Multiple - long displacement (20-bits signed)
void Assembler::stmg(Register r1, Register r2, const MemOperand& src) {
  rsy_form(STMG, r1, r2, src.rb(), src.offset());
}

#if V8_TARGET_ARCH_S390X
// 64bit specific instructions
void Assembler::ld(Register rd, const MemOperand &src) {
  int offset = src.offset();
  ASSERT(!(offset & 3) && is_int16(offset));
  offset = kImm16Mask & offset;
  emit(LD_ppc | rd.code()*B21 | src.rb().code()*B16 | offset);
}

void Assembler::ldx(Register rd, const MemOperand &src) {
  Register rb = src.rb();
  Register rx = src.rx();
  emit(EXT2 | LDX | rd.code()*B21 | rb.code()*B16 | rx.code()*B11);
}

void Assembler::ldu(Register rd, const MemOperand &src) {
  int offset = src.offset();
  ASSERT(!(offset & 3) && is_int16(offset));
  offset = kImm16Mask & offset;
  emit(LD_ppc | rd.code()*B21 | src.rb().code()*B16 | offset | 1);
}

void Assembler::ldux(Register rd, const MemOperand &src) {
  Register rb = src.rb();
  Register rx = src.rx();
  emit(EXT2 | LDUX | rd.code()*B21 | rb.code()*B16 | rx.code()*B11);
}

void Assembler::stdu(Register rs, const MemOperand &src) {
  int offset = src.offset();
  ASSERT(!(offset & 3) && is_int16(offset));
  offset = kImm16Mask & offset;
  emit(STD_ppc | rs.code()*B21 | src.rb().code()*B16 | offset | 1);
}

void Assembler::stdux(Register rs, const MemOperand &src) {
  Register rb = src.rb();
  Register rx = src.rx();
  emit(EXT2 | STDUX | rs.code()*B21 | rb.code()*B16 | rx.code()*B11);
}

void Assembler::sradi(Register rb, Register rs, int sh, RCBit r) {
  int sh0_4 = sh & 0x1f;
  int sh5   = (sh >> 5) & 0x1;

  emit(EXT2 | SRADIX | rs.code()*B21 | rb.code()*B16 | sh0_4*B11 | sh5*B1 | r);
}

void Assembler::srd(Register dst, Register src1, Register src2, RCBit r) {
  x_form(EXT2 | SRDX, dst, src1, src2, r);
}

void Assembler::sld(Register dst, Register src1, Register src2, RCBit r) {
  x_form(EXT2 | SLDX, dst, src1, src2, r);
}

void Assembler::srad(Register rb, Register rs, Register rx, RCBit r) {
  x_form(EXT2 | SRAD, rb, rs, rx, r);
}

void Assembler::cntlzd_(Register rb, Register rs, RCBit rc) {
  x_form(EXT2 | CNTLZDX, rb, rs, r0, rc);
}

void Assembler::divd(Register dst, Register src1, Register src2,
                     OEBit o, RCBit r) {
  xo_form(EXT2 | DIVD, dst, src1, src2, o, r);
}
#endif


void Assembler::fake_asm(enum FAKE_OPCODE_T fopcode) {
  ASSERT(fopcode < fLastFaker);
  emit(FAKE_OPCODE | FAKER_SUBOPCODE | fopcode);
}

void Assembler::marker_asm(int mcode) {
  if (::v8::internal::FLAG_trace_sim_stubs) {
    ASSERT(mcode < F_NEXT_AVAILABLE_STUB_MARKER);
    emit(FAKE_OPCODE | MARKER_SUBOPCODE | mcode);
  }
}

// Function descriptor for AIX.
// Code address skips the function descriptor "header".
// TOC and static chain are ignored and set to 0.
void Assembler::function_descriptor() {
  RecordRelocInfo(RelocInfo::INTERNAL_REFERENCE);
#if V8_TARGET_ARCH_S390X
  uint64_t value = reinterpret_cast<uint64_t>(pc_) + 3 * kPointerSize;
#if __BYTE_ORDER == __LITTLE_ENDIAN
  emit(static_cast<uint32_t>(value & 0xFFFFFFFF));
  emit(static_cast<uint32_t>(value >> 32));
#else
  emit(static_cast<uint32_t>(value >> 32));
  emit(static_cast<uint32_t>(value & 0xFFFFFFFF));
#endif
  emit(static_cast<Instr>(0));
  emit(static_cast<Instr>(0));
  emit(static_cast<Instr>(0));
  emit(static_cast<Instr>(0));
#else
  emit(reinterpret_cast<Instr>(pc_) + 3 * kPointerSize);
  emit(static_cast<Instr>(0));
  emit(static_cast<Instr>(0));
#endif
}
// end PowerPC

// Special register instructions
void Assembler::mtxer(Register src) {
  emit(EXT2 | MTSPR | src.code()*B21 | 32 << 11);
}

void Assembler::mcrfs(int bf, int bfa) {
  emit(EXT4 | MCRFS | bf*B23 | bfa*B18);
}

void Assembler::mfcr(Register dst) {
  emit(EXT2 | MFCR | dst.code()*B21);
}

// end PowerPC

// Exception-generating instructions and debugging support.
// Stops with a non-negative code less than kNumOfWatchedStops support
// enabling/disabling and a counter feature. See simulator-ppc.h .
void Assembler::stop(const char* msg, Condition cond, int32_t code,
                     CRegister cr) {
  if (cond != al) {
    Label skip;
    b(NegateCondition(cond), &skip /*, cr*/);
    bkpt(0);
    bind(&skip);
  } else {
    bkpt(0);
  }
}

void Assembler::bkpt(uint32_t imm16) {
  emit2bytes(0x0001);
  // emit(0x7d821008);
}


void Assembler::info(const char* msg, Condition cond, int32_t code,
                     CRegister cr) {
  if (::v8::internal::FLAG_trace_sim_stubs) {
    emit(0x7d9ff808);
#if V8_TARGET_ARCH_S390X
    uint64_t value = reinterpret_cast<uint64_t>(msg);
    emit(static_cast<uint32_t>(value >> 32));
    emit(static_cast<uint32_t>(value & 0xFFFFFFFF));
#else
    emit(reinterpret_cast<Instr>(msg));
#endif
  }
}

void Assembler::dcbf(Register rb, Register rx) {
    emit(EXT2 | DCBF | rb.code()*B16 | rx.code()*B11);
}

void Assembler::sync() {
    emit(EXT2 | SYNC);
}

void Assembler::icbi(Register rb, Register rx) {
    emit(EXT2 | ICBI | rb.code()*B16 | rx.code()*B11);
}

void Assembler::isync() {
    emit(EXT1 | ISYNC);
}

// Floating point support
void Assembler::lfdu(const DoubleRegister frt, const MemOperand &src) {
  int offset = src.offset();
  Register rb = src.rb();
  ASSERT(is_int16(offset));
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(LFDU | frt.code()*B21 | rb.code()*B16 | imm16);
}

void Assembler::lfdux(const DoubleRegister frt, const MemOperand & src) {
  Register rb = src.rb();
  Register rx = src.rx();
  ASSERT(!rb.is(r0));
  emit(EXT2 | LFDUX | frt.code()*B21 | rb.code()*B16 | rx.code()*B11 | LeaveRC);
}

void Assembler::lfs(const DoubleRegister frt, const MemOperand &src) {
  int offset = src.offset();
  Register rb = src.rb();
  ASSERT(is_int16(offset));
  ASSERT(!rb.is(r0));
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(LFS | frt.code()*B21 | rb.code()*B16 | imm16);
}

void Assembler::lfsu(const DoubleRegister frt, const MemOperand &src) {
  int offset = src.offset();
  Register rb = src.rb();
  ASSERT(is_int16(offset));
  ASSERT(!rb.is(r0));
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(LFSU | frt.code()*B21 | rb.code()*B16 | imm16);
}

void Assembler::lfsx(const DoubleRegister frt, const MemOperand &src) {
  Register rb = src.rb();
  Register rx = src.rx();
  ASSERT(!rb.is(r0));
  emit(EXT2 | LFSX | frt.code()*B21 | rb.code()*B16 | rx.code()*B11 | LeaveRC);
}

void Assembler::lfsux(const DoubleRegister frt, const MemOperand & src) {
  Register rb = src.rb();
  Register rx = src.rx();
  ASSERT(!rb.is(r0));
  emit(EXT2 | LFSUX | frt.code()*B21 | rb.code()*B16 | rx.code()*B11 | LeaveRC);
}

void Assembler::fsel(const DoubleRegister frt, const DoubleRegister fra,
                     const DoubleRegister frc, const DoubleRegister frb,
                     RCBit rc) {
  emit(EXT4 | FSEL | frt.code()*B21 | fra.code()*B16 | frb.code()*B11 |
       frc.code()*B6 | rc);
}

void Assembler::mtfsfi(int bf, int immediate, RCBit rc) {
  emit(EXT4 | MTFSFI | bf*B23 | immediate*B12 | rc);
}

void Assembler::mffs(const DoubleRegister frt, RCBit rc) {
  emit(EXT4 | MFFS | frt.code()*B21 | rc);
}

void Assembler::mtfsf(const DoubleRegister frb, bool L,
                      int FLM, bool W, RCBit rc) {
  emit(EXT4 | MTFSF | frb.code()*B11 | W*B16 | FLM*B17 | L*B25 | rc);
}

// Pseudo instructions.
void Assembler::nop(int type) {
  switch (type) {
    case 0:
      lr(r0, r0);
      break;
    case DEBUG_BREAK_NOP:
      // @TODO Need to come up with a better NOP break
      oill(r3, Operand::Zero());
      break;
    default:
      UNIMPLEMENTED();
  }
}

// I format <insn> i
//    +--------+---------+
//    | OpCode |    i    |
//    +--------+---------+
//    0        8        15
//
#define I_FORM_EMIT(name, op)\
void Assembler::name(const Operand& i) {\
    i_form(op, i);\
}
void Assembler::i_form(Opcode op, const Operand& i) {
    ASSERT(is_uint8(i.imm_));
    ASSERT(is_uint8(op));
    emit2bytes(op << 8 | i.imm_);
}

// E format <insn>
//    +------------------+
//    |      OpCode      |
//    +------------------+
//    0                 15
//
#define E_FORM_EMIT(name, op)\
void Assembler::name() {\
    e_form(op);\
}
void Assembler::e_form(Opcode op) {
    ASSERT(is_uint16(op));
    emit2bytes(op);
}

// IE format: <insn> i1, i2
//    +--------+---------+--------+----+----+
//    |      OpCode      |////////| I1 | I2 |
//    +--------+---------+--------+----+----+
//    0        8         16      24   28   31
#define IE_FORM_EMIT(name, op)\
void Assembler::name(const Operand& i1, const Operand& i2) {\
    ie_form(op, i1, i2);\
}
void Assembler::ie_form(Opcode op, const Operand& i1, const Operand& i2) {
    ASSERT(is_uint16(op));
    ASSERT(is_uint4(i1.imm_));
    ASSERT(is_uint4(i2.imm_));
    emit4bytes((op << 16) |
               ((i1.imm_ & 0xf) * B4) |
               (i2.imm_ & 0xf));
}

// RR format: <insn> R1,R2
//    +--------+----+----+
//    | OpCode | R1 | R2 |
//    +--------+----+----+
//    0        8    12  15
#define RR_FORM_EMIT(name, op) \
void Assembler::name(Register r1, Register r2) { \
    rr_form(op, r1, r2); \
}
void Assembler::rr_form(Opcode op, Register r1, Register r2) {
    ASSERT(is_uint8(op));
    // ASSERT(is_uint4(r1.code()));
    // ASSERT(is_uint4(r2.code()));
    emit2bytes(op*B8 | r1.code()*B4 | r2.code());
}

void Assembler::rr_form(Opcode op,
                               DoubleRegister r1,
                               DoubleRegister r2) {
    ASSERT(is_uint8(op));
    // ASSERT(is_uint4(r1.code()));
    // ASSERT(is_uint4(r2.code()));
    emit2bytes(op*B8 | r1.code()*B4 | r2.code());
}

// RR2 format: <insn> M1,R2
//    +--------+----+----+
//    | OpCode | M1 | R2 |
//    +--------+----+----+
//    0        8    12  15
#define RR2_FORM_EMIT(name, op) \
void Assembler::name(Condition m1, Register r2) { \
    rr_form(op, m1, r2); \
}
void Assembler::rr_form(Opcode op, Condition m1, Register r2) {
    ASSERT(is_uint8(op));
    ASSERT(is_uint4(m1));
    // ASSERT(is_uint4(r2.code()));
    emit2bytes(op*B8 | m1*B4 | r2.code());
}


// RX format: <insn> R1,D2(X2,B2)
//    +--------+----+----+----+-------------+
//    | OpCode | R1 | X2 | B2 |     D2      |
//    +--------+----+----+----+-------------+
//    0        8    12   16   20           31
#define RX_FORM_EMIT(name, op) \
void Assembler::name(Register r, const MemOperand& opnd) { \
    name(r, opnd.getIndexRegister(), opnd.getBaseRegister(), \
         opnd.getDisplacement());\
}\
void Assembler::name(Register r1, Register x2, \
                     Register b2, Disp d2) {\
    rx_form(op, r1, x2, b2, d2);\
}
void Assembler::rx_form(Opcode op,
                        Register r1,
                        Register x2,
                        Register b2,
                        Disp d2) {
    ASSERT(is_uint8(op));
    ASSERT(is_uint12(d2));
    emit4bytes(op*B24 | r1.code()*B20 |
             x2.code()*B16 | b2.code()*B12 | d2);
}

void Assembler::rx_form(Opcode op,
                               DoubleRegister r1,
                               Register x2,
                               Register b2,
                               Disp d2) {
  ASSERT(is_uint8(op));
  ASSERT(is_uint12(d2));
    emit4bytes(op*B24 | r1.code()*B20 |
             x2.code()*B16 | b2.code()*B12 | d2);
}


// RI1 format: <insn> R1,I2
//    +--------+----+----+------------------+
//    | OpCode | R1 |OpCd|        I2        |
//    +--------+----+----+------------------+
//    0        8    12   16                31
#define RI1_FORM_EMIT(name, op) \
void Assembler::name(Register r, const Operand& i2) { \
    ri_form(op, r, i2);\
}
void Assembler::ri_form(Opcode op, Register r1, const Operand& i2) {
    ASSERT(is_uint12(op));
    ASSERT(is_uint16(i2.imm_) || is_int16(i2.imm_));
    emit4bytes((op & 0xFF0) * B20 |
             r1.code() * B20 |
             (op & 0xF) * B16 |
             (i2.imm_ & 0xFFFF));
}

// RI2 format: <insn> M1,I2
//    +--------+----+----+------------------+
//    | OpCode | M1 |OpCd|        I2        |
//    +--------+----+----+------------------+
//    0        8    12   16                31
#define RI2_FORM_EMIT(name, op) \
void Assembler::name(Condition m, const Operand& i2) {\
    ri_form(op, m, i2);\
}
void Assembler::ri_form(Opcode op, Condition m1, const Operand& i2) {
    ASSERT(is_uint12(op));
    ASSERT(is_uint4(m1));
    ASSERT(is_uint16(i2.imm_));
    emit4bytes((op & 0xFF0) * B20 |
             m1 * B20 |
             (op & 0xF) * B16 |
             (i2.imm_ & 0xFFFF));
}

// RIE-f format: <insn> R1,R2,I3,I4,I5
//    +--------+----+----+------------------+--------+--------+
//    | OpCode | R1 | R2 |   I3   |    I4   |   I5   | OpCode |
//    +--------+----+----+------------------+--------+--------+
//    0        8    12   16      24         32       40      47
#define RIE_F_FORM_EMIT(name, op) \
void Assembler::name(Register r1, Register r2, const Operand &i3, \
                     const Operand& i4, const Operand& i5) {\
  rie_f_form(op, r1, r2, i3, i4, i5);\
}
void Assembler::rie_f_form(Opcode op, Register r1, Register r2,
         const Operand &i3, const Operand& i4, const Operand& i5) {
    ASSERT(is_uint16(op));
    ASSERT(is_uint8(i3.imm_));
    ASSERT(is_uint8(i4.imm_));
    ASSERT(is_uint8(i5.imm_));
    uint64_t code = (static_cast<uint64_t>(op & 0xFF00)) * B32       |
                    (static_cast<uint64_t>(r1.code())) * B36         |
                    (static_cast<uint64_t>(r2.code())) * B32         |
                    (static_cast<uint64_t>(i3.imm_)) * B24           |
                    (static_cast<uint64_t>(i4.imm_)) * B16           |
                    (static_cast<uint64_t>(i5.imm_)) * B8            |
                    (static_cast<uint64_t>(op & 0x00FF));
    emit6bytes(code);
}

// RIE format: <insn> R1,R3,I2
//    +--------+----+----+------------------+--------+--------+
//    | OpCode | R1 | R3 |        I2        |////////| OpCode |
//    +--------+----+----+------------------+--------+--------+
//    0        8    12   16                 32       40      47
#define RIE_FORM_EMIT(name, op) \
void Assembler::name(Register r1, Register r3, \
                     const Operand& i2) {\
    rie_form(op, r1, r3, i2);\
}
void Assembler::rie_form(Opcode op, Register r1, Register r3,
                     const Operand& i2) {
    ASSERT(is_uint16(op));
    ASSERT(is_uint16(i2.imm_));
    uint64_t code = (static_cast<uint64_t>(op & 0xFF00)) * B32       |
                    (static_cast<uint64_t>(r1.code())) * B36         |
                    (static_cast<uint64_t>(r3.code())) * B32         |
                    (static_cast<uint64_t>(i2.imm_)) * B16           |
                    (static_cast<uint64_t>(op & 0x00FF));
    emit6bytes(code);
}

// RIL1 format: <insn> R1,I2
//   +--------+----+----+------------------------------------+
//   | OpCode | R1 |OpCd|                  I2                |
//   +--------+----+----+------------------------------------+
//   0        8    12   16                                  47
#define RIL1_FORM_EMIT(name, op) \
void Assembler::name(Register r, const Operand& i2) {\
    ril_form(op, r, i2);\
}
void Assembler::ril_form(Opcode op, Register r1, const Operand& i2) {
    ASSERT(is_uint12(op));
    // ASSERT(is_uint4(r1.code()));
    uint64_t code = (static_cast<uint64_t>(op & 0xFF0)) * B36        |
                    (static_cast<uint64_t>(r1.code())) * B36         |
                    (static_cast<uint64_t>(op & 0x00F)) * B32        |
                    (static_cast<uint64_t>(i2.imm_) & 0xFFFFFFFF);
    emit6bytes(code);
}

// RIL2 format: <insn> M1,I2
//   +--------+----+----+------------------------------------+
//   | OpCode | M1 |OpCd|                  I2                |
//   +--------+----+----+------------------------------------+
//   0        8    12   16                                  47
#define RIL2_FORM_EMIT(name, op) \
void Assembler::name(Condition m1, const Operand& i2) {\
    ril_form(op, m1, i2);\
}
void Assembler::ril_form(Opcode op, Condition m1, const Operand& i2) {
    ASSERT(is_uint12(op));
    ASSERT(is_uint4(m1));
    uint64_t code = (static_cast<uint64_t>(op & 0xFF0)) * B36        |
                    (static_cast<uint64_t>(m1)) * B36                |
                    (static_cast<uint64_t>(op & 0x00F)) * B32        |
                    (static_cast<uint64_t>(i2.imm_ & 0xFFFFFFFF));
    emit6bytes(code);
}

// RRE format: <insn> R1,R2
//    +------------------+--------+----+----+
//    |      OpCode      |////////| R1 | R2 |
//    +------------------+--------+----+----+
//    0                  16       24   28  31
#define RRE_FORM_EMIT(name, op) \
void Assembler::name(Register r1, Register r2) {\
    rre_form(op, r1, r2);\
}
void Assembler::rre_form(Opcode op, Register r1, Register r2) {
    ASSERT(is_uint16(op));
    // ASSERT(is_uint4(r1.code()));
    // ASSERT(is_uint4(r2.code()));
    emit4bytes(op << 16 | r1.code()*B4 | r2.code());
}

void Assembler::rre_form(Opcode op, DoubleRegister r1,
                                DoubleRegister r2) {
    ASSERT(is_uint16(op));
    // ASSERT(is_uint4(r1.code()));
    // ASSERT(is_uint4(r2.code()));
    emit4bytes(op << 16 | r1.code()*B4 | r2.code());
}

// RRD format: <insn> R1,R3, R2
//    +------------------+----+----+----+----+
//    |      OpCode      | R1 |////| R3 | R2 |
//    +------------------+----+----+----+----+
//    0                  16  20   24   28   31
#define RRD_FORM_EMIT(name, op) \
void Assembler::name(Register r1, Register r3, \
                     Register r2) {\
    rrd_form(op, r1, r3, r2);\
}
void Assembler::rrd_form(Opcode op, Register r1, Register r3, \
                     Register r2) {
    emit4bytes(op << 16 | r1.code()*B12 | r3.code()*B4\
            | r2.code());
}

// RS1 format: <insn> R1,R3,D2(B2)
//    +--------+----+----+----+-------------+
//    | OpCode | R1 | R3 | B2 |     D2      |
//    +--------+----+----+----+-------------+
//    0        8    12   16   20           31
#define RS1_FORM_EMIT(name, op) \
void Assembler::name(Register r1, Register r3, \
                     Register b2, Disp d2) {\
    rs_form(op, r1, r3, b2, d2);\
}\
void Assembler::name(Register r1, Register r3, \
                     const MemOperand& opnd) {\
    name(r1, r3, opnd.getBaseRegister(), opnd.getDisplacement());\
}
void Assembler::rs_form(Opcode op,
                        Register r1,
                        Register r3,
                        Register b2,
                        const Disp d2) {
  ASSERT(is_uint12(d2));
  emit4bytes(op * B24 | r1.code() * B20 | r3.code() * B16 |
             b2.code() * B12 | d2);
}

// RS2 format: <insn> R1,M3,D2(B2)
//    +--------+----+----+----+-------------+
//    | OpCode | R1 | M3 | B2 |     D2      |
//    +--------+----+----+----+-------------+
//    0        8    12   16   20           31
#define RS2_FORM_EMIT(name, op) \
void Assembler::name(Register r1, Condition m3, \
                     Register b2, Disp d2) {\
    rs_form(op, r1, m3, b2, d2);\
}\
void Assembler::name(Register r1, Condition m3, \
                     const MemOperand& opnd) {\
    name(r1, m3, opnd.getBaseRegister(), opnd.getDisplacement());\
}
void Assembler::rs_form(Opcode op,
                        Register r1,
                        Condition m3,
                        Register b2,
                        const Disp d2) {
  ASSERT(is_uint12(d2));
  emit4bytes(op * B24 | r1.code() * B20 | m3 * B16 |
             b2.code() * B12 | d2);
}

// RSI format: <insn> R1,R3,I2
//    +--------+----+----+------------------+
//    | OpCode | R1 | R3 |        RI2       |
//    +--------+----+----+------------------+
//    0        8    12   16                 31
#define RSI_FORM_EMIT(name, op)\
void Assembler::name(Register r1, Register r3, const Operand& i2) {\
    rsi_form(op, r1, r3, i2);\
}
void Assembler::rsi_form(Opcode op, Register r1,
                           Register r3, const Operand& i2) {
    ASSERT(is_uint8(op));
    ASSERT(is_uint16(i2.imm_));
    emit4bytes(op * B24 | r1.code() * B20 | r3.code() * B16 |
               (i2.imm_ & 0xFFFF));
}

// RSL format: <insn> R1,R3,D2(B2)
//    +--------+----+----+----+-------------+--------+--------+
//    | OpCode | L1 |    | B2 |    D2       |        | OpCode |
//    +--------+----+----+----+-------------+--------+--------+
//    0        8    12   16   20            32       40      47
#define RSL_FORM_EMIT(name, op)\
void Assembler::name(Length l1, Register b2, Disp d2) {\
    rsl_form(op, l1, b2, d2);\
}
void Assembler::rsl_form(Opcode op, Length l1, Register b2, Disp d2) {
    ASSERT(is_uint16(op));
    uint64_t code = (static_cast<uint64_t>(op & 0xFF00)) * B32   |
                    (static_cast<uint64_t>(l1)) * B36            |
                    (static_cast<uint64_t>(b2.code())) * B28     |
                    (static_cast<uint64_t>(d2)) * B16            |
                    (static_cast<uint64_t>(op & 0x00FF));
    emit6bytes(code);
}

// RSY1 format: <insn> R1,R3,D2(B2)
//    +--------+----+----+----+-------------+--------+--------+
//    | OpCode | R1 | R3 | B2 |    DL2      |  DH2   | OpCode |
//    +--------+----+----+----+-------------+--------+--------+
//    0        8    12   16   20            32       40      47
#define RSY1_FORM_EMIT(name, op)\
void Assembler::name(Register r1, Register r3, Register b2, \
                     Disp d2) {\
    rsy_form(op, r1, r3, b2, d2);\
}\
void Assembler::name(Register r1, Register r3, const MemOperand& opnd) {\
    name(r1, r3, opnd.getBaseRegister(), opnd.getDisplacement());\
}
void Assembler::rsy_form(Opcode op,
                        Register r1,
                        Register r3,
                        Register b2,
                        const Disp d2) {
    ASSERT(is_uint20(d2));
    ASSERT(is_uint16(op));
    uint64_t code = (static_cast<uint64_t>(op & 0xFF00)) * B32  |
                    (static_cast<uint64_t>(r1.code())) * B36     |
                    (static_cast<uint64_t>(r3.code())) * B32     |
                    (static_cast<uint64_t>(b2.code())) * B28     |
                    (static_cast<uint64_t>(d2 & 0x0FFF)) * B16   |
                    (static_cast<uint64_t>(d2 & 0x0FF000)) >> 4  |
                    (static_cast<uint64_t>(op & 0x00FF));
    emit6bytes(code);
}


// RSY2 format: <insn> R1,M3,D2(B2)
//    +--------+----+----+----+-------------+--------+--------+
//    | OpCode | R1 | M3 | B2 |    DL2      |  DH2   | OpCode |
//    +--------+----+----+----+-------------+--------+--------+
//    0        8    12   16   20            32       40      47
#define RSY2_FORM_EMIT(name, op)\
void Assembler::name(Register r1, Condition m3, Register b2, \
                     Disp d2) {\
    rsy_form(op, r1, m3, b2, d2);\
}\
void Assembler::name(Register r1, Condition m3, const MemOperand& opnd) {\
    name(r1, m3, opnd.getBaseRegister(), opnd.getDisplacement());\
}
void Assembler::rsy_form(Opcode op,
                        Register r1,
                        Condition m3,
                        Register b2,
                        const Disp d2) {
    ASSERT(is_int20(d2));
    ASSERT(is_uint16(op));
    uint64_t code = (static_cast<uint64_t>(op & 0xFF00)) * B32  |
                    (static_cast<uint64_t>(r1.code())) * B36     |
                    (static_cast<uint64_t>(m3)) * B32    |
                    (static_cast<uint64_t>(b2.code())) * B28     |
                    (static_cast<uint64_t>(d2 & 0x0FFF)) * B16   |
                    (static_cast<uint64_t>(d2 & 0x0FF000)) >> 4  |
                    (static_cast<uint64_t>(op & 0x00FF));
    emit6bytes(code);
}


// RXE format: <insn> R1,D2(X2,B2)
//    +--------+----+----+----+-------------+--------+--------+
//    | OpCode | R1 | X2 | B2 |     D2      |////////| OpCode |
//    +--------+----+----+----+-------------+--------+--------+
//    0        8    12   16   20            32       40      47
#define RXE_FORM_EMIT(name, op)\
void Assembler::name(Register r1, Register x2, Register b2, \
                     Disp d2) {\
    rxe_form(op, r1, x2, b2, d2);\
}\
void Assembler::name(Register r1, const MemOperand& opnd) {\
    name(r1, opnd.getIndexRegister(), opnd.getBaseRegister(), \
         opnd.getDisplacement());\
}
void Assembler::rxe_form(Opcode op, Register r1, Register x2, Register b2,
                     Disp d2) {
    ASSERT(is_uint12(d2));
    ASSERT(is_uint16(op));
    uint64_t code = (static_cast<uint64_t>(op & 0xFF00)) * B32  |
                    (static_cast<uint64_t>(r1.code())) * B36     |
                    (static_cast<uint64_t>(x2.code())) * B32     |
                    (static_cast<uint64_t>(b2.code())) * B28     |
                    (static_cast<uint64_t>(d2 & 0x0FFF)) * B16   |
                    (static_cast<uint64_t>(op & 0x00FF));
    emit6bytes(code);
}

// RXY format: <insn> R1,D2(X2,B2)
//    +--------+----+----+----+-------------+--------+--------+
//    | OpCode | R1 | X2 | B2 |     DL2     |   DH2  | OpCode |
//    +--------+----+----+----+-------------+--------+--------+
//    0        8    12   16   20            32   36   40      47
#define RXY_FORM_EMIT(name, op)\
void Assembler::name(Register r1, Register x2, Register b2, \
                     Disp d2) {\
    rxy_form(op, r1, x2, b2, d2);\
}\
void Assembler::name(Register r1, const MemOperand& opnd) {\
    name(r1, opnd.getIndexRegister(), opnd.getBaseRegister(), \
         opnd.getDisplacement());\
}
void Assembler::rxy_form(Opcode op, Register r1, Register x2, Register b2,
                     Disp d2) {
    ASSERT(is_int20(d2));
    ASSERT(is_uint16(op));
    uint64_t code = (static_cast<uint64_t>(op & 0xFF00)) * B32  |
                    (static_cast<uint64_t>(r1.code())) * B36     |
                    (static_cast<uint64_t>(x2.code())) * B32     |
                    (static_cast<uint64_t>(b2.code())) * B28     |
                    (static_cast<uint64_t>(d2 & 0x0FFF)) * B16   |
                    (static_cast<uint64_t>(d2 & 0x0FF000)) >> 4  |
                    (static_cast<uint64_t>(op & 0x00FF));
    emit6bytes(code);
}

void Assembler::rxy_form(Opcode op, DoubleRegister r1,
                                Register x2, Register b2,
                                Disp d2) {
    ASSERT(is_int20(d2));
    ASSERT(is_uint16(op));
    uint64_t code = (static_cast<uint64_t>(op & 0xFF00)) * B32  |
                    (static_cast<uint64_t>(r1.code())) * B36     |
                    (static_cast<uint64_t>(x2.code())) * B32     |
                    (static_cast<uint64_t>(b2.code())) * B28     |
                    (static_cast<uint64_t>(d2 & 0x0FFF)) * B16   |
                    (static_cast<uint64_t>(d2 & 0x0FF000)) >> 4  |
                    (static_cast<uint64_t>(op & 0x00FF));
    emit6bytes(code);
}


// RRS format: <insn> R1,R2,M3,D4(B4)
//    +--------+----+----+----+-------------+----+---+--------+
//    | OpCode | R1 | R2 | B4 |     D4      | M3 |///| OpCode |
//    +--------+----+----+----+-------------+----+---+--------+
//    0        8    12   16   20            32   36   40      47
#define RRS_FORM_EMIT(name, op)\
void Assembler::name(Register r1, Register r2, Register b4, \
                     Disp d4, Condition m3) {\
    rrs_form(op, r1, r2, b4, d4, m3);\
}\
void Assembler::name(Register r1, Register r2, Condition m3, \
                     const MemOperand& opnd) {\
    name(r1, r2, opnd.getBaseRegister(), opnd.getDisplacement(), m3);\
}
void Assembler::rrs_form(Opcode op, Register r1, Register r2, Register b4,
                     Disp d4, Condition m3) {
    ASSERT(is_uint12(d4));
    ASSERT(is_uint16(op));
    uint64_t code = (static_cast<uint64_t>(op & 0xFF00)) * B32  |
                    (static_cast<uint64_t>(r1.code())) * B36     |
                    (static_cast<uint64_t>(r2.code())) * B32     |
                    (static_cast<uint64_t>(b4.code())) * B28     |
                    (static_cast<uint64_t>(d4)) * B16            |
                    (static_cast<uint64_t>(m3)) << 12    |
                    (static_cast<uint64_t>(op & 0x00FF));
    emit6bytes(code);
}

// RIS format: <insn> R1,I2,M3,D4(B4)
//    +--------+----+----+----+-------------+--------+--------+
//    | OpCode | R1 | M3 | B4 |     D4      |   I2   | OpCode |
//    +--------+----+----+----+-------------+--------+--------+
//    0        8    12   16   20            32        40      47
#define RIS_FORM_EMIT(name, op)\
void Assembler::name(Register r1, Condition m3, Register b4, \
                     Disp d4, const Operand& i2) {\
    ris_form(op, r1, m3, b4, d4, i2);\
}\
void Assembler::name(Register r1, const Operand& i2, Condition m3, \
                     const MemOperand& opnd) {\
    name(r1, m3, opnd.getBaseRegister(), opnd.getDisplacement(), i2);\
}
void Assembler::ris_form(Opcode op, Register r1, Condition m3, Register b4, \
                     Disp d4, const Operand& i2) {
    ASSERT(is_uint12(d4));
    ASSERT(is_uint16(op));
    ASSERT(is_uint8(i2.imm_));
    uint64_t code = (static_cast<uint64_t>(op & 0xFF00)) * B32  |
                    (static_cast<uint64_t>(r1.code())) * B36     |
                    (static_cast<uint64_t>(m3)) * B32    |
                    (static_cast<uint64_t>(b4.code())) * B28     |
                    (static_cast<uint64_t>(d4)) * B16            |
                    (static_cast<uint64_t>(i2.imm_)) << 8        |
                    (static_cast<uint64_t>(op & 0x00FF));
    emit6bytes(code);
}


// S format: <insn> D2(B2)
//    +------------------+----+-------------+
//    |      OpCode      | B2 |     D2      |
//    +------------------+----+-------------+
//    0                  16   20           31
#define S_FORM_EMIT(name, op)\
void Assembler::name(Register b1, Disp d2) {\
    s_form(op, b1, d2);\
}\
void Assembler::name(const MemOperand& opnd) {\
    name(opnd.getBaseRegister(), opnd.getDisplacement());\
}
void Assembler::s_form(Opcode op, Register b1, Disp d2) {
    ASSERT(is_uint12(d2));
    emit4bytes(op << 16 | b1.code()*B12 | d2);
}

// SI format: <insn> D1(B1),I2
//    +--------+---------+----+-------------+
//    | OpCode |   I2    | B1 |     D1      |
//    +--------+---------+----+-------------+
//    0        8         16   20           31
#define SI_FORM_EMIT(name, op)\
void Assembler::name(const Operand& i2, Register b1, \
                     Disp d1) {\
    si_form(op, i2, b1, d1);\
}\
void Assembler::name(const MemOperand& opnd, const Operand& i2) {\
    name(i2, opnd.getBaseRegister(), opnd.getDisplacement()); \
}
void Assembler::si_form(Opcode op, const Operand& i2, Register b1,
                     Disp d1) {
    emit4bytes((op & 0x00FF) << 24 | i2.imm_*B16 | b1.code()*B12 | d1);
}

// SIY format: <insn> D1(B1),I2
//    +--------+---------+----+-------------+--------+--------+
//    | OpCode |   I2    | B1 |     DL1     |  DH1   | OpCode |
//    +--------+---------+----+-------------+--------+--------+
//    0        8         16   20            32   36   40      47
#define SIY_FORM_EMIT(name, op)\
void Assembler::name(const Operand& i2, Register b1, \
                     Disp d1) {\
    siy_form(op, i2, b1, d1);\
}\
void Assembler::name(const MemOperand& opnd, const Operand& i2) {\
    name(i2, opnd.getBaseRegister(), opnd.getDisplacement());\
}
void Assembler::siy_form(Opcode op, const Operand& i2, Register b1, \
                     Disp d1) {
    ASSERT(is_uint20(d1));
    ASSERT(is_uint16(op));
    ASSERT(is_uint8(i2.imm_));
    uint64_t code = (static_cast<uint64_t>(op & 0xFF00)) * B32  |
                    (static_cast<uint64_t>(i2.imm_)) * B32       |
                    (static_cast<uint64_t>(b1.code())) * B28     |
                    (static_cast<uint64_t>(d1 & 0x0FFF)) * B16   |
                    (static_cast<uint64_t>(d1 & 0x0FF000)) >> 4  |
                    (static_cast<uint64_t>(op & 0x00FF));
    emit6bytes(code);
}

// SIL format: <insn> D1(B1),I2
//    +------------------+----+-------------+-----------------+
//    |     OpCode       | B1 |      D1     |        I2       |
//    +------------------+----+-------------+-----------------+
//    0                 16   20            32                47
#define SIL_FORM_EMIT(name, op)\
void Assembler::name(Register b1, Disp d1, \
                     const Operand& i2) {\
    sil_form(op, b1, d1, i2);\
}\
void Assembler::name(const MemOperand& opnd, const Operand& i2) {\
    name(opnd.getBaseRegister(), opnd.getDisplacement(), i2);\
}
void Assembler::sil_form(Opcode op, Register b1, Disp d1,
                     const Operand& i2) {
    ASSERT(is_uint12(d1));
    ASSERT(is_uint16(op));
    ASSERT(is_uint16(i2.imm_));
    uint64_t code = (static_cast<uint64_t>(op)) * B32            |
                    (static_cast<uint64_t>(b1.code())) * B28     |
                    (static_cast<uint64_t>(d1)) * B16            |
                    (static_cast<uint64_t>(i2.imm_));
    emit6bytes(code);
}

// RXF format: <insn> R1,R3,D2(X2,B2)
//    +--------+----+----+----+-------------+----+---+--------+
//    | OpCode | R3 | X2 | B2 |     D2      | R1 |///| OpCode |
//    +--------+----+----+----+-------------+----+---+--------+
//    0        8    12   16   20            32   36  40      47
#define RXF_FORM_EMIT(name, op)\
void Assembler::name(Register r1, Register r3, Register b2, \
                     Register x2, Disp d2) {\
    rxf_form(op, r1, r3, b2, x2, d2);\
}\
void Assembler::name(Register r1, Register r3, const MemOperand& opnd) {\
    name(r1, r3, opnd.getBaseRegister(), opnd.getIndexRegister(), \
         opnd.getDisplacement());\
}
void Assembler::rxf_form(Opcode op, Register r1, Register r3, Register b2, \
                     Register x2, Disp d2) {
    ASSERT(is_uint12(d2));
    ASSERT(is_uint16(op));
    uint64_t code = (static_cast<uint64_t>(op & 0xFF00)) * B32   |
                    (static_cast<uint64_t>(r3.code())) * B36     |
                    (static_cast<uint64_t>(x2.code())) * B32     |
                    (static_cast<uint64_t>(b2.code())) * B28     |
                    (static_cast<uint64_t>(d2))        * B16     |
                    (static_cast<uint64_t>(r1.code())) * B12     |
                    (static_cast<uint64_t>(op & 0x00FF));
    emit6bytes(code);
}

// SS1 format: <insn> D1(L,B1),D2(B3)
//    +--------+----+----+----+-------------+----+------------+
//    | OpCode |    L    | B1 |     D1      | B2 |     D2     |
//    +--------+----+----+----+-------------+----+------------+
//    0        8    12   16   20            32   36          47
#define SS1_FORM_EMIT(name, op)\
void Assembler::name(Register b1, Disp d1, \
                     Register b2, Disp d2, Length l) {\
    ss_form(op, l, b1, d1, b2, d2);\
}\
void Assembler::name(const MemOperand& opnd1, const MemOperand& opnd2, \
                     Length length) {\
    name(opnd1.getBaseRegister(), \
         opnd1.getDisplacement(), opnd2.getBaseRegister(), \
         opnd2.getDisplacement(), length);\
}
void Assembler::ss_form(Opcode op, Length l, Register b1, Disp d1,
                     Register b2, Disp d2) {
    ASSERT(is_uint12(d2));
    ASSERT(is_uint12(d1));
    ASSERT(is_uint8(op));
    ASSERT(is_uint8(l));
    uint64_t code = (static_cast<uint64_t>(op)) * B40            |
                    (static_cast<uint64_t>(l)) * B32             |
                    (static_cast<uint64_t>(b1.code())) * B28     |
                    (static_cast<uint64_t>(d1))        * B16     |
                    (static_cast<uint64_t>(b2.code())) * B12     |
                    (static_cast<uint64_t>(d2));
    emit6bytes(code);
}

// SS2 format: <insn> D1(L1,B1), D2(L3,B3)
//    +--------+----+----+----+-------------+----+------------+
//    | OpCode | L1 | L2 | B1 |     D1      | B2 |     D2     |
//    +--------+----+----+----+-------------+----+------------+
//    0        8    12   16   20            32   36          47
#define SS2_FORM_EMIT(name, op)\
void Assembler::name(Register b1, \
                     Disp d1, Register b2, \
                     Disp d2, Length l1, Length l2) {\
    ss_form(op, l1, l2, b1, d1, b2, d2);\
}\
void Assembler::name(const MemOperand& opnd1, const MemOperand& opnd2, \
                     Length length1, Length length2) {\
    name(opnd1.getBaseRegister(), \
         opnd1.getDisplacement(), opnd2.getBaseRegister(), \
         opnd2.getDisplacement(), length1, length2);\
}
void Assembler::ss_form(Opcode op, Length l1, Length l2, Register b1,
                     Disp d1, Register b2, Disp d2) {
    ASSERT(is_uint12(d2));
    ASSERT(is_uint12(d1));
    ASSERT(is_uint8(op));
    ASSERT(is_uint4(l2));
    ASSERT(is_uint4(l1));
    uint64_t code = (static_cast<uint64_t>(op)) * B40            |
                    (static_cast<uint64_t>(l1)) * B36            |
                    (static_cast<uint64_t>(l2)) * B32            |
                    (static_cast<uint64_t>(b1.code())) * B28     |
                    (static_cast<uint64_t>(d1))        * B16     |
                    (static_cast<uint64_t>(b2.code())) * B12     |
                    (static_cast<uint64_t>(d2));
    emit6bytes(code);
}

// SS3 format: <insn> D1(L1,B1), D2(I3,B2)
//    +--------+----+----+----+-------------+----+------------+
//    | OpCode | L1 | I3 | B1 |     D1      | B2 |     D2     |
//    +--------+----+----+----+-------------+----+------------+
//    0        8    12   16   20            32   36          47
#define SS3_FORM_EMIT(name, op)\
void Assembler::name(const Operand& i3, Register b1, \
                     Disp d1, Register b2, \
                     Disp d2, Length l1) {\
    ss_form(op, l1, i3, b1, d1, b2, d2);\
}\
void Assembler::name(const MemOperand& opnd1, const MemOperand& opnd2, \
                     Length length) {\
    ASSERT(false);\
}
void Assembler::ss_form(Opcode op, Length l1, const Operand& i3, Register b1,
                     Disp d1, Register b2, Disp d2) {
    ASSERT(is_uint12(d2));
    ASSERT(is_uint12(d1));
    ASSERT(is_uint8(op));
    ASSERT(is_uint4(l1));
    ASSERT(is_uint4(i3.imm_));
    uint64_t code = (static_cast<uint64_t>(op)) * B40            |
                    (static_cast<uint64_t>(l1)) * B36            |
                    (static_cast<uint64_t>(i3.imm_)) * B32       |
                    (static_cast<uint64_t>(b1.code())) * B28     |
                    (static_cast<uint64_t>(d1))        * B16     |
                    (static_cast<uint64_t>(b2.code())) * B12     |
                    (static_cast<uint64_t>(d2));
    emit6bytes(code);
}

// SS4 format: <insn> D1(R1,B1), D2(R3,B2)
//    +--------+----+----+----+-------------+----+------------+
//    | OpCode | R1 | R3 | B1 |     D1      | B2 |     D2     |
//    +--------+----+----+----+-------------+----+------------+
//    0        8    12   16   20            32   36          47
#define SS4_FORM_EMIT(name, op)\
void Assembler::name(Register r1, Register r3, Register b1, \
                     Disp d1, Register b2, \
                     Disp d2) {\
    ss_form(op, r1, r3, b1, d1, b2, d2);\
}\
void Assembler::name(const MemOperand& opnd1, const MemOperand& opnd2) {\
    ASSERT(false);\
}
void Assembler::ss_form(Opcode op, Register r1, Register r3, Register b1,
                     Disp d1, Register b2, Disp d2) {
    ASSERT(is_uint12(d2));
    ASSERT(is_uint12(d1));
    ASSERT(is_uint8(op));
    uint64_t code = (static_cast<uint64_t>(op)) * B40            |
                    (static_cast<uint64_t>(r1.code())) * B36     |
                    (static_cast<uint64_t>(r3.code())) * B32     |
                    (static_cast<uint64_t>(b1.code())) * B28     |
                    (static_cast<uint64_t>(d1))        * B16     |
                    (static_cast<uint64_t>(b2.code())) * B12     |
                    (static_cast<uint64_t>(d2));
    emit6bytes(code);
}

// SS5 format: <insn> D1(R1,B1), D2(R3,B2)
//    +--------+----+----+----+-------------+----+------------+
//    | OpCode | R1 | R3 | B2 |     D2      | B4 |     D4     |
//    +--------+----+----+----+-------------+----+------------+
//    0        8    12   16   20            32   36          47
#define SS5_FORM_EMIT(name, op)\
void Assembler::name(Register r1, Register r3, Register b2, \
                     Disp d2, Register b4, \
                     Disp d4) {\
    ss_form(op, r1, r3, b2, d2, b4, d4); /*SS5 use the same form as SS4*/ \
}\
void Assembler::name(const MemOperand& opnd1, const MemOperand& opnd2) {\
    ASSERT(false);\
}

#define SS6_FORM_EMIT(name, op) SS1_FORM_EMIT(name, op)

// SSE format: <insn> D1(B1),D2(B2)
//    +------------------+----+-------------+----+------------+
//    |      OpCode      | B1 |     D1      | B2 |     D2     |
//    +------------------+----+-------------+----+------------+
//    0        8    12   16   20            32   36           47
#define SSE_FORM_EMIT(name, op)\
void Assembler::name(Register b1, Disp d1, Register b2, \
                     Disp d2) {\
    sse_form(op, b1, d1, b2, d2);\
}\
void Assembler::name(const MemOperand& opnd1, const MemOperand& opnd2) {\
    name(opnd1.getBaseRegister(), opnd1.getDisplacement(), \
         opnd2.getBaseRegister(), opnd2.getDisplacement());\
}
void Assembler::sse_form(Opcode op, Register b1, Disp d1, Register b2,
                     Disp d2) {
    ASSERT(is_uint12(d2));
    ASSERT(is_uint12(d1));
    ASSERT(is_uint16(op));
    uint64_t code = (static_cast<uint64_t>(op)) * B32            |
                    (static_cast<uint64_t>(b1.code())) * B28     |
                    (static_cast<uint64_t>(d1))        * B16     |
                    (static_cast<uint64_t>(b2.code())) * B12     |
                    (static_cast<uint64_t>(d2));
    emit6bytes(code);
}

// SSF format: <insn> R3, D1(B1),D2(B2),R3
//    +--------+----+----+----+-------------+----+------------+
//    | OpCode | R3 |OpCd| B1 |     D1      | B2 |     D2     |
//    +--------+----+----+----+-------------+----+------------+
//    0        8    12   16   20            32   36           47
#define SSF_FORM_EMIT(name, op)\
void Assembler::name(Register r3, Register b1, Disp d1, \
                     Register b2, Disp d2) {\
    ssf_form(op, r3, b1, d1, b2, d2);\
}\
void Assembler::name(Register r3, const MemOperand& opnd1, \
                     const MemOperand& opnd2) {\
    name(r3, opnd1.getBaseRegister(), opnd1.getDisplacement(), \
         opnd2.getBaseRegister(), opnd2.getDisplacement());\
}
void Assembler::ssf_form(Opcode op, Register r3, Register b1, Disp d1,
                     Register b2, Disp d2) {
    ASSERT(is_uint12(d2));
    ASSERT(is_uint12(d1));
    ASSERT(is_uint12(op));
    uint64_t code = (static_cast<uint64_t>(op & 0xFF0)) * B36    |
                    (static_cast<uint64_t>(r3.code())) * B36     |
                    (static_cast<uint64_t>(op & 0x00F)) * B32    |
                    (static_cast<uint64_t>(b1.code())) * B28     |
                    (static_cast<uint64_t>(d1))        * B16     |
                    (static_cast<uint64_t>(b2.code())) * B12     |
                    (static_cast<uint64_t>(d2));
    emit6bytes(code);
}

//  RRF1 format: <insn> R1,R2,R3
//    +------------------+----+----+----+----+
//    |      OpCode      | R1 |    | R3 | R2 |
//    +------------------+----+----+----+----+
//    0                  16   20   24   28  31
#define RRF1_FORM_EMIT(name, op)\
void Assembler::name(Register r1, Register r3, Register r2) {\
    rrf1_form(op << 16 | r1.code()*B12 | r3.code()*B4 | r2.code());\
}
void Assembler::rrf1_form(uint32_t code) {
    emit4bytes(code);
}

//  RRF2 format: <insn> R1,R2,M3
//    +------------------+----+----+----+----+
//    |      OpCode      | M3 |    | R1 | R2 |
//    +------------------+----+----+----+----+
//    0                  16   20   24   28  31
#define RRF2_FORM_EMIT(name, op)\
void Assembler::name(Condition m3, Register r1, Register r2) {\
    rrf2_form(op << 16 |m3*B12 | r1.code()*B4 | r2.code());\
}
void Assembler::rrf2_form(uint32_t code) {
    emit4bytes(code);
}

//  RRF3 format: <insn> R1,R2,R3,M4
//    +------------------+----+----+----+----+
//    |      OpCode      | R3 | M4 | R1 | R2 |
//    +------------------+----+----+----+----+
//    0                  16   20   24   28  31
#define RRF3_FORM_EMIT(name, op)\
void Assembler::name(Register r3, Conition m4, Register r1, \
                     Register r2) {\
    rrf3_form(op << 16 | r3.code()*B12 | m4*B8 | \
              r1.code()*B4 | r2.code());\
}
void Assembler::rrf3_form(uint32_t code) {
    emit4bytes(code);
}

//  RRF-e format: <insn> R1,M3,R2,M4
//    +------------------+----+----+----+----+
//    |      OpCode      | M3 | M4 | R1 | R2 |
//    +------------------+----+----+----+----+
//    0                  16   20   24   28  31
void Assembler::rrfe_form(Opcode op,
                          Condition m3,
                          Condition m4,
                          Register r1,
                          Register r2) {
  uint32_t code = op << 16 | m3*B12 | m4*B8 | r1.code()*B4
                | r2.code();
  emit4bytes(code);
}


// end of S390 Instruction generation

// start of S390 instruction
RRF1_FORM_EMIT(adtr, ADTR)
RRF1_FORM_EMIT(adtra, ADTRA)
RXE_FORM_EMIT(aeb, AEB)
RRE_FORM_EMIT(aebr, AEBR)
RIL1_FORM_EMIT(afi, AFI)
RXY_FORM_EMIT(agf, AGF)
RIL1_FORM_EMIT(agfi, AGFI)
RRE_FORM_EMIT(agfr, AGFR)
SIY_FORM_EMIT(agsi, AGSI)
RRF1_FORM_EMIT(ahhhr, AHHHR)
RRF1_FORM_EMIT(ahhlr, AHHLR)
RIE_FORM_EMIT(ahik, AHIK)
RIL1_FORM_EMIT(aih, AIH)
RXY_FORM_EMIT(alc, ALC)
RXY_FORM_EMIT(alcg, ALCG)
RRE_FORM_EMIT(alcgr, ALCGR)
RRE_FORM_EMIT(alcr, ALCR)
RXY_FORM_EMIT(algf, ALGF)
RIL1_FORM_EMIT(algfi, ALGFI)
RRE_FORM_EMIT(algfr, ALGFR)
RIE_FORM_EMIT(alghsik, ALGHSIK)
SIY_FORM_EMIT(algsi, ALGSI)
RRF1_FORM_EMIT(alhhhr, ALHHHR)
RRF1_FORM_EMIT(alhhlr, ALHHLR)
RIE_FORM_EMIT(alhsik, ALHSIK)
SIY_FORM_EMIT(alsi, ALSI)
RIL1_FORM_EMIT(alsih, ALSIH)
RIL1_FORM_EMIT(alsihn, ALSIHN)
SS2_FORM_EMIT(ap, AP)
RRF1_FORM_EMIT(ark, ARK)
SIY_FORM_EMIT(asi, ASI)
RRE_FORM_EMIT(axbr, AXBR)
RRF1_FORM_EMIT(axtr, AXTR)
RRF1_FORM_EMIT(axtra, AXTRA)
RX_FORM_EMIT(bal, BAL)
RR_FORM_EMIT(balr, BALR)
RX_FORM_EMIT(bas, BAS)
RR_FORM_EMIT(bassm, BASSM)
RX_FORM_EMIT(bc, BC)
RRE_FORM_EMIT(bctgr, BCTGR)
RR_FORM_EMIT(bctr, BCTR)
RIL1_FORM_EMIT(brcth, BRCTH)
RSI_FORM_EMIT(brxh, BRXH)
RIE_FORM_EMIT(brxhg, BRXHG)
RSI_FORM_EMIT(brxle, BRXLE)
RIE_FORM_EMIT(brxlg, BRXLG)
RR_FORM_EMIT(bsm, BSM)
RS1_FORM_EMIT(bxle, BXLE)
RSY1_FORM_EMIT(bxleg, BXLEG)
RRF2_FORM_EMIT(cdfbra, CDFBRA)
RRE_FORM_EMIT(cdftr, CDFTR)
RRF2_FORM_EMIT(cdgbra, CDGBRA)
RRE_FORM_EMIT(cdgtr, CDGTR)
RRF2_FORM_EMIT(cdgtra, CDGTRA)
RRF2_FORM_EMIT(cdlftr, CDLFTR)
RRF2_FORM_EMIT(cdlgtr, CDLGTR)
RS1_FORM_EMIT(cds, CDS)
RSY1_FORM_EMIT(cdsg, CDSG)
RRE_FORM_EMIT(cdstr, CDSTR)
RSY1_FORM_EMIT(cdsy, CDSY)
RRE_FORM_EMIT(cdtr, CDTR)
RRE_FORM_EMIT(cdutr, CDUTR)
RSL_FORM_EMIT(cdzt, CDZT)
RXE_FORM_EMIT(ceb, CEB)
RRE_FORM_EMIT(cebr, CEBR)
RRE_FORM_EMIT(cedtr, CEDTR)
RRE_FORM_EMIT(cefbr, CEFBR)
RRF2_FORM_EMIT(cefbra, CEFBRA)
RRE_FORM_EMIT(cegbr, CEGBR)
RRF2_FORM_EMIT(cegbra, CEGBRA)
RRF2_FORM_EMIT(celfbr, CELFBR)
RRF2_FORM_EMIT(celgbr, CELGBR)
RRE_FORM_EMIT(cextr, CEXTR)
S_FORM_EMIT(cfc, CFC)
RRF2_FORM_EMIT(cfdbra, CFDBRA)
RRF2_FORM_EMIT(cfdr, CFDR)
RRF2_FORM_EMIT(cfdtr, CFDTR)
RRF2_FORM_EMIT(cfebr, CFEBR)
RRF2_FORM_EMIT(cfebra, CFEBRA)
RRF2_FORM_EMIT(cfer, CFER)
RRF2_FORM_EMIT(cfxbr, CFXBR)
RRF2_FORM_EMIT(cfxbra, CFXBRA)
RRF2_FORM_EMIT(cfxr, CFXR)
RRF2_FORM_EMIT(cfxtr, CFXTR)
RRF2_FORM_EMIT(cgdbra, CGDBRA)
RRF2_FORM_EMIT(cgdr, CGDR)
RRF2_FORM_EMIT(cgdtr, CGDTR)
RRF2_FORM_EMIT(cgdtra, CGDTRA)
RRF2_FORM_EMIT(cgebr, CGEBR)
RRF2_FORM_EMIT(cgebra, CGEBRA)
RRF2_FORM_EMIT(cger, CGER)
RXY_FORM_EMIT(cgf, CGF)
RIL1_FORM_EMIT(cgfi, CGFI)
RRE_FORM_EMIT(cgfr, CGFR)
RIL1_FORM_EMIT(cgfrl, CGFRL)
RXY_FORM_EMIT(cgh, CGH)
RIL1_FORM_EMIT(cghrl, CGHRL)
SIL_FORM_EMIT(cghsi, CGHSI)
RIS_FORM_EMIT(cgib, CGIB)
RIE_FORM_EMIT(cgij, CGIJ)
RIE_FORM_EMIT(cgit, CGIT)
RRS_FORM_EMIT(cgrb, CGRB)
RIE_FORM_EMIT(cgrj, CGRJ)
RIL1_FORM_EMIT(cgrl, CGRL)
RRF2_FORM_EMIT(cgrt, CGRT)
RRF2_FORM_EMIT(cgxbr, CGXBR)
RRF2_FORM_EMIT(cgxbra, CGXBRA)
RRF2_FORM_EMIT(cgxr, CGXR)
RRF2_FORM_EMIT(cgxtr, CGXTR)
RRF2_FORM_EMIT(cgxtra, CGXTRA)
RXY_FORM_EMIT(chf, CHF)
RRE_FORM_EMIT(chhr, CHHR)
SIL_FORM_EMIT(chhsi, CHHSI)
RRE_FORM_EMIT(chlr, CHLR)
RIL1_FORM_EMIT(chrl, CHRL)
SIL_FORM_EMIT(chsi, CHSI)
RIS_FORM_EMIT(cib, CIB)
RIL1_FORM_EMIT(cih, CIH)
RIE_FORM_EMIT(cij, CIJ)
RIE_FORM_EMIT(cit, CIT)
RRE_FORM_EMIT(cksm, CKSM)
RR_FORM_EMIT(clcl, CLCL)
RS1_FORM_EMIT(clcle, CLCLE)
RSY1_FORM_EMIT(clclu, CLCLU)
RRF2_FORM_EMIT(clfebr, CLFEBR)
SIL_FORM_EMIT(clfhsi, CLFHSI)
RIE_FORM_EMIT(clfit, CLFIT)
RRF2_FORM_EMIT(clfxbr, CLFXBR)
RRF2_FORM_EMIT(clfxtr, CLFXTR)
RRF2_FORM_EMIT(clgdtr, CLGDTR)
RRF2_FORM_EMIT(clgebr, CLGEBR)
RXY_FORM_EMIT(clgf, CLGF)
SSF_FORM_EMIT(csst, CSST)
RRF2_FORM_EMIT(csxtr, CSXTR)
RSY1_FORM_EMIT(csy, CSY)
RRF2_FORM_EMIT(cu12, CU12)
RRF2_FORM_EMIT(cu14, CU14)
RRF2_FORM_EMIT(cu21, CU21)
RRF2_FORM_EMIT(cu24, CU24)
RRE_FORM_EMIT(cu41, CU41)
RRE_FORM_EMIT(cu42, CU42)
RRE_FORM_EMIT(cudtr, CUDTR)
RRE_FORM_EMIT(cuse, CUSE)
RRF2_FORM_EMIT(cutfu, CUTFU)
RRF2_FORM_EMIT(cuutf, CUUTF)
RRE_FORM_EMIT(cuxtr, CUXTR)
RX_FORM_EMIT(cvb, CVB)
RXY_FORM_EMIT(cvbg, CVBG)
RXY_FORM_EMIT(cvby, CVBY)
RX_FORM_EMIT(cvd, CVD)
RXY_FORM_EMIT(cvdg, CVDG)
RXY_FORM_EMIT(cvdy, CVDY)
RRE_FORM_EMIT(cxbr, CXBR)
RRE_FORM_EMIT(cxfbr, CXFBR)
RRF2_FORM_EMIT(cxfbra, CXFBRA)
RRE_FORM_EMIT(cxftr, CXFTR)
RRE_FORM_EMIT(cxgbr, CXGBR)
RRF2_FORM_EMIT(cxgbra, CXGBRA)
RRE_FORM_EMIT(cxgtr, CXGTR)
RRF2_FORM_EMIT(cxgtra, CXGTRA)
RRF2_FORM_EMIT(cxlfbr, CXLFBR)
RRF2_FORM_EMIT(cxlftr, CXLFTR)
RRF2_FORM_EMIT(cxlgbr, CXLGBR)
RRF2_FORM_EMIT(cxlgtr, CXLGTR)
RRE_FORM_EMIT(cxstr, CXSTR)
RRE_FORM_EMIT(cxtr, CXTR)
RRE_FORM_EMIT(cxutr, CXUTR)
RSL_FORM_EMIT(cxzt, CXZT)
RSL_FORM_EMIT(czdt, CZDT)
RSL_FORM_EMIT(czxt, CZXT)
RX_FORM_EMIT(d, D)
RRF1_FORM_EMIT(ddtr, DDTR)
RRF1_FORM_EMIT(ddtra, DDTRA)
RXE_FORM_EMIT(deb, DEB)
RRE_FORM_EMIT(debr, DEBR)
RRF1_FORM_EMIT(didbr, DIDBR)
RRF1_FORM_EMIT(diebr, DIEBR)
RXY_FORM_EMIT(dl, DL)
RXY_FORM_EMIT(dlg, DLG)
RRE_FORM_EMIT(dlgr, DLGR)
RRE_FORM_EMIT(dlr, DLR)
SS2_FORM_EMIT(dp, DP)
RXY_FORM_EMIT(dsg, DSG)
RXY_FORM_EMIT(dsgf, DSGF)
RRE_FORM_EMIT(dsgfr, DSGFR)
RRE_FORM_EMIT(dsgr, DSGR)
RRE_FORM_EMIT(dxbr, DXBR)
RRF1_FORM_EMIT(dxtr, DXTR)
RRF1_FORM_EMIT(dxtra, DXTRA)
RRE_FORM_EMIT(ear, EAR)
RSY1_FORM_EMIT(ecag, ECAG)
SSF_FORM_EMIT(ectg, ECTG)
SS1_FORM_EMIT(ed, ED)
SS1_FORM_EMIT(edmk, EDMK)
RRE_FORM_EMIT(eedtr, EEDTR)
RRE_FORM_EMIT(eextr, EEXTR)
RRE_FORM_EMIT(efpc, EFPC)
RRE_FORM_EMIT(epsw, EPSW)
RRE_FORM_EMIT(esdtr, ESDTR)
RRE_FORM_EMIT(esxtr, ESXTR)
RRE_FORM_EMIT(etnd, ETND)
RX_FORM_EMIT(ex, EX)
RIL1_FORM_EMIT(exrl, EXRL)
RRF2_FORM_EMIT(fidbr, FIDBR)
RRF2_FORM_EMIT(fidbra, FIDBRA)
RRF2_FORM_EMIT(fidtr, FIDTR)
RRF2_FORM_EMIT(fiebr, FIEBR)
RRF2_FORM_EMIT(fiebra, FIEBRA)
RRF2_FORM_EMIT(fixbr, FIXBR)
RRF2_FORM_EMIT(fixbra, FIXBRA)
RRF2_FORM_EMIT(fixtr, FIXTR)
RRE_FORM_EMIT(flogr, FLOGR)
S_FORM_EMIT(hsch, HSCH)
RS2_FORM_EMIT(icm, ICM)
RSY2_FORM_EMIT(icmh, ICMH)
RSY2_FORM_EMIT(icmy, ICMY)
RRF1_FORM_EMIT(iedtr, IEDTR)
RRF1_FORM_EMIT(iextr, IEXTR)
RRE_FORM_EMIT(ipm, IPM)
RXE_FORM_EMIT(kdb, KDB)
RRE_FORM_EMIT(kdbr, KDBR)
RRE_FORM_EMIT(kdtr, KDTR)
RXE_FORM_EMIT(keb, KEB)
RRE_FORM_EMIT(kebr, KEBR)
RRE_FORM_EMIT(kimd, KIMD)
RRE_FORM_EMIT(klmd, KLMD)
RRE_FORM_EMIT(km, KM)
RRE_FORM_EMIT(kmac, KMAC)
RRE_FORM_EMIT(kmc, KMC)
RRF1_FORM_EMIT(kmctr, KMCTR)
RRE_FORM_EMIT(kmf, KMF)
RRE_FORM_EMIT(kmo, KMO)
RRE_FORM_EMIT(kxbr, KXBR)
RRE_FORM_EMIT(kxtr, KXTR)
RSY1_FORM_EMIT(laa, LAA)
RSY1_FORM_EMIT(laag, LAAG)
RSY1_FORM_EMIT(laal, LAAL)
RSY1_FORM_EMIT(laalg, LAALG)
RX_FORM_EMIT(lae, LAE)
RXY_FORM_EMIT(laey, LAEY)
RSY1_FORM_EMIT(lan, LAN)
RSY1_FORM_EMIT(lang, LANG)
RSY1_FORM_EMIT(lao, LAO)
RSY1_FORM_EMIT(laog, LAOG)
RIL1_FORM_EMIT(larl, LARL)
RXY_FORM_EMIT(lat, LAT)
RSY1_FORM_EMIT(lax, LAX)
RSY1_FORM_EMIT(laxg, LAXG)
RXY_FORM_EMIT(lbh, LBH)
RRE_FORM_EMIT(lcdfr, LCDFR)
RRE_FORM_EMIT(lcebr, LCEBR)
RRE_FORM_EMIT(lcgfr, LCGFR)
RRE_FORM_EMIT(lcgr, LCGR)
RR_FORM_EMIT(lcr, LCR)
RRE_FORM_EMIT(lcxbr, LCXBR)
RRE_FORM_EMIT(ldebr, LDEBR)
RRF2_FORM_EMIT(ldetr, LDETR)
RRE_FORM_EMIT(ldgr, LDGR)
RRE_FORM_EMIT(ldxbr, LDXBR)
RRF2_FORM_EMIT(ldxbra, LDXBRA)
RRF2_FORM_EMIT(ldxtr, LDXTR)
RX_FORM_EMIT(le_z, LE)
RRF2_FORM_EMIT(ledbra, LEDBRA)
RRF2_FORM_EMIT(ledtr, LEDTR)
RR_FORM_EMIT(ler, LER)
RRE_FORM_EMIT(lexbr, LEXBR)
RRF2_FORM_EMIT(lexbra, LEXBRA)
RXY_FORM_EMIT(ley, LEY)
S_FORM_EMIT(lfas, LFAS)
RXY_FORM_EMIT(lfh, LFH)
RXY_FORM_EMIT(lfhat, LFHAT)
S_FORM_EMIT(lfpc, LFPC)
RXY_FORM_EMIT(lgat, LGAT)
RRE_FORM_EMIT(lgdr, LGDR)
RXY_FORM_EMIT(lgf, LGF)
RIL1_FORM_EMIT(lgfi, LGFI)
RIL1_FORM_EMIT(lgfrl, LGFRL)
RIL1_FORM_EMIT(lghrl, LGHRL)
RIL1_FORM_EMIT(lgrl, LGRL)
RX_FORM_EMIT(lh, LH)
RXY_FORM_EMIT(lhh, LHH)
RRE_FORM_EMIT(lhr, LHR)
RIL1_FORM_EMIT(lhrl, LHRL)
RXY_FORM_EMIT(lhy, LHY)
RXY_FORM_EMIT(llch, LLCH)
RRE_FORM_EMIT(llcr, LLCR)
RRE_FORM_EMIT(llgcr, LLGCR)
RXY_FORM_EMIT(llgf, LLGF)
RXY_FORM_EMIT(llgfat, LLGFAT)
RRE_FORM_EMIT(llgfr, LLGFR)
RIL1_FORM_EMIT(llgfrl, LLGFRL)
RIL1_FORM_EMIT(llghrl, LLGHRL)
RXY_FORM_EMIT(llgt, LLGT)
RXY_FORM_EMIT(llgtat, LLGTAT)
RRE_FORM_EMIT(llgtr, LLGTR)
RXY_FORM_EMIT(llhh, LLHH)
RIL1_FORM_EMIT(llhrl, LLHRL)
RIL1_FORM_EMIT(llihf, LLIHF)
RI1_FORM_EMIT(llihh, LLIHH)
RI1_FORM_EMIT(llihl, LLIHL)
RIL1_FORM_EMIT(llilf, LLILF)
RI1_FORM_EMIT(llilh, LLILH)
RI1_FORM_EMIT(llill, LLILL)
SS5_FORM_EMIT(lmd, LMD)
RSY1_FORM_EMIT(lmh, LMH)
RRE_FORM_EMIT(lndfr, LNDFR)
RRE_FORM_EMIT(lnebr, LNEBR)
RRE_FORM_EMIT(lngfr, LNGFR)
RRE_FORM_EMIT(lngr, LNGR)
RR_FORM_EMIT(lnr, LNR)
RRE_FORM_EMIT(lnxbr, LNXBR)
RSY1_FORM_EMIT(loc, LOC)
RSY1_FORM_EMIT(locg, LOCG)
RRF2_FORM_EMIT(locgr, LOCGR)
RRF2_FORM_EMIT(locr, LOCR)
SSF_FORM_EMIT(lpd, LPD)
RRE_FORM_EMIT(lpdfr, LPDFR)
SSF_FORM_EMIT(lpdg, LPDG)
RRE_FORM_EMIT(lpebr, LPEBR)
RRE_FORM_EMIT(lpgfr, LPGFR)
RRE_FORM_EMIT(lpgr, LPGR)
RXY_FORM_EMIT(lpq, LPQ)
RR_FORM_EMIT(lpr, LPR)
RRE_FORM_EMIT(lpxbr, LPXBR)
RIL1_FORM_EMIT(lrl, LRL)
RXY_FORM_EMIT(lrv, LRV)
RXY_FORM_EMIT(lrvg, LRVG)
RRE_FORM_EMIT(lrvgr, LRVGR)
RXY_FORM_EMIT(lrvh, LRVH)
RRE_FORM_EMIT(lrvr, LRVR)
RRE_FORM_EMIT(ltdbr, LTDBR)
RRE_FORM_EMIT(ltdtr, LTDTR)
RRE_FORM_EMIT(ltebr, LTEBR)
RXY_FORM_EMIT(ltgf, LTGF)
RRE_FORM_EMIT(ltgfr, LTGFR)
RRE_FORM_EMIT(ltxbr, LTXBR)
RRE_FORM_EMIT(ltxtr, LTXTR)
RXE_FORM_EMIT(lxdb, LXDB)
RRE_FORM_EMIT(lxdbr, LXDBR)
RRF2_FORM_EMIT(lxdtr, LXDTR)
RXE_FORM_EMIT(lxeb, LXEB)
RRE_FORM_EMIT(lxebr, LXEBR)
RRE_FORM_EMIT(lxr, LXR)
RRE_FORM_EMIT(lzdr, LZDR)
RRE_FORM_EMIT(lzer, LZER)
RRE_FORM_EMIT(lzxr, LZXR)
RXF_FORM_EMIT(madb, MADB)
RRD_FORM_EMIT(madbr, MADBR)
RXF_FORM_EMIT(maeb, MAEB)
RRD_FORM_EMIT(maebr, MAEBR)
SI_FORM_EMIT(mc, MC)
RXE_FORM_EMIT(mdeb, MDEB)
RRE_FORM_EMIT(mdebr, MDEBR)
RRF1_FORM_EMIT(mdtr, MDTR)
RRF1_FORM_EMIT(mdtra, MDTRA)
RXE_FORM_EMIT(meeb, MEEB)
RRE_FORM_EMIT(meebr, MEEBR)
RXY_FORM_EMIT(mfy, MFY)
SS2_FORM_EMIT(mp, MP)
RX_FORM_EMIT(ms, MS)
S_FORM_EMIT(msch, MSCH)
RXF_FORM_EMIT(msdb, MSDB)
RRD_FORM_EMIT(msdbr, MSDBR)
RXF_FORM_EMIT(mseb, MSEB)
RRD_FORM_EMIT(msebr, MSEBR)
RIL1_FORM_EMIT(msfi, MSFI)
RXY_FORM_EMIT(msg, MSG)
RXY_FORM_EMIT(msgf, MSGF)
RIL1_FORM_EMIT(msgfi, MSGFI)
RRE_FORM_EMIT(msgfr, MSGFR)
RRE_FORM_EMIT(msgr, MSGR)
RRE_FORM_EMIT(msr, MSR)
RXY_FORM_EMIT(msy, MSY)
SS4_FORM_EMIT(mvcp, MVCP)
SSE_FORM_EMIT(mvcdk, MVCDK)
SS1_FORM_EMIT(mvcin, MVCIN)
RR_FORM_EMIT(mvcl, MVCL)
RS1_FORM_EMIT(mvcle, MVCLE)
RSY1_FORM_EMIT(mvclu, MVCLU)
SIL_FORM_EMIT(mvghi, MVGHI)
SIL_FORM_EMIT(mvhhi, MVHHI)
SIL_FORM_EMIT(mvhi, MVHI)
SI_FORM_EMIT(mvi, MVI)
SIY_FORM_EMIT(mviy, MVIY)
SS1_FORM_EMIT(mvn, MVN)
SS2_FORM_EMIT(mvo, MVO)
RRE_FORM_EMIT(mvst, MVST)
SS1_FORM_EMIT(mvz, MVZ)
RRE_FORM_EMIT(mxbr, MXBR)
RXE_FORM_EMIT(mxdb, MXDB)
RRE_FORM_EMIT(mxdbr, MXDBR)
RRF1_FORM_EMIT(mxtr, MXTR)
RRF1_FORM_EMIT(mxtra, MXTRA)
RX_FORM_EMIT(n, N)
SS1_FORM_EMIT(nc, NC)
RXY_FORM_EMIT(ng, NG)
SI_FORM_EMIT(ni, NI)
IE_FORM_EMIT(niai, NIAI)
RIL1_FORM_EMIT(nihf, NIHF)
RI1_FORM_EMIT(nihh, NIHH)
RI1_FORM_EMIT(nihl, NIHL)
RIL1_FORM_EMIT(nilf, NILF)
RI1_FORM_EMIT(nilh, NILH)
RI1_FORM_EMIT(nill, NILL)
SIY_FORM_EMIT(niy, NIY)
RRF1_FORM_EMIT(nrk, NRK)
RXY_FORM_EMIT(ntstg, NTSTG)
SS1_FORM_EMIT(oc, OC)
SI_FORM_EMIT(oi, OI)
RIL1_FORM_EMIT(oihf, OIHF)
RI1_FORM_EMIT(oihh, OIHH)
RI1_FORM_EMIT(oihl, OIHL)
RIL1_FORM_EMIT(oilf, OILF)
RI1_FORM_EMIT(oilh, OILH)
RI1_FORM_EMIT(oill, OILL)
SIY_FORM_EMIT(oiy, OIY)
RRF1_FORM_EMIT(ork, ORK)
SS2_FORM_EMIT(pack, PACK)
RRE_FORM_EMIT(pcc, PCC)
RXY_FORM_EMIT(pfd, PFD)
RIL2_FORM_EMIT(pfdrl, PFDRL)
E_FORM_EMIT(pfpo, PFPO)
SS1_FORM_EMIT(pka, PKA)
SS1_FORM_EMIT(pku, PKU)
SS5_FORM_EMIT(plo, PLO)
RRE_FORM_EMIT(popcnt, POPCNT)
RRF1_FORM_EMIT(ppa, PPA)
RRF1_FORM_EMIT(qadtr, QADTR)
RRF1_FORM_EMIT(qaxtr, QAXTR)
S_FORM_EMIT(rchp, RCHP)
RIE_F_FORM_EMIT(risbg, RISBG)
RIE_F_FORM_EMIT(risbgn, RISBGN)
RIE_FORM_EMIT(risbhg, RISBHG)
RIE_FORM_EMIT(risblg, RISBLG)
RSY1_FORM_EMIT(rll, RLL)
RSY1_FORM_EMIT(rllg, RLLG)
RIE_FORM_EMIT(rnsbg, RNSBG)
RIE_FORM_EMIT(rosbg, ROSBG)
// S_FORM_EMIT(rp, RP) RP is not a opcode
RRF1_FORM_EMIT(rrdtr, RRDTR)
RRF1_FORM_EMIT(rrxtr, RRXTR)
S_FORM_EMIT(rsch, RSCH)
RIE_FORM_EMIT(rxsbg, RXSBG)
S_FORM_EMIT(sal, SAL)
RRE_FORM_EMIT(sar, SAR)
S_FORM_EMIT(schm, SCHM)
RRF1_FORM_EMIT(sdtr, SDTR)
RRF1_FORM_EMIT(sdtra, SDTRA)
RXE_FORM_EMIT(seb, SEB)
RRE_FORM_EMIT(sebr, SEBR)
RRE_FORM_EMIT(sfasr, SFASR)
RRE_FORM_EMIT(sfpc, SFPC)
RXY_FORM_EMIT(sgf, SGF)
RRE_FORM_EMIT(sgfr, SGFR)
RRF1_FORM_EMIT(shhhr, SHHHR)
RRF1_FORM_EMIT(shhlr, SHHLR)
RXY_FORM_EMIT(slb, SLB)
RXY_FORM_EMIT(slbg, SLBG)
RRE_FORM_EMIT(slbgr, SLBGR)
RRE_FORM_EMIT(slbr, SLBR)
RS1_FORM_EMIT(slda, SLDA)
RS1_FORM_EMIT(sldl, SLDL)
RXF_FORM_EMIT(sldt, SLDT)
RIL1_FORM_EMIT(slfi, SLFI)
RXY_FORM_EMIT(slgf, SLGF)
RIL1_FORM_EMIT(slgfi, SLGFI)
RRE_FORM_EMIT(slgfr, SLGFR)
RRF1_FORM_EMIT(slgrk, SLGRK)
RRF1_FORM_EMIT(slhhhr, SLHHHR)
RRF1_FORM_EMIT(slhhlr, SLHHLR)
RXF_FORM_EMIT(slxt, SLXT)
SS2_FORM_EMIT(sp_z, SP)
RR_FORM_EMIT(spm, SPM)
RXE_FORM_EMIT(sqeb, SQEB)
RRE_FORM_EMIT(sqebr, SQEBR)
RRE_FORM_EMIT(sqxbr, SQXBR)
RS1_FORM_EMIT(srdl, SRDL)
RXF_FORM_EMIT(srdt, SRDT)
RRF1_FORM_EMIT(srk, SRK)
S_FORM_EMIT(srnm, SRNM)
S_FORM_EMIT(srnmb, SRNMB)
S_FORM_EMIT(srnmt, SRNMT)
SS3_FORM_EMIT(srp, SRP)
RRE_FORM_EMIT(srst, SRST)
RRE_FORM_EMIT(srstu, SRSTU)
RXF_FORM_EMIT(srxt, SRXT)
S_FORM_EMIT(ssch, SSCH)
RXY_FORM_EMIT(stch, STCH)
S_FORM_EMIT(stck, STCK)
S_FORM_EMIT(stcke, STCKE)
S_FORM_EMIT(stckf, STCKF)
RS2_FORM_EMIT(stcm, STCM)
RSY2_FORM_EMIT(stcmh, STCMH)
RSY2_FORM_EMIT(stcmy, STCMY)
S_FORM_EMIT(stcps, STCPS)
S_FORM_EMIT(stcrw, STCRW)
RXY_FORM_EMIT(stfh, STFH)
S_FORM_EMIT(stfle, STFLE)
S_FORM_EMIT(stfpc, STFPC)
RIL1_FORM_EMIT(stgrl, STGRL)
RXY_FORM_EMIT(sthh, STHH)
RIL1_FORM_EMIT(sthrl, STHRL)
RSY1_FORM_EMIT(stmh, STMH)
RSY2_FORM_EMIT(stoc, STOC)
RSY2_FORM_EMIT(stocg, STOCG)
RXY_FORM_EMIT(stpq, STPQ)
RIL1_FORM_EMIT(strl, STRL)
RXY_FORM_EMIT(strv, STRV)
RXY_FORM_EMIT(strvg, STRVG)
RXY_FORM_EMIT(strvh, STRVH)
S_FORM_EMIT(stsch, STSCH)
I_FORM_EMIT(svc, SVC)
RRE_FORM_EMIT(sxbr, SXBR)
RRF1_FORM_EMIT(sxtr, SXTR)
RRF1_FORM_EMIT(sxtra, SXTRA)
S_FORM_EMIT(tabort, TABORT)
RRF2_FORM_EMIT(tbdr, TBDR)
RRF2_FORM_EMIT(tbedr, TBEDR)
SIL_FORM_EMIT(tbegin, TBEGIN)
SIL_FORM_EMIT(tbeginc, TBEGINC)
RXE_FORM_EMIT(tcdb, TCDB)
RXE_FORM_EMIT(tceb, TCEB)
RXE_FORM_EMIT(tcxb, TCXB)
RXE_FORM_EMIT(tdcdt, TDCDT)
RXE_FORM_EMIT(tdcet, TDCET)
RXE_FORM_EMIT(tdcxt, TDCXT)
RXE_FORM_EMIT(tdgdt, TDGDT)
RXE_FORM_EMIT(tdget, TDGET)
RXE_FORM_EMIT(tdgxt, TDGXT)
S_FORM_EMIT(tend, TEND)
RRE_FORM_EMIT(thder, THDER)
RRE_FORM_EMIT(thdr, THDR)
SI_FORM_EMIT(tm, TM)
RI1_FORM_EMIT(tmh, TMH)
RI1_FORM_EMIT(tmhh, TMHH)
RI1_FORM_EMIT(tmhl, TMHL)
RI1_FORM_EMIT(tml, TML)
RI1_FORM_EMIT(tmlh, TMLH)
RI1_FORM_EMIT(tmll, TMLL)
SIY_FORM_EMIT(tmy, TMY)
RSL_FORM_EMIT(tp, TP)
S_FORM_EMIT(tpi, TPI)
SS1_FORM_EMIT(tr, TR)
RRE_FORM_EMIT(tre, TRE)
RRF2_FORM_EMIT(troo, TROO)
RRF2_FORM_EMIT(trot, TROT)
SS1_FORM_EMIT(trt, TRT)
RRF2_FORM_EMIT(trte, TRTE)
RRF2_FORM_EMIT(trto, TRTO)
SS1_FORM_EMIT(trtr, TRTR)
RRF2_FORM_EMIT(trtre, TRTRE)
RRF2_FORM_EMIT(trtt, TRTT)
S_FORM_EMIT(ts, TS)
S_FORM_EMIT(tsch, TSCH)
SS2_FORM_EMIT(unpk, UNPK)
SS1_FORM_EMIT(unpka, UNPKA)
SS1_FORM_EMIT(unpku, UNPKU)
E_FORM_EMIT(upt, UPT)
RX_FORM_EMIT(x, X)
RXY_FORM_EMIT(xg, XG)
SI_FORM_EMIT(xi, XI)
RIL1_FORM_EMIT(xihf, XIHF)
RIL1_FORM_EMIT(xilf, XILF)
SIY_FORM_EMIT(xiy, XIY)
RRF1_FORM_EMIT(xrk, XRK)
S_FORM_EMIT(xsch, XSCH)
SS2_FORM_EMIT(zap, ZAP)

// materialized assemblers
// Add Register (32)
void Assembler::ar(Register r1, Register r2) {
  rr_form(AR, r1, r2);
}

// Subtract Register (32)
void Assembler::sr(Register r1, Register r2) {
  rr_form(SR, r1, r2);
}

// Multiply Register (64<32)
void Assembler::mr_z(Register r1, Register r2) {
  ASSERT(r1.code() % 2 == 0);
  rr_form(MR, r1, r2);
}

// Multiply Logical Register (64<32)
void Assembler::mlr(Register r1, Register r2) {
  rre_form(MLR, r1, r2);
}

// Divide Register (32)
void Assembler::dr(Register r1, Register r2) {
  rr_form(DR, r1, r2);
}

// And Register (32)
void Assembler::or_z(Register r1, Register r2) {
  rr_form(OR, r1, r2);
}

// And Register (32)
void Assembler::nr(Register r1, Register r2) {
  rr_form(NR, r1, r2);
}

// XOR Register (32)
void Assembler::xr(Register r1, Register r2) {
  rr_form(XR, r1, r2);
}

// XOR Register (32)
void Assembler::agr(Register r1, Register r2) {
  rre_form(AGR, r1, r2);
}

// Subtract Register (64)
void Assembler::sgr(Register r1, Register r2) {
  rre_form(SGR, r1, r2);
}

// Multiply Register (128<64)
void Assembler::mlgr(Register r1, Register r2) {
  rre_form(MLGR, r1, r2);
}

// Or Register (64)
void Assembler::ogr(Register r1, Register r2) {
  rre_form(OGR, r1, r2);
}

// And Register (64)
void Assembler::ngr(Register r1, Register r2) {
  rre_form(NGR, r1, r2);
}

// Xor Register (64)
void Assembler::xgr(Register r1, Register r2) {
  rre_form(XGR, r1, r2);
}

// Add Register-Storage (32)
void Assembler::a(Register r1, const MemOperand& opnd) {
  rx_form(A, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Subtract Register-Storage (32)
void Assembler::s(Register r1, const MemOperand& opnd) {
  rx_form(S, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Multiply Register-Storage (64<32)
void Assembler::m(Register r1, const MemOperand& opnd) {
  rx_form(M, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Multiply Logical Register-Storage (64<32)
void Assembler::ml(Register r1, const MemOperand& opnd) {
  rxy_form(ML, r1, opnd.rb(), opnd.rx(), opnd.offset());
}

// Or Register-Storage (32)
void Assembler::o(Register r1, const MemOperand& opnd) {
  rx_form(O, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Add Register-Storage (32)
void Assembler::ay(Register r1, const MemOperand& opnd) {
  rxy_form(AY, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Subtract Register-Storage (32)
void Assembler::sy(Register r1, const MemOperand& opnd) {
  rxy_form(SY, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// And Register-Storage (32)
void Assembler::ny(Register r1, const MemOperand& opnd) {
  rxy_form(NY, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Or Register-Storage (32)
void Assembler::oy(Register r1, const MemOperand& opnd) {
  rxy_form(OY, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// XOR Register-Storage (32)
void Assembler::xy(Register r1, const MemOperand& opnd) {
  rxy_form(XY, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Add Register-Storage (64)
void Assembler::ag(Register r1, const MemOperand& opnd) {
  rxy_form(AG, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Subtract Register-Storage (64)
void Assembler::sg(Register r1, const MemOperand& opnd) {
  rxy_form(SG, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Or Register-Storage (64)
void Assembler::og(Register r1, const MemOperand& opnd) {
  rxy_form(OG, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Multiply Logical Register-Storage (128<64)
void Assembler::mlg(Register r1, const MemOperand& opnd) {
  rxy_form(MLG, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Load Register-Register (32)
void Assembler::lr(Register r1, Register r2) {
  rr_form(LR, r1, r2);
}

// Load Register-Register (64)
void Assembler::lgr(Register r1, Register r2) {
  rre_form(LGR, r1, r2);
}

// Load Halfword Register-Storage (64)
void Assembler::lgh(Register r1, const MemOperand& opnd) {
  rx_form(LGH, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Load halfword Register-Register (64)
void Assembler::lghr(Register r1, Register r2) {
  rre_form(LGHR, r1, r2);
}

// Load and Test Register-Storage (32)
void Assembler::lt_z(Register r1, const MemOperand& opnd) {
  rxy_form(LT, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Load and Test Register-Storage (64)
void Assembler::ltg(Register r1, const MemOperand& opnd) {
  rxy_form(LTG, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Load and Test Register-Register (32)
void Assembler::ltr(Register r1, Register r2) {
  rr_form(LTR, r1, r2);
}

// Load and Test Register-Register (64)
void Assembler::ltgr(Register r1, Register r2) {
  rre_form(LTGR, r1, r2);
}


// Add Halfword Register-Storage (32)
void Assembler::ah(Register r1, const MemOperand& opnd) {
  rx_form(AH, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Subtract Halfword Register-Storage (32)
void Assembler::sh(Register r1, const MemOperand& opnd) {
  rx_form(SH, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Multiply Halfword Register-Storage (32)
void Assembler::mh(Register r1, const MemOperand& opnd) {
  rx_form(MH, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Add Halfword Register-Storage (32)
void Assembler::ahy(Register r1, const MemOperand& opnd) {
  rxy_form(AHY, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Subtract Halfword Register-Storage (32)
void Assembler::shy(Register r1, const MemOperand& opnd) {
  rxy_form(AHY, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Add Halfword Immediate (32)
void Assembler::ahi(Register r1, const Operand& i2) {
  ri_form(AHI, r1, i2);
}

// Add Halfword Immediate (64)
void Assembler::aghi(Register r1, const Operand& i2) {
  ri_form(AGHI, r1, i2);
}

// Add Logical Register-Storage (32)
void Assembler::al_z(Register r1, const MemOperand& opnd) {
  rx_form(AL, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Add Logical Register-Storage (32)
void Assembler::aly(Register r1, const MemOperand& opnd) {
  rxy_form(ALY, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Add Logical (64)
void Assembler::alg(Register r1, const MemOperand& opnd) {
  rxy_form(ALG, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Add Logical Register-Register (32)
void Assembler::alr(Register r1, Register r2) {
  rr_form(ALR, r1, r1);
}

// Add Logical Register-Register (64)
void Assembler::algr(Register r1, Register r2) {
  rre_form(ALGR, r1, r1);
}

// Add Logical Immediate (32)
void Assembler::alfi(Register r1, const Operand& opnd) {
  ril_form(ALFI, r1, opnd);
}

// Subtract Logical Register-Storage (32)
void Assembler::sl(Register r1, const MemOperand& opnd) {
  rx_form(SL, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Subtract Logical Register-Storage (32)
void Assembler::sly(Register r1, const MemOperand& opnd) {
  rxy_form(SL, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Subtract Logical Register-Storage (64)
void Assembler::slg(Register r1, const MemOperand& opnd) {
  rxy_form(SLG, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Subtract Logical Register-Register (32)
void Assembler::slr(Register r1, Register r2) {
  rr_form(SLR, r1, r2);
}

// Subtract Logical Register-Register (64)
void Assembler::slgr(Register r1, Register r2) {
  rre_form(SLGR, r1, r2);
}

// Multiply Halfword Immediate (32)
void Assembler::mhi(Register r1, const Operand& opnd) {
  ri_form(MHI, r1, opnd);
}

// Multiply Halfword Immediate (64)
void Assembler::mghi(Register r1, const Operand& opnd) {
  ri_form(MGHI, r1, opnd);
}


// Load (32)
void Assembler::l(Register r1, const MemOperand& opnd) {
  rx_form(L, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Load (32)
void Assembler::ly(Register r1, const MemOperand& opnd) {
  rxy_form(LY, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Load (64)
void Assembler::lg(Register r1, const MemOperand& opnd) {
  rxy_form(LG, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Load Halfword Immediate (32)
void Assembler::lhi(Register dst, const Operand& imm) {
  ri_form(LHI, dst, imm);
}

// Load Halfword Immediate (64)
void Assembler::lghi(Register r1, const Operand& i2) {
  ri_form(LGHI, r1, i2);
}

// Load Logical halfword Register-Storage (32)
void Assembler::llh(Register r1, const MemOperand& opnd) {
  rxy_form(LLH, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Load Logical halfword Register-Storage (64)
void Assembler::llgh(Register r1, const MemOperand& opnd) {
  rxy_form(LLH, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Load Logical Character (32) - loads a byte and zero ext.
void Assembler::llc(Register r1, const MemOperand& opnd) {
  rxy_form(LLC, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Load Logical Character (64) - loads a byte and zero ext.
void Assembler::llgc(Register r1, const MemOperand& opnd) {
  rxy_form(LLGC, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Load Address Register-Storage
void Assembler::la(Register r1, const MemOperand& opnd) {
  rx_form(LA, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Load Address Register-Storage
void Assembler::lay(Register r1, const MemOperand& opnd) {
  rxy_form(LAY, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Load Byte Register-Storage (32)
void Assembler::lb(Register r1, const MemOperand& opnd) {
  rx_form(LB, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Load Byte Register-Register (32)
void Assembler::lbr(Register r1, Register r2) {
  rre_form(LBR, r1, r2);
}

// Load Byte Register-Storage (64)
void Assembler::lgb(Register r1, const MemOperand& opnd) {
  rxy_form(LGB, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Load Byte Register-Register (64)
void Assembler::lgbr(Register r1, Register r2) {
  rre_form(LGBR, r1, r2);
}

// Load Logical halfword Register-Register (32)
void Assembler::llhr(Register r1, Register r2) {
  rre_form(LLHR, r1, r2);
}

// Load Logical halfword Register-Register (64)
void Assembler::llghr(Register r1, Register r2) {
  rre_form(LLGHR, r1, r2);
}

// Load 64<-32 sign extended
void Assembler::lgfr(Register r1, Register r2) {
  rre_form(LGFR, r1, r2);
}

// Shift Left Single Logical (32)
void Assembler::sll(Register r1, const Operand& opnd) {
  rs_form(SLL, r1, r0, r0, opnd.immediate());
}

void Assembler::sll(Register r1, Register opnd) {
  rs_form(SLL, r1, r0, opnd, 0);
}


// Shift Left Single Logical (64)
void Assembler::sllg(Register r1, Register r3, const MemOperand& opnd) {
  rsy_form(SLLG, r1, r3, opnd.rb(), opnd.offset());
}

void Assembler::sllg(Register r1, Register r3, const Operand& opnd) {
  rsy_form(SLLG, r1, r3, r0, opnd.immediate());
}

// Shift Right Single Logical (32)
void Assembler::srl(Register r1, const Operand& opnd) {
  rs_form(SRL, r1, r0, r0, opnd.immediate());
}

// Shift Right Single Logical (32)
void Assembler::srl(Register r1, Register opnd) {
  rs_form(SRL, r1, r0, opnd, 0);
}


// Shift Right Single Logical (32)
void Assembler::srlk(Register r1, Register r3, const Operand& opnd) {
  rsy_form(SRLK, r1, r3, r0, opnd.immediate());
}

// Shift Right Single Logical (64)
void Assembler::srlg(Register r1, Register r3, const MemOperand& opnd) {
  rsy_form(SRLG, r1, r3, opnd.rb(), opnd.offset());
}

void Assembler::srlg(Register r1, Register r3, const Operand& opnd) {
  rsy_form(SRLG, r1, r3, r0, opnd.immediate());
}

// Shift Left Single (32)
void Assembler::sla(Register r1, const Operand& opnd) {
  rs_form(SLA, r1, r0, r0, opnd.immediate());
}

// Shift Left Single (64)
void Assembler::slag(Register r1, Register r3, const MemOperand& opnd) {
  rsy_form(SLAG, r1, r3, opnd.rb(), opnd.offset());
}

void Assembler::slag(Register r1, Register r3, const Operand& opnd) {
  rsy_form(SLAG, r1, r3, r0, opnd.immediate());
}

// Shift Right Single (32)
void Assembler::sra(Register r1, const Operand& opnd) {
  rs_form(SRA, r1, r0, r0, opnd.immediate());
}

// Shift Right Single (32)
void Assembler::sra(Register r1, Register opnd) {
  rs_form(SRA, r1, r0, opnd, 0);
}

// Shift Right Single (64)
void Assembler::srag(Register r1, Register r3, const MemOperand& opnd) {
  rsy_form(SRAG, r1, r3, opnd.rb(), opnd.offset());
}

void Assembler::srag(Register r1, Register r3, const Operand& opnd) {
  rsy_form(SRAG, r1, r3, r0, opnd.immediate());
}

// Shift Right Double
void Assembler::srda(Register r1, const Operand& opnd) {
  ASSERT(r1.code() % 2 == 0);
  rs_form(SRDA, r1, r0, r0, opnd.immediate());
}

// Compare Halfword Immediate (32)
void Assembler::chi(Register r1, const Operand& i2) {
  ri_form(CHI, r1, i2);
}

// Compare Halfword Register-Storage (32)
void Assembler::ch(Register r1, const MemOperand& opnd) {
  rx_form(CH, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Compare Halfword Register-Storage (32)
void Assembler::chy(Register r1, const MemOperand& opnd) {
  rxy_form(CHY, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Compare Halfword Immediate (64)
void Assembler::cghi(Register r1, const Operand& i2) {
  ri_form(CGHI, r1, i2);
}

// Compare Logical Immediate (64<32)
void Assembler::clgfi(Register r1, const Operand& i2) {
  ril_form(CLGFI, r1, i2);
}

// Compare Logical Immediate (32)
void Assembler::clfi(Register r1, const Operand& i2) {
  ril_form(CLFI, r1, i2);
}

// Compare Logical Register-Register (64)
void Assembler::clgr(Register r1, Register r2) {
  rre_form(CLGR, r1, r2);
}

// Compare Logical Register-Register (32)
void Assembler::clr(Register r1, Register r2) {
  rr_form(CLR, r1, r2);
}

// Compare Logical Register-Storage (32)
void Assembler::cl(Register r, const MemOperand& opnd) {
  rx_form(CL, r, opnd.rx(), opnd.rb(), opnd.offset());
}

// Compare Logical Register-Storage (32)
void Assembler::cly(Register r, const MemOperand& opnd) {
  rxy_form(CLY, r, opnd.rx(), opnd.rb(), opnd.offset());
}

// Compare Logical Register-Storage (64)
void Assembler::clg(Register r, const MemOperand& opnd) {
  rxy_form(CLG, r, opnd.rx(), opnd.rb(), opnd.offset());
}

// Compare Register-Storage (32)
void Assembler::c(Register r, const MemOperand& opnd) {
  rx_form(C, r, opnd.rx(), opnd.rb(), opnd.offset());
}

// Compare Register-Storage (32)
void Assembler::cy(Register r, const MemOperand& opnd) {
  rxy_form(CY, r, opnd.rx(), opnd.rb(), opnd.offset());
}

// Compare Register-Storage (64)
void Assembler::cg(Register r, const MemOperand& opnd) {
  rxy_form(CG, r, opnd.rx(), opnd.rb(), opnd.offset());
}

// Compare Register-Register (32)
void Assembler::cr_z(Register r1, Register r2) {
  rr_form(CR, r1, r2);
}

// Compare Register-Register (64)
void Assembler::cgr(Register r1, Register r2) {
  rre_form(CGR, r1, r2);
}

// Compare Immediate (64)
void Assembler::cfi(Register r, const Operand& opnd) {
  ril_form(CFI, r, opnd);
}

// Branch Relative and save (32)
void Assembler::bras(Register r, const Operand& opnd) {
  ri_form(BRAS, r, opnd);
}

// Branch Relative and save (64)
void Assembler::brasl(Register r, const Operand& opnd) {
  ril_form(BRASL, r, opnd);
}

// Branch relative on Condition (32)
void Assembler::brc(Condition c, const Operand& opnd) {
  // BRC actually encodes # of halfwords, so divide by 2.
  int16_t numHalfwords = static_cast<int16_t>(opnd.immediate()) / 2;
  Operand halfwordOp = Operand(numHalfwords);
  halfwordOp.setBits(16);
  ri_form(BRC, c, halfwordOp);
}

// Branch Relative on Condition (64)
void Assembler::brcl(Condition c, const Operand& opnd) {
  // BRCL actually encodes # of halfwords, so divide by 2.
  int32_t numHalfwords = static_cast<int32_t>(opnd.immediate()) / 2;
  Operand halfwordOp = Operand(numHalfwords);
  ril_form(BRCL, c, halfwordOp);
}

// Branch and Save
void Assembler::basr(Register r1, Register r2) {
  rr_form(BASR, r1, r2);
}

// Branch on Count (32)
void Assembler::bct(Register r, const MemOperand& opnd) {
  rx_form(BCT, r, opnd.rx(), opnd.rb(), opnd.offset());
}

// Branch on Count (64)
void Assembler::bctg(Register r, const MemOperand& opnd) {
  rxy_form(BCTG, r, opnd.rx(), opnd.rb(), opnd.offset());
}

// Store (32)
void Assembler::st(Register src, const MemOperand &dst) {
  rx_form(ST, src, dst.rx(), dst.rb(), dst.offset());
}

// Store (32)
void Assembler::sty(Register src, const MemOperand& dst) {
  rxy_form(STY, src, dst.rx(), dst.rb(), dst.offset());
}

// Store Halfword
void Assembler::sth(Register src, const MemOperand &dst) {
  rx_form(STH, src, dst.rx(), dst.rb(), dst.offset());
}

// Store Halfword
void Assembler::sthy(Register src, const MemOperand &dst) {
  rxy_form(STHY, src, dst.rx(), dst.rb(), dst.offset());
}

// Store Character
void Assembler::stc(Register src, const MemOperand &dst) {
  rx_form(STC, src, dst.rx(), dst.rb(), dst.offset());
}

// Store Character
void Assembler::stcy(Register src, const MemOperand &dst) {
  rxy_form(STCY, src, dst.rx(), dst.rb(), dst.offset());
}

// 32-bit Load Multiple - short displacement (12-bits unsigned)
void Assembler::lm(Register r1, Register r2, const MemOperand& src) {
  rs_form(LM, r1, r2, src.rb(), src.offset());
}
// 32-bit Load Multiple - long displacement (20-bits signed)
void Assembler::lmy(Register r1, Register r2, const MemOperand& src) {
  rsy_form(LMY, r1, r2, src.rb(), src.offset());
}
// 64-bit Load Multiple - long displacement (20-bits signed)
void Assembler::lmg(Register r1, Register r2, const MemOperand& src) {
  rsy_form(LMG, r1, r2, src.rb(), src.offset());
}

// Move charactor - mem to mem operation
void Assembler::mvc(const MemOperand& opnd1, const MemOperand& opnd2,
                    Length length) {
    ss_form(MVC, length-1, opnd1.getBaseRegister(),
         opnd1.getDisplacement(), opnd2.getBaseRegister(),
         opnd2.getDisplacement());
}

// Compare logical - mem to mem operation
void Assembler::clc(const MemOperand& opnd1, const MemOperand& opnd2,
                    Length length) {
    ss_form(CLC, length-1, opnd1.getBaseRegister(),
         opnd1.getDisplacement(), opnd2.getBaseRegister(),
         opnd2.getDisplacement());
}

// Exclusive Or - mem to mem operation
void Assembler::xc(const MemOperand& opnd1, const MemOperand& opnd2,
                    Length length) {
    ss_form(XC, length-1, opnd1.getBaseRegister(),
         opnd1.getDisplacement(), opnd2.getBaseRegister(),
         opnd2.getDisplacement());
}


#ifdef V8_TARGET_ARCH_S390X
// Store Register (64)
void Assembler::stg(Register src, const MemOperand &dst) {
  rxy_form(STG, src, dst.rx(), dst.rb(), dst.offset());
}
#endif

// Insert Character
void Assembler::ic_z(Register r1, const MemOperand& opnd) {
  rx_form(IC_z, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Insert Character
void Assembler::icy(Register r1, const MemOperand& opnd) {
  rxy_form(ICY, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Insert Immediate (High)
void Assembler::iihf(Register r1, const Operand& opnd) {
  ril_form(IIHF, r1, opnd);
}

// Insert Immediate (low)
void Assembler::iilf(Register r1, const Operand& opnd) {
  ril_form(IILF, r1, opnd);
}

// Insert Immediate (high high)
void Assembler::iihh(Register r1, const Operand& opnd) {
  ri_form(IIHH, r1, opnd);
}

// Insert Immediate (high low)
void Assembler::iihl(Register r1, const Operand& opnd) {
  ri_form(IIHL, r1, opnd);
}

// Insert Immediate (low high)
void Assembler::iilh(Register r1, const Operand& opnd) {
  ri_form(IILH, r1, opnd);
}

// Insert Immediate (low low)
void Assembler::iill(Register r1, const Operand& opnd) {
  ri_form(IILL, r1, opnd);
}

// Floating point instructions
// Add Register-Storage (LB)
void Assembler::adb(DoubleRegister r1, const MemOperand& opnd) {
  rxe_form(ADB,
           Register::from_code(r1.code()),
           opnd.rx(), opnd.rb(), opnd.offset());
}

// Add Register-Register (LB)
void Assembler::adbr(DoubleRegister r1, DoubleRegister r2) {
  rre_form(ADBR,
           Register::from_code(r1.code()),
           Register::from_code(r2.code()));
}

// Compare Register-Storage (LB)
void Assembler::cdb(DoubleRegister r1, const MemOperand& opnd) {
  rx_form(CD, Register::from_code(r1.code()),
          opnd.rx(), opnd.rb(), opnd.offset());
}

// Compare Register-Register (LB)
void Assembler::cdbr(DoubleRegister r1, DoubleRegister r2) {
  rre_form(CDBR,
           Register::from_code(r1.code()),
           Register::from_code(r2.code()));
}

// Divide Register-Storage (LB)
void Assembler::ddb(DoubleRegister r1, const MemOperand& opnd) {
  rxe_form(DDB, Register::from_code(r1.code()),
           opnd.rx(), opnd.rb(), opnd.offset());
}

// Divide Register-Register (LB)
void Assembler::ddbr(DoubleRegister r1, DoubleRegister r2) {
  rre_form(DDBR,
           Register::from_code(r1.code()),
           Register::from_code(r2.code()));
}

// Multiply Register-Storage (LB)
void Assembler::mdb(DoubleRegister r1, const MemOperand& opnd) {
  rxe_form(MDB, Register::from_code(r1.code()),
           opnd.rb(), opnd.rx(), opnd.offset());
}

// Multiply Register-Register (LB)
void Assembler::mdbr(DoubleRegister r1, DoubleRegister r2) {
  rre_form(MDBR,
           Register::from_code(r1.code()),
           Register::from_code(r2.code()));
}

// Subtract Register-Storage (LB)
void Assembler::sdb(DoubleRegister r1, const MemOperand& opnd) {
  rxe_form(SDB, Register::from_code(r1.code()),
           opnd.rx(), opnd.rb(), opnd.offset());
}

// Subtract Register-Register (LB)
void Assembler::sdbr(DoubleRegister r1, DoubleRegister r2) {
  rre_form(SDBR,
           Register::from_code(r1.code()),
           Register::from_code(r2.code()));
}

// Square Root (LB)
void Assembler::sqdb(DoubleRegister r1, const MemOperand& opnd) {
  rxe_form(SQDB, Register::from_code(r1.code()),
           opnd.rx(), opnd.rb(), opnd.offset());
}

// Square Root Register-Register (LB)
void Assembler::sqdbr(DoubleRegister r1, DoubleRegister r2) {
  rre_form(SQDBR,
           Register::from_code(r1.code()),
           Register::from_code(r2.code()));
}

// Load S <- L
void Assembler::ledbr(DoubleRegister r1, DoubleRegister r2) {
  rre_form(LEDBR,
           Register::from_code(r1.code()),
           Register::from_code(r2.code()));
}

// Load Complement Register-Register (LB)
void Assembler::lcdbr(DoubleRegister r1, DoubleRegister r2) {
  rre_form(LCDBR,
           Register::from_code(r1.code()),
           Register::from_code(r2.code()));
}

// Load Positive Register-Register (LB)
void Assembler::lpdbr(DoubleRegister r1, DoubleRegister r2) {
  rre_form(LPDBR,
           Register::from_code(r1.code()),
           Register::from_code(r2.code()));
}

// Store (L)
void Assembler::std(DoubleRegister r1, const MemOperand& opnd) {
  rx_form(STD, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Store (L)
void Assembler::stdy(DoubleRegister r1, const MemOperand& opnd) {
  rxy_form(STDY, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Store (S)
void Assembler::ste(DoubleRegister r1, const MemOperand& opnd) {
  rx_form(STE, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Store (S)
void Assembler::stey(DoubleRegister r1, const MemOperand& opnd) {
  rxy_form(STEY, r1, opnd.rx(), opnd.rb(), opnd.offset());
}


// Load (L)
void Assembler::ld(DoubleRegister r1, const MemOperand& opnd) {
  rx_form(LD, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Load (L)
void Assembler::ldy(DoubleRegister r1, const MemOperand& opnd) {
  rx_form(LDY, r1, opnd.rx(), opnd.rb(), opnd.offset());
}

// Load Register-Register (L)
void Assembler::ldr(DoubleRegister r1, DoubleRegister r2) {
  rr_form(LDR, r1, r2);
}

// Convert to Fixed point (64<-L)
void Assembler::cgdbr(Condition m, Register r1, DoubleRegister r2) {
  rrfe_form(CGDBR, m, Condition(0), r1, Register::from_code(r2.code()));
}

// Convert to Fixed point (32<-L)
void Assembler::cfdbr(Condition m, Register r1, DoubleRegister r2) {
  rrfe_form(CFDBR, m, Condition(0), r1, Register::from_code(r2.code()));
}

// Convert from Fixed point (L<-64)
void Assembler::cdgbr(DoubleRegister r1, Register r2) {
  rre_form(CDGBR, Register::from_code(r1.code()), r2);
}

// Convert from Fixed point (L<-32)
void Assembler::cdfbr(DoubleRegister r1, Register r2) {
  rre_form(CDFBR, Register::from_code(r1.code()), r2);
}

// TODO(AlanLi): check condition code
// Convert to Fixed Logical (64<-L)
void Assembler::clgdbr(Condition m3, Condition m4,
                       Register r1, DoubleRegister r2) {
  rrfe_form(CLGDBR, Condition(0), Condition(0),
            r1, Register::from_code(r2.code()));
}

// Convert to Fixed Logical (32<-L)
void Assembler::clfdbr(Condition m3, Condition m4,
                       Register r1, DoubleRegister r2) {
  rrfe_form(CLFDBR, Condition(0), Condition(0),
            r1, Register::from_code(r2.code()));
}

// Convert from Fixed Logical (L<-64)
void Assembler::cdlgbr(Condition m3, Condition m4,
                       DoubleRegister r1, Register r2) {
  rrfe_form(CDLGBR, Condition(0), Condition(0),
            Register::from_code(r1.code()), r2);
}

// Convert from Fixed Logical (L<-32)
void Assembler::cdlfbr(Condition m3, Condition m4,
                       DoubleRegister r1, Register r2) {
  rrfe_form(CDLFBR, Condition(0), Condition(0),
            Register::from_code(r1.code()), r2);
}

// Convert from Fixed point (S<-32)
void Assembler::cefbr(DoubleRegister r1, Register r2) {
  rre_form(CDFBR, Register::from_code(r1.code()), r2);
}

// Convert to Fixed point (32<-S)
void Assembler::cfebr(Register r1, DoubleRegister r2) {
  rre_form(CFDBR, r1, Register::from_code(r2.code()));
}

// Load (L <- S)
void Assembler::ldeb(DoubleRegister d1, const MemOperand& opnd) {
  rxe_form(LDEB, Register::from_code(d1.code()), opnd.rx(), opnd.rb(),
           opnd.offset());
}


// end of S390instructions


bool Assembler::IsNop(SixByteInstr instr, int type) {
  ASSERT((0 == type) || (DEBUG_BREAK_NOP == type));
  if (DEBUG_BREAK_NOP == type) {
    return (instr == 0xa53b0000);   // oill r3, 0
  }
  return (instr == 0x1800);   // lr r0,r0
}

// Debugging.
void Assembler::RecordJSReturn() {
  positions_recorder()->WriteRecordedPositions();
  CheckBuffer();
  RecordRelocInfo(RelocInfo::JS_RETURN);
}


void Assembler::RecordDebugBreakSlot() {
  positions_recorder()->WriteRecordedPositions();
  CheckBuffer();
  RecordRelocInfo(RelocInfo::DEBUG_BREAK_SLOT);
}


void Assembler::RecordComment(const char* msg) {
  if (FLAG_code_comments) {
    CheckBuffer();
    RecordRelocInfo(RelocInfo::COMMENT, reinterpret_cast<intptr_t>(msg));
  }
}


void Assembler::GrowBuffer() {
  if (!own_buffer_) FATAL("external code buffer is too small");

  // Compute new buffer size.
  CodeDesc desc;  // the new buffer
  if (buffer_size_ < 4*KB) {
    desc.buffer_size = 4*KB;
  } else if (buffer_size_ < 1*MB) {
    desc.buffer_size = 2*buffer_size_;
  } else {
    desc.buffer_size = buffer_size_ + 1*MB;
  }
  CHECK_GT(desc.buffer_size, 0);  // no overflow

  // Set up new buffer.
  desc.buffer = NewArray<byte>(desc.buffer_size);

  desc.instr_size = pc_offset();
  desc.reloc_size = (buffer_ + buffer_size_) - reloc_info_writer.pos();

  // Copy the data.
  intptr_t pc_delta = desc.buffer - buffer_;
  intptr_t rc_delta = (desc.buffer + desc.buffer_size) -
    (buffer_ + buffer_size_);
  memmove(desc.buffer, buffer_, desc.instr_size);
  memmove(reloc_info_writer.pos() + rc_delta,
          reloc_info_writer.pos(), desc.reloc_size);

  // Switch buffers.
  DeleteArray(buffer_);
  buffer_ = desc.buffer;
  buffer_size_ = desc.buffer_size;
  pc_ += pc_delta;
  reloc_info_writer.Reposition(reloc_info_writer.pos() + rc_delta,
                               reloc_info_writer.last_pc() + pc_delta);

  // None of our relocation types are pc relative pointing outside the code
  // buffer nor pc absolute pointing inside the code buffer, so there is no need
  // to relocate any emitted relocation entries.

#if ABI_USES_FUNCTION_DESCRIPTORS
  // Relocate runtime entries.
  for (RelocIterator it(desc); !it.done(); it.next()) {
    RelocInfo::Mode rmode = it.rinfo()->rmode();
    if (rmode == RelocInfo::INTERNAL_REFERENCE) {
      intptr_t* p = reinterpret_cast<intptr_t*>(it.rinfo()->pc());
      if (*p != 0) {  // 0 means uninitialized.
        *p += pc_delta;
      }
    }
  }
#endif
}


void Assembler::db(uint8_t data) {
  CheckBuffer();
  *reinterpret_cast<uint8_t*>(pc_) = data;
  pc_ += sizeof(uint8_t);
}


void Assembler::dd(uint32_t data) {
  CheckBuffer();
  *reinterpret_cast<uint32_t*>(pc_) = data;
  pc_ += sizeof(uint32_t);
}


void Assembler::RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data) {
  RelocInfo rinfo(pc_, rmode, data, NULL);
  if (rmode >= RelocInfo::JS_RETURN && rmode <= RelocInfo::DEBUG_BREAK_SLOT) {
    // Adjust code for new modes.
    ASSERT(RelocInfo::IsDebugBreakSlot(rmode)
           || RelocInfo::IsJSReturn(rmode)
           || RelocInfo::IsComment(rmode)
           || RelocInfo::IsPosition(rmode));
  }
  if (rinfo.rmode() != RelocInfo::NONE) {
    // Don't record external references unless the heap will be serialized.
    if (rmode == RelocInfo::EXTERNAL_REFERENCE) {
#ifdef DEBUG
      if (!Serializer::enabled()) {
        Serializer::TooLateToEnableNow();
      }
#endif
      if (!Serializer::enabled() && !emit_debug_code()) {
        return;
      }
    }
    ASSERT(buffer_space() >= kMaxRelocSize);  // too late to grow buffer here
    if (rmode == RelocInfo::CODE_TARGET_WITH_ID) {
      RelocInfo reloc_info_with_ast_id(pc_,
                                       rmode,
                                       RecordedAstId().ToInt(),
                                       NULL);
      ClearRecordedAstId();
      reloc_info_writer.Write(&reloc_info_with_ast_id);
    } else {
      reloc_info_writer.Write(&rinfo);
    }
  }
}


void Assembler::BlockTrampolinePoolFor(int instructions) {
  BlockTrampolinePoolBefore(pc_offset() + instructions * kInstrSize);
}


void Assembler::CheckTrampolinePool() {
  // Some small sequences of instructions must not be broken up by the
  // insertion of a trampoline pool; such sequences are protected by setting
  // either trampoline_pool_blocked_nesting_ or no_trampoline_pool_before_,
  // which are both checked here. Also, recursive calls to CheckTrampolinePool
  // are blocked by trampoline_pool_blocked_nesting_.
  if ((trampoline_pool_blocked_nesting_ > 0) ||
      (pc_offset() < no_trampoline_pool_before_)) {
    // Emission is currently blocked; make sure we try again as soon as
    // possible.
    if (trampoline_pool_blocked_nesting_ > 0) {
      next_buffer_check_ = pc_offset() + kInstrSize;
    } else {
      next_buffer_check_ = no_trampoline_pool_before_;
    }
    return;
  }

  ASSERT(!trampoline_emitted_);
  ASSERT(unbound_labels_count_ >= 0);
  if (unbound_labels_count_ > 0) {
    // First we emit jump, then we emit trampoline pool.
    { BlockTrampolinePoolScope block_trampoline_pool(this);
      Label after_pool;
      b(&after_pool);

      int pool_start = pc_offset();
      for (int i = 0; i < unbound_labels_count_; i++) {
        b(&after_pool);
      }
      bind(&after_pool);
      trampoline_ = Trampoline(pool_start, unbound_labels_count_);

      trampoline_emitted_ = true;
      // As we are only going to emit trampoline once, we need to prevent any
      // further emission.
      next_buffer_check_ = kMaxInt;
    }
  } else {
    // Number of branches to unbound label at this point is zero, so we can
    // move next buffer check to maximum.
    next_buffer_check_ = pc_offset() +
      kMaxCondBranchReach - kMaxBlockTrampolineSectionSize;
  }
  return;
}
} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_S390
