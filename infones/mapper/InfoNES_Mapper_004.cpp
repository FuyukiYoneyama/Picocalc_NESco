/*===================================================================*/

/*                                                                   */
/*                       Mapper 4 (MMC3)                             */
/*                                                                   */
/*===================================================================*/

BYTE  Map4_Regs[ 8 ];
DWORD Map4_Rom_Bank;
DWORD Map4_Prg0, Map4_Prg1;
DWORD Map4_Chr01, Map4_Chr23;
DWORD Map4_Chr4, Map4_Chr5, Map4_Chr6, Map4_Chr7;

#define Map4_Chr_Swap()    ( Map4_Regs[ 0 ] & 0x80 )
#define Map4_Prg_Swap()    ( Map4_Regs[ 0 ] & 0x40 )

BYTE Map4_IRQ_Enable;
BYTE Map4_IRQ_Cnt;
BYTE Map4_IRQ_Latch;
BYTE Map4_IRQ_Present;
BYTE Map4_A12;
BYTE Map4_Wram_Enabled;
BYTE Map4_Wram_Write_Enabled;

#if defined(NESCO_MAPPER4_A12_SINGLE_SOURCE)
static int Map4_A12_Low_Clock = -1;
#endif

#if defined(NESCO_MAPPER4_TIMING_TRACE)
static unsigned Map4_Irq_Trace_Count;
static unsigned Map4_Write_Trace_Count;
#endif

static void Map4_ClockIrq()
{
  if ( Map4_IRQ_Cnt == 0 || Map4_IRQ_Present )
  {
    Map4_IRQ_Cnt = Map4_IRQ_Latch;
  }
  else
  {
    --Map4_IRQ_Cnt;
  }

  Map4_IRQ_Present = 0;

  /* MMC3B behavior: an IRQ is generated when the post-clock value is 0. */
  if ( Map4_IRQ_Cnt == 0 && Map4_IRQ_Enable )
  {
#if defined(NESCO_MAPPER4_TIMING_TRACE)
    if (Map4_Irq_Trace_Count < 16u)
    {
      ++Map4_Irq_Trace_Count;
      printf("[M4_IRQ_REQ] n=%u cpu=%d sl=%u fs=%u pc=%04X cnt=%u latch=%u en=%u present=%u\n",
             Map4_Irq_Trace_Count,
             getCurrentClocks32(),
             (unsigned)PPU_Scanline,
             (unsigned)FrameStep,
             (unsigned)PC,
             (unsigned)Map4_IRQ_Cnt,
             (unsigned)Map4_IRQ_Latch,
             (unsigned)Map4_IRQ_Enable,
             (unsigned)Map4_IRQ_Present);
      fflush(stdout);
    }
#endif
    IRQ_REQ;
  }
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 4                                              */
/*-------------------------------------------------------------------*/
void Map4_Init()
{
  /* Initialize Mapper */
  MapperInit = Map4_Init;

  /* Write to Mapper */
  MapperWrite = Map4_Write;

  /* Write to SRAM */
  MapperSram = Map0_Sram;

  /* Write to APU */
  MapperApu = Map0_Apu;

  /* Read from APU */
  MapperReadApu = Map0_ReadApu;

  /* Callback at VSync */
  MapperVSync = Map0_VSync;

  /* Callback at HSync */
  /* IRQs are clocked from the PPU A12 bus callback, not once per scanline. */
  MapperHSync = Map0_HSync;

  /* Callback at PPU */
  MapperPPU = Map4_PPU;

  /* Callback at Rendering Screen ( 1:BG, 0:Sprite ) */
  MapperRenderScreen = Map0_RenderScreen;

  /* Set SRAM Banks */
  SRAMBANK = SRAM;

  /* Initialize State Registers */
  for ( int nPage = 0; nPage < 8; nPage++ )
  {
    Map4_Regs[ nPage ] = 0x00;
  }

  /* Set ROM Banks */
  Map4_Prg0 = 0;
  Map4_Prg1 = 1;
  Map4_Set_CPU_Banks();

  /* Set PPU Banks */
  if ( NesHeader.byVRomSize > 0 )
  {
    Map4_Chr01 = 0;
    Map4_Chr23 = 2;
    Map4_Chr4  = 4;
    Map4_Chr5  = 5;
    Map4_Chr6  = 6;
    Map4_Chr7  = 7;
    Map4_Set_PPU_Banks();
  } else {
    Map4_Chr01 = Map4_Chr23 = 0;
    Map4_Chr4 = Map4_Chr5 = Map4_Chr6 = Map4_Chr7 = 0;
  }

  /* Initialize IRQ Registers */
  Map4_IRQ_Enable = 0;
  Map4_IRQ_Cnt = 0;
  Map4_IRQ_Latch = 0;
  Map4_IRQ_Present = 0;
  Map4_A12 = 0;
#if defined(NESCO_MAPPER4_A12_SINGLE_SOURCE)
  Map4_A12_Low_Clock = -1;
#endif
  Map4_Wram_Enabled = 0;
  Map4_Wram_Write_Enabled = 0;

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 ); 
}

/*-------------------------------------------------------------------*/
/*  Mapper 4 Write Function                                          */
/*-------------------------------------------------------------------*/
void Map4_Write( WORD wAddr, BYTE byData )
{
  DWORD dwBankNum;

#if defined(NESCO_MAPPER4_TIMING_TRACE)
  const WORD trace_reg = wAddr & 0xe001;
  if ((trace_reg == 0xc000 || trace_reg == 0xc001 ||
       trace_reg == 0xe000 || trace_reg == 0xe001) &&
      Map4_Write_Trace_Count < 32u)
  {
    ++Map4_Write_Trace_Count;
    printf("[M4_WRITE] n=%u cpu=%d sl=%u fs=%u pc=%04X addr=%04X data=%02X\n",
           Map4_Write_Trace_Count,
           getCurrentClocks32(),
           (unsigned)PPU_Scanline,
           (unsigned)FrameStep,
           (unsigned)PC,
           (unsigned)trace_reg,
           (unsigned)byData);
    fflush(stdout);
  }
#endif

  switch ( wAddr & 0xe001 )
  {
    case 0x8000:
      Map4_Regs[ 0 ] = byData;
      Map4_Set_PPU_Banks();
      Map4_Set_CPU_Banks();
      break;

    case 0x8001:
      Map4_Regs[ 1 ] = byData;
      dwBankNum = Map4_Regs[ 1 ];

      switch ( Map4_Regs[ 0 ] & 0x07 )
      {
        /* Set PPU Banks */
        case 0x00:
          dwBankNum &= 0xfe;
          Map4_Chr01 = dwBankNum;
          Map4_Set_PPU_Banks();
          break;

        case 0x01:
          dwBankNum &= 0xfe;
          Map4_Chr23 = dwBankNum;
          Map4_Set_PPU_Banks();
          break;

        case 0x02:
          Map4_Chr4 = dwBankNum;
          Map4_Set_PPU_Banks();
          break;

        case 0x03:
          Map4_Chr5 = dwBankNum;
          Map4_Set_PPU_Banks();
          break;

        case 0x04:
          Map4_Chr6 = dwBankNum;
          Map4_Set_PPU_Banks();
          break;

        case 0x05:
          Map4_Chr7 = dwBankNum;
          Map4_Set_PPU_Banks();
          break;

        /* Set ROM Banks */
        case 0x06:
          Map4_Prg0 = dwBankNum;
          Map4_Set_CPU_Banks();
          break;

        case 0x07:
          Map4_Prg1 = dwBankNum;
          Map4_Set_CPU_Banks();
          break;
      }
      break;

    case 0xa000:
      Map4_Regs[ 2 ] = byData;

      if ( !ROM_FourScr )
      {
        if ( byData & 0x01 )
        {
          InfoNES_Mirroring( 0 );
        } else {
          InfoNES_Mirroring( 1 );
        }
      }
      break;

    case 0xa001:
      Map4_Regs[ 3 ] = byData;
      Map4_Wram_Enabled = (byData & 0x80) ? 1 : 0;
      Map4_Wram_Write_Enabled = ((byData & 0xc0) == 0x80) ? 1 : 0;
      break;

    case 0xc000:
      Map4_Regs[ 4 ] = byData;
      Map4_IRQ_Latch = byData;
      break;

    case 0xc001:
      Map4_Regs[ 5 ] = byData;
      Map4_IRQ_Cnt = 0;
      Map4_IRQ_Present = 0xff;
      break;

    case 0xe000:
      Map4_Regs[ 6 ] = byData;
      Map4_IRQ_Enable = 0;
      IRQ_State = 1;
      break;

    case 0xe001:
      Map4_Regs[ 7 ] = byData;
      Map4_IRQ_Enable = 1;
      break;
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 4 PPU A12 callback                                        */
/*-------------------------------------------------------------------*/
void Map4_PPU( WORD wAddr )
{
  const BYTE a12 = (wAddr & 0x1000) ? 1 : 0;

#if defined(NESCO_MAPPER4_A12_SINGLE_SOURCE)
  /*
   * Mesen2's MMC3 watcher counts a rising edge only when A12 stayed low
   * for at least three master-clock ticks.  Keep the low timestamp until a
   * high access consumes it.
   */
  if (!a12)
  {
    if (Map4_A12_Low_Clock < 0)
      Map4_A12_Low_Clock = getCurrentClocks32();
    Map4_A12 = 0;
    return;
  }

  const int now = getCurrentClocks32();
  const bool valid_rising_edge =
      !Map4_A12 &&
      Map4_A12_Low_Clock >= 0 &&
      now - Map4_A12_Low_Clock >= 3;
  if (valid_rising_edge)
  {
#else
  if ( !Map4_A12 && a12 )
  {
#endif
#if defined(NESCO_MAPPER4_TIMING_TRACE)
    static unsigned map4_a12_trace_count;
    if (map4_a12_trace_count < 32u)
    {
      ++map4_a12_trace_count;
      printf("[M4_A12] n=%u cpu=%d sl=%u fs=%u addr=%04X pc=%04X\n",
             map4_a12_trace_count,
             getCurrentClocks32(),
             (unsigned)PPU_Scanline,
             (unsigned)FrameStep,
             (unsigned)wAddr,
             (unsigned)PC);
      fflush(stdout);
    }
#endif
    Map4_ClockIrq();
  }

  Map4_A12 = a12;
#if defined(NESCO_MAPPER4_A12_SINGLE_SOURCE)
  Map4_A12_Low_Clock = -1;
#endif
}

/*-------------------------------------------------------------------*/
/*  Mapper 4 Set CPU Banks Function                                  */
/*-------------------------------------------------------------------*/
void Map4_Set_CPU_Banks()
{
  if ( Map4_Prg_Swap() )
  {
    ROMBANK0 = ROMLASTPAGE( 1 );
    ROMBANK1 = ROMPAGE( Map4_Prg1 % ( NesHeader.byRomSize << 1 ) );
    ROMBANK2 = ROMPAGE( Map4_Prg0 % ( NesHeader.byRomSize << 1 ) );
    ROMBANK3 = ROMLASTPAGE( 0 );
  } else {
    ROMBANK0 = ROMPAGE( Map4_Prg0 % ( NesHeader.byRomSize << 1 ) );
    ROMBANK1 = ROMPAGE( Map4_Prg1 % ( NesHeader.byRomSize << 1 ) );
    ROMBANK2 = ROMLASTPAGE( 1 );
    ROMBANK3 = ROMLASTPAGE( 0 );
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 4 Set PPU Banks Function                                  */
/*-------------------------------------------------------------------*/
void Map4_Set_PPU_Banks()
{
  if ( NesHeader.byVRomSize > 0 )
  {
    if ( Map4_Chr_Swap() )
    { 
      PPUBANK[ 0 ] = VROMPAGE( Map4_Chr4 % ( NesHeader.byVRomSize << 3 ) );
      PPUBANK[ 1 ] = VROMPAGE( Map4_Chr5 % ( NesHeader.byVRomSize << 3 ) );
      PPUBANK[ 2 ] = VROMPAGE( Map4_Chr6 % ( NesHeader.byVRomSize << 3 ) );
      PPUBANK[ 3 ] = VROMPAGE( Map4_Chr7 % ( NesHeader.byVRomSize << 3 ) );
      PPUBANK[ 4 ] = VROMPAGE( ( Map4_Chr01 + 0 ) % ( NesHeader.byVRomSize << 3 ) );
      PPUBANK[ 5 ] = VROMPAGE( ( Map4_Chr01 + 1 ) % ( NesHeader.byVRomSize << 3 ) );
      PPUBANK[ 6 ] = VROMPAGE( ( Map4_Chr23 + 0 ) % ( NesHeader.byVRomSize << 3 ) );
      PPUBANK[ 7 ] = VROMPAGE( ( Map4_Chr23 + 1 ) % ( NesHeader.byVRomSize << 3 ) );
      InfoNES_SetupChr();
    } else {
      PPUBANK[ 0 ] = VROMPAGE( ( Map4_Chr01 + 0 ) % ( NesHeader.byVRomSize << 3 ) );
      PPUBANK[ 1 ] = VROMPAGE( ( Map4_Chr01 + 1 ) % ( NesHeader.byVRomSize << 3 ) );
      PPUBANK[ 2 ] = VROMPAGE( ( Map4_Chr23 + 0 ) % ( NesHeader.byVRomSize << 3 ) );
      PPUBANK[ 3 ] = VROMPAGE( ( Map4_Chr23 + 1 ) % ( NesHeader.byVRomSize << 3 ) );
      PPUBANK[ 4 ] = VROMPAGE( Map4_Chr4 % ( NesHeader.byVRomSize << 3 ) );
      PPUBANK[ 5 ] = VROMPAGE( Map4_Chr5 % ( NesHeader.byVRomSize << 3 ) );
      PPUBANK[ 6 ] = VROMPAGE( Map4_Chr6 % ( NesHeader.byVRomSize << 3 ) );
      PPUBANK[ 7 ] = VROMPAGE( Map4_Chr7 % ( NesHeader.byVRomSize << 3 ) );
      InfoNES_SetupChr();
    }
  }
  else
  {
    if ( Map4_Chr_Swap() )
    { 
      PPUBANK[ 0 ] = CRAMPAGE( Map4_Chr4 % 8 );
      PPUBANK[ 1 ] = CRAMPAGE( Map4_Chr5 % 8 );
      PPUBANK[ 2 ] = CRAMPAGE( Map4_Chr6 % 8 );
      PPUBANK[ 3 ] = CRAMPAGE( Map4_Chr7 % 8 );
      PPUBANK[ 4 ] = CRAMPAGE( ( Map4_Chr01 + 0 ) % 8 );
      PPUBANK[ 5 ] = CRAMPAGE( ( Map4_Chr01 + 1 ) % 8 );
      PPUBANK[ 6 ] = CRAMPAGE( ( Map4_Chr23 + 0 ) % 8 );
      PPUBANK[ 7 ] = CRAMPAGE( ( Map4_Chr23 + 1 ) % 8 );
      InfoNES_SetupChr();
    } else {
      PPUBANK[ 0 ] = CRAMPAGE( ( Map4_Chr01 + 0 ) % 8 );
      PPUBANK[ 1 ] = CRAMPAGE( ( Map4_Chr01 + 1 ) % 8 );
      PPUBANK[ 2 ] = CRAMPAGE( ( Map4_Chr23 + 0 ) % 8 );
      PPUBANK[ 3 ] = CRAMPAGE( ( Map4_Chr23 + 1 ) % 8 );
      PPUBANK[ 4 ] = CRAMPAGE( Map4_Chr4 % 8 );
      PPUBANK[ 5 ] = CRAMPAGE( Map4_Chr5 % 8 );
      PPUBANK[ 6 ] = CRAMPAGE( Map4_Chr6 % 8 );
      PPUBANK[ 7 ] = CRAMPAGE( Map4_Chr7 % 8 );
      InfoNES_SetupChr();
    }
  }
}
