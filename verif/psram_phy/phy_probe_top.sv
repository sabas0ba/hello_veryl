// PsramPhy 単体実証用トップ (docs/psram.md「物理層」)
// PSRAM マジックパッドへ ODDR/IDDR/IOBUF が配置できること (同一パッドの
// ODDR+IDDR 同居を含む) と，phy 内部の接続を PnR 後ネットリストで検査する
// ための入力を生成する．本体ビルドには含めない (run.sh から読む)
module top (
    input  wire        i_clk,
    output wire [ 1:0] O_psram_ck,
    output wire [ 1:0] O_psram_ck_n,
    output wire [ 1:0] O_psram_cs_n,
    output wire [ 1:0] O_psram_reset_n,
    inout  wire [15:0] IO_psram_dq,
    inout  wire [ 1:0] IO_psram_rwds,
    output wire        led
);
    wire clk_mem;
    wire clk_mem_p;
    wire lock;

    hello_veryl_PsramClkGen u_clkgen (
        .i_clk      (i_clk),
        .o_clk_mem  (clk_mem),
        .o_clk_mem_p(clk_mem_p),
        .o_lock     (lock)
    );

    // 適当な駆動でプルーニングを防ぐ
    reg [7:0] cnt;
    always @(posedge clk_mem) cnt <= cnt + 8'd1;

    wire [7:0] dq_a_in;
    wire [7:0] dq_b_in;
    wire       rwds_a_in;
    wire       rwds_b_in;

    hello_veryl_PsramPhy u_phy (
        .i_clk        (clk_mem),
        .i_clk_p      (clk_mem_p),
        .i_rst        (1'b1),          // async_low: 非アサート
        .i_ck_en      (cnt[0]),
        .i_cs_n       (cnt[1]),
        .i_reset_n    (cnt[2]),
        .i_dq_a       (cnt),
        .i_dq_b       (~cnt),
        .i_dq_oe      (cnt[3]),
        .i_rwds_a     (cnt[4]),
        .i_rwds_b     (cnt[5]),
        .i_rwds_oe    (cnt[6]),
        .o_dq_a       (dq_a_in),
        .o_dq_b       (dq_b_in),
        .o_rwds_a     (rwds_a_in),
        .o_rwds_b     (rwds_b_in),
        .o_pad_ck     (O_psram_ck[0]),
        .o_pad_ck_n   (O_psram_ck_n[0]),
        .o_pad_cs_n   (O_psram_cs_n[0]),
        .o_pad_reset_n(O_psram_reset_n[0]),
        .io_pad_dq    (IO_psram_dq[7:0]),
        .io_pad_rwds  (IO_psram_rwds[0])
    );

    // ch1 は非活性固定，DQ/RWDS は未使用 (High-Z)
    assign O_psram_ck[1]      = 1'b0;
    assign O_psram_ck_n[1]    = 1'b1;
    assign O_psram_cs_n[1]    = 1'b1;
    assign O_psram_reset_n[1] = 1'b1;

    // 入力経路の保持
    assign led = lock ^ (^dq_a_in) ^ (^dq_b_in) ^ rwds_a_in ^ rwds_b_in;
endmodule
