#!/usr/bin/env python3
"""Artifact controls: validation must fail before constructing a guest machine."""
import json
from pathlib import Path
import sys
import subprocess
import tempfile
sys.dont_write_bytecode = True
import run

def main():
    image, elf = map(Path,sys.argv[1:])
    checked=[]
    with tempfile.TemporaryDirectory(dir=run.BUILD) as temp:
        temp=Path(temp)
        bad_image=temp/"mismatch.bin"
        data=bytearray(image.read_bytes()); data[0x100]^=1; bad_image.write_bytes(data)
        bad_elf=temp/"unreviewed.elf"
        bad_elf.write_bytes(elf.read_bytes()+b"unreviewed")
        for name,bin_path,elf_path in [("mismatched-BIN-ELF",bad_image,elf),
                                        ("unreviewed-ELF",image,bad_elf)]:
            report_path=temp/(name+".json")
            result=subprocess.run([sys.executable,str(run.HERE/"run.py"),"--image",str(bin_path),
                                   "--elf",str(elf_path),"--report",str(report_path)],
                                  capture_output=True,text=True,timeout=60)
            report=json.loads(report_path.read_text())
            assert result.returncode==1 and not report["passed"], f"accepted {name}"
            assert "cases" not in report and "byte_identity_verified" not in report
            checked.append({"name":name,"rejected_before_execution":True,
                            "exit_code":result.returncode,"reason":report["failures"]})
    print(json.dumps(checked))

if __name__=="__main__": main()
