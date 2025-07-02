# This will setup a simple pulseaudio loopback device you can use for experiments
RX_LOOP=$(pactl load-module module-null-sink sink_name=rrloop_rx sink_properties=device.description="RustyRig RX Loop")
TX_LOOP=$(pactl load-module module-null-sink sink_name=rrloop_tx sink_properties=device.description="RustyRig TX Loop")

echo "rx_loop=${RX_LOOP}"
echo "tx_loop=${TX_LOOP}"


