/*===================================================================*/
/*                                                                   */
/*  K6502.cpp : 6502 Emulator                                        */
/*                                                                   */
/*  2000/5/10   InfoNES Project ( based on pNesX )                   */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------*/
/*  Include files                                                    */
/*-------------------------------------------------------------------*/

#include "K6502.h"
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_StructuredLog.h"
#include "boko_flash_trace.h"

#include <stdio.h>
#include <pico.h>

/*-------------------------------------------------------------------*/
/*  Operation Macros                                                 */
/*-------------------------------------------------------------------*/

// Clock Op.
#define CLK(a) g_wPassedClocks += (a);

// Addressing Op.
// Address
// (Indirect,X)
#define AA_IX K6502_ReadZpW(K6502_Read(PC++) + X)
// (Indirect),Y
#define AA_IY K6502_ReadZpW(K6502_Read(PC++)) + Y
// Zero Page
#define AA_ZP K6502_Read(PC++)
// Zero Page,X
#define AA_ZPX (BYTE)(K6502_Read(PC++) + X)
// Zero Page,Y
#define AA_ZPY (BYTE)(K6502_Read(PC++) + Y)
// Absolute
#define AA_ABS (K6502_Read(PC++) | (WORD)K6502_Read(PC++) << 8)
// Absolute2 ( PC-- )
#define AA_ABS2 (K6502_Read(PC++) | (WORD)K6502_Read(PC) << 8)
// Absolute,X
#define AA_ABSX AA_ABS + X
// Absolute,Y
#define AA_ABSY AA_ABS + Y

// Data
// (Indirect,X)
#define A_IX K6502_Read(AA_IX)
// (Indirect),Y
#define A_IY K6502_ReadIY()
// Zero Page
#define A_ZP K6502_ReadZp(AA_ZP)
// Zero Page,X
#define A_ZPX K6502_ReadZp(AA_ZPX)
// Zero Page,Y
#define A_ZPY K6502_ReadZp(AA_ZPY)
// Absolute
#define A_ABS K6502_Read(AA_ABS)
// Absolute,X
#define A_ABSX K6502_ReadAbsX()
// Absolute,Y
#define A_ABSY K6502_ReadAbsY()
// Immediate
#define A_IMM K6502_Read(PC++)

// Flag Op.
#define SETF(a) F |= (a)
#define RSTF(a) F &= ~(a)
#define TEST(a)          \
  RSTF(FLAG_N | FLAG_Z); \
  SETF(g_byTestTable[a])

// Load & Store Op.
#define STA(a) K6502_Write((a), A);
#define STX(a) K6502_Write((a), X);
#define STY(a) K6502_Write((a), Y);
#define LDA(a) \
  A = (a);     \
  TEST(A);
#define LDX(a) \
  X = (a);     \
  TEST(X);
#define LDY(a) \
  Y = (a);     \
  TEST(Y);

// Stack Op.
#define PUSH(a) K6502_Write(BASE_STACK + SP--, (a))
#define PUSHW(a)  \
  PUSH((a) >> 8); \
  PUSH((a)&0xff)
#define POP(a) a = K6502_Read(BASE_STACK + ++SP)
#define POPW(a) \
  POP(a);       \
  a |= (K6502_Read(BASE_STACK + ++SP) << 8)

// Logical Op.
#define ORA(a) \
  A |= (a);    \
  TEST(A)
#define AND(a) \
  A &= (a);    \
  TEST(A)
#define EOR(a) \
  A ^= (a);    \
  TEST(A)
#define BIT(a)                    \
  byD0 = (a);                     \
  RSTF(FLAG_N | FLAG_V | FLAG_Z); \
  SETF((byD0 & (FLAG_N | FLAG_V)) | ((byD0 & A) ? 0 : FLAG_Z));
#define CMP(a)                    \
  wD0 = (WORD)A - (a);            \
  RSTF(FLAG_N | FLAG_Z | FLAG_C); \
  SETF(g_byTestTable[wD0 & 0xff] | (wD0 < 0x100 ? FLAG_C : 0));
#define CPX(a)                    \
  wD0 = (WORD)X - (a);            \
  RSTF(FLAG_N | FLAG_Z | FLAG_C); \
  SETF(g_byTestTable[wD0 & 0xff] | (wD0 < 0x100 ? FLAG_C : 0));
#define CPY(a)                    \
  wD0 = (WORD)Y - (a);            \
  RSTF(FLAG_N | FLAG_Z | FLAG_C); \
  SETF(g_byTestTable[wD0 & 0xff] | (wD0 < 0x100 ? FLAG_C : 0));

// Math Op. (A D flag isn't being supported.)
#define ADC(a)                                                                                 \
  byD0 = (a);                                                                                  \
  wD0 = A + byD0 + (F & FLAG_C);                                                               \
  byD1 = (BYTE)wD0;                                                                            \
  RSTF(FLAG_N | FLAG_V | FLAG_Z | FLAG_C);                                                     \
  SETF(g_byTestTable[byD1] | ((~(A ^ byD0) & (A ^ byD1) & 0x80) ? FLAG_V : 0) | (wD0 > 0xff)); \
  A = byD1;

#define SBC(a)                                                                                 \
  byD0 = (a);                                                                                  \
  wD0 = A - byD0 - (~F & FLAG_C);                                                              \
  byD1 = (BYTE)wD0;                                                                            \
  RSTF(FLAG_N | FLAG_V | FLAG_Z | FLAG_C);                                                     \
  SETF(g_byTestTable[byD1] | (((A ^ byD0) & (A ^ byD1) & 0x80) ? FLAG_V : 0) | (wD0 < 0x100)); \
  A = byD1;

#define DEC(a)            \
  wA0 = a;                \
  byD0 = K6502_Read(wA0); \
  --byD0;                 \
  K6502_Write(wA0, byD0); \
  TEST(byD0)
#define INC(a)            \
  wA0 = a;                \
  byD0 = K6502_Read(wA0); \
  ++byD0;                 \
  K6502_Write(wA0, byD0); \
  TEST(byD0)

// Shift Op.
#define ASLA                      \
  RSTF(FLAG_N | FLAG_Z | FLAG_C); \
  SETF(g_ASLTable[A].byFlag);     \
  A = g_ASLTable[A].byValue
#define ASL(a)                    \
  RSTF(FLAG_N | FLAG_Z | FLAG_C); \
  wA0 = a;                        \
  byD0 = K6502_Read(wA0);         \
  SETF(g_ASLTable[byD0].byFlag);  \
  K6502_Write(wA0, g_ASLTable[byD0].byValue)
#define LSRA                      \
  RSTF(FLAG_N | FLAG_Z | FLAG_C); \
  SETF(g_LSRTable[A].byFlag);     \
  A = g_LSRTable[A].byValue
#define LSR(a)                    \
  RSTF(FLAG_N | FLAG_Z | FLAG_C); \
  wA0 = a;                        \
  byD0 = K6502_Read(wA0);         \
  SETF(g_LSRTable[byD0].byFlag);  \
  K6502_Write(wA0, g_LSRTable[byD0].byValue)
#define ROLA                        \
  byD0 = F & FLAG_C;                \
  RSTF(FLAG_N | FLAG_Z | FLAG_C);   \
  SETF(g_ROLTable[byD0][A].byFlag); \
  A = g_ROLTable[byD0][A].byValue
#define ROL(a)                         \
  byD1 = F & FLAG_C;                   \
  RSTF(FLAG_N | FLAG_Z | FLAG_C);      \
  wA0 = a;                             \
  byD0 = K6502_Read(wA0);              \
  SETF(g_ROLTable[byD1][byD0].byFlag); \
  K6502_Write(wA0, g_ROLTable[byD1][byD0].byValue)
#define RORA                        \
  byD0 = F & FLAG_C;                \
  RSTF(FLAG_N | FLAG_Z | FLAG_C);   \
  SETF(g_RORTable[byD0][A].byFlag); \
  A = g_RORTable[byD0][A].byValue
#define ROR(a)                         \
  byD1 = F & FLAG_C;                   \
  RSTF(FLAG_N | FLAG_Z | FLAG_C);      \
  wA0 = a;                             \
  byD0 = K6502_Read(wA0);              \
  SETF(g_RORTable[byD1][byD0].byFlag); \
  K6502_Write(wA0, g_RORTable[byD1][byD0].byValue)

// Stable unofficial opcodes implemented by common NES emulators.
#define SLO(a)                    \
  wA0 = (a);                      \
  byD0 = K6502_Read(wA0);         \
  byD1 = g_ASLTable[byD0].byValue;\
  RSTF(FLAG_C);                   \
  SETF(g_ASLTable[byD0].byFlag & FLAG_C); \
  K6502_Write(wA0, byD1);         \
  A |= byD1;                      \
  TEST(A)
#define RLA(a)                    \
  byD1 = F & FLAG_C;              \
  wA0 = (a);                      \
  byD0 = K6502_Read(wA0);         \
  RSTF(FLAG_C);                   \
  SETF(g_ROLTable[byD1][byD0].byFlag & FLAG_C); \
  byD0 = g_ROLTable[byD1][byD0].byValue; \
  K6502_Write(wA0, byD0);         \
  A &= byD0;                      \
  TEST(A)
#define SRE(a)                    \
  wA0 = (a);                      \
  byD0 = K6502_Read(wA0);         \
  byD1 = g_LSRTable[byD0].byValue;\
  RSTF(FLAG_C);                   \
  SETF(g_LSRTable[byD0].byFlag & FLAG_C); \
  K6502_Write(wA0, byD1);         \
  A ^= byD1;                      \
  TEST(A)
/* RRA: ROR memory then ADC. Carry flow:
   byD1 saves old C as ROR input; SETF(bit0) sets C to ROR carry-out (old bit0),
   which then feeds into ADC as carry-in via F & FLAG_C inside the ADC macro. */
#define RRA(a)                    \
  byD1 = F & FLAG_C;              \
  wA0 = (a);                      \
  byD0 = K6502_Read(wA0);         \
  RSTF(FLAG_C);                   \
  SETF((byD0 & 0x01) ? FLAG_C : 0); \
  byD0 = g_RORTable[byD1][byD0].byValue; \
  K6502_Write(wA0, byD0);         \
  ADC(byD0)
#define SAX(a) K6502_Write((a), A & X)
#define LAX(a)                    \
  byD0 = (a);                     \
  A = byD0;                       \
  X = byD0;                       \
  TEST(byD0)
#define DCP(a)                    \
  wA0 = (a);                      \
  byD0 = K6502_Read(wA0) - 1;     \
  K6502_Write(wA0, byD0);         \
  CMP(byD0)
#define ISB(a)                    \
  wA0 = (a);                      \
  byD0 = K6502_Read(wA0) + 1;     \
  K6502_Write(wA0, byD0);         \
  SBC(byD0)
#define ANC(a)                    \
  A &= (a);                       \
  RSTF(FLAG_N | FLAG_Z | FLAG_C); \
  SETF(g_byTestTable[A] | ((A & 0x80) ? FLAG_C : 0))
#define ALR(a)                    \
  A &= (a);                       \
  LSRA

// Jump Op.
#define JSR      \
  wA0 = AA_ABS2; \
  PUSHW(PC);     \
  PC = wA0;
#define BRA(a)                                  \
  if (a)                                        \
  {                                             \
    wA0 = PC;                                   \
    PC += (int8_t)K6502_Read(PC);               \
    CLK(3 + ((wA0 & 0x0100) != (PC & 0x0100))); \
    ++PC;                                       \
  }                                             \
  else                                          \
  {                                             \
    ++PC;                                       \
    CLK(2);                                     \
  }
#define JMP(a) PC = a;

/*-------------------------------------------------------------------*/
/*  Global valiables                                                 */
/*-------------------------------------------------------------------*/

// 6502 Register
WORD PC;
BYTE SP;
BYTE F;
BYTE A;
BYTE X;
BYTE Y;

// The state of the IRQ pin
BYTE IRQ_State;

// Wiring of the IRQ pin
BYTE IRQ_Wiring;

// The state of the NMI pin
BYTE NMI_State;

// Wiring of the NMI pin
BYTE NMI_Wiring;

// The number of the clocks that it passed
int g_wPassedClocks;
int g_wCurrentClocks;

WORD getPassedClocks()
{
  return g_wCurrentClocks;
}

// A table for the test
BYTE g_byTestTable[256];

// Value and Flag Data
struct value_table_tag
{
  BYTE byValue;
  BYTE byFlag;
};

// A table for ASL
struct value_table_tag g_ASLTable[256];

// A table for LSR
struct value_table_tag g_LSRTable[256];

// A table for ROL
struct value_table_tag g_ROLTable[2][256];

// A table for ROR
struct value_table_tag g_RORTable[2][256];

static bool g_unofficialOpcodeTable[256];

#if INFONES_ENABLE_BOKOSUKA_STATE_LOG
static BYTE g_bokosuka_last_8827_a = 0xff;
static BYTE g_bokosuka_last_8827_1b = 0xff;
static BYTE g_bokosuka_last_8827_25 = 0xff;
static BYTE g_bokosuka_last_8827_26 = 0xff;
static BYTE g_bokosuka_last_8827_2c = 0xff;
static BYTE g_bokosuka_last_8827_3f = 0xff;
static BYTE g_bokosuka_last_8827_48 = 0xff;
static BYTE g_bokosuka_last_888e_a = 0xff;
static BYTE g_bokosuka_last_888e_1f = 0xff;
static BYTE g_bokosuka_last_888e_25 = 0xff;
static BYTE g_bokosuka_last_888e_26 = 0xff;
static BYTE g_bokosuka_last_888e_29 = 0xff;
static BYTE g_bokosuka_last_888e_2c = 0xff;
unsigned g_bokosuka_uart_window = 0;
unsigned long g_bokosuka_uart_window_seq = 0;
#endif

static bool __attribute__((noinline)) K6502_RunUnofficial(BYTE byCode)
{
  WORD wA0;
  BYTE byD0;
  BYTE byD1;
  WORD wD0;

  switch (byCode)
  {
  case 0x0B: ANC(A_IMM); CLK(2); return true;
  case 0x2B: ANC(A_IMM); CLK(2); return true;

  case 0x03: SLO(AA_IX); CLK(8); return true;
  case 0x07: SLO(AA_ZP); CLK(5); return true;
  case 0x0F: SLO(AA_ABS); CLK(6); return true;
  case 0x13: SLO(AA_IY); CLK(8); return true;
  case 0x17: SLO(AA_ZPX); CLK(6); return true;
  case 0x1B: SLO(AA_ABSY); CLK(7); return true;
  case 0x1F: SLO(AA_ABSX); CLK(7); return true;

  case 0x23: RLA(AA_IX); CLK(8); return true;
  case 0x27: RLA(AA_ZP); CLK(5); return true;
  case 0x2F: RLA(AA_ABS); CLK(6); return true;
  case 0x33: RLA(AA_IY); CLK(8); return true;
  case 0x37: RLA(AA_ZPX); CLK(6); return true;
  case 0x3B: RLA(AA_ABSY); CLK(7); return true;
  case 0x3F: RLA(AA_ABSX); CLK(7); return true;

  case 0x4B: ALR(A_IMM); CLK(2); return true;

  case 0x43: SRE(AA_IX); CLK(8); return true;
  case 0x47: SRE(AA_ZP); CLK(5); return true;
  case 0x4F: SRE(AA_ABS); CLK(6); return true;
  case 0x53: SRE(AA_IY); CLK(8); return true;
  case 0x57: SRE(AA_ZPX); CLK(6); return true;
  case 0x5B: SRE(AA_ABSY); CLK(7); return true;
  case 0x5F: SRE(AA_ABSX); CLK(7); return true;

  case 0x63: RRA(AA_IX); CLK(8); return true;
  case 0x67: RRA(AA_ZP); CLK(5); return true;
  case 0x6F: RRA(AA_ABS); CLK(6); return true;
  case 0x73: RRA(AA_IY); CLK(8); return true;
  case 0x77: RRA(AA_ZPX); CLK(6); return true;
  case 0x7B: RRA(AA_ABSY); CLK(7); return true;
  case 0x7F: RRA(AA_ABSX); CLK(7); return true;

  case 0x83: SAX(AA_IX); CLK(6); return true;
  case 0x87: SAX(AA_ZP); CLK(3); return true;
  case 0x8F: SAX(AA_ABS); CLK(4); return true;
  case 0x97: SAX(AA_ZPY); CLK(4); return true;

  case 0xA3: LAX(A_IX); CLK(6); return true;
  case 0xA7: LAX(A_ZP); CLK(3); return true;
  case 0xAF: LAX(A_ABS); CLK(4); return true;
  case 0xB3: LAX(A_IY); CLK(5); return true;
  case 0xB7: LAX(A_ZPY); CLK(4); return true;
  case 0xBF: LAX(A_ABSY); CLK(4); return true;

  case 0xC3: DCP(AA_IX); CLK(8); return true;
  case 0xC7: DCP(AA_ZP); CLK(5); return true;
  case 0xCF: DCP(AA_ABS); CLK(6); return true;
  case 0xD3: DCP(AA_IY); CLK(8); return true;
  case 0xD7: DCP(AA_ZPX); CLK(6); return true;
  case 0xDB: DCP(AA_ABSY); CLK(7); return true;
  case 0xDF: DCP(AA_ABSX); CLK(7); return true;

  case 0xE3: ISB(AA_IX); CLK(8); return true;
  case 0xE7: ISB(AA_ZP); CLK(5); return true;
  case 0xEF: ISB(AA_ABS); CLK(6); return true;
  case 0xF3: ISB(AA_IY); CLK(8); return true;
  case 0xF7: ISB(AA_ZPX); CLK(6); return true;
  case 0xFB: ISB(AA_ABSY); CLK(7); return true;
  case 0xFF: ISB(AA_ABSX); CLK(7); return true;

  case 0xCB:
    byD0 = A_IMM;
    wD0 = static_cast<WORD>(A & X) - byD0;
    X = static_cast<BYTE>(wD0);
    RSTF(FLAG_N | FLAG_Z | FLAG_C);
    SETF(g_byTestTable[X] | (wD0 < 0x100 ? FLAG_C : 0));
    CLK(2);
    return true;

  case 0xEB:
    SBC(A_IMM);
    CLK(2);
    return true;

  case 0x1A:
  case 0x3A:
  case 0x5A:
  case 0x7A:
  case 0xDA:
  case 0xFA:
    CLK(2);
    return true;

  case 0x80:
  case 0x82:
  case 0x89:
  case 0xC2:
  case 0xE2:
    PC++;
    CLK(2);
    return true;

  case 0x04:
  case 0x44:
  case 0x64:
    PC++;
    CLK(3);
    return true;

  case 0x14:
  case 0x34:
  case 0x54:
  case 0x74:
  case 0xD4:
  case 0xF4:
    PC++;
    CLK(4);
    return true;

  case 0x0C:
  case 0x1C:
  case 0x3C:
  case 0x5C:
  case 0x7C:
  case 0xDC:
  case 0xFC:
    PC += 2;
    CLK(4);
    return true;

  default:
    return false;
  }
}

/*===================================================================*/
/*                                                                   */
/*                K6502_Init() : Initialize K6502                    */
/*                                                                   */
/*===================================================================*/
void K6502_Init()
{
  /*
 *  Initialize K6502
 *
 *  You must call this function only once at first.
 */

  BYTE idx;
  BYTE idx2;

  // The establishment of the IRQ pin
  NMI_Wiring = NMI_State = 1;
  IRQ_Wiring = IRQ_State = 1;

  for (int code = 0; code < 256; ++code)
  {
    g_unofficialOpcodeTable[code] = false;
  }
  static const BYTE unofficialOpcodes[] = {
      0x0B, 0x2B, 0x4B,
      0x03, 0x07, 0x0F, 0x13, 0x17, 0x1B, 0x1F,
      0x23, 0x27, 0x2F, 0x33, 0x37, 0x3B, 0x3F,
      0x43, 0x47, 0x4F, 0x53, 0x57, 0x5B, 0x5F,
      0x63, 0x67, 0x6F, 0x73, 0x77, 0x7B, 0x7F,
      0x83, 0x87, 0x8F, 0x97,
      0xA3, 0xA7, 0xAF, 0xB3, 0xB7, 0xBF,
      0xC3, 0xC7, 0xCB, 0xCF, 0xD3, 0xD7, 0xDB, 0xDF,
      0xE3, 0xE7, 0xEB, 0xEF, 0xF3, 0xF7, 0xFB, 0xFF,
      0x1A, 0x3A, 0x5A, 0x7A, 0xDA, 0xFA,
      0x80, 0x82, 0x89, 0xC2, 0xE2,
      0x04, 0x44, 0x64,
      0x14, 0x34, 0x54, 0x74, 0xD4, 0xF4,
      0x0C, 0x1C, 0x3C, 0x5C, 0x7C, 0xDC, 0xFC};
  for (BYTE code : unofficialOpcodes)
  {
    g_unofficialOpcodeTable[code] = true;
  }

  // Make a table for the test
  idx = 0;
  do
  {
    if (idx == 0)
      g_byTestTable[0] = FLAG_Z;
    else if (idx > 127)
      g_byTestTable[idx] = FLAG_N;
    else
      g_byTestTable[idx] = 0;

    ++idx;
  } while (idx != 0);

  // Make a table ASL
  idx = 0;
  do
  {
    g_ASLTable[idx].byValue = idx << 1;
    g_ASLTable[idx].byFlag = 0;

    if (idx > 127)
      g_ASLTable[idx].byFlag = FLAG_C;

    if (g_ASLTable[idx].byValue == 0)
      g_ASLTable[idx].byFlag |= FLAG_Z;
    else if (g_ASLTable[idx].byValue & 0x80)
      g_ASLTable[idx].byFlag |= FLAG_N;

    ++idx;
  } while (idx != 0);

  // Make a table LSR
  idx = 0;
  do
  {
    g_LSRTable[idx].byValue = idx >> 1;
    g_LSRTable[idx].byFlag = 0;

    if (idx & 1)
      g_LSRTable[idx].byFlag = FLAG_C;

    if (g_LSRTable[idx].byValue == 0)
      g_LSRTable[idx].byFlag |= FLAG_Z;

    ++idx;
  } while (idx != 0);

  // Make a table ROL
  for (idx2 = 0; idx2 < 2; ++idx2)
  {
    idx = 0;
    do
    {
      g_ROLTable[idx2][idx].byValue = (idx << 1) | idx2;
      g_ROLTable[idx2][idx].byFlag = 0;

      if (idx > 127)
        g_ROLTable[idx2][idx].byFlag = FLAG_C;

      if (g_ROLTable[idx2][idx].byValue == 0)
        g_ROLTable[idx2][idx].byFlag |= FLAG_Z;
      else if (g_ROLTable[idx2][idx].byValue & 0x80)
        g_ROLTable[idx2][idx].byFlag |= FLAG_N;

      ++idx;
    } while (idx != 0);
  }

  // Make a table ROR
  for (idx2 = 0; idx2 < 2; ++idx2)
  {
    idx = 0;
    do
    {
      g_RORTable[idx2][idx].byValue = (idx >> 1) | (idx2 << 7);
      g_RORTable[idx2][idx].byFlag = 0;

      if (idx & 1)
        g_RORTable[idx2][idx].byFlag = FLAG_C;

      if (g_RORTable[idx2][idx].byValue == 0)
        g_RORTable[idx2][idx].byFlag |= FLAG_Z;
      else if (g_RORTable[idx2][idx].byValue & 0x80)
        g_RORTable[idx2][idx].byFlag |= FLAG_N;

      ++idx;
    } while (idx != 0);
  }
}

/*===================================================================*/
/*                                                                   */
/*                K6502_Reset() : Reset a CPU                        */
/*                                                                   */
/*===================================================================*/
void K6502_Reset()
{
  /*
 *  Reset a CPU
 *
 */

  // Reset Registers
  PC = K6502_ReadW(VECTOR_RESET);
  SP = 0xFF;
  A = X = Y = 0;
  F = FLAG_Z | FLAG_R | FLAG_I;

  // Set up the state of the Interrupt pin.
  NMI_State = NMI_Wiring;
  IRQ_State = IRQ_Wiring;

  // Reset Passed Clocks
  g_wPassedClocks = 0;
  g_wCurrentClocks = 0;
}

/*===================================================================*/
/*                                                                   */
/*    K6502_Set_Int_Wiring() : Set up wiring of the interrupt pin    */
/*                                                                   */
/*===================================================================*/
void K6502_Set_Int_Wiring(BYTE byNMI_Wiring, BYTE byIRQ_Wiring)
{
  /*
 * Set up wiring of the interrupt pin
 *
 */

  NMI_Wiring = byNMI_Wiring;
  IRQ_Wiring = byIRQ_Wiring;
}

static void __not_in_flash_func(procNMI)()
{
  // Dispose of it if there is an interrupt requirement
  if (NMI_State != NMI_Wiring)
  {
    // NMI Interrupt
    NMI_State = NMI_Wiring;
    CLK(7);

    PUSHW(PC);
    PUSH(F & ~FLAG_B);

    RSTF(FLAG_D);
    SETF(FLAG_I);

    PC = K6502_ReadW(VECTOR_NMI);
  }
  else if (IRQ_State != IRQ_Wiring)
  {
    // IRQ Interrupt
    // Execute IRQ if an I flag isn't being set
    if (!(F & FLAG_I))
    {
      IRQ_State = IRQ_Wiring;
      CLK(7);

      PUSHW(PC);
      PUSH(F & ~FLAG_B);

      RSTF(FLAG_D);
      SETF(FLAG_I);

      PC = K6502_ReadW(VECTOR_IRQ);
    }
  }
}

static void __not_in_flash_func(step)(int wClocks)
{
  /*
 *  Only the specified number of the clocks execute Op.
 *
 *  Parameters
 *    WORD wClocks              (Read)
 *      The number of the clocks
 */

  BYTE byCode;

  WORD wA0;
  BYTE byD0;
  BYTE byD1;
  WORD wD0;

  auto prePassedClocks = g_wPassedClocks;

  // It has a loop until a constant clock passes
  while (g_wPassedClocks < wClocks)
  {
    // if (PC == 0xc449 || PC == 0xc955)
    // {
    //   printf("%04x:%02x\n", PC, A);
    // }

    // if (PC == 0xc44a)
    // {
    //   printf("A:%02X X:%02X Y:%02X SP:%02X F:%02X  %04X\n", A, X, Y, SP, F, PC);
    // }

    // Read an instruction
    structured_log_note_initial_a5_exec(PC);
#if INFONES_ENABLE_BOKOSUKA_STATE_LOG
    if (PC == 0x8827)
    {
      const BYTE v1b = RAM[0x001B];
      const BYTE v25 = RAM[0x0025];
      const BYTE v26 = RAM[0x0026];
      const BYTE v2c = RAM[0x002C];
      const BYTE v3f = RAM[0x003F];
      const BYTE v48 = RAM[0x0048];
      if (g_bokosuka_uart_window != 0 &&
          (A != g_bokosuka_last_8827_a ||
          v1b != g_bokosuka_last_8827_1b ||
          v25 != g_bokosuka_last_8827_25 ||
          v26 != g_bokosuka_last_8827_26 ||
          v2c != g_bokosuka_last_8827_2c ||
          v3f != g_bokosuka_last_8827_3f ||
          v48 != g_bokosuka_last_8827_48))
      {
        g_bokosuka_last_8827_a = A;
        g_bokosuka_last_8827_1b = v1b;
        g_bokosuka_last_8827_25 = v25;
        g_bokosuka_last_8827_26 = v26;
        g_bokosuka_last_8827_2c = v2c;
        g_bokosuka_last_8827_3f = v3f;
        g_bokosuka_last_8827_48 = v48;
        printf("[BOKO_8827] trig=%lu w=%u f=%lu sl=%u a=%02X fz=%u fc=%u r1b=%02X r25=%02X r26=%02X r2c=%02X r3f=%02X r48=%02X r3b=%02X r3c=%02X r9d=%02X\n",
               g_bokosuka_uart_window_seq,
               g_bokosuka_uart_window,
               structured_log_current_frame(),
               (unsigned)PPU_Scanline,
               (unsigned)A,
               (unsigned)((F & FLAG_Z) ? 1 : 0),
               (unsigned)((F & FLAG_C) ? 1 : 0),
               (unsigned)v1b,
               (unsigned)v25,
               (unsigned)v26,
               (unsigned)v2c,
               (unsigned)v3f,
               (unsigned)v48,
               (unsigned)RAM[0x003B],
               (unsigned)RAM[0x003C],
               (unsigned)RAM[0x009D]);
        boko_flash_trace_record_8827(g_bokosuka_uart_window_seq,
                                     g_bokosuka_uart_window,
                                     structured_log_current_frame(),
                                     PPU_Scanline,
                                     A,
                                     (BYTE)(F & (FLAG_Z | FLAG_C)),
                                     v1b,
                                     v25,
                                     v26,
                                     v2c,
                                     v3f,
                                     v48,
                                     RAM[0x003B],
                                     RAM[0x003C],
                                     RAM[0x009D]);
        g_bokosuka_uart_window--;
      }
    }
    if (PC == 0x888E)
    {
      const BYTE v1f = RAM[0x001F];
      const BYTE v25 = RAM[0x0025];
      const BYTE v26 = RAM[0x0026];
      const BYTE v29 = RAM[0x0029];
      const BYTE v2c = RAM[0x002C];
      if (g_bokosuka_uart_window != 0 &&
          (A != g_bokosuka_last_888e_a ||
          v1f != g_bokosuka_last_888e_1f ||
          v25 != g_bokosuka_last_888e_25 ||
          v26 != g_bokosuka_last_888e_26 ||
          v29 != g_bokosuka_last_888e_29 ||
          v2c != g_bokosuka_last_888e_2c))
      {
        g_bokosuka_last_888e_a = A;
        g_bokosuka_last_888e_1f = v1f;
        g_bokosuka_last_888e_25 = v25;
        g_bokosuka_last_888e_26 = v26;
        g_bokosuka_last_888e_29 = v29;
        g_bokosuka_last_888e_2c = v2c;
        printf("[BOKO_888E] trig=%lu w=%u f=%lu sl=%u a=%02X fz=%u fc=%u r1f=%02X r25=%02X r26=%02X r29=%02X r2c=%02X r3b=%02X r3c=%02X\n",
               g_bokosuka_uart_window_seq,
               g_bokosuka_uart_window,
               structured_log_current_frame(),
               (unsigned)PPU_Scanline,
               (unsigned)A,
               (unsigned)((F & FLAG_Z) ? 1 : 0),
               (unsigned)((F & FLAG_C) ? 1 : 0),
               (unsigned)v1f,
               (unsigned)v25,
               (unsigned)v26,
               (unsigned)v29,
               (unsigned)v2c,
               (unsigned)RAM[0x003B],
               (unsigned)RAM[0x003C]);
        boko_flash_trace_record_888e(g_bokosuka_uart_window_seq,
                                     g_bokosuka_uart_window,
                                     structured_log_current_frame(),
                                     PPU_Scanline,
                                     A,
                                     (BYTE)(F & (FLAG_Z | FLAG_C)),
                                     v1f,
                                     v25,
                                     v26,
                                     v29,
                                     v2c,
                                     RAM[0x003B],
                                     RAM[0x003C]);
        g_bokosuka_uart_window--;
      }
    }
#endif
#if INFONES_ENABLE_INITIAL_SEQUENCE_LOG
    switch (PC) {
    case 0xD9D2:
    case 0xD9D6:
    case 0xD9D9:
    case 0xEB15:
    case 0xEB18:
    case 0xEB25:
    case 0xEB35:
    case 0xEB37:
    case 0xEB39:
    case 0xEB3B:
      structured_log_note_initial_a5_code_bytes(PC, K6502_Read(PC), K6502_Read(PC + 1), K6502_Read(PC + 2));
      break;
    default:
      break;
    }
#endif
    byCode = K6502_Read(PC++);

    //    printf("PC %04x %02x\n", PC - 1, byCode);

    if (g_unofficialOpcodeTable[byCode] && K6502_RunUnofficial(byCode))
    {
      continue;
    }

    // Execute an instruction.
    switch (byCode)
    {
    case 0x00: // BRK
      ++PC;
      PUSHW(PC);
      SETF(FLAG_B);
      PUSH(F);
      SETF(FLAG_I);
      RSTF(FLAG_D);
      PC = K6502_ReadW(VECTOR_IRQ);
      CLK(7);
      break;

    case 0x01: // ORA (Zpg,X)
      ORA(A_IX);
      CLK(6);
      break;

    case 0x05: // ORA Zpg
      ORA(A_ZP);
      CLK(3);
      break;

    case 0x06: // ASL Zpg
      ASL(AA_ZP);
      CLK(5);
      break;

    case 0x08: // PHP
      SETF(FLAG_B);
      PUSH(F);
      CLK(3);
      break;

    case 0x09: // ORA #Oper
      ORA(A_IMM);
      CLK(2);
      break;

    case 0x0A: // ASL A
      ASLA;
      CLK(2);
      break;

    case 0x0D: // ORA Abs
      ORA(A_ABS);
      CLK(4);
      break;

    case 0x0e: // ASL Abs
      ASL(AA_ABS);
      CLK(6);
      break;

    case 0x10: // BPL Oper
      BRA(!(F & FLAG_N));
      break;

    case 0x11: // ORA (Zpg),Y
      ORA(A_IY);
      CLK(5);
      break;

    case 0x15: // ORA Zpg,X
      ORA(A_ZPX);
      CLK(4);
      break;

    case 0x16: // ASL Zpg,X
      ASL(AA_ZPX);
      CLK(6);
      break;

    case 0x18: // CLC
      RSTF(FLAG_C);
      CLK(2);
      break;

    case 0x19: // ORA Abs,Y
      ORA(A_ABSY);
      CLK(4);
      break;

    case 0x1D: // ORA Abs,X
      ORA(A_ABSX);
      CLK(4);
      break;

    case 0x1E: // ASL Abs,X
      ASL(AA_ABSX);
      CLK(7);
      break;

    case 0x20: // JSR Abs
      JSR;
      CLK(6);
      break;

    case 0x21: // AND (Zpg,X)
      AND(A_IX);
      CLK(6);
      break;

    case 0x24: // BIT Zpg
      BIT(A_ZP);
      CLK(3);
      break;

    case 0x25: // AND Zpg
      AND(A_ZP);
      CLK(3);
      break;

    case 0x26: // ROL Zpg
      ROL(AA_ZP);
      CLK(5);
      break;

    case 0x28: // PLP
      POP(F);
      SETF(FLAG_R);
      CLK(4);
      break;

    case 0x29: // AND #Oper
      AND(A_IMM);
      CLK(2);
      break;

    case 0x2A: // ROL A
      ROLA;
      CLK(2);
      break;

    case 0x2C: // BIT Abs
      BIT(A_ABS);
      CLK(4);
      break;

    case 0x2D: // AND Abs
      AND(A_ABS);
      CLK(4);
      break;

    case 0x2E: // ROL Abs
      ROL(AA_ABS);
      CLK(6);
      break;

    case 0x30: // BMI Oper
      BRA(F & FLAG_N);
      break;

    case 0x31: // AND (Zpg),Y
      AND(A_IY);
      CLK(5);
      break;

    case 0x35: // AND Zpg,X
      AND(A_ZPX);
      CLK(4);
      break;

    case 0x36: // ROL Zpg,X
      ROL(AA_ZPX);
      CLK(6);
      break;

    case 0x38: // SEC
      SETF(FLAG_C);
      CLK(2);
      break;

    case 0x39: // AND Abs,Y
      AND(A_ABSY);
      CLK(4);
      break;

    case 0x3D: // AND Abs,X
      AND(A_ABSX);
      CLK(4);
      break;

    case 0x3E: // ROL Abs,X
      ROL(AA_ABSX);
      CLK(7);
      break;

    case 0x40: // RTI
      POP(F);
      SETF(FLAG_R);
      POPW(PC);
      CLK(6);
      break;

    case 0x41: // EOR (Zpg,X)
      EOR(A_IX);
      CLK(6);
      break;

    case 0x45: // EOR Zpg
      EOR(A_ZP);
      CLK(3);
      break;

    case 0x46: // LSR Zpg
      LSR(AA_ZP);
      CLK(5);
      break;

    case 0x48: // PHA
      PUSH(A);
      CLK(3);
      break;

    case 0x49: // EOR #Oper
      EOR(A_IMM);
      CLK(2);
      break;

    case 0x4A: // LSR A
      LSRA;
      CLK(2);
      break;

    case 0x4C: // JMP Abs
#if 0
      JMP(AA_ABS);
      CLK(3);
#else
    {
      auto addr = AA_ABS;
      if (addr == PC - 3)
      {
        JMP(addr);
        do
        {
          CLK(3);
        } while (g_wPassedClocks < wClocks);
        break;
      }
      else
      {
        JMP(addr);
        CLK(3);
      }
    }
#endif
      break;

    case 0x4D: // EOR Abs
      EOR(A_ABS);
      CLK(4);
      break;

    case 0x4E: // LSR Abs
      LSR(AA_ABS);
      CLK(6);
      break;

    case 0x50: // BVC
      BRA(!(F & FLAG_V));
      break;

    case 0x51: // EOR (Zpg),Y
      EOR(A_IY);
      CLK(5);
      break;

    case 0x55: // EOR Zpg,X
      EOR(A_ZPX);
      CLK(4);
      break;

    case 0x56: // LSR Zpg,X
      LSR(AA_ZPX);
      CLK(6);
      break;

    case 0x58: // CLI
      byD0 = F;
      RSTF(FLAG_I);
      CLK(2);
      if ((byD0 & FLAG_I) && IRQ_State != IRQ_Wiring)
      {
        IRQ_State = IRQ_Wiring;
        CLK(7);

        PUSHW(PC);
        PUSH(F & ~FLAG_B);

        RSTF(FLAG_D);
        SETF(FLAG_I);

        PC = K6502_ReadW(VECTOR_IRQ);
      }
      break;

    case 0x59: // EOR Abs,Y
      EOR(A_ABSY);
      CLK(4);
      break;

    case 0x5D: // EOR Abs,X
      EOR(A_ABSX);
      CLK(4);
      break;

    case 0x5E: // LSR Abs,X
      LSR(AA_ABSX);
      CLK(7);
      break;

    case 0x60: // RTS
      POPW(PC);
      ++PC;
      CLK(6);
      break;

    case 0x61: // ADC (Zpg,X)
      ADC(A_IX);
      CLK(6);
      break;

    case 0x65: // ADC Zpg
      ADC(A_ZP);
      CLK(3);
      break;

    case 0x66: // ROR Zpg
      ROR(AA_ZP);
      CLK(5);
      break;

    case 0x68: // PLA
      POP(A);
      TEST(A);
      CLK(4);
      break;

    case 0x69: // ADC #Oper
      ADC(A_IMM);
      CLK(2);
      break;

    case 0x6A: // ROR A
      RORA;
      CLK(2);
      break;

    case 0x6C: // JMP (Abs)
      JMP(K6502_ReadW2(AA_ABS));
      CLK(5);
      break;

    case 0x6D: // ADC Abs
      ADC(A_ABS);
      CLK(4);
      break;

    case 0x6E: // ROR Abs
      ROR(AA_ABS);
      CLK(6);
      break;

    case 0x70: // BVS
      BRA(F & FLAG_V);
      break;

    case 0x71: // ADC (Zpg),Y
      ADC(A_IY);
      CLK(5);
      break;

    case 0x75: // ADC Zpg,X
      ADC(A_ZPX);
      CLK(4);
      break;

    case 0x76: // ROR Zpg,X
      ROR(AA_ZPX);
      CLK(6);
      break;

    case 0x78: // SEI
      SETF(FLAG_I);
      CLK(2);
      break;

    case 0x79: // ADC Abs,Y
      ADC(A_ABSY);
      CLK(4);
      break;

    case 0x7D: // ADC Abs,X
      ADC(A_ABSX);
      CLK(4);
      break;

    case 0x7E: // ROR Abs,X
      ROR(AA_ABSX);
      CLK(7);
      break;

    case 0x81: // STA (Zpg,X)
      STA(AA_IX);
      CLK(6);
      break;

    case 0x84: // STY Zpg
      STY(AA_ZP);
      CLK(3);
      break;

    case 0x85: // STA Zpg
      STA(AA_ZP);
      CLK(3);
      break;

    case 0x86: // STX Zpg
      STX(AA_ZP);
      CLK(3);
      break;

    case 0x88: // DEY
      --Y;
      TEST(Y);
      CLK(2);
      break;

    case 0x8A: // TXA
      A = X;
      TEST(A);
      CLK(2);
      break;

    case 0x8C: // STY Abs
      STY(AA_ABS);
      CLK(4);
      break;

    case 0x8D: // STA Abs
      STA(AA_ABS);
      CLK(4);
      break;

    case 0x8E: // STX Abs
      STX(AA_ABS);
      CLK(4);
      break;

    case 0x90: // BCC
      BRA(!(F & FLAG_C));
      break;

    case 0x91: // STA (Zpg),Y
      STA(AA_IY);
      CLK(6);
      break;

    case 0x94: // STY Zpg,X
      STY(AA_ZPX);
      CLK(4);
      break;

    case 0x95: // STA Zpg,X
      STA(AA_ZPX);
      CLK(4);
      break;

    case 0x96: // STX Zpg,Y
      STX(AA_ZPY);
      CLK(4);
      break;

    case 0x98: // TYA
      A = Y;
      TEST(A);
      CLK(2);
      break;

    case 0x99: // STA Abs,Y
      STA(AA_ABSY);
      CLK(5);
      break;

    case 0x9A: // TXS
      SP = X;
      CLK(2);
      break;

    case 0x9D: // STA Abs,X
      STA(AA_ABSX);
      CLK(5);
      break;

    case 0xA0: // LDY #Oper
      LDY(A_IMM);
      CLK(2);
      break;

    case 0xA1: // LDA (Zpg,X)
      LDA(A_IX);
      CLK(6);
      break;

    case 0xA2: // LDX #Oper
      LDX(A_IMM);
      CLK(2);
      break;

    case 0xA4: // LDY Zpg
      LDY(A_ZP);
      CLK(3);
      break;

    case 0xA5: // LDA Zpg
      LDA(A_ZP);
      CLK(3);
      break;

    case 0xA6: // LDX Zpg
      LDX(A_ZP);
      CLK(3);
      break;

    case 0xA8: // TAY
      Y = A;
      TEST(A);
      CLK(2);
      break;

    case 0xA9: // LDA #Oper
      LDA(A_IMM);
      CLK(2);
      break;

    case 0xAA: // TAX
      X = A;
      TEST(A);
      CLK(2);
      break;

    case 0xAC: // LDY Abs
      LDY(A_ABS);
      CLK(4);
      break;

    case 0xAD: // LDA Abs
      LDA(A_ABS);
      CLK(4);
      break;

    case 0xAE: // LDX Abs
      LDX(A_ABS);
      CLK(4);
      break;

    case 0xB0: // BCS
      BRA(F & FLAG_C);
      break;

    case 0xB1: // LDA (Zpg),Y
      LDA(A_IY);
      CLK(5);
      break;

    case 0xB4: // LDY Zpg,X
      LDY(A_ZPX);
      CLK(4);
      break;

    case 0xB5: // LDA Zpg,X
      LDA(A_ZPX);
      CLK(4);
      break;

    case 0xB6: // LDX Zpg,Y
      LDX(A_ZPY);
      CLK(4);
      break;

    case 0xB8: // CLV
      RSTF(FLAG_V);
      CLK(2);
      break;

    case 0xB9: // LDA Abs,Y
      LDA(A_ABSY);
      CLK(4);
      break;

    case 0xBA: // TSX
      X = SP;
      TEST(X);
      CLK(2);
      break;

    case 0xBC: // LDY Abs,X
      LDY(A_ABSX);
      CLK(4);
      break;

    case 0xBD: // LDA Abs,X
      LDA(A_ABSX);
      CLK(4);
      break;

    case 0xBE: // LDX Abs,Y
      LDX(A_ABSY);
      CLK(4);
      break;

    case 0xC0: // CPY #Oper
      CPY(A_IMM);
      CLK(2);
      break;

    case 0xC1: // CMP (Zpg,X)
      CMP(A_IX);
      CLK(6);
      break;

    case 0xC4: // CPY Zpg
      CPY(A_ZP);
      CLK(3);
      break;

    case 0xC5: // CMP Zpg
      CMP(A_ZP);
      CLK(3);
      break;

    case 0xC6: // DEC Zpg
      DEC(AA_ZP);
      CLK(5);
      break;

    case 0xC8: // INY
      ++Y;
      TEST(Y);
      CLK(2);
      break;

    case 0xC9: // CMP #Oper
      CMP(A_IMM);
      CLK(2);
      break;

    case 0xCA: // DEX
      --X;
      TEST(X);
      CLK(2);
      break;

    case 0xCC: // CPY Abs
      CPY(A_ABS);
      CLK(4);
      break;

    case 0xCD: // CMP Abs
      CMP(A_ABS);
      CLK(4);
      break;

    case 0xCE: // DEC Abs
      DEC(AA_ABS);
      CLK(6);
      break;

    case 0xD0: // BNE
      BRA(!(F & FLAG_Z));
      break;

    case 0xD1: // CMP (Zpg),Y
      CMP(A_IY);
      CLK(5);
      break;

    case 0xD5: // CMP Zpg,X
      CMP(A_ZPX);
      CLK(4);
      break;

    case 0xD6: // DEC Zpg,X
      DEC(AA_ZPX);
      CLK(6);
      break;

    case 0xD8: // CLD
      RSTF(FLAG_D);
      CLK(2);
      break;

    case 0xD9: // CMP Abs,Y
      CMP(A_ABSY);
      CLK(4);
      break;

    case 0xDD: // CMP Abs,X
      CMP(A_ABSX);
      CLK(4);
      break;

    case 0xDE: // DEC Abs,X
      DEC(AA_ABSX);
      CLK(7);
      break;

    case 0xE0: // CPX #Oper
      CPX(A_IMM);
      CLK(2);
      break;

    case 0xE1: // SBC (Zpg,X)
      SBC(A_IX);
      CLK(6);
      break;

    case 0xE4: // CPX Zpg
      CPX(A_ZP);
      CLK(3);
      break;

    case 0xE5: // SBC Zpg
      SBC(A_ZP);
      CLK(3);
      break;

    case 0xE6: // INC Zpg
      INC(AA_ZP);
      CLK(5);
      break;

    case 0xE8: // INX
      ++X;
      TEST(X);
      CLK(2);
      break;

    case 0xE9: // SBC #Oper
      SBC(A_IMM);
      CLK(2);
      break;

    case 0xEA: // NOP
      CLK(2);
      break;

    case 0xEC: // CPX Abs
      CPX(A_ABS);
      CLK(4);
      break;

    case 0xED: // SBC Abs
      SBC(A_ABS);
      CLK(4);
      break;

    case 0xEE: // INC Abs
      INC(AA_ABS);
      CLK(6);
      break;

    case 0xF0: // BEQ
      BRA(F & FLAG_Z);
      break;

    case 0xF1: // SBC (Zpg),Y
      SBC(A_IY);
      CLK(5);
      break;

    case 0xF5: // SBC Zpg,X
      SBC(A_ZPX);
      CLK(4);
      break;

    case 0xF6: // INC Zpg,X
      INC(AA_ZPX);
      CLK(6);
      break;

    case 0xF8: // SED
      SETF(FLAG_D);
      CLK(2);
      break;

    case 0xF9: // SBC Abs,Y
      SBC(A_ABSY);
      CLK(4);
      break;

    case 0xFD: // SBC Abs,X
      SBC(A_ABSX);
      CLK(4);
      break;

    case 0xFE: // INC Abs,X
      INC(AA_ABSX);
      CLK(7);
      break;

    default: // Unknown Instruction
      CLK(2);
#if 0
        InfoNES_MessageBox( "0x%02x is unknown instruction.\n", byCode ) ;
#endif
      break;

    } /* end of switch ( byCode ) */

  } /* end of while ... */

  // Correct the number of the clocks
  g_wCurrentClocks += (g_wPassedClocks - prePassedClocks);
  g_wPassedClocks -= wClocks;
}

/*===================================================================*/
/*                                                                   */
/*  K6502_Step() :                                                   */
/*          Only the specified number of the clocks execute Op.      */
/*                                                                   */
/*===================================================================*/
void __not_in_flash_func(K6502_Step)(int wClocks)
{
  if (NMI_State != NMI_Wiring)
  {
    // NMI前に少し実行したい
    step(7);
    wClocks -= 7;
  }
  procNMI();
  step(wClocks);
}

// Addressing Op.
// Data
// Absolute,X
static BYTE __not_in_flash_func(K6502_ReadAbsX)()
{
  WORD wA0, wA1;
  wA0 = AA_ABS;
  wA1 = wA0 + X;
  CLK((wA0 & 0x0100) != (wA1 & 0x0100));
  return K6502_Read(wA1);
};
// Absolute,Y
static BYTE __not_in_flash_func(K6502_ReadAbsY)()
{
  WORD wA0, wA1;
  wA0 = AA_ABS;
  wA1 = wA0 + Y;
  CLK((wA0 & 0x0100) != (wA1 & 0x0100));
  return K6502_Read(wA1);
};
// (Indirect),Y
static BYTE __not_in_flash_func(K6502_ReadIY)()
{
  WORD wA0, wA1;
  wA0 = K6502_ReadZpW(K6502_Read(PC++));
  wA1 = wA0 + Y;
  CLK((wA0 & 0x0100) != (wA1 & 0x0100));
  return K6502_Read(wA1);
};

/*===================================================================*/
/*                                                                   */
/*                  6502 Reading/Writing Operation                   */
/*                                                                   */
/*===================================================================*/
#include "K6502_rw.h"
