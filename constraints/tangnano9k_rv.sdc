# TopRv のタイミング制約 (nextpnr-himbaechel --sdc)
#
# --freq は全クロック一律のため，PsramSubsystem 内の rPLL 出力 clk_mem
# (54 MHz) が基準クロック (27 MHz) 想定で評価されてしまう．
# ここでは clk_mem のみを個別に制約する (docs/psram.md「クロック別タイミング制約」)．
#
# 注意: マッチ対象は合成後ネットリストのネット名．TopRv では clk_mem が
# PsramSubsystem の内部ネットなので階層名になる．
# マッチしない create_clock は警告なく無視されるため，
# 適用されたことは scripts/synth_pnr_rv.sh のログ検査で担保する．

create_clock -name clk_mem -period 18.518 [get_nets psram.clk_mem]
