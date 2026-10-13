#!/usr/bin/env bash
# Host (PC) tests for the Distributed BMS firmware. No hardware needed.
#   1. Node 1 logic      : protection, limits, SoC, balancing, OLED renders
#   2. Node 2 controller : contactor / precharge state machine vs. a plant model
#   3. Protocol          : Node 1 -> Node 2 -> gateway frame round trip + JSON
# Test 3 needs ArduinoJson: run "pio run" once in Node3_IoT_Gateway first.
set -euo pipefail
cd "$(dirname "$0")"

FW=..
OUT=out
CC=${CC:-cc}
CXX=${CXX:-c++}
CFLAGS="-std=c11 -Wall -Wextra -Wno-unused-parameter"
N1=$FW/Node1_Monitoring_Unit/App
N2=$FW/Node2_System_Controller/App
AJ=$FW/Node3_IoT_Gateway/.pio/libdeps/esp32doit-devkit-v1/ArduinoJson/src
mkdir -p "$OUT"

echo "== 1. Node 1 logic"
$CC $CFLAGS -Istubs/node1 -I$N1/Inc -I$FW/common -o $OUT/test_node1 test_node1_logic.c \
    $N1/Src/bms_protection.c $N1/Src/bms_soc.c $N1/Src/bms_balancing.c \
    $N1/Src/bms_display.c $N1/Src/ssd1306.c -lm
(cd $OUT && ./test_node1)

echo "== 2. Node 2 state machine"
$CC $CFLAGS -Istubs/node2 -I$N2/Inc -I$FW/common -o $OUT/test_node2 test_node2_statemachine.c \
    $N2/Src/ctrl_app.c -lm
$OUT/test_node2

echo "== 3. Protocol round trip"
if [ ! -d "$AJ" ]; then
    echo "skipped: ArduinoJson not found (run 'pio run' in Firmware/Node3_IoT_Gateway)"
    exit 0
fi
P=stubs/protocol
$CC $CFLAGS -c -I$P -I$N1/Inc -I$FW/common -o $OUT/n1can.o $N1/Src/bms_can.c
$CC $CFLAGS -c -I$P -I$N1/Inc -I$FW/common -o $OUT/node1_side.o $P/node1_side.c
# Node 2's HAL callbacks are renamed so both nodes can live in one binary
$CC $CFLAGS -c -I$P -I$N2/Inc -I$FW/common \
    -DHAL_CAN_RxFifo0MsgPendingCallback=Ctrl_RxCb -DHAL_CAN_ErrorCallback=Ctrl_ErrCb \
    -DHAL_CAN_TxMailbox0CompleteCallback=Ctrl_Tx0 -DHAL_CAN_TxMailbox1CompleteCallback=Ctrl_Tx1 \
    -DHAL_CAN_TxMailbox2CompleteCallback=Ctrl_Tx2 -o $OUT/n2can.o $N2/Src/ctrl_can.c
$CC $CFLAGS -c -I$P -I$N2/Inc -I$FW/common -o $OUT/node2_side.o $P/node2_side.c
$CC $CFLAGS -c -I$P -o $OUT/hal_stub.o $P/hal_stub.c
$CXX -std=c++17 -Wall -c -I$P -I$FW/Node3_IoT_Gateway/include -I$FW/common -I$AJ \
    -o $OUT/model.o $FW/Node3_IoT_Gateway/src/bms_model.cpp
$CXX -std=c++17 -Wall -c -I$P -I$FW/Node3_IoT_Gateway/include -I$N1/Inc -I$N2/Inc -I$FW/common -I$AJ \
    -o $OUT/test_protocol.o test_protocol_roundtrip.cpp
$CXX -o $OUT/test_protocol $OUT/test_protocol.o $OUT/model.o $OUT/n1can.o $OUT/node1_side.o \
    $OUT/n2can.o $OUT/node2_side.o $OUT/hal_stub.o
$OUT/test_protocol | head -4
