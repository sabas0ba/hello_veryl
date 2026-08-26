// Synthesis-only FontRom implementation for TopRv.
// The Veryl case implementation remains the simulation reference model.
module hello_veryl_FontRom (
    input  logic       i_clk,
    input  logic [6:0] i_code,
    input  logic [3:0] i_row,
    output logic [7:0] o_line
);
    logic [7:0] mem [0:2047];

    initial $readmemh("font/font8x16.hex", mem);

    always_ff @(posedge i_clk)
        o_line <= mem[{i_code, i_row}];
endmodule
