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
