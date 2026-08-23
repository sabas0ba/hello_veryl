# Tang Nano 9K タイミング制約 (nextpnr-himbaechel --sdc)
#
# nextpnr の --freq は全クロック一律の目標周波数を与えるため，rPLL 出力の
# clk_mem (54 MHz) が基準クロック (27 MHz) 想定で評価されてしまう．
# ここでは clk_mem のみを個別に制約し，基準クロック側は --freq 27 に委ねる．
#
# 注意: マッチ対象は「合成後ネットリストのネット名」である．トップの入力ポート
# i_clk のクロックネットは yosys がシンク側の名前 (blink_alive.i_clk 等) へ
# 改名するため [get_ports i_clk] / [get_nets i_clk] はマッチしない．
# マッチしない create_clock は警告なく無視されるため，制約が実際に効いている
# ことは scripts/synth_pnr.sh のログ検査で担保する．

# clk_mem: rPLL 27 MHz x2 = 54 MHz (period 18.518 ns)
create_clock -name clk_mem -period 18.518 [get_nets clk_mem]
