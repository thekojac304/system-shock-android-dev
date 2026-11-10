import importlib.util
from pathlib import Path
import sys
import tempfile
import wave

sys.dont_write_bytecode = True
module_path = Path(__file__).resolve().parents[2] / "scripts/validate-ambient-assets.py"
spec = importlib.util.spec_from_file_location("ambient_validator", module_path)
validator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary) / "ambient"
    assert validator.validate(root)["zero_assets_supported"]
    (root / "generic/ventilation").mkdir(parents=True)
    assert validator.validate(root)["zero_assets_supported"]
    with wave.open(str(root / "generic/ventilation/loop_air.wav"), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(8000)
        output.writeframes(b"\0\0" * 8000)
    (root / "medical/ventilation").mkdir(parents=True)
    (root / "medical/ventilation/air.wav").write_bytes(
        (root / "generic/ventilation/loop_air.wav").read_bytes()
    )
    (root / "generic/ventilation/bad.wav").write_bytes(b"bad")
    (root / "generic/ventilation/bad.ogg").write_bytes(b"bad")
    report = validator.validate(root)
    assert report["asset_count"] == 2
    assert any(item["kind"] == "loop" and item["deck"] == "generic" for item in report["assets"])
    assert any(item["deck"] == "medical" for item in report["assets"])
    assert any("duplicate SHA-256" in item for item in report["issues"])
    assert any("bad.wav" in item for item in report["issues"])
    assert any("bad.ogg" in item for item in report["issues"])
print("AMBIENT_VALIDATOR_TESTS=PASS (missing, empty, WAV, duplicate, invalid)")
