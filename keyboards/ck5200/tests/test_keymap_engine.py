"""Build and run the CK-5200 stock-extension keymap engine behavioral suite.

Milestone deliverable 2 acceptance: the C suite at
tests/ck5200_keymap_engine_test.c must compile warning-clean and pass on the
host. Also measures the engine's RISC-V footprint when the pinned toolchain
is present (deliverable 3 evidence).
"""
import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).parents[1]
ENGINE = ROOT / "stock_extension/ck5200_keymap_engine.c"
TEST = ROOT / "tests/ck5200_keymap_engine_test.c"


def test_engine_behavioral_suite(tmp_path):
    binary = tmp_path / "keymap_engine_test"
    subprocess.run(
        ["cc", "-std=c99", "-Wall", "-Werror", "-Os",
         "-I", str(ROOT / "stock_extension"),
         str(ENGINE), str(TEST), "-o", str(binary)],
        check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    passes = result.stdout.count("PASS ")
    assert result.returncode == 0, result.stdout
    assert result.stdout.rstrip().endswith("ok")
    assert passes >= 30, f"expected >= 30 behavioral checks, saw {passes}"
    assert "FAIL" not in result.stdout


def test_engine_riscv_footprint_budget(tmp_path):
    """Deliverable 3: the engine must fit the measured flash budget.

    Skipped when the pinned RISC-V toolchain is not installed (for example
    on a runner without scripts/setup.sh). The revised action validation
    and deterministic native/custom ownership need a 1920-byte sub-budget.
    """
    tc = sorted(ROOT.glob(".toolchains/xpack-riscv-none-elf-gcc-*/bin"))
    if not tc:
        pytest.skip("pinned riscv toolchain not installed")
    gcc = str(tc[0] / "riscv-none-elf-gcc")
    obj = tmp_path / "engine.o"
    subprocess.run(
        [gcc, "-std=c99", "-Os", "-march=rv32imac_zicsr", "-mabi=ilp32",
         "-mcmodel=medlow", "-ffreestanding", "-ffunction-sections",
         "-fdata-sections", "-I", str(ROOT / "stock_extension"),
         "-c", str(ENGINE), "-o", str(obj)],
        check=True)
    size = subprocess.run([str(tc[0] / "riscv-none-elf-size"), str(obj)],
                           capture_output=True, text=True, check=True)
    fields = size.stdout.splitlines()[1].split()
    text = int(fields[0])
    # The separate linked-image test retains the 27,136-byte application
    # limit. This sub-budget covers mode selection and phased transitions.
    assert text <= 1920, f"engine .text grew to {text} bytes (budget 1920)"
    print(f"engine riscv .text = {text} bytes")


def test_shim_and_serialization_suite(tmp_path):
    """Correctness milestone: the host shim harness and the packed
    serialization round-trip must compile warning-clean and pass."""
    binary = tmp_path / "shim_test"
    subprocess.run(
        ["cc", "-std=c99", "-Wall", "-Werror", "-Os", "-DCK_SHIM_HOST",
         "-I", str(ROOT / "stock_extension"), "-I", str(ROOT / "tests"),
         str(ENGINE),
         str(ROOT / "stock_extension/ck5200_keymap_serialize.c"),
         str(ROOT / "stock_extension/ck5200_extension_shim.c"),
         str(ROOT / "tests/ck5200_extension_shim_test.c"),
         "-o", str(binary)],
        check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    passes = result.stdout.count("PASS ")
    assert result.returncode == 0, result.stdout
    assert result.stdout.rstrip().endswith("ok")
    assert passes >= 18, f"expected >= 18 shim/serialization checks, saw {passes}"


def test_linked_extension_image_build():
    """Correctness milestone: the fully linked research image builds and
    stays inside the application-area capacity. Skipped without the
    pinned toolchain (CI installs it before the host tests run)."""
    if not list(ROOT.glob(".toolchains/xpack-riscv-none-elf-gcc-*")):
        pytest.skip("pinned riscv toolchain not installed")
    script = ROOT / "stock_extension/build_extension_image.sh"
    result = subprocess.run(["bash", str(script)],
                             capture_output=True, text=True, timeout=600)
    assert result.returncode == 0, result.stdout + result.stderr
    report = (ROOT / "build/stock_extension/build-report.txt").read_text()
    assert "headroom" in report
    headroom = int([l for l in report.splitlines()
                    if "headroom" in l][0].split("headroom ")[1])
    assert headroom > 0, report
