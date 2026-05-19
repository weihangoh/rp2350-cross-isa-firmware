cd riscv_worker
rmdir /s /q build
cd ..\arm_worker
rmdir /s /q build
cd ..\arm_host
rmdir /s /q build
del /q bcdemo_program.h worker_arm_blob.h worker_riscv_blob.h
cd ..