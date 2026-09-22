"""Verify fwdsp transform, capture, and playback pipeline framing."""
import os
from pathlib import Path
import subprocess
import tempfile
import struct

root = Path.cwd()
env = dict(os.environ, LD_LIBRARY_PATH=str(root))
frame = bytes((i % 251 for i in range(640)))
raw_caps = "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved"
transform_pipeline = (
    "appsrc name=processor-src is-live=true format=time do-timestamp=true "
    f"caps={raw_caps} ! identity ! appsink name=processor-sink "
    "emit-signals=false sync=false max-buffers=8 drop=false"
)
effect_pipeline = (
    "appsrc name=processor-src is-live=true format=time do-timestamp=true "
    f"caps={raw_caps} ! volume volume=0.5 ! appsink name=processor-sink "
    "emit-signals=false sync=false max-buffers=8 drop=false"
)
capture_pipeline = (
    f"audiotestsrc num-buffers=1 samplesperbuffer=320 do-timestamp=true ! {raw_caps} "
    "! appsink name=processor-sink emit-signals=false sync=false "
    "max-buffers=8 drop=false"
)
playback_pipeline = (
    f"appsrc name=processor-src is-live=true format=time do-timestamp=true caps={raw_caps} "
    "! fakesink sync=false"
)


def run_fwdsp(work, config, pipeline, input_bytes=b"", mode="auto"):
    config_path = work / "processor.cfg"
    config_path.write_text(config)
    result = subprocess.run(
        [str(root / "bin/fwdsp"), "-f", str(config_path), "-T", "-M", mode, "-p", pipeline],
        input=input_bytes,
        capture_output=True,
        env=env,
        cwd=work,
        timeout=10,
        check=True,
    )
    assert b"GStreamer error:" not in result.stderr, result.stderr.decode(errors="replace")
    return result


def assert_pcm_frame(stdout, expected):
    assert len(stdout) >= 4, "fwdsp produced no PCM frame"
    output_len = int.from_bytes(stdout[:4], "big")
    assert output_len == len(expected), (output_len, len(expected))
    assert stdout[4:4 + output_len] == expected


with tempfile.TemporaryDirectory() as temp:
    work = Path(temp)
    config = "[fwdsp]\nlog.file=-\n"
    framed_input = len(frame).to_bytes(4, "big") + frame

    # Two-ended processor with a pipeline selected inline.
    result = run_fwdsp(work, config, transform_pipeline, framed_input,
        mode="bidirectional")
    assert_pcm_frame(result.stdout, frame)

    # A real GStreamer effect modifies PCM while preserving its framing.
    loud_frame = struct.pack("<h", 12000) * 320
    loud_input = len(loud_frame).to_bytes(4, "big") + loud_frame
    result = run_fwdsp(work, config, effect_pipeline, loud_input,
        mode="bidirectional")
    assert len(result.stdout) >= 4 + len(loud_frame), len(result.stdout)
    effect_len = int.from_bytes(result.stdout[:4], "big")
    assert effect_len == len(loud_frame), effect_len
    effected = struct.unpack("<320h", result.stdout[4:4 + effect_len])
    assert all(abs(sample - 6000) <= 2 for sample in effected), effected[:8]

    # Config-selected proc pipeline also uses the normal -T framing.
    named_config = config + "[pipelines]\nproc.identity=" + transform_pipeline + "\n"
    config_path = work / "processor.cfg"
    config_path.write_text(named_config)
    result = subprocess.run(
        [str(root / "bin/fwdsp"), "-f", str(config_path), "-T", "-M", "process",
         "-P", "proc.identity"],
        input=framed_input,
        capture_output=True,
        env=env,
        cwd=work,
        timeout=10,
        check=True,
    )
    assert_pcm_frame(result.stdout, frame)

    # Named source endpoint resolves through the src.* namespace.
    named_src_config = config + "[pipelines]\nsrc.rig0=" + capture_pipeline + "\n"
    config_path.write_text(named_src_config)
    result = subprocess.run(
        [str(root / "bin/fwdsp"), "-f", str(config_path), "-T", "-M", "capture",
         "-P", "src.rig0"],
        capture_output=True,
        env=env,
        cwd=work,
        timeout=10,
        check=True,
    )
    assert len(result.stdout) >= 4, "named source endpoint produced no PCM frame"
    assert int.from_bytes(result.stdout[:4], "big") == 640

    # Source-only pipeline frames captured PCM to stdout.
    result = run_fwdsp(work, config, capture_pipeline, mode="capture")
    assert len(result.stdout) >= 4, "capture pipeline produced no PCM frame"
    captured_len = int.from_bytes(result.stdout[:4], "big")
    assert captured_len == 640, captured_len
    assert len(result.stdout) >= 4 + captured_len, len(result.stdout)
    assert captured_len % 2 == 0

    # Sink-only pipeline accepts framed PCM and does not produce stdout data.
    result = run_fwdsp(work, config, playback_pipeline, framed_input,
        mode="playback")
    assert result.stdout == b"", result.stdout

    # Named sink endpoint resolves through the sink.* namespace.
    named_sink_config = config + "[pipelines]\nsink.rig0=" + playback_pipeline + "\n"
    config_path.write_text(named_sink_config)
    result = subprocess.run(
        [str(root / "bin/fwdsp"), "-f", str(config_path), "-T", "-M", "playback",
         "-P", "sink.rig0"],
        input=framed_input,
        capture_output=True,
        env=env,
        cwd=work,
        timeout=10,
        check=True,
    )
    assert result.stdout == b"", result.stdout

    # The temporary rig TX endpoint finalizes an Ogg/Vorbis file on input EOF.
    ogg_path = work / "rig-tx.ogg"
    ogg_pipeline = (
        f"appsrc name=processor-src is-live=true format=time do-timestamp=true caps={raw_caps} "
        "! audioconvert ! audioresample ! vorbisenc quality=0.3 ! oggmux ! filesink location="
        + str(ogg_path)
    )
    result = run_fwdsp(work, config, ogg_pipeline, framed_input, mode="playback")
    assert result.stdout == b"", result.stdout
    assert ogg_path.exists() and ogg_path.stat().st_size > 4, "Ogg file was not finalized"
    assert ogg_path.read_bytes()[:4] == b"OggS", "rig TX endpoint did not write an Ogg stream"

    print("PASS: fwdsp -T proc/src/sink namespaces, PCM routing, and Ogg/Vorbis pipelines")
