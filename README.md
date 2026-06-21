# zenoh_zephyr_litex

Zenoh on Zephyr on LiteX を実現するトライアルのためのリポジトリ

サードパーティ的にダウンロード・インストールするものや venv 環境は gitignore しているため，再現には種々のコマンドの実行が必要になる．なるべく再現手順を記しているつもり．

次の環境変数が設定されている想定とする．
```bash
cd <this_repo_root>   # typically, `zenoh_zephyr_litex`
export REPO_ROOT=${PWD}
export LITEX_WS_ROOT=${REPO_ROOT}/litex_ws
export ZEPHYR_WS_ROOT=${REPO_ROOT}/zephyr_ws
```

## 1. LiteXでのカスタムSoCの生成

[Vivado Design Tools 2025.1](https://www.amd.com/en/support/downloads/adaptive-socs-and-fpgas/development-tools/2025-1.html) はインストール済みであるとする．

下記のコマンドを実行し，LiteX向けvenv環境の作成，LiteX環境の導入，SoCイメージのビルド，FPGA（Arty A7-100T）への書き込みを行っていく．

```bash
python3 -m venv ${LITEX_WS_ROOT}/litex_env
source ${LITEX_WS_ROOT}/litex_env/bin/activate

mkdir ${LITEX_WS_ROOT}/litex_setup && cd ${LITEX_WS_ROOT}/litex_setup
wget https://raw.githubusercontent.com/enjoy-digital/litex/master/litex_setup.py
chmod +x litex_setup.py
python3 litex_setup.py --init --install

pip install meson

source /tools/Xilinx/2025.1/Vivado/settings64.sh

mkdir -p ${LITEX_WS_ROOT}/fpga_image/arty_a7_100
cd ${LITEX_WS_ROOT}/fpga_image/arty_a7_100
mkdir build

python3 -m litex_boards.targets.digilent_arty \
  --toolchain vivado \
  --variant a7-100 \
  --cpu-type vexriscv \
  --timer-uptime \
  --with-ethernet \
  --csr-json build/csr.json \
  --output-dir build \
  --build

python3 -m litex_boards.targets.digilent_arty \
  --variant a7-100 \
  --output-dir build \
  --load
```

## 2. Zephyrの準備と動作確認

### 環境準備

上記の `litex_env` とは異なるターミナルを開き，必要なパッケージとZephyr用向けvenv環境の作成，Zephyrのインストールを実行する．

manifest は Zephyr v4.4.0 本体だけ，toolchain も `riscv64-zephyr-elf` のみをインストールしている．

```bash
sudo apt install --no-install-recommends git cmake ninja-build gperf \
  ccache dfu-util device-tree-compiler wget python3-dev python3-venv python3-tk \
  xz-utils file make gcc gcc-multilib g++-multilib libsdl2-dev libmagic1

python3 -m venv ${ZEPHYR_WS_ROOT}/zephyr_env 
source ${ZEPHYR_WS_ROOT}/zephyr_env/bin/activate

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
