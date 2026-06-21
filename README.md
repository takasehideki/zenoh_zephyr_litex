# zenoh_zepyhr_litex

Zenoh on Zephyr on LiteX を実現するトライアルのためのリポジトリ

サードパーティ的にダウンロード・インストールするものや venv 環境は gitignore しているため，再現には種々のコマンドの実行が必要になる．なるべく再現手順を記しているつもり．

##  1. LiteXでのカスタムSoCの生成

[Vivado Design Tools 2025.1](https://www.amd.com/en/support/downloads/adaptive-socs-and-fpgas/development-tools/2025-1.html) はインストール済みであるとする．

下記のコマンドを実行し，LiteX環境の導入，SoCイメージのビルド，FPGA（Arty A7-100T）への書き込みを行っていく．

```bash
python3 -m venv litex_ws/litex_env
source litex_ws/litex_env/bin/activate

mkdir litex_ws/litex_setup && cd litex_ws/litex_setup
wget https://raw.githubusercontent.com/enjoy-digital/litex/master/litex_setup.py
chmod +x litex_setup.py
python3 litex_setup.py --init --install

pip install meson

source /tools/Xilinx/2025.1/Vivado/settings64.sh

mkdir -p ../fpga_image/arty_a7_100
cd ../fpga_image/arty_a7_100
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
