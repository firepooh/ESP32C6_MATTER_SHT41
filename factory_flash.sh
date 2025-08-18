#!/bin/bash
echo "Write Factory Data"

# 파라미터 체크
if [ $# -eq 0 ]; then
    echo "Usage: $0 <1|2|3>"
    exit 1
fi

# case문으로 분기
case "$1" in
    1)
        echo "write 4cf80187-89f8-4ca6-9b94-85774a613165-partition.bin"
        esptool.py write_flash 0x3E0000 out/fff2_8001/4cf80187-89f8-4ca6-9b94-85774a613165/4cf80187-89f8-4ca6-9b94-85774a613165-partition.bin
        ;;
    2)
        echo "write 4d33caac-768f-433d-ab0d-43bf3a0ee01a-partition.bin"
        esptool.py write_flash 0x3E0000 out/fff2_8001/4d33caac-768f-433d-ab0d-43bf3a0ee01a/4d33caac-768f-433d-ab0d-43bf3a0ee01a-partition.bin
        ;;
    3)
        echo "write 8a3b5632-03ab-40c9-99bb-e784b00b0b16-partition.bin"
        esptool.py write_flash 0x3E0000 out/fff2_8001/8a3b5632-03ab-40c9-99bb-e784b00b0b16/8a3b5632-03ab-40c9-99bb-e784b00b0b16-partition.bin
        ;;
    *)
        echo "Error: Invalid parameter '$1'"
        echo "Available options: 1, 2, 3"
        exit 1
        ;;
esac    

echo "Flash write completed!"

# 1 input
#esptool.py write_flash 0x3E0000 out/fff2_8001/eafb9b79-a9ae-4132-a727-100c2d8f1202/eafb9b79-a9ae-4132-a727-100c2d8f1202-partition.bin
#echo "Write 1"

# 2 input
#esptool.py write_flash 0x3E0000 out/fff2_8001/eafb9b79-a9ae-4132-a727-100c2d8f1202/eafb9b79-a9ae-4132-a727-100c2d8f1202-partition.bin

# 3 input
#esptool.py write_flash 0x3E0000 out/fff2_8001/eafb9b79-a9ae-4132-a727-100c2d8f1202/eafb9b79-a9ae-4132-a727-100c2d8f1202-partition.bin