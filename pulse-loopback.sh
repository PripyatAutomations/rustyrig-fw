RX_LOOP9$(pactl load-module module-null-sink sink_name=rrloop_rx sink_properties=device.description="RustyRig RX Loop")
TX_LOOP=$(pactl load-module module-null-sink sink_name=rrloop_tx sink_properties=device.description="RustyRig TX Loop")

#536870916
#536870917


