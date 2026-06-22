# zenoh_zephyr_litex

Zenoh on Zephyr on LiteX を実現するトライアルのためのリポジトリ

サードパーティ的にダウンロード・インストールするものや venv 環境は gitignore しているため，再現には種々のコマンドの実行が必要になる．なるべく再現手順を記しているつもり．

## 環境

次の環境とツール・バージョンでの作業を想定している．

- host
  - Ubuntu 24.04.4 LTS
  - Linux Ubuntu24 6.17.0-35-generic
- target
  - [Digilent Arty A7-100T (Artix-7 XC7A100T/XC7A35T)](https://digilent.com/reference/programmable-logic/arty-a7/start)
- tools
  - Vivado Design Tools 2025.1 (Ubuntu 24 に合わせるため)
  - Python 3.12.3 (default on Ubuntu 24)
  - Zephyr v4.4.0
    - West v1.5.0
    - Zephyr SDK v1.0.1
  - Zenoh (zenoh-pico, zenohd) 1.8.0

ターミナルには次の環境変数が設定されている想定とする．

```bash
cd <this_repo_root>   # typically, `zenoh_zephyr_litex`
export REPO_ROOT=${PWD}
export LITEX_WS_ROOT=${REPO_ROOT}/litex_ws
export ZEPHYR_WS_ROOT=${REPO_ROOT}/zephyr_ws
```

さらに，LiteX, Zephyr, Zenoh向けのvenv環境が設定されたターミナルでは冒頭に `### litex_venv` `### zephyr_venv` `### zenoh_venv` とそれぞれ記載する．

[Vivado Design Tools 2025.1](https://www.amd.com/en/support/downloads/adaptive-socs-and-fpgas/development-tools/2025-1.html) はインストール済みであるとする．

<details>

<summary>Tips: Vivado インストールの詳細</summary>

下記からインストーラをダウンロード
> AMD Unified Installer for FPGAs & Adaptive SoCs 2025.1: Linux Self Extracting Web Installer

https://www.amd.com/en/support/downloads/adaptive-socs-and-fpgas/development-tools/2025-1.html

実行権限を与えて，デフォルトインストール先の `/tools/Xilinx` を作成して，インストーラを実行

```bash
cd ~/Downloads
chmod +x FPGAs_AdaptiveSoCs_Unified_SDI_2025.1_0530_0145_Lin64.bin
sudo mkdir -p /tools/Xilinx
sudo chown -R "$USER:$USER" /tools/Xilinx
./FPGAs_AdaptiveSoCs_Unified_SDI_2025.1_0530_0145_Lin64.bin
```

次のように選択していく

- A Newer Version is Available のポップアップ: Continue
- Select Install Type: Download and Install Now
- Select Product to Install: Vivado
- Select Edition to Install: Vivado ML Standard
- Vivado ML Standard: Devices -> 7 Series -> Artix-7 FPGAs のみ選択
- Accept License Agreements: I Agree to all checkboxes
- Select Destination Directory: default
- Installation Summary: Install

完了後に出てくるポップアップには素直に従う

```bash
sudo /tools/Xilinx/2025.1/Vivado/scripts/installLibs.sh 
```

ご丁寧に（勝手に）ログを生成してくれるので削除

```bash
rm installLibs.sh_*
```

License Manager はとりあえず閉じておく（Standard なら無料で使えたはず,,,

パスを通して動作確認．いったん GUI が立ち上がったら満足しておく（また（勝手に）作成されるログも後片付け）

```bash
source /tools/Xilinx/2025.1/Vivado/settings64.sh 
vivado
rm vivado.*
```

</details>

## 1. LiteXでのカスタムSoCの生成

下記のコマンドを実行し，LiteX向けvenv環境の作成，LiteX環境の導入，SoCイメージのビルドを行っていく．

```bash
### litex_venv
python3 -m venv ${LITEX_WS_ROOT}/litex_venv
source ${LITEX_WS_ROOT}/litex_venv/bin/activate

mkdir -p ${LITEX_WS_ROOT}/litex_setup && cd ${LITEX_WS_ROOT}/litex_setup
wget https://raw.githubusercontent.com/enjoy-digital/litex/master/litex_setup.py
chmod +x litex_setup.py
python3 litex_setup.py --init --install

pip install meson
sudo apt install gcc-riscv64-unknown-elf

source /tools/Xilinx/2025.1/Vivado/settings64.sh

mkdir -p ${LITEX_WS_ROOT}/fpga_image/arty_a7_100
cd ${LITEX_WS_ROOT}/fpga_image/arty_a7_100
mkdir -p build

python3 -m litex_boards.targets.digilent_arty \
  --toolchain vivado \
  --variant a7-100 \
  --cpu-type vexriscv \
  --timer-uptime \
  --with-ethernet \
  --csr-json build/csr.json \
  --output-dir build \
  --build
```

最後にこんなのが表示されたらOK

```bash
### litex_venv
0 Infos, 0 Warnings, 0 Critical Warnings and 0 Errors encountered.
write_cfgmem completed successfully
# quit
INFO: [Common 17-206] Exiting Vivado at Sun Jun 21 13:01:04 2026...
```

Arty A7 board をPCとUSB接続して，下記のコマンドでSoCイメージを書き込む．

```bash
### litex_venv
cd ${LITEX_WS_ROOT}/fpga_image/arty_a7_100

python3 -m litex_boards.targets.digilent_arty \
  --variant a7-100 \
  --output-dir build \
  --load
```

<details>

<summary>Tips: FPGA 書き込みのための環境設定</summary>

OpenOCD と udev rule が必要な場合は下記の通り設定する．

```bash
sudo apt install openocd

sudo tee /etc/udev/rules.d/99-digilent-ftdi.rules >/dev/null <<'EOF'
SUBSYSTEM=="usb", ATTR{idVendor}=="0403", ATTR{idProduct}=="6010", MODE="0666", GROUP="plugdev", TAG+="uaccess"
EOF

sudo groupadd -f plugdev
sudo usermod -aG plugdev,dialout $USER

sudo udevadm control --reload-rules
sudo udevadm trigger
```

</details>

最後にこんな感じのが出たらOKのはず

```bash
### litex_venv
fpga_program
Info : ftdi: if you experience problems at higher adapter clocks, try the command "ftdi tdo_sample_edge falling"
Info : clock speed 25000 kHz
Info : JTAG tap: xc7.tap tap/device found: 0x13631093 (mfg: 0x049 (Xilinx), part: 0x3631, ver: 0x1)
```

SoCイメージの書き込みはFPGAボードの再起動(電源投入)ごとに必要となる．

## 2. Zephyrの準備と動作確認

### 環境準備

ここまでの `litex_venv` とは異なるターミナルを開き，必要なパッケージとZephyr用向けvenv環境の作成，Zephyrのインストールを実行する．

manifest は Zephyr v4.4.0 本体だけ，toolchain も `riscv64-zephyr-elf` のみをインストールしている．

```bash
### zephyr_venv
sudo apt install --no-install-recommends git cmake ninja-build gperf \
  ccache dfu-util device-tree-compiler wget python3-dev python3-venv python3-tk \
  xz-utils file make gcc gcc-multilib g++-multilib libsdl2-dev libmagic1

python3 -m venv ${ZEPHYR_WS_ROOT}/zephyr_venv
source ${ZEPHYR_WS_ROOT}/zephyr_venv/bin/activate

pip install west

cd ${ZEPHYR_WS_ROOT}
west init -l manifest
west update zephyr
west zephyr-export
west packages pip --install

mkdir -p ${ZEPHYR_WS_ROOT}/toolchains
cd ${ZEPHYR_WS_ROOT}/zephyr

west sdk install \
  --install-dir ${ZEPHYR_WS_ROOT}/toolchains/zephyr-sdk \
  --gnu-toolchains riscv64-zephyr-elf
```

### samples/hello_world のビルドと実行

まずは Hello, World!!

LiteX 向けの venv で下記を実行し，カスタムSoCに対応した Zephyr Overlay を作成する．

```bash
### litex_venv
cd ${LITEX_WS_ROOT}/fpga_image/arty_a7_100/

python3 ${LITEX_WS_ROOT}/litex_setup/litex/litex/tools/litex_json2dts_zephyr.py \
  --dts build/overlay.dts \
  --config build/overlay.config \
  build/csr.json
```

samples/hello_world をビルドする．
今度は Zephyr 向けの venv で実行する．

```bash
### zephyr_venv
cd ${ZEPHYR_WS_ROOT}/zephyr

west build -p always \
  -b litex_vexriscv \
  samples/hello_world \
  -d ${ZEPHYR_WS_ROOT}/build/hello_litex \
  -- \
  -DDTC_OVERLAY_FILE=${LITEX_WS_ROOT}/fpga_image/arty_a7_100/build/overlay.dts
```

最後にこんなのが表示されればよい

```bash
### zephyr_venv
[124/124] Linking C executable zephyr/zephyr.elf
Memory region         Used Size  Region Size  %age Used
             RAM:       22788 B       256 MB      0.01%
        IDT_LIST:           0 B         4 KB      0.00%
Generating files from /home/takasehideki/nviot/zephyr_ws/build/hello_litex/zephyr/zephyr.elf for board: litex_vexriscv/litex_vexriscv
```

今度は LiteX 用の(ry で，Zephyr をアプリごと serial boot する

```bash
### litex_venv
litex_term /dev/ttyUSB1 \
  --speed 115200 \
  --kernel ${ZEPHYR_WS_ROOT}/build/hello_litex/zephyr/zephyr.bin
```

最後にこんな表示が出てきたらOK

```bash
### litex_venv
Executing booted program at 0x40000000

--============== Liftoff! ==============--
*** Booting Zephyr OS build v4.4.0 ***
Hello World! litex_vexriscv/litex_vexriscv
```

### ネットワーク疎通の確認

次は samples/net/dhcpv4_client でボードとのネットワーク疎通がシュッといくかを確認してみる．
このビルドは Zephyr の venv で(ry

```bash
### zephyr_venv
cd ${ZEPHYR_WS_ROOT}/zephyr

west build -p always \
  -b litex_vexriscv \
  samples/net/dhcpv4_client \
  -d ${ZEPHYR_WS_ROOT}/build/dhcp_litex \
  -- \
  -DDTC_OVERLAY_FILE=${LITEX_WS_ROOT}/fpga_image/arty_a7_100/build/overlay.dts
```

host <-> board は直結ではなくルータ経由でのEthernet接続とする．

LiteX の venv で serial boot していく．

```bash
### litex_venv
litex_term /dev/ttyUSB1 \
  --speed 115200 \
  --kernel ${ZEPHYR_WS_ROOT}/build/dhcp_litex/zephyr/zephyr.bin
```

こうなっていればだいたい良さそう

```bash
[LITEX-TERM] Upload complete (10.9KB/s).
[LITEX-TERM] Booting the device.
[LITEX-TERM] Done.
Executing booted program at 0x40000000

--============== Liftoff! ==============--

[00:00:00.060,000] <inf> phy_mii: PHY (1) ID 20005C90
*** Booting Zephyr OS build v4.4.0 ***
[00:00:00.060,000] <inf> net_dhcpv4_client_sample: Run dhcpv4 client
[00:00:00.060,000] <inf> net_dhcpv4_client_sample: Start on ethernet@e0009800: index=1
[00:00:01.820,000] <inf> phy_mii: PHY (1) Link speed 100 Mb, full duplex
[00:00:03.860,000] <inf> net_dhcpv4: Received: 192.168.11.102
[00:00:03.860,000] <inf> net_dhcpv4_client_sample:    Address[1]: 192.168.11.102
[00:00:03.860,000] <inf> net_dhcpv4_client_sample:     Subnet[1]: 255.255.255.0
[00:00:03.860,000] <inf> net_dhcpv4_client_sample:     Router[1]: 192.168.11.1
[00:00:03.860,000] <inf> net_dhcpv4_client_sample: Lease time[1]: 86400 seconds
uart:~$ 
```

念のため host の別ターミナルから `ping` してみる．よきよき．

```bash
$ ping 192.168.11.102 
PING 192.168.11.102 (192.168.11.102) 56(84) bytes of data.
64 bytes from 192.168.11.102: icmp_seq=1 ttl=64 time=1.47 ms
64 bytes from 192.168.11.102: icmp_seq=2 ttl=64 time=0.895 ms
64 bytes from 192.168.11.102: icmp_seq=3 ttl=64 time=0.860 ms
64 bytes from 192.168.11.102: icmp_seq=4 ttl=64 time=0.913 ms
64 bytes from 192.168.11.102: icmp_seq=5 ttl=64 time=0.911 ms
^C
--- 192.168.11.102 ping statistics ---
5 packets transmitted, 5 received, 0% packet loss, time 4030ms
rtt min/avg/max/mdev = 0.860/1.009/1.466/0.229 ms
```

ボードからホストにも疎通確認してみる．

```bash
uart:~$ net ping 192.168.11.105 
PING 192.168.11.105
28 bytes from 192.168.11.105 to 192.168.11.102: icmp_seq=1 ttl=64 time=1 ms
28 bytes from 192.168.11.105 to 192.168.11.102: icmp_seq=2 ttl=64 time=0 ms
28 bytes from 192.168.11.105 to 192.168.11.102: icmp_seq=3 ttl=64 time=0 ms
```

## 3. zenoh-picoの導入と動作確認

### 準備

Zephyr 向けの zenoh-pico のモジュールを取得する設定は [zephyr_ws/manifest/submanifests/zenoh-pico.yaml](zephyr_ws/manifest/submanifests/zenoh-pico.yaml) に記述してある．

`west update` でこのモジュールを取得する．

```bash
### zephyr_venv
cd ${ZEPHYR_WS_ROOT}

west update zenoh-pico
```

ホストに Zenoh router (zenohd) の standalone binary をインストールする．

```bash
mkdir ${REPO_ROOT}/zenoh_ws/zenohd && cd ${REPO_ROOT}/zenoh_ws/zenohd
wget https://github.com/eclipse-zenoh/zenoh/releases/download/1.8.0/zenoh-1.8.0-x86_64-unknown-linux-gnu-standalone.zip
unzip zenoh-1.8.0-x86_64-unknown-linux-gnu-standalone.zip
```

こんなのが展開されていればよい．

```bash
$ ls
libzenoh_plugin_rest.so
libzenoh_plugin_storage_manager.so
zenoh-1.8.0-x86_64-unknown-linux-gnu-standalone.zip
zenohd
```

ホストでの Zenoh 通信確認用には [zenoh-python](https://github.com/eclipse-zenoh/zenoh-python) を使用する．

```bash
### zenoh_venv
cd ${REPO_ROOT}/zenoh_ws
python3 -m venv zenoh_venv
source zenoh_venv/bin/activate

pip install eclipse-zenoh==1.8.0
```

### ビルド

下記の例のようにホストPCのIPアドレスを環境変数 `ZENOH_LOCATOR` に設定し，pub,sub 両方のアプリをビルドする．

```bash
### zephyr_venv
cd ${ZEPHYR_WS_ROOT}

export ZENOH_LOCATOR="tcp/192.168.11.105:7447"

west build -p always \
  -b litex_vexriscv \
  app/zenoh_pub \
  -d ${ZEPHYR_WS_ROOT}/build/zenoh_pub \
  -- \
  -DDTC_OVERLAY_FILE=${LITEX_WS_ROOT}/fpga_image/arty_a7_100/build/overlay.dts

west build -p always \
  -b litex_vexriscv \
  app/zenoh_sub \
  -d ${ZEPHYR_WS_ROOT}/build/zenoh_sub \
  -- \
  -DDTC_OVERLAY_FILE=${LITEX_WS_ROOT}/fpga_image/arty_a7_100/build/overlay.dts
```

### 動作確認

#### target board Pub -> host laptop Sub

ターミナルを３つ開く．

１つめでは `zenohd` を起動する．

```bash
cd ${REPO_ROOT}/zenoh_ws/zenohd

./zenohd
```

２つめでは zenoh-python 実装の Subscriber である [z_sub.py](zenoh_ws/z_sub.py) を実行する．

```bash
### zenoh_venv
cd ${REPO_ROOT}/zenoh_ws

python3 z_sub.py
```

３つめでは LiteX の venv で serial boot する．

```bash
### litex_venv
litex_term /dev/ttyUSB1 \
  --speed 115200 \
  --kernel ${ZEPHYR_WS_ROOT}/build/zenoh_pub/zephyr/zephyr.bin
```

#### target board Sub <- host laptop Pub

１つめでは `zenohd` を起動する．

```bash
cd ${REPO_ROOT}/zenoh_ws/zenohd

./zenohd
```

２つめでは [z_pub.py](zenoh_ws/z_pub.py) を実行する．

```bash
### zenoh_venv
cd ${REPO_ROOT}/zenoh_ws

python3 z_pub.py
```

３つめでは LiteX の venv で serial boot する．

```bash
### litex_venv
litex_term /dev/ttyUSB1 \
  --speed 115200 \
  --kernel ${ZEPHYR_WS_ROOT}/build/zenoh_sub/zephyr/zephyr.bin
```
