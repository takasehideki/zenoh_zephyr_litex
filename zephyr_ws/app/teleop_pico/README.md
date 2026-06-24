# teleop_pico: Pmod ACL2 で turtlesim teleop

[Pmod ACL2 (ADXL362)](https://digilent.com/reference/pmod/pmodacl2/start) を Arty A7 の Pmod コネクタにつなぎ，ボードの傾きに応じて turtlesim の `/turtle1/cmd_vel` を publish する手順．

## 概要

この `teleop_pico` は以下を実装している．

1. ADXL362 の加速度を Zephyr sensor API で取得
2. 起動直後に静止状態でバイアス推定
3. deadzone と速度クランプを適用して Twist に変換
4. rmw_zenoh 互換 keyexpr で `geometry_msgs/msg/Twist` を publish

## 1. LiteX 側: SPI master を有効化

Arty target へ Pmod ACL2 用 SPI master 追加の patch を適用する．

```bash
### litex_venv
cd ${LITEX_WS_ROOT}/litex_setup/litex-boards

git apply ${ZEPHYR_WS_ROOT}/app/teleop_pico/digilent_arty_pmod_acl2.patch
```

必要なら reverse apply で patch をもとに戻せる．

```bash
### litex_venv
cd ${LITEX_WS_ROOT}/litex_setup/litex-boards

git apply -R ${ZEPHYR_WS_ROOT}/app/teleop_pico/digilent_arty_pmod_acl2.patch
```

patch が有効かどうかは `digilent_arty.py` を grep して確認する．

```bash
### litex_venv
$ grep -n 'pmod-acl2\|pmod_acl2_spi\|spimaster' \
  ${LITEX_WS_ROOT}/litex_setup/litex-boards/litex_boards/targets/digilent_arty.py
193:                ("pmod_acl2_spi", 0,
202:                name="spimaster",
203:                pads=platform.request("pmod_acl2_spi"),
240:    parser.add_target_argument("--pmod-acl2", choices=["pmoda", "pmodb", "pmodc", "pmodd"], help="Enable Pmod ACL2 SPI on selected PMOD.")
```

Pmod D を使う場合のビルド例:

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
  --pmod-acl2 pmodd \
  --csr-json build/csr.json \
  --output-dir build \
  --build
```

`csr.csv` に `spimaster` が出ることを確認する．

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
csr_register,spimaster_loopback,0xf0003814,1,rw
csr_register,spimaster_clk_divider,0xf0003818,1,rw
```

FPGA に書き込む．

```bash
### litex_venv
python3 -m litex_boards.targets.digilent_arty \
  --variant a7-100 \
  --output-dir build \
  --load
```

問題なければ configuration flash に bitstream を書き込んで不揮発化してもよい．

```bash
### litex_venv
openFPGALoader -b arty_a7_100t -f \
  ${LITEX_WS_ROOT}/fpga_image/arty_a7_100/build/gateware/digilent_arty.bit
```

## 2. Zephyr overlay の生成と ACL2 ノード追加

LiteX の `csr.json` から Zephyr overlay を生成し直す．

```bash
### litex_venv
cd ${LITEX_WS_ROOT}/fpga_image/arty_a7_100/

python3 ${LITEX_WS_ROOT}/litex_setup/litex/litex/tools/litex_json2dts_zephyr.py \
  --dts build/overlay.dts \
  --config build/overlay.config \
  build/csr.json
```

`overlay.dts` で `&spi0` の `reg` / `reg-names` が LiteX の `0xf000...` 側に更新されていることを確認する．

```bash
### litex_venv
$ grep -A10 '&spi0' ${LITEX_WS_ROOT}/fpga_image/arty_a7_100/build/overlay.dts
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

ACL2 ノードの定義は [overlay-pmodacl2.dts](overlay-pmodacl2.dts) に用意した．

## 3. teleop_pico のビルド

```bash
### zephyr_venv
cd ${ZEPHYR_WS_ROOT}

export ZENOH_LOCATOR="tcp/192.168.11.105:7447"

west build -p always \
  -b litex_vexriscv \
  app/teleop_pico \
  -d ${ZEPHYR_WS_ROOT}/build/teleop_pico \
  -- \
  -DDTC_OVERLAY_FILE="${LITEX_WS_ROOT}/fpga_image/arty_a7_100/build/overlay.dts;${ZEPHYR_WS_ROOT}/app/teleop_pico/overlay-pmodacl2.dts"
```

ビルドされた `zephyr.dts` で `spi0` が有効で，`adxl362@0` が `spi0` の子になっていることを確認する．

```bash
### zephyr_venv
$ awk '/spi0: spi@e0002000 \{/,/^[[:space:]]*\};/ { if ($0 ~ /status = "okay"/) print }' \
  ${ZEPHYR_WS_ROOT}/build/teleop_pico/zephyr/zephyr.dts
			status = "okay";               /* in app/teleop_pico/overlay-pmodacl2.dts:2 */
				status = "okay";                  /* in app/teleop_pico/overlay-pmodacl2.dts:8 */
$ awk '/spi0: spi@e0002000 \{/,/^[[:space:]]*\};/ { if ($0 ~ /adxl362@0|pmod_acl2/) print }' \
  ${ZEPHYR_WS_ROOT}/build/teleop_pico/zephyr/zephyr.dts
			/* node '/soc/spi@e0002000/adxl362@0' defined in app/teleop_pico/overlay-pmodacl2.dts:4 */
			pmod_acl2: adxl362@0 {
```

## 4. 実行

別ターミナルで router と turtlesim を起動する．

```bash
### ros2_env
ros2 run rmw_zenoh_cpp rmw_zenohd
```

```bash
### ros2_env
ros2 run turtlesim turtlesim_node
```

ボードを起動する．

```bash
### litex_venv
litex_term /dev/ttyUSB1 \
  --speed 115200 \
  --kernel ${ZEPHYR_WS_ROOT}/build/teleop_pico/zephyr/zephyr.bin
```

ログ例:

```text
*** Booting Zephyr OS build v4.4.0 ***
[00:00:00.000,000] <inf> teleop_pico: Estimating accelerometer bias (50 samples)... keep the board still
[00:00:05.000,000] <inf> teleop_pico: Bias m/s^2 x=... y=... z=...
[00:00:05.000,000] <inf> teleop_pico: Teleop started: publishing Twist from ACL2
[00:00:05.100,000] <inf> teleop_pico: accel x=... y=... z=... delta x=... y=... -> Twist linear.x=... angular.z=...
```

## 5. トラブルシュート

- `Pmod ACL2 device is not ready`
  - `overlay-pmodacl2.dts` がビルドに含まれているか確認
- `Accelerometer read failed`
  - ACL2 配線（CS/MOSI/MISO/SCLK）と Pmod 向き確認
  - `--pmod-acl2` で指定した PMOD と実配線が一致しているか確認
- Twist が反転する/強すぎる
  - `src/main.c` の `LINEAR_GAIN_PER_MS2` / `ANGULAR_GAIN_PER_MS2` と軸符号を調整
