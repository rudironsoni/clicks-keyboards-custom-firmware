#!/usr/bin/env python3
"""Artifact controls: validation must fail before constructing a guest machine."""
import json
from pathlib import Path
import sys
import subprocess
import struct
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
        data=bytearray(elf.read_bytes())
        header=struct.unpack_from("<16sHHIIIIIHHHHHH", data)
        sections=[struct.unpack_from("<IIIIIIIIII",data,header[6]+i*header[11]) for i in range(header[12])]
        section_names=sections[header[13]]
        names=data[section_names[4]:section_names[4]+section_names[5]]
        comment=next(s for s in sections if names[s[0]:].split(b"\0",1)[0]==b".comment")
        metadata_only=temp/"changed-compiler-comment.elf"
        changed=bytearray(data); changed[comment[4]+4]^=1; metadata_only.write_bytes(changed)
        assert run.digest(metadata_only)!=run.digest(elf)
        run.validate(image, metadata_only)
        checked.append({"name":"non-loadable-compiler-comment", "accepted_equivalent":True})
        symbols=next(s for s in sections if s[1]==2)
        strings=sections[symbols[6]]
        names=data[strings[4]:strings[4]+strings[5]]
        gp=next(off for off in range(symbols[4],symbols[4]+symbols[5],symbols[9])
                if names[struct.unpack_from("<I",data,off)[0]:].split(b"\0",1)[0]==b"__global_pointer$")
        load=next(header[5]+i*header[9] for i in range(header[10])
                  if struct.unpack_from("<I",data,header[5]+i*header[9])[0]==1)
        cases=[("mismatched-BIN-ELF",bad_image,elf)]
        for name,offset,value in [("changed-runtime-symbol",gp+4,struct.unpack_from("<I",data,gp+4)[0]+4),
                                  ("changed-segment-permissions",load+24,7),
                                  ("changed-entry",24,0x2100),
                                  ("changed-ABI",36,3)]:
            changed=bytearray(data); struct.pack_into("<I",changed,offset,value)
            bad_elf=temp/(name+".elf"); bad_elf.write_bytes(changed)
            cases.append((name,image,bad_elf))
        for name,bin_path,elf_path in cases:
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
