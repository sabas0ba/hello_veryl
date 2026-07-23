// Gowin primitive blackbox stubs for synthesis elaboration.
// yosys-slang requires definitions for every instantiated module; the real
// implementations are resolved downstream by nextpnr-himbaechel / gowin_pack.
// Port definitions per Gowin UG289 (docs/datasheets/UG289E.pdf).
// This file is listed explicitly in scripts/synth.ys (it is not part of the
// Veryl filelist) and must not contain synthesizable logic.

(* blackbox *)
module IOBUF (
    output O  , // pad -> fabric
    inout  IO , // pad
    input  I  , // fabric -> pad
    input  OEN  // 1: output disabled (High-Z)
);
endmodule

// rPLL: ports/parameters per UG286 Table 5-2 / 5-3
// fCLKOUT = fCLKIN * FBDIV / IDIV (divider = *_SEL + 1), fVCO = fCLKOUT * ODIV
(* blackbox *)
module rPLL #(
    parameter FCLKIN          = "100.0"   ,
    parameter DEVICE          = "GW1N-9C" ,
    parameter DYN_IDIV_SEL    = "false"   ,
    parameter IDIV_SEL        = 0         ,
    parameter DYN_FBDIV_SEL   = "false"   ,
    parameter FBDIV_SEL       = 0         ,
    parameter DYN_ODIV_SEL    = "false"   ,
    parameter ODIV_SEL        = 8         ,
    parameter PSDA_SEL        = "0000"    ,
    parameter DYN_DA_EN       = "false"   ,
    parameter DUTYDA_SEL      = "1000"    ,
    parameter CLKOUT_FT_DIR   = 1'b1      ,
    parameter CLKOUTP_FT_DIR  = 1'b1      ,
    parameter CLKOUT_DLY_STEP = 0         ,
    parameter CLKOUTP_DLY_STEP= 0         ,
    parameter CLKFB_SEL       = "internal",
    parameter CLKOUT_BYPASS   = "false"   ,
    parameter CLKOUTP_BYPASS  = "false"   ,
    parameter CLKOUTD_BYPASS  = "false"   ,
    parameter DYN_SDIV_SEL    = 2         ,
    parameter CLKOUTD_SRC     = "CLKOUT"  ,
    parameter CLKOUTD3_SRC    = "CLKOUT"
) (
    input        CLKIN   ,
    input        CLKFB   ,
    input        RESET   , // active-high
    input        RESET_P , // active-high (power down)
    input  [5:0] FBDSEL  ,
    input  [5:0] IDSEL   ,
    input  [5:0] ODSEL   ,
    input  [3:0] PSDA    ,
    input  [3:0] DUTYDA  ,
    input  [3:0] FDLY    ,
    output       CLKOUT  ,
    output       LOCK    ,
    output       CLKOUTP ,
    output       CLKOUTD ,
    output       CLKOUTD3
);
endmodule
