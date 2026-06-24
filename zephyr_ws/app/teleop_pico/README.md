# teleop_pico: Pmod GYRO で turtlesim teleop するための準備

Pmod GYRO を Arty A7 の Pmod コネクタにつなぎ，最終的には傾きや回転に応じて turtlesim の `/turtle1/cmd_vel` を publish する．まずは以下の段階で進める．

1. 制御仕様を決める
2. Pmod GYRO のチップと SPI 仕様を確認する
3. LiteX FPGA 側に Pmod 用 SPI master を追加する
4. Zephyr 側の devicetree/Kconfig を整える
5. Pmod GYRO 単体読み出しアプリ `teleop_pico` を動かす

## 1. 制御仕様の初期案

Pmod GYRO は加速度センサではなく角速度センサなので，静的な「傾き角」を安定して得るには積分とドリフト補正が必要になる．最初のデモでは角速度をそのまま turtlesim の速度指令へ割り当てるのがよさそう．

- `gyro_z` を `angular.z` に割り当てる: ボードを水平面内でひねると turtle が旋回する
- `gyro_y` を `linear.x` に割り当てる: ボードを前後方向に回すと turtle が前進/後退する
- 起動後に数秒静止させてゼロバイアスを測る
- deadzone と最大速度 clamp を入れて，手を離したときに turtle が暴れないようにする

この節ではまだ turtlesim へ publish せず，まず Pmod GYRO の raw 値が読めるところまで進める．

## 2. Pmod GYRO の SPI 仕様確認

Digilent Pmod GYRO は ST L3G4200D を使っている想定で進める．接続・実装前に Digilent のリファレンスと L3G4200D のデータシートで以下を確認する．

- 電源は 3.3 V
- SPI 信号は `CS`, `MOSI`, `MISO`, `SCLK`
- `WHO_AM_I` register は `0x0f`
- `WHO_AM_I` の期待値は `0xd3`
- X/Y/Z の出力レジスタは `OUT_X_L = 0x28` から 6 byte
- 250 dps full-scale 時の感度はおおむね `8.75 mdps/LSB`

注意点として，Zephyr の `litex,spi` ドライバと LiteX の `SPIMaster` は現状 `CPOL=0, CPHA=0` のみ対応している．Pmod GYRO 側が SPI mode 3 必須だった場合，`teleop_pico` の `WHO_AM_I` が `0xd3` にならない可能性がある．その場合は LiteX/Zephyr の SPI mode 対応を追加するか，mode 0 で通信できる別の SPI 実装を用意する必要がある．

## 3. LiteX に Pmod 用 SPI master を追加

Arty A7 の Pmod D に Pmod GYRO を接続する想定で進める．Pmod の SPI 4 信号は以下の割り当てにする．

| Pmod GYRO | Pmod D | LiteX pads |
| --- | --- | --- |
| `CS` | pin 1 | `pmodd:0` |
| `MOSI` | pin 2 | `pmodd:1` |
| `MISO` | pin 3 | `pmodd:2` |
| `SCLK` | pin 4 | `pmodd:3` |

まず LiteX の Arty target に Pmod GYRO 用オプションを追加する．直接手で編集する代わりに，[digilent_arty_pmod_gyro.patch](digilent_arty_pmod_gyro.patch) を当てる．この patch は `--pmod-gyro pmoda|pmodb|pmodc|pmodd` を追加し，Pmod の位置を引数で選べるようにする．

```bash
### litex_venv
cd ${LITEX_WS_ROOT}/litex_setup/litex-boards

git apply ${ZEPHYR_WS_ROOT}/app/teleop_pico/digilent_arty_pmod_gyro.patch
```

この patch により `digilent_arty.py` に `--pmod-gyro` が追加され，指定した Pmod の `:0..3` に LiteX 側の `spimaster` が割り当てられる．Zephyr の overlay ではこれが `spi0` として有効化される．Pmod D を使うなら `--pmod-gyro pmodd` を指定する．すでに patch が当たっているか確認するには以下を見る．

```bash
### litex_venv
$ grep -n 'pmod-gyro\|pmod_gyro_spi\|spimaster' \
  ${LITEX_WS_ROOT}/litex_setup/litex-boards/litex_boards/targets/digilent_arty.py
191:                ("pmod_gyro_spi", 0,
201:                pads=platform.request("pmod_gyro_spi"),
202:                name="spimaster",
238:    parser.add_target_argument("--pmod-gyro", choices=["pmoda", "pmodb", "pmodc", "pmodd"], help="Enable Pmod GYRO SPI on selected PMOD.")
```

patch を戻したい場合は同じ場所で reverse apply する．

```bash
### litex_venv
cd ${LITEX_WS_ROOT}/litex_setup/litex-boards

git apply -R ${ZEPHYR_WS_ROOT}/app/teleop_pico/digilent_arty_pmod_gyro.patch
```

その後，SoC イメージを再生成する．

```bash
### litex_venv
source /tools/Xilinx/2025.1/Vivado/settings64.sh

cd ${LITEX_WS_ROOT}/fpga_image/arty_a7_100

python3 -m litex_boards.targets.digilent_arty \
  --toolchain vivado \
  --variant a7-100 \
  --cpu-type vexriscv \
  --timer-uptime \
  --with-ethernet \
  --pmod-gyro pmodd \
  --csr-json build/csr.json \
  --output-dir build \
  --build
```

生成物に `spimaster` が含まれていることを確認する．

```bash
### litex_venv
$ grep -E 'csr_base,spimaster|csr_register,spimaster|config_spimaster' \
  ${LITEX_WS_ROOT}/fpga_image/arty_a7_100/build/csr.csv
csr_base,spimaster,0xf0003800,,
csr_register,spimaster_control,0xf0003800,1,rw
csr_register,spimaster_status,0xf0003804,1,ro
csr_register,spimaster_mosi,0xf0003808,1,rw
csr_register,spimaster_miso,0xf000380c,1,ro
csr_register,spimaster_cs,0xf0003810,1,rw
csr_register,spi0_loopback,0xf0003814,1,rw
csr_register,spi0_clk_divider,0xf0003818,1,rw
```

FPGA に書き込む．

```bash
### litex_venv
cd ${LITEX_WS_ROOT}/fpga_image/arty_a7_100

python3 -m litex_boards.targets.digilent_arty \
  --variant a7-100 \
  --output-dir build \
  --load
```

## 4. Zephyr overlay を生成して Pmod GYRO node を重ねる

LiteX の `csr.json` から Zephyr overlay を生成し直す．

```bash
### litex_venv
cd ${LITEX_WS_ROOT}/fpga_image/arty_a7_100/

python3 ${LITEX_WS_ROOT}/litex_setup/litex/litex/tools/litex_json2dts_zephyr.py \
  --dts build/overlay.dts \
  --config build/overlay.config \
  build/csr.json
```

生成された overlay では，`spi0` の `reg` / `reg-names` が LiteX 側の値に上書きされていることを確認する．

```bash
### litex_venv
$ grep -A14 '&spi0' ${LITEX_WS_ROOT}/fpga_image/arty_a7_100/build/overlay.dts
&spi0 {
    reg = <0xf0003800 0x4>,
        <0xf0003804 0x4>,
        <0xf0003808 0x4>,
        <0xf000380c 0x4>,
        <0xf0003810 0x4>,
        <0xf0003814 0x4>,
        <0xf0003818 0x4>;
    reg-names = "control",
        "status",
        "mosi",
```

Pmod GYRO の child node は [overlay-pmodgyro.dts](overlay-pmodgyro.dts) に用意してある．ビルド時には LiteX 生成 overlay とこの overlay を両方指定する．

## 5. `teleop_pico` のビルドと実行

Pmod GYRO 単体読み出しアプリはこのディレクトリに置いている．この段階ではネットワークや ROS 2 は使わず，SPI で `WHO_AM_I` と X/Y/Z の raw 値を読むだけにする．

```bash
### zephyr_venv
cd ${ZEPHYR_WS_ROOT}

west build -p always \
  -b litex_vexriscv \
  app/teleop_pico \
  -d ${ZEPHYR_WS_ROOT}/build/teleop_pico \
  -- \
  -DDTC_OVERLAY_FILE="${LITEX_WS_ROOT}/fpga_image/arty_a7_100/build/overlay.dts;${ZEPHYR_WS_ROOT}/app/teleop_pico/overlay-pmodgyro.dts"
```

ビルド後の最終合成結果で，`spi0` が `okay` かつ `gyro@0` が `spi0` の子になっていることを確認する．

```bash
### zephyr_venv
$ awk '/spi0: spi@e0002000 \{/,/^[[:space:]]*\};/ { if ($0 ~ /status = "okay"/) print }' \
  ${ZEPHYR_WS_ROOT}/build/teleop_pico/zephyr/zephyr.dts
            status = "okay";
$ awk '/spi0: spi@e0002000 \{/,/^[[:space:]]*\};/ { if ($0 ~ /gyro@0/) print }' \
  ${ZEPHYR_WS_ROOT}/build/teleop_pico/zephyr/zephyr.dts
            pmod_gyro: gyro@0 {
```

LiteX の venv で serial boot する．

```bash
### litex_venv
litex_term /dev/ttyUSB1 \
  --speed 115200 \
  --kernel ${ZEPHYR_WS_ROOT}/build/teleop_pico/zephyr/zephyr.bin
```

起動後，以下のようなログになれば SPI 通信はまず成功．

```text
*** Booting Zephyr OS build v4.4.0 ***
[00:00:00.000,000] <inf> teleop_pico: WHO_AM_I=0xd3
[00:00:00.000,000] <inf> teleop_pico: Pmod GYRO sampling started
[00:00:00.100,000] <inf> teleop_pico: raw x=... y=... z=..., mdps x=... y=... z=...
```

`WHO_AM_I` が `0xd3` にならない場合は，以下を順に確認する．

- Pmod GYRO の向きと，`--pmod-gyro` で選んだ Pmod への接続
- `CS/MOSI/MISO/SCLK` のピン割り当て
- `csr.csv` に LiteX 名 `spimaster` が生成されていること
- LiteX 生成 `overlay.dts` で `&spi0` の `reg` が `0xf000...` 側に上書きされていること
- 最終 `${ZEPHYR_WS_ROOT}/build/teleop_pico/zephyr/zephyr.dts` で `spi0` が `status = "okay"` になっていること
- SPI mode の不一致: 現状の `litex,spi` は `CPOL=0, CPHA=0` のみ対応
- `WHO_AM_I=0xff` や `raw=-1` が続く場合: MISO が pull-up 相当で全ビット1を読んでいる可能性が高いので，Pmod D の向き，CS 配線，MISO/MOSI の入れ違いを最優先で確認する

ここまで通れば，次の段階で `teleop_pico` に bias 推定，deadzone，`geometry_msgs/msg/Twist` publish を入れて turtlesim とつなぐ．
